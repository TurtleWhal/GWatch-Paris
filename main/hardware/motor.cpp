#include "watch.hpp"
#include "driver/gpio.h"

#define MAX_CONTROLS 12

TaskHandle_t motor_task;

uint16_t control[MAX_CONTROLS];
bool spinning = false;

void motor_task_handler(void *pvParameters)
{

    while (true)
    {
        if (control[0] > 0)
        {
            gpio_set_level(MOTOR_PIN1, spinning);
            gpio_set_level(MOTOR_PIN2, spinning);

            vTaskDelay(pdMS_TO_TICKS(control[0]));

            gpio_set_level(MOTOR_PIN1, 0);
            gpio_set_level(MOTOR_PIN2, 0);

            spinning = !spinning;

            for (int i = 0; i < MAX_CONTROLS - 1; i++)
            {
                control[i] = control[i + 1];
            }
            control[MAX_CONTROLS - 1] = 0;

            // Only suspend if there are no more vibrations queued
            if (control[0] == 0)
            {
                vTaskSuspend(NULL);
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(10)); // Add small delay instead of immediate suspension
        }
    }
}

/** Initialize the motor subsystem */
void motor_init()
{
    gpio_set_direction(MOTOR_PIN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(MOTOR_PIN2, GPIO_MODE_OUTPUT);

    gpio_set_level(MOTOR_PIN1, 0);
    gpio_set_level(MOTOR_PIN2, 0);

    // xTaskCreate(
    //     motor_task_handler,
    //     "motor_task",
    //     2048,
    //     nullptr,
    //     5,
    //     &motor_task);
}

/** Start a vibration pattern.
 * For Example: vibrate(100, 50, 200); will vibrate for 100ms, pause for 50ms, then vibrate for 200ms.
 * @param duration_ms Duration of the first vibration in milliseconds
 * @param ... Subsequent durations (vibration and pause) in milliseconds
 * @note A value of 0 will end the pattern early
 */
void Watch::vibrate(uint16_t duration_ms, ...)
{
    return;
    for (int i = 0; i < MAX_CONTROLS; i++)
    {
        control[i] = 0;
    }

    va_list args;
    va_start(args, duration_ms);

    // Clear control array and set it to arguments
    control[0] = duration_ms;

    for (int i = 1; i < MAX_CONTROLS; ++i)
    {
        int d = va_arg(args, int); // smaller integer types are promoted to int
        if (d <= 0)
        {
            control[i] = 0; // 0 ends the pattern early
            break;
        }
        control[i] = static_cast<uint16_t>(d);
    }

    va_end(args);

    spinning = true;

    // Only resume if task is actually suspended
    eTaskState state = eTaskGetState(motor_task);
    if (state == eSuspended)
    {
        vTaskResume(motor_task);
    }
}