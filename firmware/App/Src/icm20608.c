/*
 * icm20608.c - ICM20608 driver (register map from the TDK ICM-20608-G datasheet).
 *
 * Settings: +-8 g, +-2000 dps, 10 Hz digital low-pass on both sensors (the
 * firmware samples at 5 Hz like the training data), internal 100 Hz rate.
 */
#include "icm20608.h"

#include "cmsis_os2.h"
#include "soft_i2c.h"

#define REG_SMPLRT_DIV     0x19
#define REG_CONFIG         0x1A
#define REG_GYRO_CONFIG    0x1B
#define REG_ACCEL_CONFIG   0x1C
#define REG_ACCEL_CONFIG2  0x1D
#define REG_ACCEL_XOUT_H   0x3B
#define REG_PWR_MGMT_1     0x6B
#define REG_PWR_MGMT_2     0x6C
#define REG_WHO_AM_I       0x75

static uint8_t addr;
static int16_t gyro_bias[3];

int icm20608_init(uint8_t *who_am_i)
{
  static const uint8_t candidates[] = {0x68, 0x69};
  addr = 0;
  for (unsigned i = 0; i < sizeof candidates; i++) {
    if (soft_i2c_probe(candidates[i])) {
      addr = candidates[i];
      break;
    }
  }
  if (addr == 0)
    return 0;

  int ok = soft_i2c_write_reg(addr, REG_PWR_MGMT_1, 0x80);   /* device reset */
  osDelay(100);
  ok &= soft_i2c_write_reg(addr, REG_PWR_MGMT_1, 0x01);      /* wake, best clock source */
  ok &= soft_i2c_write_reg(addr, REG_PWR_MGMT_2, 0x00);      /* all axes on */
  ok &= soft_i2c_write_reg(addr, REG_SMPLRT_DIV, 9);         /* 1 kHz / (1 + 9) = 100 Hz */
  ok &= soft_i2c_write_reg(addr, REG_CONFIG, 0x05);          /* gyro DLPF 10 Hz */
  ok &= soft_i2c_write_reg(addr, REG_GYRO_CONFIG, 0x18);     /* +-2000 dps */
  ok &= soft_i2c_write_reg(addr, REG_ACCEL_CONFIG, 0x10);    /* +-8 g */
  ok &= soft_i2c_write_reg(addr, REG_ACCEL_CONFIG2, 0x05);   /* accel DLPF 10.2 Hz */
  ok &= soft_i2c_read_regs(addr, REG_WHO_AM_I, who_am_i, 1);
  osDelay(50);
  return ok;
}

void icm20608_calibrate_gyro(int n)
{
  int32_t sum[3] = {0, 0, 0};
  int got = 0;
  uint8_t raw[14];
  for (int i = 0; i < n; i++) {
    if (soft_i2c_read_regs(addr, REG_ACCEL_XOUT_H, raw, sizeof raw)) {
      sum[0] += imu_be16(&raw[8]);
      sum[1] += imu_be16(&raw[10]);
      sum[2] += imu_be16(&raw[12]);
      got++;
    }
    osDelay(10);
  }
  for (int k = 0; k < 3; k++)
    gyro_bias[k] = got ? (int16_t)(sum[k] / got) : 0;
}

int icm20608_read(imu_sample_t *out)
{
  uint8_t raw[14];
  if (!soft_i2c_read_regs(addr, REG_ACCEL_XOUT_H, raw, sizeof raw))
    return 0;
  *out = imu_convert(raw, gyro_bias);
  return 1;
}
