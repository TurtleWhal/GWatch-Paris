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
// Rotation-aware wrist-tilt detector — FADE-CANCEL ONLY. Used during the
// 2 s backlight fade in Watch::sleep(), where the gyro is still powered
// (imu_sleep runs after the fade) and a single strong rotation reading
// means "the user moved, don't sleep". The asleep→wake path uses the
// accel-only wrist-raise state machine in imu.cpp instead — the gyro is
// fully off during sleep. The mapping picks the gyro axis + sign whose
// positive rotation brings the screen's "up" toward the user's face:
//   0°    → -Y
//   90°   → -X
//   180°  → +Y
//   270°  → +X
static bool tilt_wake_detected(float *out_signed_dps = nullptr)
{
    // 150 dps: a deliberate wrist-raise peaks well above this; the cost
    // of a false cancel is only one more idle-timeout cycle of screen
    // time, so this can afford to be far more sensitive than the old
    // 250 dps shared-with-wake threshold that needed exaggerated motion.
    constexpr float TILT_WAKE_DPS = 150.0f;
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
            else if (this->system.tiltwake)
            {
                // Wrist-raise wake. Accel-only orientation state machine
                // (see imu.cpp) — the end state of a glance persists for
                // hundreds of ms, so a 10 Hz poll can't miss it, unlike
                // the old instantaneous-gyro-sample approach that only
                // caught violent motions. The gyro is fully powered down
                // during sleep; the QMI8658's own WoM stays unused (it's
                // broken on this chip revision, per CLAUDE.md). With
                // tiltwake off the poll (and its I²C read) is skipped
                // entirely — touch wake still works via the GPIO IRQ.
                if (imu_wrist_raise_poll())
                {
                    ESP_LOGI("pm", "wrist-raise wake");
                    // Buzz BEFORE wakeup(): haptic_play just queues to the
                    // motor task (non-blocking), while wakeup() spends
                    // ~0.5 s on BLE renegotiation + panel re-init + first
                    // repaint. Queuing first means the tactile "got it"
                    // lands at the moment of detection instead of after
                    // the display pipeline is back up.
                    if (this->system.tiltwake_buzz)
                        haptic_play(false, 100, 0);
                    wakeup();
                }
            }
        }

        // 100 ms while sleeping. Touch wake is GPIO-IRQ driven and
        // doesn't depend on this poll period — only the wrist-raise
        // detector does, and its arm/dwell counts are calibrated for
        // 10 Hz. Each iteration is a brief wake from light sleep
        // (~6-byte I²C read at low CPU freq); the ~0.2 mA this adds is
        // more than paid for by the gyro (~0.6 mA) now being fully off
        // while asleep.
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
                if (this->system.tiltwake && tilt_wake_detected(&dps))
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

            // Ask the central to relax the connection interval. Done
            // before display/imu teardown so any back-and-forth with the
            // phone happens while we're still at the higher CPU freq —
            // and so the slow params are active by the time we drop the
            // pm locks below. The previous 30–60 ms request fired ~22
            // radio events / s; 320–400 ms drops that to ~3.
            ble.request_low_power_conn_params();

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

        // Re-request the fast interactive conn params right away. The
        // negotiation rides the next connection event (which at the
        // slow params arrives in up to ~400 ms), so doing this before
        // display.wake() means the link is usually already back to
        // 30–60 ms by the time the user is poking the screen.
        ble.request_normal_conn_params();

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
            // Skip the wake-time scroll-back ONLY when the user was on
            // the music screen AND music is currently playing — that's
            // the single case where leaving them there is useful
            // (active media control). For any other lower-row screen,
            // or for music with playback stopped/paused, snap back to
            // the watch face just like we would from any other screen.
            bool preserve_lower = false;
            if (ver_layer && lower_layer && musicscr)
            {
                int32_t sy = lv_obj_get_scroll_y(ver_layer);
                int32_t ly = lv_obj_get_y(lower_layer);
                bool on_lower_row = (sy >= ly - 50);

                int32_t mx_in_scroll =
                    lv_obj_get_x(musicscr) - lv_obj_get_scroll_x(lower_layer);
                bool on_music = (mx_in_scroll > -120 && mx_in_scroll < 120);

                preserve_lower = on_lower_row && on_music &&
                                 ble.music().state == "play";
            }

            if (!preserve_lower)
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

            // Sync the music screen to whatever musicstate/musicinfo/
            // album-art updates arrived via BLE during sleep, and
            // rebase its local position clock so the sleep duration
            // isn't added to the displayed playback time. Done before
            // the lv_refr_now below so the first frame on backlight
            // restore already shows the up-to-date music state. No-op
            // if music_create hasn't been called or has been retired.
            music_refresh();

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

    // Load schedules from config.json into the Schedule class's
    // in-memory vectors. Must run after Settings::init has mounted
    // SPIFFS but before anything reads schedule state — the watchface
    // update timer calls watch.schedule.getText() every tick.
    watch.schedule.init();

    // Persisted wake preferences. Defaults (both on) apply on first boot
    // or after config.json is missing / wiped; the settings screen
    // writes these keys back when toggled.
    system.tiltwake = settings.readBool("settings", "tiltwake", true);
    system.tiltwake_buzz = settings.readBool("settings", "vibrateontilt", true);

    iic_init();

    // ble.init() FIRST among the heavyweight subsystems. The BT
    // controller asks for a ~30 KB contiguous internal-RAM block
    // (emi.c:164 / `param 0x7800`); after LVGL's draw buffers, font
    // copies, and several task stacks have been allocated, the largest
    // free internal block is too small and ble_init aborts with
    // "BLE_INIT: Malloc failed" + an EMI param assert. NimBLE doesn't
    // depend on display/IMU/battery, so it's safe to run before them.
    ble.init();

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

    // (ble.init() now runs above, before display.init, so the BT
    // controller's 30 KB malloc can land on a contiguous internal-RAM
    // block before LVGL fragments the heap.)

    // Register apps that can't be created during ui_init because their
    // boot-time widget allocation would starve the BT controller's malloc.
    lvgl_port_lock(0);
    unistroke_register_app();
    lvgl_port_unlock();

    haptic_play(false, 80, 80, 80, 80, 80, 0); // vibrate 3 times

    // i2c_scan();
}

// ----- Low-battery shutdown + early-boot voltage check -----
//
// When the cell rail dips below CRITICAL_MV battery_task calls
// low_battery_shutdown(). That tears down BLE/IMU/display and drops
// the chip into deep sleep with a 30 s timer wake. Each wake re-runs
// app_main, which calls battery_early_check_or_sleep_again() FIRST —
// if the rail is still under SAFE_BOOT_MV it re-enters deep sleep
// without bringing up LVGL or BLE, so we don't hit the ~80 mA boot
// burst on a dead cell and trip the protection circuit again. Plug
// in USB → rail jumps to ~4.5 V → next wake passes the check and
// proceeds with normal init.

#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

// Sleep period between checks while the cell is below SAFE_BOOT_MV.
// 30 s keeps average current under 30 µA (10 µA deep sleep + a brief
// ADC wake) while still recovering within seconds of plug-in.
#define LOW_BAT_WAKE_PERIOD_US (30LL * 1000 * 1000)

// Rail values (post-divider × BAT_DIVIDER_RATIO).
//
// SAFE_BOOT_MV needs LARGE hysteresis above the shutdown trigger
// (which fires under load around 2.95 V): once we deep-sleep, load
// drops to ~10 µA and the cell's voltage sag relaxes — a depleted
// cell that read 2.95 V loaded can easily rebound to 3.2–3.4 V
// unloaded, and continues drifting up as chemistry settles. Without
// the hysteresis, the next timer wake passes a low threshold (e.g.
// 3.1 V) → app_main brings everything up → ~80 mA cold boot burst
// → rail collapses again → shutdown re-trips → cell-relax-and-boot
// loop, same as the original brown-out loop we set out to fix.
//
// 3.7 V is the right cut-off: it's comfortably above what a
// depleted cell can rebound to on this board (~3.4 V max even
// after long rest) but well below what USB plug-in produces
// (3.7–4.5 V rail — see CLAUDE.md "Hardware quirks (board)").
// In effect this means "only boot when USB is plugged in," which
// is exactly the behaviour we want.
#define SAFE_BOOT_MV 3700

static bool sample_battery_mv_once(uint32_t *out_mv)
{
    adc_oneshot_unit_handle_t adc = nullptr;
    adc_cali_handle_t cali = nullptr;
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&unit_cfg, &adc) != ESP_OK) return false;
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(adc, ADC_CHANNEL_0 /* GPIO1, BAT_ADC */, &ch_cfg);

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = ADC_CHANNEL_0,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali);

    // Cheap oversample — 8 reads, ~1 ms total. We just need a coarse
    // "is the rail >= SAFE_BOOT" answer, not the smoothed value the
    // main battery_task produces.
    int sum_mv = 0, got = 0;
    for (int i = 0; i < 8; i++)
    {
        int raw = 0, mv = 0;
        if (adc_oneshot_read(adc, ADC_CHANNEL_0, &raw) != ESP_OK) continue;
        if (adc_cali_raw_to_voltage(cali, raw, &mv) != ESP_OK) continue;
        sum_mv += mv;
        got++;
    }
    if (cali) adc_cali_delete_scheme_curve_fitting(cali);
    if (adc)  adc_oneshot_del_unit(adc);
    if (got == 0) return false;
    *out_mv = (uint32_t)((sum_mv / got) * 3 /* BAT_DIVIDER_RATIO */);
    return true;
}

// Persistent "we entered low-battery shutdown" marker. RTC_NOINIT_ATTR
// memory survives every reset cause EXCEPT a full power-on reset
// (POR — i.e. battery physically removed or completely flat) so the
// flag also survives a brown-out reset that happens mid-shutdown
// sequence. The early-boot check uses BOTH the wake-cause AND this
// marker so that a brown-out mid-shutdown still ends up in the
// re-sleep path instead of doing a full boot.
//
// The "valid" sentinel is a 32-bit magic; any other value (including
// the random garbage RTC RAM holds on first power-up) is treated as
// "no marker." Cleared once we successfully boot through the check.
#define LOW_BAT_MARKER_MAGIC 0xB47B47B4u
static RTC_NOINIT_ATTR uint32_t s_low_bat_marker;

void Watch::low_battery_shutdown()
{
    ESP_LOGW("pm", "low-battery shutdown");

    // Set the persistent marker FIRST, before doing anything else.
    // If we brown-out mid-tear-down, the marker is still set, and the
    // next boot's early check will re-sleep instead of running full
    // watch_init.
    s_low_bat_marker = LOW_BAT_MARKER_MAGIC;

    // Kill the heavy loads BEFORE the user-visible message. BLE radio
    // bursts and IMU polling are what tip the cell over the brown-out
    // edge during the shutdown window — turning them off first means
    // the message renders on a much steadier rail, and even if we
    // crash partway through, the marker (above) gets us out next
    // boot.
    ble.set_enabled(false);
    imu_sleep();

    // Brief on-screen warning at a dimmed backlight. lvgl_port_lock
    // failure is non-fatal — we still power down even if LVGL wedged.
    if (lvgl_port_lock(0))
    {
        lv_obj_t *scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_t *l = lv_label_create(scr);
        lv_label_set_text(l, "Battery dead\nplug in to charge");
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_center(l);
        lv_screen_load(scr);
        lv_refr_now(NULL);
        lvgl_port_unlock();
    }
    display.set_backlight(40);   // lower than before (less rail load)
    vTaskDelay(pdMS_TO_TICKS(1500));

    display.set_backlight(0);
    display.sleep();

    // Clear ANY wake source left over from prior sleep cycles
    // (Watch::sleep arms a GPIO5 level-low wake source — if we don't
    // disarm it, esp_deep_sleep_start can return immediately as soon
    // as the touch line is low, looking like "the chip woke up
    // before sleep even finished").
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup(LOW_BAT_WAKE_PERIOD_US);

    esp_deep_sleep_start();
}

extern "C" void battery_early_check_or_sleep_again(void)
{
    // Run the check on EITHER a timer wake (the happy path) OR on any
    // reset where the persistent marker is still set (the brown-out-
    // mid-shutdown path). POR clears RTC RAM, so a fresh power-up
    // sees the marker as garbage and falls through to a normal boot.
    bool from_timer = (esp_sleep_get_wakeup_causes() &
                       BIT(ESP_SLEEP_WAKEUP_TIMER)) != 0;
    bool marker_set = (s_low_bat_marker == LOW_BAT_MARKER_MAGIC);
    if (!from_timer && !marker_set)
        return;

    uint32_t mv = 0;
    if (!sample_battery_mv_once(&mv))
    {
        // ADC failed — fall through to full boot rather than getting
        // stuck in a wake-can't-read-sleep loop forever.
        s_low_bat_marker = 0; // don't keep retrying the early check
        return;
    }

    ESP_LOGI("pm", "early battery check: %u mV (need %u)",
             (unsigned)mv, (unsigned)SAFE_BOOT_MV);

    if (mv >= SAFE_BOOT_MV)
    {
        // Rail recovered (USB plugged in, or cell genuinely rebounded
        // above the safe-boot threshold). Clear the marker so future
        // resets — including ones unrelated to low battery — don't keep
        // re-running this early check on every boot.
        s_low_bat_marker = 0;
        return; // proceed with normal watch_init
    }

    // Still flat. Back to sleep. No subsystems were brought up yet,
    // so the only ongoing draw is RTC + the ADC bring-up we just
    // ran (which the deinit calls above tore back down).
    esp_sleep_enable_timer_wakeup(LOW_BAT_WAKE_PERIOD_US);
    esp_deep_sleep_start();
}

/** Wrapper for C -> C++ shenanigans */
void watch_init()
{
    watch.init();
}