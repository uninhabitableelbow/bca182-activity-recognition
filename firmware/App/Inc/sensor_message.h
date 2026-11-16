/*
 * sensor_message.h - builds the MQTT payload the cloud classifier expects:
 *
 *   {"device":"rt-spark-01","seq":42,"samples":[[ax,ay,az,gx,gy,gz],...]}
 *
 * One message per second with the 5 readings of that second (5 Hz), in g and
 * rad/s, 4 decimals. Hardware independent, unit-tested on the PC.
 */
#ifndef SENSOR_MESSAGE_H
#define SENSOR_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

#include "imu_convert.h"

#define SAMPLES_PER_MESSAGE 5

/* Writes a NUL-terminated JSON object into buf.
 * Returns its length, or -1 if buf is too small or an argument is invalid. */
int sensor_message_build(char *buf, size_t size, const char *device, uint32_t seq,
                         const imu_sample_t *samples, int count);

/* Formats v with 4 decimals (no printf float support needed). Returns length or -1. */
int sensor_format_fixed4(char *buf, size_t size, float v);

#endif
