/* Unit tests for ICM20608 raw-count conversion (pio test -e native). */
#include <unity.h>

#include "imu_convert.h"

void setUp(void) {}
void tearDown(void) {}

static void put(uint8_t *raw, int index, int16_t v)
{
  raw[index] = (uint8_t)((uint16_t)v >> 8);
  raw[index + 1] = (uint8_t)v;
}

static void test_be16_decodes_signed_big_endian(void)
{
  const uint8_t a[2] = {0x12, 0x34}, b[2] = {0xFF, 0xFE}, c[2] = {0x80, 0x00};
  TEST_ASSERT_EQUAL_INT16(0x1234, imu_be16(a));
  TEST_ASSERT_EQUAL_INT16(-2, imu_be16(b));
  TEST_ASSERT_EQUAL_INT16(-32768, imu_be16(c));
}

static void test_accel_is_in_g(void)
{
  uint8_t raw[14] = {0};
  const int16_t bias[3] = {0, 0, 0};
  put(raw, 0, 4096);    /* +1 g at +-8 g full scale */
  put(raw, 2, -2048);   /* -0.5 g */
  put(raw, 4, 32767);   /* just under +8 g */
  imu_sample_t s = imu_convert(raw, bias);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, s.ax);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.5f, s.ay);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 8.0f, s.az);
}

static void test_gyro_is_in_rad_per_s(void)
{
  uint8_t raw[14] = {0};
  const int16_t bias[3] = {0, 0, 0};
  put(raw, 8, 164);     /* 10 dps at 16.4 LSB/dps */
  put(raw, 10, -1640);  /* -100 dps */
  imu_sample_t s = imu_convert(raw, bias);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.17453293f, s.gx);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.7453293f, s.gy);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.gz);
}

static void test_gyro_bias_is_subtracted_and_temperature_ignored(void)
{
  uint8_t raw[14] = {0};
  const int16_t bias[3] = {10, -20, 164};
  put(raw, 6, 12345);   /* temperature: must not leak into any axis */
  put(raw, 8, 10);
  put(raw, 10, -20);
  put(raw, 12, 0);
  imu_sample_t s = imu_convert(raw, bias);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.gx);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.gy);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.17453293f, s.gz);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.ax);
}

static void test_bias_at_range_edge_does_not_overflow(void)
{
  uint8_t raw[14] = {0};
  const int16_t bias[3] = {32767, 0, 0};
  put(raw, 8, -32768);  /* difference -65535 must be computed in int, not int16 */
  imu_sample_t s = imu_convert(raw, bias);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -65535.0f / 16.4f * 0.017453292f, s.gx);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_be16_decodes_signed_big_endian);
  RUN_TEST(test_accel_is_in_g);
  RUN_TEST(test_gyro_is_in_rad_per_s);
  RUN_TEST(test_gyro_bias_is_subtracted_and_temperature_ignored);
  RUN_TEST(test_bias_at_range_edge_does_not_overflow);
  return UNITY_END();
}
