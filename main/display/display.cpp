// Display + LVGL pipeline for GWatch (Waveshare ESP32-S3-Touch-LCD-1.28: GC9A01 + CST816S).
//
// Performance choices:
//  - 80 MHz SPI pclk to the GC9A01.
//  - Two DMA-capable buffers in internal SRAM, 1/4 frame each (60 lines), ping-ponged
//    by esp_lvgl_port: CPU renders one buffer while DMA streams the other.
//  - swap_bytes is done by the LCD peripheral (free, no CPU cost).
//  - LVGL runs in its own FreeRTOS task spawned by esp_lvgl_port, pinned to APP_CPU.
//    Cross-task LVGL mutation goes through lvgl_port_lock/unlock.

#include "display.hpp"
#include "watch.hpp"

#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>

static const char *TAG = "display";

// -------------------- LCD --------------------
#define LCD_HOST            SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ  (80 * 1000 * 1000)
#define LCD_CMD_BITS        8
#define LCD_PARAM_BITS      8
#define LCD_BIT_PER_PIXEL   16

// One DMA buffer = LCD_WIDTH * LVGL_BUF_LINES pixels (RGB565).
// 60 * 240 * 2 = 28 800 B per buffer; double-buffered -> 57 600 B in internal SRAM.
#define LVGL_BUF_LINES 60

// -------------------- Touch --------------------
#define TOUCH_I2C_CLK_HZ 400000
#define TOUCH_PIN_RST    (gpio_num_t)13
#define TOUCH_PIN_INT    GPIO_NUM_NC  // see note in touch_init()

// -------------------- Backlight (LEDC PWM) --------------------
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES   LEDC_TIMER_13_BIT  // 13-bit: 0..8191
#define LEDC_FREQUENCY  5000

// -------------------- Backlight ---------------------------------

uint16_t Display::get_brightness() { return bgval; }

void Display::set_backlight_gradual(int16_t val, uint32_t ms)
{
    int32_t tempbk;
    old_backlight = bgval;
    tempbk = (val - old_backlight);
    endtime   = (esp_timer_get_time() / 1000) + ms;
    starttime = esp_timer_get_time() / 1000;

    k = (float)((tempbk * 1.0) / ms);

    adjust = true;
    vTaskResume(backlight_handle);
}

void Display::set_backlight(int16_t val, bool stopgrad)
{
    if (stopgrad)
        adjust = false;

    uint32_t duty = 0;

    if (val < 100)
    {
        duty = (uint32_t)((val * val * 0.8192) + 0.5);
        bgval = val;
    }
    else
    {
        duty = 0x1FFF;
        bgval = 100;
    }

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

void Display::backlight_update()
{
    while (1)
    {
        static uint32_t Millis;
        static uint32_t duration;

        if (adjust)
        {
            uint32_t mils = esp_timer_get_time() / 1000;
            uint32_t time_ms = 1;

            if (mils - Millis > time_ms)
            {
                duration = mils - starttime;
                set_backlight((uint16_t)((k * duration) + old_backlight), false);

                if (Millis > endtime)
                {
                    adjust = false;
                }

                Millis = mils;
            }
        }
        else
        {
            vTaskSuspend(backlight_handle);
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// -------------------- Rotation --------------------

void Display::set_rotation(lv_display_rotation_t rotation)
{
    // esp_lvgl_port expects rotation through its own API so it can re-set the
    // panel's MADCTL and re-arm any flush state.
    lvgl_port_rotation_cfg_t cfg = {};
    switch (rotation)
    {
        case LV_DISPLAY_ROTATION_0:   cfg = { .swap_xy = false, .mirror_x = true,  .mirror_y = false }; break;
        case LV_DISPLAY_ROTATION_90:  cfg = { .swap_xy = true,  .mirror_x = true,  .mirror_y = true  }; break;
        case LV_DISPLAY_ROTATION_180: cfg = { .swap_xy = false, .mirror_x = false, .mirror_y = true  }; break;
        case LV_DISPLAY_ROTATION_270: cfg = { .swap_xy = true,  .mirror_x = false, .mirror_y = false }; break;
    }

    lvgl_port_lock(0);
    lv_display_set_rotation(disp, rotation);
    esp_lcd_panel_swap_xy(panel, cfg.swap_xy);
    esp_lcd_panel_mirror(panel, cfg.mirror_x, cfg.mirror_y);
    lvgl_port_unlock();
}

// -------------------- Touch wake-suppression --------------------

// Set in Watch::sleep() so the very first touch after wake (the same finger
// press that woke the watch) does not propagate as a click into the UI.
// Cleared after the finger lifts, in lvgl_touch_read.
bool wakeup_touch = false;

static esp_lcd_touch_handle_t s_touch = NULL;

bool Display::is_touching()
{
    if (!touch) return false;
    uint16_t x = 0, y = 0;
    uint8_t pts = 0;
    esp_lcd_touch_read_data(touch);
    return esp_lcd_touch_get_coordinates(touch, &x, &y, NULL, &pts, 1) && pts > 0;
}

void Display::reset_touch()
{
    if (touch) esp_lcd_touch_read_data(touch);
}

void Display::set_wakeup_touch(bool enable)
{
    wakeup_touch = enable;
}

// Custom LVGL indev read callback. Mirrors what esp_lvgl_port's built-in touch
// wrapper does, plus drops the first press-while-still-suppressed so a touch-
// to-wake doesn't fire the button underneath the finger.
static void lvgl_touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    static uint16_t last_x = 0, last_y = 0;
    if (!s_touch) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    uint16_t x = 0, y = 0;
    uint8_t  pts = 0;
    esp_lcd_touch_read_data(s_touch);
    bool pressed = esp_lcd_touch_get_coordinates(s_touch, &x, &y, NULL, &pts, 1) && pts > 0;

    if (pressed) {
        // Reset the sleep timer / clear pending-sleep on every touch — without
        // this the pm task only sees touches when sleeping, so the screen
        // dims off while you're using it.
        watch.wakeup();

        last_x = x;
        last_y = y;
        // suppress the wake-press until the finger lifts
        data->state = wakeup_touch ? LV_INDEV_STATE_REL : LV_INDEV_STATE_PR;
    } else {
        wakeup_touch = false;
        data->state = LV_INDEV_STATE_REL;
    }

    data->point.x = last_x;
    data->point.y = last_y;
}

// -------------------- Panel + touch init --------------------

static void panel_init(esp_lcd_panel_io_handle_t *out_io,
                       esp_lcd_panel_handle_t    *out_panel)
{
    spi_bus_config_t bus_cfg = {};
    bus_cfg.sclk_io_num   = LCD_CLK;
    bus_cfg.mosi_io_num   = LCD_MOSI;
    bus_cfg.miso_io_num   = -1;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = LCD_WIDTH * LVGL_BUF_LINES * sizeof(uint16_t) + 16;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.cs_gpio_num       = LCD_CS;
    io_cfg.dc_gpio_num       = LCD_DC;
    io_cfg.spi_mode          = 0;
    io_cfg.pclk_hz           = LCD_PIXEL_CLOCK_HZ;
    // Deep queue lets SPI keep streaming while LVGL renders the next chunk.
    io_cfg.trans_queue_depth = 10;
    io_cfg.lcd_cmd_bits      = LCD_CMD_BITS;
    io_cfg.lcd_param_bits    = LCD_PARAM_BITS;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, out_io));

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = LCD_RST;
    panel_cfg.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_cfg.bits_per_pixel = LCD_BIT_PER_PIXEL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(*out_io, &panel_cfg, out_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(*out_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*out_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(*out_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*out_panel, true));
}

static void touch_init(i2c_master_bus_handle_t bus,
                       esp_lcd_panel_io_handle_t *out_io,
                       esp_lcd_touch_handle_t    *out_touch)
{
    // Hand-rolled instead of ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG() because that
    // macro orders its designated initializers wrong for C++ — scl_speed_hz is
    // declared last in the struct but listed first in the macro, which g++
    // rejects under -std=gnu++2b.
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
    tp_io_cfg.dev_addr            = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
    tp_io_cfg.control_phase_bytes = 1;
    tp_io_cfg.dc_bit_offset       = 0;
    tp_io_cfg.lcd_cmd_bits        = 8;
    tp_io_cfg.lcd_param_bits      = 0;
    tp_io_cfg.flags.disable_control_phase = 1;
    tp_io_cfg.scl_speed_hz        = TOUCH_I2C_CLK_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &tp_io_cfg, out_io));

    // INT pin left disconnected (GPIO_NUM_NC). esp_lvgl_port polls touch from the
    // LVGL task at refresh rate; CST816S firmware variants pulse INT inconsistently
    // and event-mode reads frequently break.
    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max        = LCD_WIDTH;
    tp_cfg.y_max        = LCD_HEIGHT;
    tp_cfg.rst_gpio_num = TOUCH_PIN_RST;
    tp_cfg.int_gpio_num = TOUCH_PIN_INT;
    tp_cfg.levels.reset      = 0;
    tp_cfg.levels.interrupt  = 0;
    tp_cfg.flags.swap_xy   = 0;
    tp_cfg.flags.mirror_x  = 0;
    tp_cfg.flags.mirror_y  = 0;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(*out_io, &tp_cfg, out_touch));

    // Pin CST816S in active reporting (DisAutoSleep = 0x01). Without this the
    // chip NACKs touch-data reads after ~hundreds of ms idle and the polling
    // touch read panics inside esp_lvgl_port.
    const uint8_t disable_auto_sleep = 0x01;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(*out_io, 0xFE, &disable_auto_sleep, 1));
}

// -------------------- Init --------------------

// esp_lvgl_port allocates the ping-pong DMA buffers itself inside
// lvgl_port_add_disp(); init_memory() is kept only so callers (Watch::init)
// don't have to change.
void Display::init_memory()
{
}

void Display::init_graphics()
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_affinity   = 1;     // pin LVGL to APP_CPU; PRO_CPU stays free for IRQs/wifi
    port_cfg.task_priority   = 4;
    port_cfg.task_stack      = 8192;
    port_cfg.timer_period_ms = 5;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle     = panel_io;
    disp_cfg.panel_handle  = panel;
    disp_cfg.buffer_size   = LCD_WIDTH * LVGL_BUF_LINES;
    disp_cfg.double_buffer = true;
    disp_cfg.hres          = LCD_WIDTH;
    disp_cfg.vres          = LCD_HEIGHT;
    disp_cfg.monochrome    = false;
    disp_cfg.rotation      = { .swap_xy = false, .mirror_x = true, .mirror_y = false };
    disp_cfg.flags.buff_dma    = true;
    disp_cfg.flags.buff_spiram = false;
    disp_cfg.flags.swap_bytes  = true;  // RGB565 LE -> BE on the LCD peripheral
    disp = lvgl_port_add_disp(&disp_cfg);

    // Custom indev so we can drop the wake-press; see lvgl_touch_read above.
    s_touch = touch;
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_read);
    lv_indev_set_display(indev, disp);

    // ui_init() touches LVGL objects; must be inside the lock.
    lvgl_port_lock(0);
    set_rotation(LV_DISPLAY_ROTATION_90);
    ui_init();
    lvgl_port_unlock();
}

void Display::init(i2c_master_bus_handle_t bus)
{
    panel_init(&panel_io, &panel);
    touch_init(bus, &tp_io, &touch);

    init_graphics();

    // Backlight PWM
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode      = LEDC_MODE;
    ledc_timer.duty_resolution = LEDC_DUTY_RES;
    ledc_timer.timer_num       = LEDC_TIMER;
    ledc_timer.freq_hz         = LEDC_FREQUENCY;
    ledc_timer.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {};
    ledc_channel.gpio_num   = LCD_BLK;
    ledc_channel.speed_mode = LEDC_MODE;
    ledc_channel.channel    = LEDC_CHANNEL;
    ledc_channel.intr_type  = LEDC_INTR_DISABLE;
    ledc_channel.timer_sel  = LEDC_TIMER;
    ledc_channel.duty       = 0;
    ledc_channel.hpoint     = 0;
    ledc_channel_config(&ledc_channel);

    xTaskCreatePinnedToCore([](void *pvParameters)
                            {
                                auto *obj = static_cast<Display *>(pvParameters);
                                obj->backlight_update(); },
                            "backlight", 1024 * 4, this, 2, &backlight_handle, 0);
}

// -------------------- Sleep / wake --------------------

void Display::sleep()
{
    if (suspended) return;

    // Stop the LVGL tick first (no new timers fire), then take the lock to
    // serialize with any in-flight lv_timer_handler iteration before we power
    // the panel down.
    lvgl_port_stop();
    if (lvgl_port_lock(portMAX_DELAY)) {
        // GC9A01 panel driver doesn't implement disp_sleep — disp_on_off is
        // enough to blank the panel; the LEDC backlight is what makes it look
        // off, and lvgl_port_stop halts rendering so SPI is idle.
        esp_lcd_panel_disp_on_off(panel, false);
        lvgl_port_unlock();
    }
    suspended = true;
}

void Display::wake()
{
    if (!suspended) return;

    if (lvgl_port_lock(portMAX_DELAY)) {
        esp_lcd_panel_disp_on_off(panel, true);
        lvgl_port_unlock();
    }
    lvgl_port_resume();
    suspended = false;
}

// Kept for ABI compatibility with the previous pipeline; no-op now because
// sleep() is synchronous (lvgl_port_stop blocks until the task is parked).
void Display::lvgl_done()
{
}

void Display::refresh()
{
    if (lvgl_port_lock(0)) {
        lv_obj_invalidate(lv_screen_active());
        lvgl_port_unlock();
    }
}
