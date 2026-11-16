/* icm20608.h - on-board ICM20608 6-axis IMU (accelerometer + gyroscope) on soft I2C2. */
#ifndef ICM20608_H
#define ICM20608_H

#include <stdint.h>

#include "imu_convert.h"

/* Finds the sensor (address 0x68 or 0x69), resets and configures it.
 * Returns 1 on success and reports the WHO_AM_I value. */
int icm20608_init(uint8_t *who_am_i);

/* Averages the gyro over n readings while the board lies still and stores the bias. */
void icm20608_calibrate_gyro(int n);

/* One reading in g and rad/s. Returns 1 on success. */
int icm20608_read(imu_sample_t *out);

#endif
