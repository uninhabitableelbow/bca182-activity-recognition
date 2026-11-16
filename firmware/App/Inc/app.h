/* app.h - Lab 3 device application (called from the CubeMX-generated freertos.c). */
#ifndef APP_H
#define APP_H

void app_init(void);           /* before the scheduler starts: queues, events, logging */
void app_sensor_task(void);    /* body of SensorTask  */
void app_network_task(void);   /* body of NetworkTask */

void app_stack_overflow(const char *task_name);   /* FreeRTOS hooks */
void app_malloc_failed(void);

#endif
