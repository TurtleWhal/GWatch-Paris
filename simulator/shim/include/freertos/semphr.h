#ifndef SIM_FREERTOS_SEMPHR_H
#define SIM_FREERTOS_SEMPHR_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t        xSemaphoreTake(SemaphoreHandle_t h, TickType_t wait);
BaseType_t        xSemaphoreGive(SemaphoreHandle_t h);
void              vSemaphoreDelete(SemaphoreHandle_t h);

#ifdef __cplusplus
}
#endif

#endif
