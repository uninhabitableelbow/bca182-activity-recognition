/* imu_convert.c - ICM20608 raw counts -> g and rad/s (see imu_convert.h). */
#include "imu_convert.h"

#define DEG_TO_RAD 0.017453292519943295f

int16_t imu_be16(const uint8_t *p)
{
  return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

imu_sample_t imu_convert(const uint8_t raw[14], const int16_t gyro_bias[3])
{
  imu_sample_t s;
  s.ax = imu_be16(&raw[0]) / IMU_ACCEL_LSB_PER_G;
  s.ay = imu_be16(&raw[2]) / IMU_ACCEL_LSB_PER_G;
  s.az = imu_be16(&raw[4]) / IMU_ACCEL_LSB_PER_G;
  /* raw[6..7] is the temperature, not used */
  s.gx = (imu_be16(&raw[8]) - gyro_bias[0]) / IMU_GYRO_LSB_PER_DPS * DEG_TO_RAD;
  s.gy = (imu_be16(&raw[10]) - gyro_bias[1]) / IMU_GYRO_LSB_PER_DPS * DEG_TO_RAD;
  s.gz = (imu_be16(&raw[12]) - gyro_bias[2]) / IMU_GYRO_LSB_PER_DPS * DEG_TO_RAD;
  return s;
}
