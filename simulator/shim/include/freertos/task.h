#ifndef SIM_FREERTOS_TASK_H
#define SIM_FREERTOS_TASK_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack,
                       void *arg, UBaseType_t prio, TaskHandle_t *out_handle);

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name,
                                   uint32_t stack, void *arg, UBaseType_t prio,
                                   TaskHandle_t *out_handle, BaseType_t core);

void vTaskDelete(TaskHandle_t h);
void vTaskDelay(TickType_t ticks);
void vTaskSuspend(TaskHandle_t h);
void vTaskResume(TaskHandle_t h);

TaskHandle_t xTaskGetCurrentTaskHandle(void);
TaskHandle_t xTaskGetHandle(const char *name);

#ifdef __cplusplus
}
#endif

#endif
