#include "watch.hpp"
#include "ui.hpp"

#include <sys/time.h>
#include "driver/gpio.h"
#include "esp_lvgl_port.h"

#define BACKLIGHT_FADE_MS 2000

Watch watch;

uint16_t prevBrightness = 100;

// Touch INT (GPIO5) edge ISR: set by the kernel when the CST816S pulls
// INT low for a touch event. Cleared by pm_update once consumed.
static volatile bool touch_interrupt = false;

static void IRAM_ATTR touch_isr(void *arg)
{
    touch_interrupt = true;
    // Mask further IRQs on this pin until pm_update has read the touch
    // data (which clears INT on the CST816S). With the level-low wake
    // source still armed alongside, an un-cleared INT would otherwise
    // re-fire constantly.
    gpio_intr_disable(GPIO_NUM_5);
}

/** Update power management and sleep logic */
// Rotation-aware wrist-tilt detector. Returns true if the gyro shows a
// "lift to look at the watch" rotation strong enough to count. Shared by
// the fade-cancel path (awake but goingtosleep) and the asleep wake
// path. The mapping picks the gyro axis + sign whose positive rotation
// brings the screen's "up" toward the user's face:
//   0°    → -Y
//   90°   → -X
//   180°  → +Y
//   270°  → +X
static bool tilt_wake_detected(float *out_signed_dps = nullptr)
{
    constexpr float TILT_WAKE_DPS = 250.0f;
    GyroData g = gyro_read();
    float axis_dps = 0;
    switch (lv_display_get_rotation(NULL))
    {
        case LV_DISPLAY_ROTATION_0:   axis_dps = -g.y; break;
        case LV_DISPLAY_ROTATION_90:  axis_dps = -g.x; break;
        case LV_DISPLAY_ROTATION_180: axis_dps =  g.y; break;
        case LV_DISPLAY_ROTATION_270: axis_dps =  g.x; break;
    }
    if (out_signed_dps) *out_signed_dps = axis_dps;
    return axis_dps > TILT_WAKE_DPS;
}

void Watch::pm_update()
{
    while (true)
    {
        if (!this->sleeping) // if awake
        {
            int64_t now_ms = esp_timer_get_time() / 1000;

            // (Fade-cancel for tilt lives inside Watch::sleep itself —
            // pm_task is blocked inside that call for the whole fade,
            // so a check here would never run during the fade window.)

            if (now_ms < this->prevent_sleep_until_ms)
            {
                // Stay-awake hold (alarm / timer ring screen). Roll the
                // idle clock forward so the normal timeout starts fresh
                // once the hold ends.
                this->sleep_time = (uint32_t)now_ms;
            }
            else if (this->system.dosleep && now_ms - this->sleep_time > watch.system.sleeptime && !this->goingtosleep)
            {
                sleep();
            }
        }
        else
        {
            // The GPIO5 ISR latches touch_interrupt on the falling edge of
            // touch INT, even for taps shorter than the poll period. Trust
            // it as the source of truth — by the time we get here the
            // finger may already be lifted (so display.is_touching() would
            // return false), but we still want to wake because a touch
            // happened.
            if (touch_interrupt)
            {
                touch_interrupt = false;
                wakeup();
            }
            else
            {
                // Tilt-to-wake while asleep. See tilt_wake_detected for
                // the rotation-aware axis selection. The IMU stays at
                // 56 Hz gyro ODR through imu_sleep, so the I²C read is
                // a 6-byte burst — ~200 µs every 100 ms, well under 1 %
                // duty cycle, ~100 µA on top of the ~12 mA light-sleep
                // floor. The QMI8658's own WoM is accel-only and would
                // misfire on bumps (per CLAUDE.md), so we poll the gyro.
                float dps;
                if (tilt_wake_detected(&dps))
                {
                    ESP_LOGI("pm", "tilt wake (axis=%.0f dps)", dps);
                    wakeup();
                }
            }
        }

        // 100 ms while sleeping is a compromise: long enough that the
        // tilt-wake I²C read and FreeRTOS-tick wakeup don't dominate
        // average current, short enough that touch / tilt wake feels
        // reasonably responsive.
        vTaskDelay(pdMS_TO_TICKS(this->sleeping ? 100 : 50));
    }
}

/** Enter sleep mode */
/** Ask the pm task to sleep on its next iteration. Doesn't itself touch
 *  the display or LVGL — those run on the pm task to keep the calling
 *  task (typically an LVGL timer callback) responsive. */
void Watch::request_sleep()
{
    this->prevent_sleep_until_ms = 0;
    this->sleep_time = 0; // wall-clock idle is now huge → pm_task triggers sleep
}

void Watch::sleep() //! DO NOT TOUCH, IS A CAREFULLY BALANCED PILE OF LOGIC THAT ONLY WORKS THIS WAY
{
    if (!this->sleeping)
    {
        ESP_LOGI("pm", "SLEEP");

        goingtosleep = true;

        display.set_wakeup_touch(true);

        prevBrightness = display.get_brightness();

        display.set_backlight_gradual(0, BACKLIGHT_FADE_MS);
        // Wait out the fade in small chunks so we can poll the gyro
        // for a tilt-cancel mid-fade. A single vTaskDelay(FADE_MS) would
        // block pm_task for the whole fade and the pm_update loop's
        // tilt check would never see this window. 100 ms chunks match
        // the asleep-poll cadence; touch-cancel still works because the
        // LVGL task (which clears goingtosleep on press) is on a
        // different core and isn't blocked by our delay here.
        {
            uint16_t elapsed = 0;
            while (elapsed < BACKLIGHT_FADE_MS && this->goingtosleep)
            {
                uint16_t step = (BACKLIGHT_FADE_MS - elapsed) > 100
                                    ? 100
                                    : (BACKLIGHT_FADE_MS - elapsed);
                vTaskDelay(pdMS_TO_TICKS(step));
                elapsed += step;

                float dps;
                if (tilt_wake_detected(&dps))
                {
                    ESP_LOGI("pm", "tilt cancel fade (axis=%.0f dps)", dps);
                    this->goingtosleep = false;
                    display.set_backlight(prevBrightness);
                    this->sleep_time = (uint32_t)(esp_timer_get_time() / 1000);
                    break;
                }
            }
        }

        // make sure the display hasn't been touched while the display was fading off
        if (goingtosleep)
        {
            // Backlight is now fully off — dismiss any notification popup
            // with no animation so the screen swap is invisible. Doing
            // this before display.sleep() means lv_screen_active() is the
            // pre-popup screen when LVGL gets suspended.
            notification_popup_dismiss_now();

            // Drop any side screen (alarm editor, alarm ring, etc.) and
            // park on main_screen with no animation before LVGL is
            // suspended. Leaving e.g. setalarmscr active meant the user
            // would wake to a half-faded screen because the fade-in
            // anim that loaded it was paused mid-flight by sleep —
            // resuming it on wake also tripped a draw-buffer alloc.
            //
            // Exception: when preserve_screen_on_sleep is set, leave
            // the active screen alone so the alarm/timer ring screen
            // is what shows up on next wake.
            if (lvgl_port_lock(0))
            {
                if (!preserve_screen_on_sleep && lv_screen_active() != main_screen)
                    lv_screen_load_anim(main_screen,
                                        LV_SCREEN_LOAD_ANIM_NONE, 0, 0, false);
                // Clear any animations still in flight on the just-loaded
                // screen (e.g. a half-completed fade-in) so nothing
                // continues drawing once LVGL resumes on wake.
                lv_anim_delete_all();
                lvgl_port_unlock();
            }

            display.sleep();
            imu_sleep();

            // Drain any latched CST816S INT before arming wake sources, so
            // GPIO5 starts in its idle (high) state — otherwise a leftover
            // low level rejects sleep entry immediately.
            display.reset_touch();

            // Arm GPIO5 both as a level-low light-sleep wake source (so a
            // touch wakes the chip from deep light sleep within μs) and as
            // a regular neg-edge ISR (so even a sub-poll-period blip is
            // latched into touch_interrupt and serviced by the next
            // pm_update iteration).
            touch_interrupt = false;
            gpio_intr_enable(GPIO_NUM_5);
            esp_sleep_enable_gpio_wakeup();
            gpio_wakeup_enable(GPIO_NUM_5, GPIO_INTR_LOW_LEVEL);

            esp_pm_lock_release(pm_freq_lock);
            esp_pm_lock_release(pm_sleep_lock);

            this->sleeping = true;

            sleep_time = esp_timer_get_time() / 1000;
        }
    }
}

/** Exit sleep mode */
void Watch::wakeup() //! DO NOT TOUCH, IS A CAREFULLY BALANCED PILE OF LOGIC THAT ONLY WORKS THIS WAY
{
    static bool wakeup_in_progress = false; // Add this guard

    if (goingtosleep && !this->sleeping)
    {
        display.set_backlight(prevBrightness);
    }

    goingtosleep = false;

    if (this->sleeping && !wakeup_in_progress)
    { // Check the guard
        ESP_LOGI("pm", "WAKEUP");

        wakeup_in_progress = true; // Set guard

        // Disarm the touch wake source and ISR — pm_update polling will
        // service touches normally during awake state via lvgl_touch_read.
        gpio_wakeup_disable(GPIO_NUM_5);
        gpio_intr_disable(GPIO_NUM_5);
        touch_interrupt = false;

        esp_pm_lock_acquire(pm_freq_lock);
        esp_pm_lock_acquire(pm_sleep_lock);

        imu_wake();

        // Resume the LVGL task before any lvgl_port_lock attempt below —
        // if the task got suspended in display.sleep() while it happened
        // to hold the mutex, lvgl_port_lock would otherwise deadlock.
        TaskHandle_t lvgl_task = xTaskGetHandle("taskLVGL");
        if (lvgl_task) vTaskResume(lvgl_task);

        auto diff = esp_timer_get_time() / 1000 - sleep_time;

        // pm_update task runs on core 0, LVGL task on core 1 — wrap any LVGL
        // mutation in lvgl_port_lock so we don't race with the renderer.
        if (lvgl_port_lock(0))
        {
            // Skip the wake-time scroll-back if the user was on the
            // lower row (notifications / music). They're almost
            // certainly there mid-task and snapping them back to the
            // watch face is annoying.
            bool on_lower = false;
            if (ver_layer && lower_layer)
            {
                int32_t sy = lv_obj_get_scroll_y(ver_layer);
                int32_t ly = lv_obj_get_y(lower_layer);
                on_lower = (sy >= ly - 50);
            }

            // Exception: the music screen only earns the stay-put
            // treatment when there's actually music playing. If the
            // user stopped/paused playback before the watch slept,
            // the screen has nothing dynamic going on, and waking
            // back to it instead of the watch face is just clutter.
            if (on_lower && musicscr && lower_layer)
            {
                int32_t mx_in_scroll =
                    lv_obj_get_x(musicscr) - lv_obj_get_scroll_x(lower_layer);
                bool on_music = mx_in_scroll > -120 && mx_in_scroll < 120;
                if (on_music && ble.music().state != "play")
                    on_lower = false;
            }

            if (!on_lower)
            {
                if (diff > 15000)
                {
                    lv_obj_scroll_to_view_recursive(watchscr, LV_ANIM_OFF); // unconditionally go to watch face
                }
                else if (lv_screen_active() == main_screen)
                {
                    lv_obj_scroll_to_view_recursive(hor_layer, LV_ANIM_OFF); // only scroll vertical layer back
                }
            }
            lvgl_port_unlock();
        }

        display.wake();

        display.set_rotation(lv_display_get_rotation(NULL));

        // If a notification arrived while we were asleep (which is what
        // triggered this wake in the common case), present it as the
        // active screen *before* anything renders — that way the first
        // frame painted with the backlight off is the popup itself, and
        // when the backlight comes on the user sees it directly without
        // a flash of the watch face underneath.
        if (lvgl_port_lock(0))
        {
            notification_popup_present_now();
            lvgl_port_unlock();
        }

        // The panel's VRAM is undefined coming back from disp_off — and
        // even if it were preserved, LVGL won't issue any draw operations
        // unless something is dirty. Invalidate the active screen and
        // force an immediate render so we don't wait up to 500 ms for the
        // LVGL task's next loop iteration to pick it up.
        if (lvgl_port_lock(0))
        {
            lv_obj_t *active = lv_screen_active();
            if (active) lv_obj_invalidate(active);
            lv_refr_now(NULL);
            lvgl_port_unlock();
        }

        display.set_backlight(prevBrightness);

        this->sleeping = false;

        wakeup_in_progress = false; // Clear guard
    }

    this->sleeping = false;

    this->sleep_time = esp_timer_get_time() / 1000;
}

/** Initialise power management */
void Watch::pm_init()
{
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 10,
        .light_sleep_enable = true,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "freq_lock", &pm_freq_lock));
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "sleep_lock", &pm_sleep_lock));

    esp_pm_lock_acquire(pm_freq_lock);
    esp_pm_lock_acquire(pm_sleep_lock);

    // pm task runs Watch::sleep()/Watch::wakeup() inline — both touch
    // LVGL (lvgl_port_stop/lock, screen-active checks, lv_refr_now on
    // wake) which can use a fair chunk of stack on screens with deep
    // widget trees (the alarms screen with its arc + scale + several
    // labels was overflowing the previous 6 KB on a sleep transition).
    xTaskCreatePinnedToCore([](void *pvParameters)
                            {
                                auto *obj = static_cast<Watch *>(pvParameters);
                                obj->pm_update(); },
                            "pm", 1024 * 12, this, 0, &pm_task, 0);
}

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000

/** Initialise I²C */
void Watch::iic_init()
{

    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = IIC_SDA,
        .scl_io_num = IIC_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {.enable_internal_pullup = true},
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus));
}

void Watch::i2c_scan()
{
    ESP_LOGI("scan", "Scanning I2C bus...");
    for (uint8_t addr = 1; addr < 127; addr++)
    {
        uint8_t data = 0;
        esp_err_t ret = i2c_master_probe(i2c_bus, addr, 100);
        if (ret == ESP_OK)
        {
            ESP_LOGI("scan", "Found device at 0x%02X", addr);
        }
    }
    ESP_LOGI("scan", "I2C scan complete.");
}

/** Initialise all watch subsystems */
void Watch::init()
{
    display.init_memory();

    gpio_install_isr_service(ESP_INTR_FLAG_EDGE);

    setenv("TZ", "PST8PDT,M3.2.0/2,M11.1.0/2", 1);
    tzset();

    pm_init();
    watch.settings.init();
    iic_init();

    haptic_init();

    battery_init();

    imu_init(i2c_bus);

    display.init(i2c_bus);

    // Touch INT GPIO ISR — latches touch_interrupt=true on every neg-edge.
    // Configured here (not in display.init) because the GPIO pin is also
    // used as a light-sleep wake source by Watch::sleep(). gpio_intr_enable
    // is left off until sleep entry.
    gpio_config_t touch_int_cfg = {};
    touch_int_cfg.pin_bit_mask = 1ULL << GPIO_NUM_5;
    touch_int_cfg.mode = GPIO_MODE_INPUT;
    touch_int_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    touch_int_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    touch_int_cfg.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&touch_int_cfg);
    gpio_isr_handler_add(GPIO_NUM_5, touch_isr, NULL);
    gpio_intr_disable(GPIO_NUM_5);

    // display.refresh();

    display.set_backlight(100);

    ble.init();

    // Register apps that can't be created during ui_init because their
    // boot-time widget allocation would starve the BT controller's malloc.
    lvgl_port_lock(0);
    unistroke_register_app();
    lvgl_port_unlock();

    haptic_play(false, 80, 80, 80, 80, 80, 0); // vibrate 3 times

    // i2c_scan();
}

/** Wrapper for C -> C++ shenanigans */
void watch_init()
{
    watch.init();
}