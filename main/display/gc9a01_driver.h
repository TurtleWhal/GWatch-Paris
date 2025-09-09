#ifndef GC9A01_DMA_H
#define GC9A01_DMA_H

#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_log.h>

// Configuration defines - adjust these for your setup
#define GC9A01_SPI_HOST SPI2_HOST
#define GC9A01_PIN_MOSI 11
#define GC9A01_PIN_CLK 10
#define GC9A01_PIN_CS 9
#define GC9A01_PIN_DC 8
#define GC9A01_PIN_RST 14
#define GC9A01_PIN_BLK -1 // Set to -1 if not used

// Display dimensions
#define GC9A01_WIDTH 240
#define GC9A01_HEIGHT 240

// SPI settings
#define GC9A01_SPI_CLOCK_SPEED 80000000 // 80MHz - max for ESP32
#define GC9A01_DMA_CHAN SPI_DMA_CH_AUTO

// Commands
#define GC9A01_SLPIN 0x10
#define GC9A01_SLPOUT 0x11
#define GC9A01_INVOFF 0x20
#define GC9A01_INVON 0x21
#define GC9A01_DISPOFF 0x28
#define GC9A01_DISPON 0x29
#define GC9A01_CASET 0x2A
#define GC9A01_RASET 0x2B
#define GC9A01_RAMWR 0x2C
#define GC9A01_MADCTL 0x36
#define GC9A01_COLMOD 0x3A

// MADCTL bits
#define GC9A01_MADCTL_MY 0x80
#define GC9A01_MADCTL_MX 0x40
#define GC9A01_MADCTL_MV 0x20
#define GC9A01_MADCTL_ML 0x10
#define GC9A01_MADCTL_BGR 0x08
#define GC9A01_MADCTL_MH 0x04

static const char *TAG = "GC9A01_DMA";

typedef struct
{
    spi_device_handle_t spi;
    int pin_dc;
    int pin_rst;
    int pin_cs;
    int pin_blk;
    uint8_t rotation;
    bool swap_bytes;
    bool dma_enabled;
    SemaphoreHandle_t dma_mutex;
    bool transaction_started;
    uint16_t width;
    uint16_t height;
} gc9a01_handle_t;

static gc9a01_handle_t gc9a01;

// Helper function to send command
static void gc9a01_send_cmd(uint8_t cmd)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));

    gpio_set_level(gc9a01.pin_dc, 0); // Command mode
    t.length = 8;
    t.tx_buffer = &cmd;
    ret = spi_device_polling_transmit(gc9a01.spi, &t);
    assert(ret == ESP_OK);
}

// Helper function to send data
static void gc9a01_send_data(const uint8_t *data, int len)
{
    esp_err_t ret;
    spi_transaction_t t;

    if (len == 0)
        return;

    memset(&t, 0, sizeof(t));
    gpio_set_level(gc9a01.pin_dc, 1); // Data mode
    t.length = len * 8;
    t.tx_buffer = data;
    ret = spi_device_polling_transmit(gc9a01.spi, &t);
    assert(ret == ESP_OK);
}

// Helper function to send single data byte
static void gc9a01_send_data_byte(uint8_t data)
{
    gc9a01_send_data(&data, 1);
}

// Hardware reset
static void gc9a01_hard_reset(void)
{
    if (gc9a01.pin_rst >= 0)
    {
        gpio_set_level(gc9a01.pin_rst, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(gc9a01.pin_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Set address window
static void gc9a01_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];

    // Column address set
    gc9a01_send_cmd(GC9A01_CASET);
    data[0] = (x0 >> 8) & 0xFF;
    data[1] = x0 & 0xFF;
    data[2] = (x1 >> 8) & 0xFF;
    data[3] = x1 & 0xFF;
    gc9a01_send_data(data, 4);

    // Row address set
    gc9a01_send_cmd(GC9A01_RASET);
    data[0] = (y0 >> 8) & 0xFF;
    data[1] = y0 & 0xFF;
    data[2] = (y1 >> 8) & 0xFF;
    data[3] = y1 & 0xFF;
    gc9a01_send_data(data, 4);

    // Write to RAM
    gc9a01_send_cmd(GC9A01_RAMWR);
}

// Initialize DMA
esp_err_t initDMA(void)
{
    if (gc9a01.dma_mutex == NULL)
    {
        gc9a01.dma_mutex = xSemaphoreCreateMutex();
        if (gc9a01.dma_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create DMA mutex");
            return ESP_FAIL;
        }
    }
    gc9a01.dma_enabled = true;
    ESP_LOGI(TAG, "DMA initialized successfully");
    return ESP_OK;
}

// Initialize the display
esp_err_t begin(void)
{
    esp_err_t ret;

    // Initialize structure
    memset(&gc9a01, 0, sizeof(gc9a01_handle_t));
    gc9a01.pin_dc = GC9A01_PIN_DC;
    gc9a01.pin_rst = GC9A01_PIN_RST;
    gc9a01.pin_cs = GC9A01_PIN_CS;
    gc9a01.pin_blk = GC9A01_PIN_BLK;
    gc9a01.rotation = 0;
    gc9a01.swap_bytes = false;
    gc9a01.width = GC9A01_WIDTH;
    gc9a01.height = GC9A01_HEIGHT;

    // Configure GPIO pins
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << gc9a01.pin_dc);
    if (gc9a01.pin_rst >= 0)
    {
        io_conf.pin_bit_mask |= (1ULL << gc9a01.pin_rst);
    }
    if (gc9a01.pin_blk >= 0)
    {
        io_conf.pin_bit_mask |= (1ULL << gc9a01.pin_blk);
    }
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    // Configure SPI bus
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = GC9A01_PIN_MOSI,
        .sclk_io_num = GC9A01_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4092 // Hardware limit for ESP32 SPI DMA
    };

    ret = spi_bus_initialize(GC9A01_SPI_HOST, &buscfg, GC9A01_DMA_CHAN);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure SPI device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = GC9A01_SPI_CLOCK_SPEED,
        .mode = 0,
        .spics_io_num = gc9a01.pin_cs,
        .queue_size = 1, // Reduced queue size for better performance
        .flags = SPI_DEVICE_NO_DUMMY,
        .pre_cb = NULL,
        .post_cb = NULL};

    ret = spi_bus_add_device(GC9A01_SPI_HOST, &devcfg, &gc9a01.spi);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Hardware reset
    gc9a01_hard_reset();

    // Turn on backlight if configured
    if (gc9a01.pin_blk >= 0)
    {
        gpio_set_level(gc9a01.pin_blk, 1);
    }

    // Initialize display with GC9A01 specific commands
    gc9a01_send_cmd(0xEF);
    gc9a01_send_cmd(0xEB);
    gc9a01_send_data_byte(0x14);

    gc9a01_send_cmd(0xFE);
    gc9a01_send_cmd(0xEF);

    gc9a01_send_cmd(0xEB);
    gc9a01_send_data_byte(0x14);

    gc9a01_send_cmd(0x84);
    gc9a01_send_data_byte(0x40);

    gc9a01_send_cmd(0x85);
    gc9a01_send_data_byte(0xFF);

    gc9a01_send_cmd(0x86);
    gc9a01_send_data_byte(0xFF);

    gc9a01_send_cmd(0x87);
    gc9a01_send_data_byte(0xFF);

    gc9a01_send_cmd(0x88);
    gc9a01_send_data_byte(0x0A);

    gc9a01_send_cmd(0x89);
    gc9a01_send_data_byte(0x21);

    gc9a01_send_cmd(0x8A);
    gc9a01_send_data_byte(0x00);

    gc9a01_send_cmd(0x8B);
    gc9a01_send_data_byte(0x80);

    gc9a01_send_cmd(0x8C);
    gc9a01_send_data_byte(0x01);

    gc9a01_send_cmd(0x8D);
    gc9a01_send_data_byte(0x01);

    gc9a01_send_cmd(0x8E);
    gc9a01_send_data_byte(0xFF);

    gc9a01_send_cmd(0x8F);
    gc9a01_send_data_byte(0xFF);

    gc9a01_send_cmd(0xB6);
    gc9a01_send_data_byte(0x00);
    gc9a01_send_data_byte(0x20);

    gc9a01_send_cmd(GC9A01_MADCTL);
    gc9a01_send_data_byte(0x08);

    gc9a01_send_cmd(GC9A01_COLMOD);
    gc9a01_send_data_byte(0x05); // 16-bit color

    gc9a01_send_cmd(0x90);
    gc9a01_send_data_byte(0x08);
    gc9a01_send_data_byte(0x08);
    gc9a01_send_data_byte(0x08);
    gc9a01_send_data_byte(0x08);

    gc9a01_send_cmd(0xBD);
    gc9a01_send_data_byte(0x06);

    gc9a01_send_cmd(0xBC);
    gc9a01_send_data_byte(0x00);

    gc9a01_send_cmd(0xFF);
    gc9a01_send_data_byte(0x60);
    gc9a01_send_data_byte(0x01);
    gc9a01_send_data_byte(0x04);

    gc9a01_send_cmd(0xC3);
    gc9a01_send_data_byte(0x13);

    gc9a01_send_cmd(0xC4);
    gc9a01_send_data_byte(0x13);

    gc9a01_send_cmd(0xC9);
    gc9a01_send_data_byte(0x22);

    gc9a01_send_cmd(0xBE);
    gc9a01_send_data_byte(0x11);

    gc9a01_send_cmd(0xE1);
    gc9a01_send_data_byte(0x10);
    gc9a01_send_data_byte(0x0E);

    gc9a01_send_cmd(0xDF);
    gc9a01_send_data_byte(0x21);
    gc9a01_send_data_byte(0x0c);
    gc9a01_send_data_byte(0x02);

    gc9a01_send_cmd(0xF0);
    gc9a01_send_data_byte(0x45);
    gc9a01_send_data_byte(0x09);
    gc9a01_send_data_byte(0x08);
    gc9a01_send_data_byte(0x08);
    gc9a01_send_data_byte(0x26);
    gc9a01_send_data_byte(0x2A);

    gc9a01_send_cmd(0xF1);
    gc9a01_send_data_byte(0x43);
    gc9a01_send_data_byte(0x70);
    gc9a01_send_data_byte(0x72);
    gc9a01_send_data_byte(0x36);
    gc9a01_send_data_byte(0x37);
    gc9a01_send_data_byte(0x6F);

    gc9a01_send_cmd(0xF2);
    gc9a01_send_data_byte(0x45);
    gc9a01_send_data_byte(0x09);
    gc9a01_send_data_byte(0x08);
    gc9a01_send_data_byte(0x08);
    gc9a01_send_data_byte(0x26);
    gc9a01_send_data_byte(0x2A);

    gc9a01_send_cmd(0xF3);
    gc9a01_send_data_byte(0x43);
    gc9a01_send_data_byte(0x70);
    gc9a01_send_data_byte(0x72);
    gc9a01_send_data_byte(0x36);
    gc9a01_send_data_byte(0x37);
    gc9a01_send_data_byte(0x6F);

    gc9a01_send_cmd(0xED);
    gc9a01_send_data_byte(0x1B);
    gc9a01_send_data_byte(0x0B);

    gc9a01_send_cmd(0xAE);
    gc9a01_send_data_byte(0x77);

    gc9a01_send_cmd(0xCD);
    gc9a01_send_data_byte(0x63);

    gc9a01_send_cmd(0x70);
    gc9a01_send_data_byte(0x07);
    gc9a01_send_data_byte(0x07);
    gc9a01_send_data_byte(0x04);
    gc9a01_send_data_byte(0x0E);
    gc9a01_send_data_byte(0x0F);
    gc9a01_send_data_byte(0x09);
    gc9a01_send_data_byte(0x07);
    gc9a01_send_data_byte(0x08);
    gc9a01_send_data_byte(0x03);

    gc9a01_send_cmd(0xE8);
    gc9a01_send_data_byte(0x34);

    gc9a01_send_cmd(0x62);
    gc9a01_send_data_byte(0x18);
    gc9a01_send_data_byte(0x0D);
    gc9a01_send_data_byte(0x71);
    gc9a01_send_data_byte(0xED);
    gc9a01_send_data_byte(0x70);
    gc9a01_send_data_byte(0x70);
    gc9a01_send_data_byte(0x18);
    gc9a01_send_data_byte(0x0F);
    gc9a01_send_data_byte(0x71);
    gc9a01_send_data_byte(0xEF);
    gc9a01_send_data_byte(0x70);
    gc9a01_send_data_byte(0x70);

    gc9a01_send_cmd(0x63);
    gc9a01_send_data_byte(0x18);
    gc9a01_send_data_byte(0x11);
    gc9a01_send_data_byte(0x71);
    gc9a01_send_data_byte(0xF1);
    gc9a01_send_data_byte(0x70);
    gc9a01_send_data_byte(0x70);
    gc9a01_send_data_byte(0x18);
    gc9a01_send_data_byte(0x13);
    gc9a01_send_data_byte(0x71);
    gc9a01_send_data_byte(0xF3);
    gc9a01_send_data_byte(0x70);
    gc9a01_send_data_byte(0x70);

    gc9a01_send_cmd(0x64);
    gc9a01_send_data_byte(0x28);
    gc9a01_send_data_byte(0x29);
    gc9a01_send_data_byte(0xF1);
    gc9a01_send_data_byte(0x01);
    gc9a01_send_data_byte(0xF1);
    gc9a01_send_data_byte(0x00);
    gc9a01_send_data_byte(0x07);

    gc9a01_send_cmd(0x66);
    gc9a01_send_data_byte(0x3C);
    gc9a01_send_data_byte(0x00);
    gc9a01_send_data_byte(0xCD);
    gc9a01_send_data_byte(0x67);
    gc9a01_send_data_byte(0x45);
    gc9a01_send_data_byte(0x45);
    gc9a01_send_data_byte(0x10);
    gc9a01_send_data_byte(0x00);
    gc9a01_send_data_byte(0x00);
    gc9a01_send_data_byte(0x00);

    gc9a01_send_cmd(0x67);
    gc9a01_send_data_byte(0x00);
    gc9a01_send_data_byte(0x3C);
    gc9a01_send_data_byte(0x00);
    gc9a01_send_data_byte(0x00);
    gc9a01_send_data_byte(0x00);
    gc9a01_send_data_byte(0x01);
    gc9a01_send_data_byte(0x54);
    gc9a01_send_data_byte(0x10);
    gc9a01_send_data_byte(0x32);
    gc9a01_send_data_byte(0x98);

    gc9a01_send_cmd(0x74);
    gc9a01_send_data_byte(0x10);
    gc9a01_send_data_byte(0x85);
    gc9a01_send_data_byte(0x80);
    gc9a01_send_data_byte(0x00);
    gc9a01_send_data_byte(0x00);
    gc9a01_send_data_byte(0x4E);
    gc9a01_send_data_byte(0x00);

    gc9a01_send_cmd(0x98);
    gc9a01_send_data_byte(0x3e);
    gc9a01_send_data_byte(0x07);

    gc9a01_send_cmd(0x35);
    gc9a01_send_cmd(0x21); // Invert on

    gc9a01_send_cmd(GC9A01_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    gc9a01_send_cmd(GC9A01_DISPON);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Initialize DMA
    initDMA();

    ESP_LOGI(TAG, "GC9A01 display initialized successfully");
    return ESP_OK;
}

// Re-initialize SPI bus/device after sleep
esp_err_t gc9a01_spi_reinit(void)
{
    esp_err_t ret;

    // If bus is still active, free it first
    if (gc9a01.spi)
    {
        spi_bus_remove_device(gc9a01.spi);
        gc9a01.spi = NULL;
    }
    spi_bus_free(GC9A01_SPI_HOST);

    // Re-init SPI bus
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = GC9A01_PIN_MOSI,
        .sclk_io_num = GC9A01_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4092};
    ret = spi_bus_initialize(GC9A01_SPI_HOST, &buscfg, GC9A01_DMA_CHAN);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to reinitialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Re-add device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = GC9A01_SPI_CLOCK_SPEED,
        .mode = 0,
        .spics_io_num = gc9a01.pin_cs,
        .queue_size = 1,
        .flags = SPI_DEVICE_NO_DUMMY};
    ret = spi_bus_add_device(GC9A01_SPI_HOST, &devcfg, &gc9a01.spi);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to re-add SPI device: %s", esp_err_to_name(ret));
        return ret;
    }

    initDMA();
    ESP_LOGI(TAG, "SPI bus/device re-initialized successfully");
    return ESP_OK;
}

// Set byte swapping
void setSwapBytes(bool swap)
{
    gc9a01.swap_bytes = swap;
}

// Set display rotation
void gc9a01_setRotation(uint8_t rotation)
{
    gc9a01.rotation = rotation % 4;

    uint8_t madctl = 0;

    switch (gc9a01.rotation)
    {
    case 0:                         // Portrait
        madctl = GC9A01_MADCTL_BGR; // Removed MX to fix left-right mirroring
        gc9a01.width = GC9A01_WIDTH;
        gc9a01.height = GC9A01_HEIGHT;
        break;
    case 1: // Landscape
        madctl = GC9A01_MADCTL_MX | GC9A01_MADCTL_MV | GC9A01_MADCTL_BGR;
        gc9a01.width = GC9A01_HEIGHT;
        gc9a01.height = GC9A01_WIDTH;
        break;
    case 2: // Portrait (flipped)
        madctl = GC9A01_MADCTL_MX | GC9A01_MADCTL_MY | GC9A01_MADCTL_BGR;
        gc9a01.width = GC9A01_WIDTH;
        gc9a01.height = GC9A01_HEIGHT;
        break;
    case 3: // Landscape (flipped)
        madctl = GC9A01_MADCTL_MY | GC9A01_MADCTL_MV | GC9A01_MADCTL_BGR;
        gc9a01.width = GC9A01_HEIGHT;
        gc9a01.height = GC9A01_WIDTH;
        break;
    }

    gc9a01_send_cmd(GC9A01_MADCTL);
    gc9a01_send_data_byte(madctl);
}

// Start write transaction
void startWrite(void)
{
    if (gc9a01.dma_enabled && gc9a01.dma_mutex)
    {
        xSemaphoreTake(gc9a01.dma_mutex, portMAX_DELAY);
    }
    gc9a01.transaction_started = true;
}

// End write transaction
void endWrite(void)
{
    gc9a01.transaction_started = false;
    if (gc9a01.dma_enabled && gc9a01.dma_mutex)
    {
        xSemaphoreGive(gc9a01.dma_mutex);
    }
}

// Byte swap function
static void swap_bytes_inplace(uint16_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        data[i] = (data[i] << 8) | (data[i] >> 8);
    }
}

// Push image data using DMA
esp_err_t pushImageDMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *data)
{
    if (!gc9a01.spi)
    {
        ESP_LOGE(TAG, "Display not initialized");
        return ESP_FAIL;
    }

    if (!data)
    {
        ESP_LOGE(TAG, "Data pointer is NULL");
        return ESP_FAIL;
    }

    // Validate coordinates
    if (x >= gc9a01.width || y >= gc9a01.height)
    {
        ESP_LOGE(TAG, "Coordinates out of bounds");
        return ESP_FAIL;
    }

    // Clamp dimensions to display bounds
    if (x + w > gc9a01.width)
    {
        w = gc9a01.width - x;
    }
    if (y + h > gc9a01.height)
    {
        h = gc9a01.height - y;
    }

    if (w == 0 || h == 0)
    {
        return ESP_OK; // Nothing to draw
    }

    bool transaction_started_here = false;
    if (!gc9a01.transaction_started)
    {
        startWrite();
        transaction_started_here = true;
    }

    // Set address window
    gc9a01_set_addr_window(x, y, x + w - 1, y + h - 1);

    size_t total_pixels = w * h;

    // Handle byte swapping if needed - do this efficiently
    if (gc9a01.swap_bytes)
    {
        // Use ESP32 optimized byte swap
        for (size_t i = 0; i < total_pixels; i++)
        {
            data[i] = __builtin_bswap16(data[i]);
        }
    }

    esp_err_t ret = ESP_OK;

    // Use larger chunks - ESP32 can handle up to 4092 bytes (2046 pixels)
    const size_t max_pixels_per_chunk = 2000; // Leave some headroom
    size_t remaining_pixels = total_pixels;
    uint16_t *ptr = data;

    gpio_set_level(gc9a01.pin_dc, 1); // Data mode - set once

    // For full screen updates, use largest possible chunks
    if (total_pixels == (GC9A01_WIDTH * GC9A01_HEIGHT))
    {
        // Full screen - use maximum chunk size
        while (remaining_pixels > 0)
        {
            size_t current_chunk = (remaining_pixels > max_pixels_per_chunk) ? max_pixels_per_chunk : remaining_pixels;

            spi_transaction_t t = {0};
            t.length = current_chunk * 16; // 16 bits per pixel
            t.tx_buffer = ptr;
            t.flags = 0;

            ret = spi_device_transmit(gc9a01.spi, &t);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "DMA transfer failed: %s", esp_err_to_name(ret));
                break;
            }

            ptr += current_chunk;
            remaining_pixels -= current_chunk;
        }
    }
    else
    {
        // Smaller regions - still use DMA but may be faster with polling for very small areas
        if (total_pixels < 64)
        {
            // Very small transfers - use polling
            spi_transaction_t t = {0};
            t.length = total_pixels * 16;
            t.tx_buffer = data;
            ret = spi_device_polling_transmit(gc9a01.spi, &t);
        }
        else
        {
            // Medium transfers - use DMA with optimal chunking
            while (remaining_pixels > 0)
            {
                size_t current_chunk = (remaining_pixels > max_pixels_per_chunk) ? max_pixels_per_chunk : remaining_pixels;

                spi_transaction_t t = {0};
                t.length = current_chunk * 16;
                t.tx_buffer = ptr;

                ret = spi_device_transmit(gc9a01.spi, &t);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "DMA transfer failed: %s", esp_err_to_name(ret));
                    break;
                }

                ptr += current_chunk;
                remaining_pixels -= current_chunk;
            }
        }
    }

    // Restore byte order if we swapped
    if (gc9a01.swap_bytes)
    {
        for (size_t i = 0; i < total_pixels; i++)
        {
            data[i] = __builtin_bswap16(data[i]);
        }
    }

    if (transaction_started_here)
    {
        endWrite();
    }

    return ret;
}

// Convenience function to push image data to full screen
esp_err_t pushImageDMA_fullscreen(uint16_t *data)
{
    return pushImageDMA(0, 0, gc9a01.width, gc9a01.height, data);
}

// Get current display width (affected by rotation)
uint16_t getWidth(void)
{
    return gc9a01.width;
}

// Get current display height (affected by rotation)
uint16_t getHeight(void)
{
    return gc9a01.height;
}

// Fill screen with color
esp_err_t fillScreen(uint16_t color)
{
    // Allocate buffer for one line
    uint16_t *line_buffer = (uint16_t *)malloc(gc9a01.width * sizeof(uint16_t));
    if (!line_buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate line buffer");
        return ESP_FAIL;
    }

    // Fill buffer with color
    for (int i = 0; i < gc9a01.width; i++)
    {
        line_buffer[i] = color;
    }

    startWrite();

    // Send line by line to avoid large memory allocation
    for (int y = 0; y < gc9a01.height; y++)
    {
        esp_err_t ret = pushImageDMA(0, y, gc9a01.width, 1, line_buffer);
        if (ret != ESP_OK)
        {
            free(line_buffer);
            endWrite();
            return ret;
        }
    }

    endWrite();
    free(line_buffer);
    return ESP_OK;
}

// Draw pixel
esp_err_t drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= gc9a01.width || y >= gc9a01.height)
    {
        return ESP_FAIL;
    }

    return pushImageDMA(x, y, 1, 1, &color);
}

// Set display brightness (if backlight pin is configured)
void setBrightness(uint8_t brightness)
{
    if (gc9a01.pin_blk >= 0)
    {
        // Simple on/off control - for PWM brightness control, use ledc driver
        gpio_set_level(gc9a01.pin_blk, brightness > 0 ? 1 : 0);
    }
}

// Put display to sleep
void gc9a01_sleep(void)
{
    gc9a01_send_cmd(GC9A01_SLPIN);
    vTaskDelay(pdMS_TO_TICKS(5));
}

// Wake display from sleep
void gc9a01_wakeup(void)
{
    gc9a01_send_cmd(GC9A01_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));
}

// Turn display on
void displayOn(void)
{
    gc9a01_send_cmd(GC9A01_DISPON);
}

// Turn display off
void displayOff(void)
{
    gc9a01_send_cmd(GC9A01_DISPOFF);
}

// Invert display colors
void invertDisplay(bool invert)
{
    gc9a01_send_cmd(invert ? GC9A01_INVON : GC9A01_INVOFF);
}

// Get DMA status
bool isDMAEnabled(void)
{
    return gc9a01.dma_enabled;
}

// Cleanup function
void gc9a01_cleanup(void)
{
    esp_err_t ret;
    spi_transaction_t *rtrans;

    // Wait for all pending transactions
    while (gc9a01.transaction_started)
    {
        ret = spi_device_get_trans_result(gc9a01.spi, &rtrans, portMAX_DELAY);
        if (ret == ESP_OK)
        {
            gc9a01.transaction_started = false;
        }
    }

    // Remove device
    if (gc9a01.spi)
    {
        spi_bus_remove_device(gc9a01.spi);
        gc9a01.spi = NULL;
    }

    // Free bus
    spi_bus_free(GC9A01_SPI_HOST);
    ESP_LOGI(TAG, "GC9A01 SPI cleaned up");
}

// Color conversion utilities
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))
#define RGB888_TO_RGB565(r, g, b) RGB565(r, g, b)

// Common colors (RGB565 format)
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_BLUE 0x001F
#define COLOR_CYAN 0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_YELLOW 0xFFE0
#define COLOR_ORANGE 0xFD20
#define COLOR_GRAY 0x8410
#define COLOR_DARKGRAY 0x4208

/*
 * Usage Example:
 *
 * #include "gc9a01_dma.h"
 *
 * void app_main(void) {
 *     // Initialize display
 *     esp_err_t ret = begin();
 *     if (ret != ESP_OK) {
 *         ESP_LOGE("APP", "Failed to initialize display");
 *         return;
 *     }
 *
 *     // Set rotation (0-3)
 *     setRotation(0);
 *
 *     // Enable byte swapping if needed
 *     setSwapBytes(false);
 *
 *     // Fill screen with color
 *     fillScreen(COLOR_BLACK);
 *
 *     // Create image data
 *     uint16_t* image_data = malloc(100 * 100 * sizeof(uint16_t));
 *     for (int i = 0; i < 100 * 100; i++) {
 *         image_data[i] = COLOR_RED;
 *     }
 *
 *     // Push image using DMA
 *     startWrite();
 *     pushImageDMA(70, 70, 100, 100, image_data);
 *     endWrite();
 *
 *     free(image_data);
 * }
 */

#endif // GC9A01_DMA_H