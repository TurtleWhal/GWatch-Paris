#include "lvgl.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include <string.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"

#define TAG "MAIN"

// Adjust these to your setup
#define I2C_PORT I2C_NUM_0
#define TOUCH_RST_GPIO 13
#define TOUCH_INT_GPIO 5

#define CST816S_ADDRESS 0x15

enum GESTURE
{
    NONE = 0x00,
    SWIPE_UP = 0x01,
    SWIPE_DOWN = 0x02,
    SWIPE_LEFT = 0x03,
    SWIPE_RIGHT = 0x04,
    SINGLE_CLICK = 0x05,
    DOUBLE_CLICK = 0x0B,
    LONG_PRESS = 0x0C

};

touch_data data;

uint8_t i2c_read(uint16_t addr, uint8_t reg_addr, uint8_t *reg_data, size_t length);
uint8_t i2c_write(uint8_t addr, uint8_t reg_addr, const uint8_t *reg_data, size_t length);

/*!
    @brief  read touch data
*/
void read_touch()
{
    uint8_t data_raw[8];
    i2c_read(CST816S_ADDRESS, 0x01, data_raw, 6);

    data.gestureID = data_raw[0];
    data.points = data_raw[1];
    data.event = data_raw[2] >> 6;
    data.x = ((data_raw[2] & 0xF) << 8) + data_raw[3];
    data.y = ((data_raw[4] & 0xF) << 8) + data_raw[5];
}

volatile bool _event_available;
// _Atomic volatile bool _event_available;

void IRAM_ATTR handleISR(void *arg)
{
    _event_available = true;
    // User ISR not called here; use gpio_isr_handler_add for custom callback
}

/*!
    @brief  enable double click
*/
void enable_double_click(void)
{
    const uint8_t enableDoubleTap = 0x01; // Set EnDClick (bit 0) to enable double-tap
    i2c_write(CST816S_ADDRESS, 0xEC, &enableDoubleTap, 1);
}

/*!
    @brief  Disable auto sleep mode
*/
void disable_auto_sleep(void)
{
    const uint8_t disableAutoSleep = 0xFE; // Non-zero value disables auto sleep
    i2c_write(CST816S_ADDRESS, 0xFE, &disableAutoSleep, 1);
}

/*!
    @brief  Enable auto sleep mode
*/
void enable_auto_sleep(void)
{
    const uint8_t enableAutoSleep = 0x00; // 0 value enables auto sleep
    i2c_write(CST816S_ADDRESS, 0xFE, &enableAutoSleep, 1);
}

void set_auto_sleep_time(int seconds)
{
    if (seconds < 1)
        seconds = 1;
    else if (seconds > 255)
        seconds = 255;
    uint8_t sleepTime = (uint8_t)seconds;
    i2c_write(CST816S_ADDRESS, 0xF9, &sleepTime, 1);
}

/*!
    @brief  check for a touch event
*/
bool cst816s_available(void)
{
    if (_event_available)
    {
        read_touch();
        _event_available = false;
        return true;
    }
    return false;
}

// Example for sleep function
void touchpad_sleep()
{
    gpio_set_level(TOUCH_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    uint8_t standby_value = 0x03;
    i2c_write(CST816S_ADDRESS, 0xA5, &standby_value, 1);
}

const char *gesture()
{
    switch (data.gestureID)
    {
    case NONE:
        return "NONE";
    case SWIPE_DOWN:
        return "SWIPE DOWN";
    case SWIPE_UP:
        return "SWIPE UP";
    case SWIPE_LEFT:
        return "SWIPE LEFT";
    case SWIPE_RIGHT:
        return "SWIPE RIGHT";
    case SINGLE_CLICK:
        return "SINGLE CLICK";
    case DOUBLE_CLICK:
        return "DOUBLE CLICK";
    case LONG_PRESS:
        return "LONG PRESS";
    default:
        return "UNKNOWN";
    }
}

// I2C read (ESP-IDF)
uint8_t i2c_read(uint16_t addr, uint8_t reg_addr, uint8_t *reg_data, size_t length)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (length > 1)
    {
        i2c_master_read(cmd, reg_data, length - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, reg_data + length - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK) ? 0 : -1;
}

// I2C write (ESP-IDF)
uint8_t i2c_write(uint8_t addr, uint8_t reg_addr, const uint8_t *reg_data, size_t length)
{
    uint8_t *write_buf = malloc(length + 1);
    if (!write_buf)
        return -1;
    write_buf[0] = reg_addr;
    memcpy(write_buf + 1, reg_data, length);
    esp_err_t ret = i2c_master_write_to_device(I2C_PORT, addr, write_buf, length + 1, pdMS_TO_TICKS(100));
    free(write_buf);
    return (ret == ESP_OK) ? 0 : -1;
}

touch_data cst816s_touch_read(void)
{
    return data;
}

void cst816s_init(void)
{
    // Set up GPIO directions
    gpio_set_direction(TOUCH_INT_GPIO, GPIO_MODE_INPUT);
    gpio_set_direction(TOUCH_RST_GPIO, GPIO_MODE_OUTPUT);

    gpio_set_intr_type(TOUCH_INT_GPIO, GPIO_INTR_NEGEDGE); // falling edge trigger
    gpio_set_pull_mode(TOUCH_INT_GPIO, GPIO_PULLUP_ONLY);  // CST816S INT pin is open-drain

    // Reset sequence for CST816S
    gpio_set_level(TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(TOUCH_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Read version and version info
    i2c_read(CST816S_ADDRESS, 0x15, &data.version, 1);

    vTaskDelay(pdMS_TO_TICKS(5));
    i2c_read(CST816S_ADDRESS, 0xA7, data.versionInfo, 3);

    enable_auto_sleep();
    set_auto_sleep_time(1);

    esp_sleep_enable_gpio_wakeup();

    // Attach ISR handler for touch interrupt
    gpio_isr_handler_add(TOUCH_INT_GPIO, handleISR, NULL);
}