/*
 * app.c - BCA182 Lab 3 device firmware: IMU -> Wi-Fi -> MQTT.
 *
 * Follows the sequence in the lab PDF ("Device Firmware"):
 *   1. Initialise sensors              (SensorTask: ICM20608 accelerometer + gyroscope)
 *   2. Initialise Wi-Fi                (NetworkTask: Driver_WiFi0, the PDF's sample code)
 *   3. Connect to the MQTT broker      (coreMQTT)
 *   4. Every second, send an MQTT message with that second's readings.
 *
 * The classifier was trained on 5 Hz data, so SensorTask samples every 200 ms
 * and each one-second message carries the 5 readings of that second.
 */
#include "app.h"

#include <stdio.h>
#include <string.h>

#include "Driver_WiFi.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "event_groups.h"
#include "queue.h"
#include "task.h"

#include "app_log.h"
#include "icm20608.h"
#include "mqtt_link.h"
#include "rw007.h"
#include "sensor_message.h"
#include "wifi_config.h"
#include "wifi_netif.h"

extern ARM_DRIVER_WIFI Driver_WiFi0;

#define SAMPLE_PERIOD_MS 200            /* 5 Hz, like the training data */
#define TOPIC            "bca182/activity/" DEVICE_NAME "/sensor"
#define EV_SENSORS_READY (1u << 0)

typedef struct {
  uint32_t seq;
  imu_sample_t samples[SAMPLES_PER_MESSAGE];
} batch_t;

static QueueHandle_t batch_queue;       /* SensorTask -> NetworkTask, one item per second */
static EventGroupHandle_t app_events;

void app_init(void)
{
  app_log_init();
  batch_queue = xQueueCreate(4, sizeof(batch_t));
  app_events = xEventGroupCreate();
}

/* ------------------------------------------------------------------ SensorTask ---- */
void app_sensor_task(void)
{
  app_log("BCA182 Lab 3 - Activity Recognition device (RT-Spark, FreeRTOS)");

  /* 1. Initialise sensors */
  uint8_t who = 0;
  while (!icm20608_init(&who)) {
    app_log("ICM20608 not found on I2C (PF0/PF1), retrying...");
    osDelay(1000);
  }
  app_log("ICM20608 ready, WHO_AM_I = 0x%02X", who);
  app_log("Calibrating gyroscope: keep the board still for 1 s");
  icm20608_calibrate_gyro(100);
  xEventGroupSetBits(app_events, EV_SENSORS_READY);

  batch_t batch;
  int count = 0;
  uint32_t seq = 0;
  TickType_t next = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&next, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    imu_sample_t s;
    if (!icm20608_read(&s)) {
      app_log("ICM20608 read failed, dropping this second");
      count = 0;
      seq++;                            /* the gap tells the classifier to restart its window */
      continue;
    }
    batch.samples[count++] = s;
    if (count < SAMPLES_PER_MESSAGE)
      continue;

    batch.seq = seq++;
    count = 0;
    if (xQueueSend(batch_queue, &batch, 0) != pdTRUE) {
      batch_t oldest;                   /* network is behind: keep the newest data */
      xQueueReceive(batch_queue, &oldest, 0);
      xQueueSend(batch_queue, &batch, 0);
    }
    if (batch.seq % 10 == 0) {
      char a[3][12], g[3][12];
      const float v[6] = {s.ax, s.ay, s.az, s.gx, s.gy, s.gz};
      for (int k = 0; k < 3; k++) {
        sensor_format_fixed4(a[k], sizeof a[k], v[k]);
        sensor_format_fixed4(g[k], sizeof g[k], v[k + 3]);
      }
      app_log("IMU seq %lu: accel [%s %s %s] g, gyro [%s %s %s] rad/s", (unsigned long)batch.seq,
              a[0], a[1], a[2], g[0], g[1], g[2]);
    }
  }
}

/* ----------------------------------------------------------------- NetworkTask ---- */
static void log_net_info(void)
{
  uint32_t ip, mask, gw, dns;
  wifi_netif_get_addr(&ip, &mask, &gw, &dns);
  const uint8_t *a = (const uint8_t *)&ip, *g = (const uint8_t *)&gw;
  app_log("Wi-Fi connected to \"%s\": IP %u.%u.%u.%u, gateway %u.%u.%u.%u", WIFI_SSID, a[0], a[1], a[2],
          a[3], g[0], g[1], g[2], g[3]);
}

void app_network_task(void)
{
  xEventGroupWaitBits(app_events, EV_SENSORS_READY, pdFALSE, pdTRUE, portMAX_DELAY);

  /* 2. Initialise Wi-Fi (sample code from the lab PDF, with error checking added) */
  int32_t r = Driver_WiFi0.Initialize(NULL);
  if (r == ARM_DRIVER_OK)
    r = Driver_WiFi0.PowerControl(ARM_POWER_FULL);
  if (r != ARM_DRIVER_OK) {
    app_log("Wi-Fi module (RW007) did not start (%ld). Check the board and reset it.", (long)r);
    for (;;)
      osDelay(1000);
  }
  char module[40];
  if (Driver_WiFi0.GetModuleInfo(module, sizeof module) == ARM_DRIVER_OK)
    app_log("Wi-Fi module: %s", module);

  ARM_WIFI_CONFIG_t wifiConfig = {0};
  wifiConfig.ssid = WIFI_SSID;                      /* Your network SSID. */
  wifiConfig.pass = WIFI_PASSWORD;                  /* Your network password. */
  wifiConfig.security = ARM_WIFI_SECURITY_WPA2;     /* Your network security mode. */
  wifiConfig.ch = 0;
  wifiConfig.wps_method = ARM_WIFI_WPS_METHOD_NONE;

  int failures = 0;
  char payload[512];
  for (;;) {
    if (!Driver_WiFi0.IsConnected()) {
      app_log("Connecting to Wi-Fi \"%s\"...", WIFI_SSID);
      r = Driver_WiFi0.Activate(0, &wifiConfig);
      if (r != ARM_DRIVER_OK) {
        app_log("Wi-Fi connect failed (%s)", r == ARM_DRIVER_ERROR_TIMEOUT ? "no DHCP address" : "join failed");
        if (++failures == 1 || failures % 5 == 0)
          wifi_netif_scan_and_log();    /* show what the module can see (2.4 GHz only) */
        osDelay(3000);
        continue;
      }
      failures = 0;
      log_net_info();
    }

    /* 3. Connect to the MQTT broker */
    if (mqtt_link_connect(MQTT_BROKER_IP, MQTT_BROKER_PORT, DEVICE_NAME) != 0) {
      osDelay(3000);
      continue;
    }

    /* 4. Send one message per second */
    xQueueReset(batch_queue);           /* start fresh: no stale data after a reconnect */
    uint32_t sent = 0;
    while (Driver_WiFi0.IsConnected()) {
      batch_t batch;
      if (xQueueReceive(batch_queue, &batch, pdMS_TO_TICKS(500)) == pdTRUE) {
        int len = sensor_message_build(payload, sizeof payload, DEVICE_NAME, batch.seq, batch.samples,
                                       SAMPLES_PER_MESSAGE);
        if (len < 0 || mqtt_link_publish(TOPIC, payload, (size_t)len) != 0)
          break;
        if (sent++ % 10 == 0)
          app_log("MQTT: published seq %lu to %s (%d bytes)", (unsigned long)batch.seq, TOPIC, len);
      }
      if (mqtt_link_poll() != 0)
        break;
    }
    mqtt_link_close();
    app_log("Connection lost, reconnecting...");
    osDelay(1000);
  }
}

/* ------------------------------------------------------------------ RTOS hooks ---- */
void app_stack_overflow(const char *task_name)
{
  app_log("STACK OVERFLOW in task %s", task_name);
}

void app_malloc_failed(void)
{
  app_log("FreeRTOS heap exhausted (free %u bytes)", (unsigned)xPortGetFreeHeapSize());
}
