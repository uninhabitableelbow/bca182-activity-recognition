/*
 * mqtt_link.c - coreMQTT 5.0.2 client (AWS::coreMQTT in the lab PDF) whose
 * transport uses the CMSIS-Driver WiFi socket API (Driver_WiFi0).
 * Docs: https://docs.aws.amazon.com/freertos/latest/userguide/coremqtt.html
 */
#include "mqtt_link.h"

#include <string.h>

#include "Driver_WiFi.h"
#include "FreeRTOS.h"
#include "core_mqtt.h"
#include "task.h"

#include "app_log.h"

extern ARM_DRIVER_WIFI Driver_WiFi0;

struct NetworkContext {
  int32_t socket;
};

static NetworkContext_t net = {.socket = -1};
static MQTTContext_t mqtt;
static uint8_t mqtt_buffer[1024];

/* ---- transport interface ---- */
static int32_t transport_send(NetworkContext_t *ctx, const void *buf, size_t len)
{
  int32_t n = Driver_WiFi0.SocketSend(ctx->socket, buf, (uint32_t)len);
  if (n == ARM_SOCKET_EAGAIN)
    return 0;                       /* try again later */
  return n;
}

static int32_t transport_recv(NetworkContext_t *ctx, void *buf, size_t len)
{
  int32_t n = Driver_WiFi0.SocketRecv(ctx->socket, buf, (uint32_t)len);
  if (n == ARM_SOCKET_EAGAIN || n == ARM_SOCKET_ETIMEDOUT)
    return 0;                       /* no data within the receive timeout */
  return n;
}

static uint32_t now_ms(void) { return (uint32_t)xTaskGetTickCount(); }

/* The device only publishes; incoming packets are acknowledgements and ping responses. */
static bool event_callback(MQTTContext_t *ctx, MQTTPacketInfo_t *packet, MQTTDeserializedInfo_t *info,
                           MQTTSuccessFailReasonCode_t *reason, MQTTPropBuilder_t *send_props,
                           MQTTPropBuilder_t *get_props)
{
  (void)ctx; (void)packet; (void)info; (void)reason; (void)send_props; (void)get_props;
  return true;
}

/* ---- public ---- */
int mqtt_link_connect(const char *host, uint16_t port, const char *client_id)
{
  uint8_t ip[4];
  uint32_t ip_len = sizeof ip;
  if (Driver_WiFi0.SocketGetHostByName(host, ARM_SOCKET_AF_INET, ip, &ip_len) != 0) {
    app_log("MQTT: cannot resolve broker address %s", host);
    return -1;
  }

  net.socket = Driver_WiFi0.SocketCreate(ARM_SOCKET_AF_INET, ARM_SOCKET_SOCK_STREAM, ARM_SOCKET_IPPROTO_TCP);
  if (net.socket < 0) {
    app_log("MQTT: socket create failed (%ld)", (long)net.socket);
    return -1;
  }
  int32_t r = Driver_WiFi0.SocketConnect(net.socket, ip, 4, port);
  if (r != 0) {
    app_log("MQTT: TCP connect to %u.%u.%u.%u:%u failed (%ld)", ip[0], ip[1], ip[2], ip[3], port, (long)r);
    mqtt_link_close();
    return -1;
  }
  uint32_t rcv_timeout_ms = 20;     /* short, so polling for packets never blocks long */
  Driver_WiFi0.SocketSetOpt(net.socket, ARM_SOCKET_SO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms);
  /* Without a send timeout, a connection that died silently (e.g. the PC dropped it)
   * blocks the task forever once the TCP send buffer is full. With it, coreMQTT gets
   * "try again" and gives up after MQTT_SEND_TIMEOUT_MS, so we reconnect. */
  uint32_t snd_timeout_ms = 2000;
  Driver_WiFi0.SocketSetOpt(net.socket, ARM_SOCKET_SO_SNDTIMEO, &snd_timeout_ms, sizeof snd_timeout_ms);
  uint32_t keepalive = 1;
  Driver_WiFi0.SocketSetOpt(net.socket, ARM_SOCKET_SO_KEEPALIVE, &keepalive, sizeof keepalive);

  TransportInterface_t transport = {0};
  transport.pNetworkContext = &net;
  transport.send = transport_send;
  transport.recv = transport_recv;
  transport.writev = NULL;
  MQTTFixedBuffer_t fixed = {.pBuffer = mqtt_buffer, .size = sizeof mqtt_buffer};
  memset(&mqtt, 0, sizeof mqtt);
  MQTTStatus_t st = MQTT_Init(&mqtt, &transport, now_ms, event_callback, &fixed);
  if (st != MQTTSuccess) {
    app_log("MQTT: init failed (%s)", MQTT_Status_strerror(st));
    mqtt_link_close();
    return -1;
  }

  MQTTConnectInfo_t info = {0};
  info.cleanSession = true;
  info.pClientIdentifier = client_id;
  info.clientIdentifierLength = (uint16_t)strlen(client_id);
  info.keepAliveSeconds = 30;
  bool session_present = false;
  st = MQTT_Connect(&mqtt, &info, NULL, 5000, &session_present, NULL, NULL);
  if (st != MQTTSuccess) {
    app_log("MQTT: CONNECT failed (%s)", MQTT_Status_strerror(st));
    mqtt_link_close();
    return -1;
  }
  app_log("MQTT: connected to broker %u.%u.%u.%u:%u as %s", ip[0], ip[1], ip[2], ip[3], port, client_id);
  return 0;
}

int mqtt_link_publish(const char *topic, const char *payload, size_t len)
{
  MQTTPublishInfo_t pub = {0};
  pub.qos = MQTTQoS0;
  pub.pTopicName = topic;
  pub.topicNameLength = (uint16_t)strlen(topic);
  pub.pPayload = payload;
  pub.payloadLength = len;
  MQTTStatus_t st = MQTT_Publish(&mqtt, &pub, 0, NULL);
  if (st != MQTTSuccess) {
    app_log("MQTT: publish failed (%s)", MQTT_Status_strerror(st));
    return -1;
  }
  return 0;
}

int mqtt_link_poll(void)
{
  MQTTStatus_t st = MQTT_ProcessLoop(&mqtt);
  if (st != MQTTSuccess && st != MQTTNeedMoreBytes) {
    app_log("MQTT: process loop failed (%s)", MQTT_Status_strerror(st));
    return -1;
  }
  return 0;
}

void mqtt_link_close(void)
{
  if (net.socket >= 0) {
    if (mqtt.connectStatus == MQTTConnected)
      MQTT_Disconnect(&mqtt, NULL, NULL);
    Driver_WiFi0.SocketClose(net.socket);
    net.socket = -1;
  }
}
