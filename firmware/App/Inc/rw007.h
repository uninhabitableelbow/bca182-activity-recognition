/*
 * rw007.h - RT-Thread RW007 Wi-Fi module driver (SPI2), ported to FreeRTOS + STM32 HAL.
 *
 * The RW007 is a Wi-Fi network card: it exchanges commands, events and raw
 * Ethernet frames with the host over SPI. The TCP/IP stack (lwIP) runs on the
 * STM32 (see wifi_netif.c).
 *
 * Ported from the rw007 package (github.com/RT-Thread-packages/rw007,
 * src/spi_wifi_rw007.c, Copyright (C) 2018 Real-Thread Information Technology
 * Ltd, Apache-2.0). Wire protocol, structures and command numbers are kept
 * unchanged; RT-Thread objects were replaced with FreeRTOS ones.
 *
 * RT-Spark wiring (RT-Thread BSP stm32f407-rt-spark, board/Kconfig):
 *   SPI2 SCK PB13, MISO PC2, MOSI PC3, CS PF10, INT/BUSY PG11, RESET PG15.
 */
#ifndef RW007_H
#define RW007_H

#include <stddef.h>
#include <stdint.h>

/* Events reported by the module (values of rt_wlan_dev_event_t in RT-Thread). */
enum {
  RW007_EVT_INIT_DONE = 0,
  RW007_EVT_CONNECT,
  RW007_EVT_CONNECT_FAIL,
  RW007_EVT_DISCONNECT,
  RW007_EVT_AP_START,
  RW007_EVT_AP_STOP,
  RW007_EVT_AP_ASSOCIATED,
  RW007_EVT_AP_DISASSOCIATED,
  RW007_EVT_AP_ASSOCIATE_FAILED,
  RW007_EVT_SCAN_REPORT,
  RW007_EVT_SCAN_DONE,
};

/* Security values (rt_wlan_security_t in RT-Thread). */
#define RW007_SECURITY_OPEN          0x00000000u
#define RW007_SECURITY_WPA_AES_PSK   0x00200004u
#define RW007_SECURITY_WPA2_AES_PSK  0x00400004u
#define RW007_SECURITY_WPA2_MIXED    0x00400006u

/* Scan result passed with RW007_EVT_SCAN_REPORT. */
typedef struct {
  char ssid[33];
  uint8_t bssid[6];
  int16_t channel;
  int16_t rssi;
  uint32_t security;
} rw007_scan_result_t;

typedef void (*rw007_frame_cb_t)(const uint8_t *frame, uint32_t len);
typedef void (*rw007_event_cb_t)(int event, const void *data);

/* Resets the module, starts the driver tasks, sends INIT and selects station mode.
 * Returns 0 on success, a negative value on failure. */
int rw007_init(rw007_frame_cb_t frame_cb, rw007_event_cb_t event_cb);

int rw007_get_mac(uint8_t mac[6]);
int rw007_get_version(char *buf, size_t size);
int rw007_get_sn(char *buf, size_t size);
int rw007_get_rssi(void);

/* Starts joining a network. The result arrives as RW007_EVT_CONNECT / _CONNECT_FAIL.
 * Returns the module's command result (0 = accepted). */
int rw007_join(const char *ssid, const char *password, uint32_t security);
int rw007_disconnect(void);
int rw007_scan(void);

/* Queues one Ethernet frame for sending. Returns 0, or -1 if no buffer was free. */
int rw007_send_frame(const uint8_t *frame, uint32_t len);

/* Call from the EXTI callback when the INT/BUSY pin falls. */
void rw007_isr(void);

/* Transfer statistics for diagnostics. */
typedef struct {
  uint32_t transfers, retries, failures, stage1_errors, stage2_errors, rx_frames, tx_frames;
} rw007_stats_t;
void rw007_get_stats(rw007_stats_t *out);

#endif
