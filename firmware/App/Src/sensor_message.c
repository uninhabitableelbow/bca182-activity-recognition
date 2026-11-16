/* sensor_message.c - JSON payload for one second of IMU readings (see sensor_message.h). */
#include "sensor_message.h"

#include <stdio.h>
#include <string.h>

int sensor_format_fixed4(char *buf, size_t size, float v)
{
  /* Clamp to a sane range: the sensor's full scale is +-8 g / +-35 rad/s. */
  if (!(v == v))
    v = 0.0f;                                  /* NaN -> 0 */
  if (v > 999.0f) v = 999.0f;
  if (v < -999.0f) v = -999.0f;

  long scaled = (long)(v * 10000.0f + (v >= 0 ? 0.5f : -0.5f));
  const char *sign = scaled < 0 ? "-" : "";
  if (scaled < 0)
    scaled = -scaled;
  int n = snprintf(buf, size, "%s%ld.%04ld", sign, scaled / 10000, scaled % 10000);
  return (n < 0 || (size_t)n >= size) ? -1 : n;
}

/* Appends text to buf at *pos; returns 0 when it does not fit. */
static int append(char *buf, size_t size, size_t *pos, const char *text)
{
  size_t len = strlen(text);
  if (*pos + len >= size)
    return 0;
  memcpy(buf + *pos, text, len + 1);
  *pos += len;
  return 1;
}

int sensor_message_build(char *buf, size_t size, const char *device, uint32_t seq,
                         const imu_sample_t *samples, int count)
{
  if (buf == NULL || size == 0 || device == NULL || samples == NULL || count < 1)
    return -1;
  /* The device name goes into JSON and the MQTT topic: allow a safe character set only. */
  for (const char *c = device; *c; c++) {
    if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
          *c == '-' || *c == '_'))
      return -1;
  }

  size_t pos = 0;
  char tmp[48];
  buf[0] = '\0';
  snprintf(tmp, sizeof tmp, "\",\"seq\":%lu,\"samples\":[", (unsigned long)seq);
  if (!append(buf, size, &pos, "{\"device\":\"") || !append(buf, size, &pos, device) ||
      !append(buf, size, &pos, tmp))
    return -1;

  for (int i = 0; i < count; i++) {
    const float v[6] = {samples[i].ax, samples[i].ay, samples[i].az,
                        samples[i].gx, samples[i].gy, samples[i].gz};
    if (!append(buf, size, &pos, i ? ",[" : "["))
      return -1;
    for (int k = 0; k < 6; k++) {
      if (sensor_format_fixed4(tmp, sizeof tmp, v[k]) < 0 ||
          (k && !append(buf, size, &pos, ",")) || !append(buf, size, &pos, tmp))
        return -1;
    }
    if (!append(buf, size, &pos, "]"))
      return -1;
  }
  if (!append(buf, size, &pos, "]}"))
    return -1;
  return (int)pos;
}
