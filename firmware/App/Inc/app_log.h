/* app_log.h - thread-safe printf-style logging to USART1 (ST-Link virtual COM port, 115200 8N1). */
#ifndef APP_LOG_H
#define APP_LOG_H

void app_log_init(void);
void app_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
