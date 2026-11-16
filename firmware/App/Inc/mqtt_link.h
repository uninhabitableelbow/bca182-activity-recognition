/* mqtt_link.h - coreMQTT 5.0.2 client over CMSIS-Driver WiFi sockets (Driver_WiFi0). */
#ifndef MQTT_LINK_H
#define MQTT_LINK_H

#include <stddef.h>
#include <stdint.h>

/* Opens a TCP connection to host:port and sends MQTT CONNECT. Returns 0 on success. */
int mqtt_link_connect(const char *host, uint16_t port, const char *client_id);

/* Publishes payload (QoS 0). Returns 0 on success. */
int mqtt_link_publish(const char *topic, const char *payload, size_t len);

/* Lets coreMQTT handle incoming packets and keep-alive pings. Returns 0 on success. */
int mqtt_link_poll(void);

void mqtt_link_close(void);

#endif
