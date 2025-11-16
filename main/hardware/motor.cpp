#include "watch.hpp"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define HAPTIC_TASK_STACK_SIZE 1024 * 4 // Stack size for haptic task
#define HAPTIC_TASK_PRIORITY 5          // Task priority
#define HAPTIC_QUEUE_SIZE 5             // Max queued patterns
#define MAX_PATTERN_STEPS 32            // Max steps in a pattern

static const char *TAG = "HAPTIC";

// Haptic pattern step structure
typedef struct
{
    uint16_t duration_ms; // Duration in milliseconds
    bool motor_on;        // true = vibrate, false = pause
} haptic_step_t;

// Haptic pattern structure
typedef struct
{
    haptic_step_t steps[MAX_PATTERN_STEPS];
    uint8_t num_steps;
    bool repeat; // Repeat pattern continuously
} haptic_pattern_t;

// Pattern command for queue
typedef struct
{
    haptic_pattern_t pattern;
    bool stop_current; // Stop current pattern immediately
} haptic_command_t;

// Global handles
static QueueHandle_t haptic_queue = NULL;
static TaskHandle_t haptic_task_handle = NULL;
static volatile bool stop_pattern = false;

// Initialize GPIO for motor control
static void haptic_gpio_init(void)
{
    // gpio_config_t io_conf = {
    //     .pin_bit_mask = (1ULL << MOTOR_GPIO),
    //     .mode = GPIO_MODE_OUTPUT,
    //     .pull_up_en = GPIO_PULLUP_DISABLE,
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .intr_type = GPIO_INTR_DISABLE,
    // };
    // gpio_config(&io_conf);
    // gpio_set_level(MOTOR_GPIO, 0); // Start with motor off

    gpio_set_direction(MOTOR_PIN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(MOTOR_PIN2, GPIO_MODE_OUTPUT);

    gpio_set_level(MOTOR_PIN1, 0);
    gpio_set_level(MOTOR_PIN2, 0);
}

// Set motor state
static inline void haptic_set_motor(bool on)
{
    if (watch.donotdisturb) // do not vibrate
        on = false;

    gpio_set_level(MOTOR_PIN1, on ? 1 : 0);
    gpio_set_level(MOTOR_PIN2, on ? 1 : 0);
}

// Play a single haptic pattern
static void haptic_play_pattern(const haptic_pattern_t *pattern)
{
    do
    {
        for (int i = 0; i < pattern->num_steps && !stop_pattern; i++)
        {
            const haptic_step_t *step = &pattern->steps[i];

            // Set motor state
            haptic_set_motor(step->motor_on);

            // if (step->motor_on)
            // {
            //     ESP_LOGI(TAG, "Vibrate for %d ms", step->duration_ms);
            // }
            // else
            // {
            //     ESP_LOGI(TAG, "Pause for %d ms", step->duration_ms);
            // }

            // Wait for step duration (check for stop every 10ms)
            uint16_t elapsed = 0;
            while (elapsed < step->duration_ms && !stop_pattern)
            {
                uint16_t delay = (step->duration_ms - elapsed) > 10 ? 10 : (step->duration_ms - elapsed);
                vTaskDelay(pdMS_TO_TICKS(delay));
                elapsed += delay;
            }
        }
    } while (pattern->repeat && !stop_pattern);

    // Ensure motor is off when pattern completes
    haptic_set_motor(false);
    stop_pattern = false;
}

// Haptic control task
static void haptic_task(void *pvParameters)
{
    haptic_command_t cmd;

    ESP_LOGI(TAG, "Haptic task started");

    while (1)
    {
        // Wait for pattern command
        if (xQueueReceive(haptic_queue, &cmd, portMAX_DELAY) == pdTRUE)
        {
            if (cmd.stop_current)
            {
                stop_pattern = true;
                vTaskDelay(pdMS_TO_TICKS(50)); // Give time to stop
            }

            // Play the pattern
            haptic_play_pattern(&cmd.pattern);
        }
    }
}

// Initialize haptic controller
esp_err_t haptic_init(void)
{
    // Initialize GPIO
    haptic_gpio_init();

    // Create queue for pattern commands
    haptic_queue = xQueueCreate(HAPTIC_QUEUE_SIZE, sizeof(haptic_command_t));
    if (haptic_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create haptic queue");
        return ESP_FAIL;
    }

    // Create haptic task
    BaseType_t ret = xTaskCreate(
        haptic_task,
        "haptic_task",
        HAPTIC_TASK_STACK_SIZE,
        NULL,
        HAPTIC_TASK_PRIORITY,
        &haptic_task_handle);

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create haptic task");
        vQueueDelete(haptic_queue);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Variadic function to play haptic pattern
// Usage: haptic_play(repeat, on1, off1, on2, off2, ..., 0)
// Last argument must be 0 to terminate the list
// Example: haptic_play(false, 100, 100, 200, 0) - vibrate 100ms, pause 100ms, vibrate 200ms
esp_err_t haptic_play(bool repeat, ...)
{
    haptic_pattern_t pattern = {0};
    pattern.repeat = repeat;
    pattern.num_steps = 0;

    va_list args;
    va_start(args, repeat);

    bool is_on = true; // Start with vibration
    int duration;

    while ((duration = va_arg(args, int)) != 0)
    {
        if (pattern.num_steps >= MAX_PATTERN_STEPS)
        {
            ESP_LOGW(TAG, "Pattern truncated at %d steps", MAX_PATTERN_STEPS);
            break;
        }

        pattern.steps[pattern.num_steps].duration_ms = duration;
        pattern.steps[pattern.num_steps].motor_on = is_on;
        pattern.num_steps++;

        is_on = !is_on; // Alternate between on and off
    }

    va_end(args);

    if (pattern.num_steps == 0)
    {
        ESP_LOGW(TAG, "Empty pattern, nothing to play");
        return ESP_ERR_INVALID_ARG;
    }

    haptic_command_t cmd = {
        .pattern = pattern,
        .stop_current = false};

    if (xQueueSend(haptic_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to queue pattern (queue full)");
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Variadic function to play haptic pattern and stop current one
// Same usage as haptic_play but stops any currently playing pattern first
esp_err_t haptic_play_now(bool repeat, ...)
{
    haptic_pattern_t pattern = {0};
    pattern.repeat = repeat;
    pattern.num_steps = 0;

    va_list args;
    va_start(args, repeat);

    bool is_on = true;
    int duration;

    while ((duration = va_arg(args, int)) != 0)
    {
        if (pattern.num_steps >= MAX_PATTERN_STEPS)
        {
            ESP_LOGW(TAG, "Pattern truncated at %d steps", MAX_PATTERN_STEPS);
            break;
        }

        pattern.steps[pattern.num_steps].duration_ms = duration;
        pattern.steps[pattern.num_steps].motor_on = is_on;
        pattern.num_steps++;

        is_on = !is_on;
    }

    va_end(args);

    if (pattern.num_steps == 0)
    {
        ESP_LOGW(TAG, "Empty pattern, nothing to play");
        return ESP_ERR_INVALID_ARG;
    }

    haptic_command_t cmd = {
        .pattern = pattern,
        .stop_current = true};

    if (xQueueSend(haptic_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to queue pattern (queue full)");
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Stop current pattern immediately
void haptic_stop(void)
{
    stop_pattern = true;
    haptic_set_motor(false);
}