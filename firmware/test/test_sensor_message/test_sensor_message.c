/* Unit tests for the MQTT payload builder (pio test -e native). */
#include <string.h>
#include <unity.h>

#include "sensor_message.h"

void setUp(void) {}
void tearDown(void) {}

static void check_fixed4(float v, const char *expected)
{
  char buf[24];
  int n = sensor_format_fixed4(buf, sizeof buf, v);
  TEST_ASSERT_EQUAL_STRING(expected, buf);
  TEST_ASSERT_EQUAL_INT((int)strlen(expected), n);
}

static void test_fixed4_formats_four_decimals(void)
{
  check_fixed4(0.0f, "0.0000");
  check_fixed4(1.0239f, "1.0239");
  check_fixed4(-0.0657f, "-0.0657");
  check_fixed4(-0.5f, "-0.5000");
  check_fixed4(12.34567f, "12.3457");   /* rounds half away from zero */
}

static void test_fixed4_tiny_negative_has_no_minus_zero(void)
{
  check_fixed4(-0.00004f, "0.0000");
}

static void test_fixed4_clamps_and_handles_nan(void)
{
  check_fixed4(5000.0f, "999.0000");
  check_fixed4(-5000.0f, "-999.0000");
  volatile float zero = 0.0f;
  check_fixed4(zero / zero, "0.0000");
}

static void test_fixed4_reports_small_buffer(void)
{
  char buf[4];
  TEST_ASSERT_EQUAL_INT(-1, sensor_format_fixed4(buf, sizeof buf, 1.5f));
}

static void test_message_matches_cloud_format(void)
{
  imu_sample_t s[2] = {
      {0.0f, -0.0657f, 1.0239f, -0.0021f, 0.0f, 0.5f},
      {1.0f, 2.0f, -3.0f, 0.1f, -0.2f, 0.3f},
  };
  char buf[256];
  int n = sensor_message_build(buf, sizeof buf, "rt-spark-01", 42, s, 2);
  const char *expected =
      "{\"device\":\"rt-spark-01\",\"seq\":42,\"samples\":["
      "[0.0000,-0.0657,1.0239,-0.0021,0.0000,0.5000],"
      "[1.0000,2.0000,-3.0000,0.1000,-0.2000,0.3000]]}";
  TEST_ASSERT_EQUAL_STRING(expected, buf);
  TEST_ASSERT_EQUAL_INT((int)strlen(expected), n);
}

static void test_full_message_fits_firmware_buffer(void)
{
  /* Worst case: every value at full scale with a sign. app.c uses a 512-byte buffer. */
  imu_sample_t s[SAMPLES_PER_MESSAGE];
  for (int i = 0; i < SAMPLES_PER_MESSAGE; i++)
    s[i] = (imu_sample_t){-16.0f, -16.0f, -16.0f, -34.9f, -34.9f, -34.9f};
  char buf[512];
  int n = sensor_message_build(buf, sizeof buf, "rt-spark-01", 4294967295u, s, SAMPLES_PER_MESSAGE);
  TEST_ASSERT_GREATER_THAN_INT(0, n);
  TEST_ASSERT_LESS_THAN_INT(512, n);
}

static void test_rejects_unsafe_device_names(void)
{
  imu_sample_t s = {0};
  char buf[256];
  TEST_ASSERT_EQUAL_INT(-1, sensor_message_build(buf, sizeof buf, "a\"b", 0, &s, 1));
  TEST_ASSERT_EQUAL_INT(-1, sensor_message_build(buf, sizeof buf, "a/b", 0, &s, 1));
  TEST_ASSERT_EQUAL_INT(-1, sensor_message_build(buf, sizeof buf, "a+b", 0, &s, 1));
  TEST_ASSERT_GREATER_THAN_INT(0, sensor_message_build(buf, sizeof buf, "Dev_1-x", 0, &s, 1));
}

static void test_rejects_bad_arguments_and_small_buffer(void)
{
  imu_sample_t s = {0};
  char buf[256];
  TEST_ASSERT_EQUAL_INT(-1, sensor_message_build(NULL, sizeof buf, "d", 0, &s, 1));
  TEST_ASSERT_EQUAL_INT(-1, sensor_message_build(buf, 0, "d", 0, &s, 1));
  TEST_ASSERT_EQUAL_INT(-1, sensor_message_build(buf, sizeof buf, NULL, 0, &s, 1));
  TEST_ASSERT_EQUAL_INT(-1, sensor_message_build(buf, sizeof buf, "d", 0, NULL, 1));
  TEST_ASSERT_EQUAL_INT(-1, sensor_message_build(buf, sizeof buf, "d", 0, &s, 0));
  TEST_ASSERT_EQUAL_INT(-1, sensor_message_build(buf, 40, "d", 0, &s, 1));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_fixed4_formats_four_decimals);
  RUN_TEST(test_fixed4_tiny_negative_has_no_minus_zero);
  RUN_TEST(test_fixed4_clamps_and_handles_nan);
  RUN_TEST(test_fixed4_reports_small_buffer);
  RUN_TEST(test_message_matches_cloud_format);
  RUN_TEST(test_full_message_fits_firmware_buffer);
  RUN_TEST(test_rejects_unsafe_device_names);
  RUN_TEST(test_rejects_bad_arguments_and_small_buffer);
  return UNITY_END();
}
