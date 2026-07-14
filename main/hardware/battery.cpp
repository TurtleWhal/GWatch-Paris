#include "watch.hpp"
#include "ble.hpp"
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include "esp_log.h"

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc_cali_handle;

#define BAT_ADC_CHANNEL ADC_CHANNEL_0 // GPIO1
#define BAT_DIVIDER_RATIO 3.0f        // (R13 + R14) / R14 = 3.0

uint32_t battery_get_mV(void)
{
#ifdef ENV_WAVESHARE
    // Average several ADC reads to smooth out single-sample noise. The
    // ESP32-S3 ADC has ~10 mV peak-peak noise per read in practice; 16
    // reads cuts that to ~3 mV without slowing the loop noticeably.
    constexpr int OVERSAMPLE = 16;
    int sum = 0;
    int got = 0;
    for (int i = 0; i < OVERSAMPLE; i++)
    {
        int raw = 0;
        if (adc_oneshot_read(adc1_handle, BAT_ADC_CHANNEL, &raw) != ESP_OK)
            continue;
        int mv = 0;
        if (adc_cali_raw_to_voltage(adc_cali_handle, raw, &mv) != ESP_OK)
            continue;
        sum += mv;
        got++;
    }
    if (got == 0)
    {
        ESP_LOGE(TAG, "ADC read failed");
        return 0;
    }
    int avg_mv = sum / got;
    return (uint32_t)(avg_mv * BAT_DIVIDER_RATIO);
#else
    return 0;
#endif
}

#define CHG_THRESH 4000

#define MAX_CHG_VOLTAGE 4660
#define MIN_CHG_VOLTAGE 4200

#define MAX_VOLTAGE 3670
// MIN_VOLTAGE is the load-compensated rail value that maps to 0%.
// Pinned to CRITICAL_MV so the percent display reaches 0 at exactly
// the moment the critical-low shutdown trigger fires — no more
// "device dies at 16%" surprise.
#define MIN_VOLTAGE 2950

// brownout set to 2.84V

// Critical shutdown threshold: rail mV at which we trigger the clean
// low-battery shutdown. Picked above the 2.84 V brown-out floor with
// enough headroom to run the shutdown sequence (a few seconds of
// LVGL render + display.set_backlight ramp) without browning out
// mid-tear-down. The under-load (50 mV offset) raw reading already
// reflects active draw, so this is the rail value the firmware sees
// while the watch is actually running.
#define CRITICAL_MV         2950
// Require this many consecutive sub-threshold samples (1 s apart)
// before pulling the trigger. Filters out single-sample noise dips
// and brief load transients (BLE radio events, haptic pulses) that
// momentarily drop the rail under load.
#define CRITICAL_RUN_COUNT  5

void battery_task(void *)
{
    // Local state updates every second while awake so the on-watch
    // display stays responsive — ADC oversample + interpolation is
    // cheap. While the watch is asleep nothing on-screen is consuming
    // the value, so we slow the cadence by 5× to cut the number of
    // wake-from-light-sleep events this task causes (each ADC sweep
    // is short but the FreeRTOS-tick wake itself is what costs). BLE
    // send_status only fires when the reported percent actually
    // changes, so a connected phone still gets one event per percent
    // step; the lag between the cell crossing a percent boundary and
    // the phone seeing it goes from ~1 s to ~5 s, which is invisible
    // to the user's battery widget. last_sent_pct starts at -1, a
    // value no real reading produces, so the first sample after boot
    // always sends.
    constexpr int SAMPLE_MS_AWAKE = 1000;
    constexpr int SAMPLE_MS_SLEEP = 5000;
    // Heartbeat: re-send battery status at least this often even when
    // the displayed percent hasn't changed. Catches the case where the
    // phone reconnects, missed the previous edge-triggered send, and
    // would otherwise show stale (or unknown) battery info until the
    // next percent transition — which on a healthy cell can be tens
    // of minutes away.
    constexpr int64_t HEARTBEAT_MS = 60 * 1000;
    int8_t last_sent_pct = -1;
    int64_t last_sent_ms = 0;
    uint8_t critical_run = 0;

    // Directional ratchet on the reported percent. The cell voltage
    // bounces several percent in either direction over a few seconds
    // (BLE radio bursts, haptic pulses, IMU polling all sag the rail
    // briefly; recovery between them looks like "battery went up").
    // Showing the raw computed percent makes the meter jitter 88 ↔ 94.
    //
    // Filter: track the direction the cell is actually going and only
    // allow the meter to move that way. While discharging, percent is
    // monotonically non-increasing; while charging, monotonically non-
    // decreasing. A charging-state transition resets the ratchet so
    // the meter snaps to reality after plug-in / unplug.
    int8_t reported_pct = -1;          // -1 = uninitialized, first sample wins
    bool   prev_charging = false;

    while (true)
    {
        uint32_t raw = battery_get_mV() + (watch.sleeping ? 0 : 50);
        watch.battery.voltage = raw;

        bool charging = (raw >= CHG_THRESH);
        int8_t pct;
        if (charging)
            pct = (int8_t)((raw - MIN_CHG_VOLTAGE) * 100 /
                           (MAX_CHG_VOLTAGE - MIN_CHG_VOLTAGE));
        else
            pct = (int8_t)((raw - MIN_VOLTAGE) * 100 /
                           (MAX_VOLTAGE - MIN_VOLTAGE));
        if (pct > 100) pct = 100;
        if (pct < 0)   pct = 0;

        if (reported_pct < 0 || charging != prev_charging)
        {
            // First sample, or charging state just flipped — accept
            // the raw computed value verbatim. After a plug-in the
            // curve switches anyway (MIN_CHG_VOLTAGE…MAX_CHG_VOLTAGE),
            // so the old ratchet anchor is meaningless.
            reported_pct = pct;
        }
        else if (charging)
        {
            // Charging: never let a momentary sag drop the meter.
            if (pct > reported_pct) reported_pct = pct;
        }
        else
        {
            // Discharging: never let a momentary rebound raise the meter.
            if (pct < reported_pct) reported_pct = pct;
        }
        prev_charging = charging;

        watch.battery.charging = charging;
        watch.battery.percent  = reported_pct;

        // Critical-low shutdown: only when discharging (charging
        // resets the run-count, since when USB is plugged in the
        // rail can briefly read low between the plug-in moment and
        // the charger's regulation kicking in).
        if (!charging && raw > 0 && raw < CRITICAL_MV)
        {
            if (++critical_run >= CRITICAL_RUN_COUNT)
            {
                ESP_LOGW(TAG, "rail at %u mV — entering low-battery shutdown",
                         (unsigned)raw);
                watch.low_battery_shutdown(); // noreturn
            }
        }
        else
        {
            critical_run = 0;
        }

        int64_t now_ms = esp_timer_get_time() / 1000;
        if (reported_pct != last_sent_pct ||
            now_ms - last_sent_ms >= HEARTBEAT_MS)
        {
            ble.send_status();
            last_sent_pct = reported_pct;
            last_sent_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(watch.sleeping ? SAMPLE_MS_SLEEP : SAMPLE_MS_AWAKE));
    }
}

void battery_init(void)
{
#ifdef ENV_WAVESHARE
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc1_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,         // up to ~3.3V input (was DB_11, renamed in v5.x)
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12-bit
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, BAT_ADC_CHANNEL, &chan_cfg));

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = BAT_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc_cali_handle));

    // Stack MUST stay in internal SRAM — battery_task writes its learned
    // anchors to NVS, which the SPI flash driver implements by disabling
    // the cache around the write. A PSRAM-backed stack disappears from
    // the CPU's view the moment cache is disabled, and esp_task_stack_is_
    // sane_cache_disabled() asserts on entry to the flash op.
    xTaskCreate(battery_task, "battery_task", 1024 * 3, NULL, 2, NULL);
#else
#error "Please add battery code for this new hardware"
#endif
}
