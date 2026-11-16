/*
 * imu_convert.h - ICM20608 raw readings -> the units the cloud model was trained on.
 *
 * The Kaggle run-or-walk data uses acceleration in g (gravity included) and
 * rotation rate in rad/s, so the firmware sends exactly those units.
 * Hardware independent, unit-tested on the PC (test/test_imu_convert).
 */
#ifndef IMU_CONVERT_H
#define IMU_CONVERT_H

#include <stdint.h>

/* Full-scale settings used by icm20608.c */
#define IMU_ACCEL_LSB_PER_G    4096.0f   /* +-8 g     (ACCEL_FS_SEL = 2) */
#define IMU_GYRO_LSB_PER_DPS   16.4f     /* +-2000 dps (FS_SEL = 3)      */

typedef struct {
  float ax, ay, az;   /* g     */
  float gx, gy, gz;   /* rad/s */
} imu_sample_t;

/* Decodes the 14-byte burst read starting at ACCEL_XOUT_H (big-endian
 * accel x/y/z, temperature, gyro x/y/z) and subtracts the gyro bias (raw counts). */
imu_sample_t imu_convert(const uint8_t raw[14], const int16_t gyro_bias[3]);

/* Big-endian int16 from two bytes. */
int16_t imu_be16(const uint8_t *p);

#endif
