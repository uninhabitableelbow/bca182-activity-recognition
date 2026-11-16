/* app_log.c - thread-safe logging to USART1, also used by printf (lwIP assertions). */
#include "app_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "usart.h"

static SemaphoreHandle_t log_mutex;

void app_log_init(void)
{
  if (log_mutex == NULL)
    log_mutex = xSemaphoreCreateMutex();
}

static void uart_write(const char *text, size_t len)
{
  HAL_UART_Transmit(&huart1, (const uint8_t *)text, (uint16_t)len, 200);
}

void app_log(const char *fmt, ...)
{
  char line[192];
  int n = snprintf(line, sizeof line, "[%7lu] ", (unsigned long)xTaskGetTickCount());
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line + n, sizeof line - (size_t)n - 2, fmt, ap);
  va_end(ap);
  strcat(line, "\r\n");

  int locked = log_mutex != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
  if (locked)
    xSemaphoreTake(log_mutex, portMAX_DELAY);
  uart_write(line, strlen(line));
  if (locked)
    xSemaphoreGive(log_mutex);
}

/* newlib's printf ends up here (overrides the weak _write in syscalls.c). */
int _write(int file, char *ptr, int len)
{
  (void)file;
  uart_write(ptr, (size_t)len);
  return len;
}
