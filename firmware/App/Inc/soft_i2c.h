/*
 * soft_i2c.h - bit-banged I2C master on PF1 (SCL) / PF0 (SDA).
 *
 * RT-Spark wires the ICM20608 IMU (and the ES8388 codec, AP3216C sensor) to
 * these two pins, which RT-Thread drives as "soft I2C2"; PF0/PF1 have no
 * hardware I2C function on the STM32F407, so the bus is bit-banged here too.
 */
#ifndef SOFT_I2C_H
#define SOFT_I2C_H

#include <stddef.h>
#include <stdint.h>

/* Returns 1 on success (all bytes acknowledged), 0 otherwise. */
int soft_i2c_write_reg(uint8_t addr7, uint8_t reg, uint8_t value);
int soft_i2c_read_regs(uint8_t addr7, uint8_t reg, uint8_t *buf, size_t len);
int soft_i2c_probe(uint8_t addr7);

#endif
