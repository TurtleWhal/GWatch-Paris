#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "display.h"

#define TAG "GC9A01"

static spi_device_handle_t spi;

// Pin definitions
#define PIN_MOSI 11
#define PIN_CLK 10
#define PIN_CS 9
#define PIN_DC 8
#define PIN_RST 14
#define PIN_BCKL 2

// GC9A01 initialization commands (same as before)
typedef struct
{
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes;
} lcd_init_cmd_t;

static const lcd_init_cmd_t gc9a01_init_cmds[] = {
    {0xEF, {0}, 0},
    {0xEB, {0x14}, 1},
    {0xFE, {0}, 0},
    {0xEF, {0}, 0},
    {0xEB, {0x14}, 1},
    {0x84, {0x40}, 1},
    {0x85, {0xFF}, 1},
    {0x86, {0xFF}, 1},
    {0x87, {0xFF}, 1},
    {0x88, {0x0A}, 1},
    {0x89, {0x21}, 1},
    {0x8A, {0x00}, 1},
    {0x8B, {0x80}, 1},
    {0x8C, {0x01}, 1},
    {0x8D, {0x01}, 1},
    {0x8E, {0xFF}, 1},
    {0x8F, {0xFF}, 1},
    {0xB6, {0x00, 0x00}, 2},
    {0x3A, {0x05}, 1},
    {0x90, {0x08, 0x08, 0x08, 0x08}, 4},
    {0xBD, {0x06}, 1},
    {0xBC, {0x00}, 1},
    {0xFF, {0x60, 0x01, 0x04}, 3},
    {0xC3, {0x13}, 1},
    {0xC4, {0x13}, 1},
    {0xC9, {0x22}, 1},
    {0xBE, {0x11}, 1},
    {0xE1, {0x10, 0x0E}, 2},
    {0xDF, {0x21, 0x0c, 0x02}, 3},
    {0xF0, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6},
    {0xF1, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6},
    {0xF2, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6},
    {0xF3, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6},
    {0xED, {0x1B, 0x0B}, 2},
    {0xAE, {0x77}, 1},
    {0xCD, {0x63}, 1},
    {0x70, {0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03}, 9},
    {0xE8, {0x34}, 1},
    {0x62, {0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70}, 12},
    {0x63, {0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70}, 12},
    {0x64, {0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}, 7},
    {0x66, {0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}, 10},
    {0x67, {0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}, 10},
    {0x74, {0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00}, 7},
    {0x98, {0x3e, 0x07}, 2},
    {0x35, {0}, 0},
    {0x21, {0}, 0},
    {0x11, {0}, 0x80},
    {0x29, {0}, 0x80},
    {0x36, {0x48}, 1}, // default
    {0, {0}, 0xff},
};

static void spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc = (int)t->user;
    gpio_set_level(PIN_DC, dc);
}

static void gc9a01_send_cmd(uint8_t cmd)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &cmd;
    t.user = (void *)0;
    ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
}

void gc9a01_send_data(const uint8_t *data, int len)
{
    esp_err_t ret;
    spi_transaction_t t;
    if (len == 0)
        return;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    t.user = (void *)1;
    ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
}

static void gc9a01_init(void)
{
    // Initialize non-SPI GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = ((1ULL << PIN_DC) | (1ULL << PIN_RST) | (1ULL << PIN_BCKL)),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Reset the display
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Initialize SPI
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISP_HOR_RES * 80 * sizeof(uint16_t),
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 7,
        .pre_cb = spi_pre_transfer_callback,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi));

    // Send initialization commands
    int cmd = 0;
    while (gc9a01_init_cmds[cmd].databytes != 0xff)
    {
        gc9a01_send_cmd(gc9a01_init_cmds[cmd].cmd);
        gc9a01_send_data(gc9a01_init_cmds[cmd].data, gc9a01_init_cmds[cmd].databytes & 0x1F);
        if (gc9a01_init_cmds[cmd].databytes & 0x80)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        cmd++;
    }

    // Turn on backlight
    gpio_set_level(PIN_BCKL, 1);
}

void gc9a01_set_addr_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t data[4];

    gc9a01_send_cmd(0x2A); // Column addr set
    data[0] = x1 >> 8;
    data[1] = x1 & 0xFF;
    data[2] = x2 >> 8;
    data[3] = x2 & 0xFF;
    gc9a01_send_data(data, 4);

    gc9a01_send_cmd(0x2B); // Row addr set
    data[0] = y1 >> 8;
    data[1] = y1 & 0xFF;
    data[2] = y2 >> 8;
    data[3] = y2 & 0xFF;
    gc9a01_send_data(data, 4);

    gc9a01_send_cmd(0x2C); // Write to RAM
}

void gc9a01_sleep(void)
{
    // Turn off display (content goes black)
    gc9a01_send_cmd(0x28);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Enter sleep mode
    gc9a01_send_cmd(0x10);
    vTaskDelay(pdMS_TO_TICKS(120)); // Spec requires ~120ms

    // Turn off backlight
    // gpio_set_level(PIN_BCKL, 0);

    ESP_LOGI(TAG, "Display put to sleep");
}

void gc9a01_wake(void)
{
    // Turn on backlight
    // gpio_set_level(PIN_BCKL, 1);

    // Exit sleep mode
    gc9a01_send_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120)); // Wait for the controller to wake

    // Turn on display
    gc9a01_send_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Display woken up");
}

void gc9a01_driver_init(void)
{
    gc9a01_init();
    gpio_set_level(PIN_BCKL, 1);

    ESP_LOGI(TAG, "GC9A01 display driver initialized");
}