/*
 * rw007.c - RW007 Wi-Fi module SPI driver for FreeRTOS + STM32 HAL.
 *
 * Ported from RT-Thread's rw007 package (src/spi_wifi_rw007.c,
 * Copyright (C) 2018 Real-Thread Information Technology Ltd,
 * SPDX-License-Identifier: Apache-2.0). Changes from the original:
 *   - rt_event / rt_mailbox / rt_mempool replaced by FreeRTOS task
 *     notifications, queues and static buffer pools;
 *   - the RT-Thread WLAN framework is replaced by two callbacks (frames,
 *     events) and a small command API;
 *   - AP mode, promiscuous mode, BLE, OTA and the SPI test code are left out.
 * The SPI protocol (headers, magic words, phases, retries) is unchanged.
 */
#include "rw007.h"

#include <string.h>

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "app_log.h"
#include "main.h"
#include "spi.h"

/* ---------------------------------------------------------------- protocol (unchanged) ---- */
typedef enum { master_cmd_phase = 0x01, master_data_phase, slave_cmd_phase, slave_data_phase } phase_type;
typedef enum { TRANSFER_DATA_SUCCESS = 0x00, TRANSFER_DATA_ERROR, TRANSFER_DATA_CONTINUE } rw007_transfer_state;

/* little-endian */
struct spi_master_request {
  uint8_t reserve1;
  uint8_t flag : 4;
  uint8_t type : 4;
  uint16_t reserve2;
  uint16_t seq;
  uint16_t M2S_len;   /* master to slave data len */
  uint32_t magic1;
  uint32_t magic2;
};
#define MASTER_MAGIC1   (0x67452301)
#define MASTER_MAGIC2   (0xEFCDAB89)
#define MASTER_FLAG_MRDY (0x01)

struct spi_slave_response {
  uint8_t reserve1;
  uint8_t flag : 4;
  uint8_t type : 4;
  uint8_t slave_rx_buf : 4;
  uint8_t slave_tx_buf : 4;
  uint8_t reserve2;
  uint16_t seq;
  uint16_t S2M_len;   /* slave to master data len */
  uint32_t magic1;
  uint32_t magic2;
};
#define SLAVE_MAGIC1    (0x98BADCFE)
#define SLAVE_MAGIC2    (0x10325476)
#define SLAVE_FLAG_SRDY (0x01)

#define SPI_MAX_DATA_LEN   1520
#define SPI_TX_POOL_SIZE   4
#define SPI_RX_POOL_SIZE   4
#define SLAVE_INT_TIMEOUT  pdMS_TO_TICKS(100)

typedef enum {
  DATA_TYPE_STA_ETH_DATA = 1,
  DATA_TYPE_AP_ETH_DATA,
  DATA_TYPE_PROMISC_ETH_DATA,
  DATA_TYPE_CMD,
  DATA_TYPE_RESP,
  DATA_TYPE_CB,
  DATA_TYPE_BLE,
} app_data_type_t;

struct spi_data_packet {
  uint32_t data_len;    /* length for buffer */
  uint32_t data_type;   /* app_data_type_t */
  char buffer[SPI_MAX_DATA_LEN];
};
#define MAX_SPI_PACKET_SIZE (offsetof(struct spi_data_packet, buffer) + SPI_MAX_DATA_LEN)

typedef enum {
  RW00x_CMD_INIT = 0x00, RW00x_CMD_SET_MODE, RW00x_CMD_MAC_GET, RW00x_CMD_MAC_SET, RW00x_CMD_GET_SN,
  RW00x_CMD_GET_VSR, RW00x_CMD_SCAN, RW00x_CMD_JOIN, RW00x_CMD_SOFTAP, RW00x_CMD_DISCONNECT,
  RW00x_CMD_AP_STOP, RW00x_CMD_AP_DEAUTH, RW00x_CMD_SCAN_STOP, RW00x_CMD_GET_RSSI,
  RW00x_CMD_SET_PWR_SAVE, RW00x_CMD_GET_PWR_SAVE, RW00x_CMD_CFG_PROMISC, RW00x_CMD_CFG_FILTER,
  RW00x_CMD_SET_CHANNEL, RW00x_CMD_GET_CHANNEL, RW00x_CMD_SET_COUNTRY, RW00x_CMD_GET_COUNTRY,
  RW00x_CMD_AP_MAC_GET, RW00x_CMD_AP_MAC_SET, RW00x_CMD_CFG_OTA, RW00x_CMD_BLE_CFGWIFI,
  RW00x_CMD_MAX_NUM
} RW00x_CMD;

/* RT-Thread WLAN structures carried inside commands (dev_wlan.h layout). */
#define RT_WLAN_SSID_MAX_LENGTH     32
#define RT_WLAN_PASSWORD_MAX_LENGTH 32
struct rt_wlan_ssid { uint8_t len; uint8_t val[RT_WLAN_SSID_MAX_LENGTH + 1]; };
struct rt_wlan_info {
  uint32_t security;      /* rt_wlan_security_t */
  uint32_t band;          /* rt_802_11_band_t: 1 = 2.4 GHz */
  uint32_t datarate;
  int16_t channel;
  int16_t rssi;
  struct rt_wlan_ssid ssid;
  uint8_t bssid[6];
  uint8_t hidden;
};
struct rw007_ap_info_value {
  struct rt_wlan_info info;
  char passwd[RT_WLAN_PASSWORD_MAX_LENGTH];
};
#define RT_WLAN_STATION 1   /* rt_wlan_mode_t */

/* little-endian */
struct rw007_cmd_hdr { uint32_t cmd; uint32_t len; /* value follows */ };
struct __attribute__((packed)) rw007_resp_hdr { uint32_t cmd; uint32_t len; int32_t result; /* value follows */ };

/* ---------------------------------------------------------------- driver state ---- */
#define EVT_SLAVE_INT   (1u << 0)
#define EVT_MASTER_DATA (1u << 1)

static struct spi_data_packet tx_pool[SPI_TX_POOL_SIZE];
static struct spi_data_packet rx_pool[SPI_RX_POOL_SIZE];
static QueueHandle_t tx_free, rx_free;   /* free packet pointers */
static QueueHandle_t tx_queue, rx_queue; /* packets waiting to go out / to be parsed */

static TaskHandle_t xfer_task;
static uint32_t xfer_pending;            /* notification bits seen but not consumed yet (xfer task only) */

static SemaphoreHandle_t cmd_mutex, cmd_done;
static volatile uint32_t cmd_waiting = 0xFFFFFFFFu;
static uint8_t cmd_resp[MAX_SPI_PACKET_SIZE];

static rw007_frame_cb_t on_frame;
static rw007_event_cb_t on_event;
static rw007_stats_t stats;

static uint8_t spi_zero[MAX_SPI_PACKET_SIZE];   /* sent when there is nothing to send */
static uint8_t spi_sink[MAX_SPI_PACKET_SIZE];   /* receives bytes nobody needs */

/* ------------------------------------------------------- notification helpers (xfer task) ---- */
static void collect(TickType_t wait)
{
  uint32_t v = 0;
  if (xTaskNotifyWait(0, 0xFFFFFFFFu, &v, wait) == pdTRUE)
    xfer_pending |= v;
}

/* Waits until one of `want` is set; consumes and returns the bits that were set (0 on timeout). */
static uint32_t wait_bits(uint32_t want, TickType_t timeout)
{
  TickType_t start = xTaskGetTickCount();
  for (;;) {
    if (xfer_pending & want) {
      uint32_t got = xfer_pending & want;
      xfer_pending &= ~want;
      return got;
    }
    TickType_t elapsed = xTaskGetTickCount() - start;
    if (timeout != portMAX_DELAY && elapsed >= timeout)
      return 0;
    collect(timeout == portMAX_DELAY ? portMAX_DELAY : timeout - elapsed);
  }
}

static void clear_bits(uint32_t bits)
{
  collect(0);
  xfer_pending &= ~bits;
}

static void notify_xfer(uint32_t bits)
{
  if (xfer_task)
    xTaskNotify(xfer_task, bits, eSetBits);
}

void rw007_isr(void)
{
  BaseType_t woken = pdFALSE;
  if (xfer_task)
    xTaskNotifyFromISR(xfer_task, EVT_SLAVE_INT, eSetBits, &woken);
  portYIELD_FROM_ISR(woken);
}

/* ------------------------------------------------------------------------ SPI ---- */
static void cs(int active)
{
  HAL_GPIO_WritePin(WIFI_CS_GPIO_Port, WIFI_CS_Pin, active ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void spi_xfer(const void *tx, void *rx, uint32_t len)
{
  if (len == 0)
    return;
  HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)(tx ? tx : spi_zero), (uint8_t *)(rx ? rx : spi_sink),
                          (uint16_t)len, 100);
}

/* One master<->slave exchange (wifi_data_transfer in the original). */
static int wifi_data_transfer(uint16_t seq, struct spi_data_packet *rx_buffer,
                              const struct spi_data_packet *send_packet)
{
  struct spi_master_request cmd;
  struct spi_slave_response resp;
  uint32_t max_data_len = 0;

  memset(&cmd, 0, sizeof cmd);
  cmd.type = master_cmd_phase;
  cmd.seq = seq;
  cmd.magic1 = MASTER_MAGIC1;
  cmd.magic2 = MASTER_MAGIC2;
  if (rx_buffer != NULL)
    cmd.flag |= MASTER_FLAG_MRDY;     /* we have a buffer for the slave's data */
  if (send_packet != NULL) {
    if (send_packet->data_len == 0 || send_packet->data_len > SPI_MAX_DATA_LEN)
      send_packet = NULL;
    else
      cmd.M2S_len = (uint16_t)(send_packet->data_len + offsetof(struct spi_data_packet, buffer));
  }

  /* Stage 1: send the command header */
  memset(&resp, 0, sizeof resp);
  cs(1);
  spi_xfer(&cmd, &resp, sizeof resp);
  clear_bits(EVT_SLAVE_INT);
  cs(0);

  if (resp.magic1 != SLAVE_MAGIC1 || resp.magic2 != SLAVE_MAGIC2 || resp.type != slave_cmd_phase) {
    stats.stage1_errors++;
    vTaskDelay(1);
    return TRANSFER_DATA_ERROR;
  }
  if (!wait_bits(EVT_SLAVE_INT, SLAVE_INT_TIMEOUT))
    app_log("rw007: slave response timed out");

  /* Stage 2: read the slave's header, then exchange data in the same CS window */
  cmd.type = master_data_phase;
  memset(&resp, 0, sizeof resp);
  cs(1);
  spi_xfer(&cmd, &resp, sizeof resp);

  if (resp.magic1 != SLAVE_MAGIC1 || resp.magic2 != SLAVE_MAGIC2 || resp.seq != seq ||
      resp.type != slave_data_phase) {
    stats.stage2_errors++;
    cs(0);
    vTaskDelay(1);
    return TRANSFER_DATA_ERROR;
  }
  if (resp.flag & SLAVE_FLAG_SRDY)
    max_data_len = cmd.M2S_len;
  if (resp.S2M_len > MAX_SPI_PACKET_SIZE)
    resp.S2M_len = 0;                    /* drop bad length */
  if (resp.S2M_len > max_data_len)
    max_data_len = resp.S2M_len;
  if (resp.S2M_len == 0 && rx_buffer != NULL) {
    xQueueSend(rx_free, &rx_buffer, 0);
    rx_buffer = NULL;
  }

  spi_xfer(send_packet, rx_buffer, (max_data_len + 3u) & ~3u);   /* clock whole words */
  cs(0);

  if (rx_buffer)
    xQueueSend(rx_queue, &rx_buffer, 0);

  if (!wait_bits(EVT_SLAVE_INT, SLAVE_INT_TIMEOUT))
    app_log("rw007: slave data response timed out");

  return resp.slave_tx_buf > 0 ? TRANSFER_DATA_CONTINUE : TRANSFER_DATA_SUCCESS;
}

static int spi_wifi_transfer(const struct spi_data_packet *tx_buffer)
{
  static uint16_t cmd_seq = 0;
  struct spi_data_packet *rx_buffer = NULL;
  xQueueReceive(rx_free, &rx_buffer, 0);

  if (++cmd_seq >= 65534)
    cmd_seq = 1;
  stats.transfers++;

  int result = TRANSFER_DATA_ERROR;
  for (int retry = 3; retry > 0; retry--) {
    result = wifi_data_transfer(cmd_seq, rx_buffer, tx_buffer);
    if (result != TRANSFER_DATA_ERROR)
      return result;
    stats.retries++;
  }
  stats.failures++;
  if (rx_buffer)
    xQueueSend(rx_free, &rx_buffer, 0);
  return TRANSFER_DATA_ERROR;
}

/* -------------------------------------------------------------------- tasks ---- */
static void xfer_task_entry(void *arg)
{
  (void)arg;
  int empty_read = 1;
  struct spi_data_packet *send_packet = NULL;

  for (;;) {
    wait_bits(EVT_MASTER_DATA | EVT_SLAVE_INT, portMAX_DELAY);

    if (send_packet == NULL)
      xQueueReceive(tx_queue, &send_packet, 0);

    int state = spi_wifi_transfer(send_packet);
    if (state != TRANSFER_DATA_ERROR && send_packet != NULL) {
      xQueueSend(tx_free, &send_packet, 0);
      send_packet = NULL;
    }

    /* Keep polling while either side still has data (same logic as the original). */
    if (state == TRANSFER_DATA_CONTINUE) {
      xfer_pending |= EVT_SLAVE_INT;
      empty_read = 1;
    } else if (state == TRANSFER_DATA_SUCCESS && empty_read) {
      xfer_pending |= EVT_SLAVE_INT;
      empty_read = 0;
    } else {
      empty_read = 1;
    }
    if (send_packet != NULL || uxQueueMessagesWaiting(tx_queue) > 0)
      xfer_pending |= EVT_MASTER_DATA;
  }
}

static void report_scan(const struct rt_wlan_info *info)
{
  rw007_scan_result_t r;
  memset(&r, 0, sizeof r);
  uint8_t n = info->ssid.len > 32 ? 32 : info->ssid.len;
  memcpy(r.ssid, info->ssid.val, n);
  memcpy(r.bssid, info->bssid, 6);
  r.channel = info->channel;
  r.rssi = info->rssi;
  r.security = info->security;
  if (on_event)
    on_event(RW007_EVT_SCAN_REPORT, &r);
}

static void process_task_entry(void *arg)
{
  (void)arg;
  struct spi_data_packet *p;
  for (;;) {
    if (xQueueReceive(rx_queue, &p, portMAX_DELAY) != pdTRUE)
      continue;
    switch (p->data_type) {
    case DATA_TYPE_STA_ETH_DATA:
      stats.rx_frames++;
      if (on_frame && p->data_len <= SPI_MAX_DATA_LEN)
        on_frame((const uint8_t *)p->buffer, p->data_len);
      break;
    case DATA_TYPE_CB: {
      const struct rw007_resp_hdr *r = (const struct rw007_resp_hdr *)p->buffer;
      if (r->cmd == RW007_EVT_SCAN_REPORT)
        report_scan((const struct rt_wlan_info *)(p->buffer + sizeof *r));
      else if (on_event)
        on_event((int)r->cmd, NULL);
      break;
    }
    case DATA_TYPE_RESP: {
      const struct rw007_resp_hdr *r = (const struct rw007_resp_hdr *)p->buffer;
      if (r->cmd == cmd_waiting) {
        memcpy(cmd_resp, p->buffer, MAX_SPI_PACKET_SIZE - offsetof(struct spi_data_packet, buffer));
        cmd_waiting = 0xFFFFFFFFu;
        xSemaphoreGive(cmd_done);
      }
      break;
    }
    default:
      break;   /* AP / promiscuous / BLE data: not used */
    }
    xQueueSend(rx_free, &p, 0);
  }
}

/* ----------------------------------------------------------------- commands ---- */
/* Sends a command and waits for its response. Copies up to out_size bytes of the
 * response value into out. Returns the module's result (0 = OK) or -100 on timeout. */
static int send_cmd(uint32_t command, const void *value, uint32_t len, void *out, size_t out_size,
                    uint32_t timeout_ms)
{
  struct spi_data_packet *p = NULL;
  int result;

  xSemaphoreTake(cmd_mutex, portMAX_DELAY);
  if (xQueueReceive(tx_free, &p, pdMS_TO_TICKS(1000)) != pdTRUE) {
    xSemaphoreGive(cmd_mutex);
    return -101;
  }
  struct rw007_cmd_hdr *hdr = (struct rw007_cmd_hdr *)p->buffer;
  p->data_type = DATA_TYPE_CMD;
  hdr->cmd = command;
  hdr->len = len;
  if (len)
    memcpy(p->buffer + sizeof *hdr, value, len);
  p->data_len = sizeof *hdr + len;

  xSemaphoreTake(cmd_done, 0);            /* drop any stale completion */
  cmd_waiting = command;
  xQueueSend(tx_queue, &p, portMAX_DELAY);
  notify_xfer(EVT_MASTER_DATA);

  if (xSemaphoreTake(cmd_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    cmd_waiting = 0xFFFFFFFFu;
    result = -100;
  } else {
    const struct rw007_resp_hdr *r = (const struct rw007_resp_hdr *)cmd_resp;
    result = r->result;
    if (out && out_size) {
      size_t n = r->len < out_size ? r->len : out_size;
      memcpy(out, cmd_resp + sizeof *r, n);
    }
  }
  xSemaphoreGive(cmd_mutex);
  return result;
}

int rw007_get_mac(uint8_t mac[6])
{
  uint8_t v[8] = {0};
  int r = send_cmd(RW00x_CMD_MAC_GET, NULL, 0, v, sizeof v, 2000);
  memcpy(mac, v, 6);
  return r;
}

int rw007_get_version(char *buf, size_t size)
{
  memset(buf, 0, size);
  return send_cmd(RW00x_CMD_GET_VSR, NULL, 0, buf, size - 1, 2000);
}

int rw007_get_sn(char *buf, size_t size)
{
  memset(buf, 0, size);
  return send_cmd(RW00x_CMD_GET_SN, NULL, 0, buf, size - 1, 2000);
}

int rw007_get_rssi(void)
{
  int32_t rssi = 0;
  send_cmd(RW00x_CMD_GET_RSSI, NULL, 0, &rssi, sizeof rssi, 2000);
  return (int)rssi;
}

int rw007_join(const char *ssid, const char *password, uint32_t security)
{
  struct rw007_ap_info_value v;
  memset(&v, 0, sizeof v);
  v.info.security = security;
  v.info.band = 1;                         /* 2.4 GHz */
  v.info.channel = -1;                     /* any channel */
  size_t n = strlen(ssid);
  if (n > RT_WLAN_SSID_MAX_LENGTH)
    n = RT_WLAN_SSID_MAX_LENGTH;
  v.info.ssid.len = (uint8_t)n;
  memcpy(v.info.ssid.val, ssid, n);
  strncpy(v.passwd, password, sizeof v.passwd - 1);
  return send_cmd(RW00x_CMD_JOIN, &v, sizeof v, NULL, 0, 10000);
}

int rw007_disconnect(void) { return send_cmd(RW00x_CMD_DISCONNECT, NULL, 0, NULL, 0, 5000); }
int rw007_scan(void) { return send_cmd(RW00x_CMD_SCAN, NULL, 0, NULL, 0, 10000); }

int rw007_send_frame(const uint8_t *frame, uint32_t len)
{
  struct spi_data_packet *p = NULL;
  if (len == 0 || len > SPI_MAX_DATA_LEN)
    return -1;
  if (xQueueReceive(tx_free, &p, pdMS_TO_TICKS(200)) != pdTRUE)
    return -1;
  p->data_type = DATA_TYPE_STA_ETH_DATA;
  p->data_len = len;
  memcpy(p->buffer, frame, len);
  xQueueSend(tx_queue, &p, portMAX_DELAY);
  stats.tx_frames++;
  notify_xfer(EVT_MASTER_DATA);
  return 0;
}

void rw007_get_stats(rw007_stats_t *out) { *out = stats; }

/* ------------------------------------------------------------------- init ---- */
static void int_pin_mode(uint32_t mode, uint32_t pull)
{
  GPIO_InitTypeDef g = {0};
  g.Pin = WIFI_INT_Pin;
  g.Mode = mode;
  g.Pull = pull;
  HAL_GPIO_Init(WIFI_INT_GPIO_Port, &g);
}

int rw007_init(rw007_frame_cb_t frame_cb, rw007_event_cb_t event_cb)
{
  on_frame = frame_cb;
  on_event = event_cb;

  tx_free = xQueueCreate(SPI_TX_POOL_SIZE, sizeof(void *));
  rx_free = xQueueCreate(SPI_RX_POOL_SIZE, sizeof(void *));
  tx_queue = xQueueCreate(SPI_TX_POOL_SIZE, sizeof(void *));
  rx_queue = xQueueCreate(SPI_RX_POOL_SIZE, sizeof(void *));
  cmd_mutex = xSemaphoreCreateMutex();
  cmd_done = xSemaphoreCreateBinary();
  if (!tx_free || !rx_free || !tx_queue || !rx_queue || !cmd_mutex || !cmd_done)
    return -1;
  for (int i = 0; i < SPI_TX_POOL_SIZE; i++) {
    struct spi_data_packet *p = &tx_pool[i];
    xQueueSend(tx_free, &p, 0);
  }
  for (int i = 0; i < SPI_RX_POOL_SIZE; i++) {
    struct spi_data_packet *p = &rx_pool[i];
    xQueueSend(rx_free, &p, 0);
  }

  /* Reset sequence (rw007_gpio_init in the RT-Thread STM32 example). */
  HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
  cs(0);
  int_pin_mode(GPIO_MODE_INPUT, GPIO_PULLDOWN);
  HAL_GPIO_WritePin(WIFI_RST_GPIO_Port, WIFI_RST_Pin, GPIO_PIN_RESET);
  vTaskDelay(pdMS_TO_TICKS(100));
  HAL_GPIO_WritePin(WIFI_RST_GPIO_Port, WIFI_RST_Pin, GPIO_PIN_SET);

  TickType_t start = xTaskGetTickCount();
  while (HAL_GPIO_ReadPin(WIFI_INT_GPIO_Port, WIFI_INT_Pin) == GPIO_PIN_RESET) {   /* busy */
    if (xTaskGetTickCount() - start > pdMS_TO_TICKS(3000))
      return -2;                           /* module never became ready */
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  vTaskDelay(pdMS_TO_TICKS(200));

  /* CMSIS-RTOS v2 priorities are FreeRTOS priorities (cmsis_os2.c passes them through). */
  if (xTaskCreate(process_task_entry, "wifi_handle", 512, NULL, (UBaseType_t)osPriorityAboveNormal, NULL) != pdPASS ||
      xTaskCreate(xfer_task_entry, "wifi_xfer", 512, NULL, (UBaseType_t)osPriorityHigh, &xfer_task) != pdPASS)
    return -3;

  /* Interrupt on the falling edge of INT/BUSY, pull-up (spi_wifi_hw_init). */
  int_pin_mode(GPIO_MODE_IT_FALLING, GPIO_PULLUP);
  __HAL_GPIO_EXTI_CLEAR_IT(WIFI_INT_Pin);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  int r = send_cmd(RW00x_CMD_INIT, NULL, 0, NULL, 0, 5000);
  if (r != 0)
    return r < 0 ? r : -4;
  uint32_t mode = RT_WLAN_STATION;
  r = send_cmd(RW00x_CMD_SET_MODE, &mode, sizeof mode, NULL, 0, 5000);
  return r == 0 ? 0 : (r < 0 ? r : -5);
}
