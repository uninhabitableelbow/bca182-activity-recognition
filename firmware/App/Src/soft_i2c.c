/*
 * soft_i2c.c - bit-banged I2C master on IMU_SCL (PF1) / IMU_SDA (PF0).
 * Both pins are open-drain outputs with pull-ups (configured in CubeMX), so
 * writing 1 releases the line and the pin can be read back as an input.
 * Based on the ES8388 bus code from Lab 2, extended with reads.
 */
#include "soft_i2c.h"

#include "main.h"

static void i2c_wait(void)
{
  for (volatile int i = 0; i < 120; i++) {
  }
}
static void scl(int level)
{
  HAL_GPIO_WritePin(IMU_SCL_GPIO_Port, IMU_SCL_Pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
  i2c_wait();
}
static void sda(int level)
{
  HAL_GPIO_WritePin(IMU_SDA_GPIO_Port, IMU_SDA_Pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
  i2c_wait();
}
static int sda_read(void) { return HAL_GPIO_ReadPin(IMU_SDA_GPIO_Port, IMU_SDA_Pin) == GPIO_PIN_SET; }

static void i2c_start(void) { sda(1); scl(1); sda(0); scl(0); }
static void i2c_stop(void)  { sda(0); scl(1); sda(1); }

static int i2c_write_byte(uint8_t byte)
{
  for (int i = 7; i >= 0; i--) {
    sda((byte >> i) & 1);
    scl(1);
    scl(0);
  }
  sda(1);                       /* release SDA for the acknowledge bit */
  scl(1);
  int ack = !sda_read();
  scl(0);
  return ack;
}

static uint8_t i2c_read_byte(int ack)
{
  uint8_t byte = 0;
  sda(1);
  for (int i = 7; i >= 0; i--) {
    scl(1);
    byte |= (uint8_t)(sda_read() << i);
    scl(0);
  }
  sda(ack ? 0 : 1);             /* ACK all bytes except the last */
  scl(1);
  scl(0);
  sda(1);
  return byte;
}

int soft_i2c_probe(uint8_t addr7)
{
  i2c_start();
  int ok = i2c_write_byte((uint8_t)(addr7 << 1));
  i2c_stop();
  return ok;
}

int soft_i2c_write_reg(uint8_t addr7, uint8_t reg, uint8_t value)
{
  i2c_start();
  int ok = i2c_write_byte((uint8_t)(addr7 << 1)) && i2c_write_byte(reg) && i2c_write_byte(value);
  i2c_stop();
  return ok;
}

int soft_i2c_read_regs(uint8_t addr7, uint8_t reg, uint8_t *buf, size_t len)
{
  i2c_start();
  int ok = i2c_write_byte((uint8_t)(addr7 << 1)) && i2c_write_byte(reg);
  if (ok) {
    i2c_start();                /* repeated start, then read */
    ok = i2c_write_byte((uint8_t)((addr7 << 1) | 1));
    for (size_t i = 0; ok && i < len; i++)
      buf[i] = i2c_read_byte(i + 1 < len);
  }
  i2c_stop();
  return ok;
}
