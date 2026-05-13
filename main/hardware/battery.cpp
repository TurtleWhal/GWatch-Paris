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

// Bounds tuned to what this specific board actually shows on its BAT_ADC
// divider — there's a Schottky-style drop between the cell and the tap, so
// "full" rests around 3.9 V and the chip browns out around 2.6 V. Anything
// above ~4.0 V is the USB rail being measured (post-diode Vbus, not the
// cell), so we exclude it from the learned full-voltage range. The wide
// V_EMPTY bounds let the watch learn down toward the actual brown-out.
#define V_FULL_MIN_MV 3700
#define V_FULL_MAX_MV 4000
#define V_FULL_DEFAULT_MV 3900

#define V_EMPTY_MIN_MV 2600
#define V_EMPTY_MAX_MV 3500
#define V_EMPTY_DEFAULT_MV 2800

// Learned per-device anchors. Persisted to NVS and updated whenever we
// see a higher V_FULL or lower V_EMPTY. The curve in between is computed
// by piecewise lookup against the typical LiPo SoC table.
static uint16_t v_full_mv = V_FULL_DEFAULT_MV;
static uint16_t v_empty_mv = V_EMPTY_DEFAULT_MV;

// Typical LiPo discharge curve, normalised to [v_empty .. v_full]. The
// curve is well-known and roughly the same shape across cells; we just
// rescale it to whatever the per-device empty/full anchors happen to be.
// Each entry is (fractional voltage in [0..1], percent state-of-charge).
struct CurvePoint { float v_norm; uint8_t pct; };
static const CurvePoint kLipoCurve[] = {
    {0.000f,   0}, {0.100f,   5}, {0.200f,  10}, {0.300f,  18},
    {0.400f,  28}, {0.500f,  40}, {0.600f,  53}, {0.700f,  66},
    {0.800f,  78}, {0.900f,  90}, {1.000f, 100},
};

// Convert millivolt reading to percent SoC by clamping into the learned
// [empty..full] range and interpolating the table.
static uint8_t voltage_to_percent(uint16_t mv)
{
    if (mv <= v_empty_mv) return 0;
    if (mv >= v_full_mv) return 100;

    float norm = (float)(mv - v_empty_mv) / (float)(v_full_mv - v_empty_mv);
    for (size_t i = 1; i < sizeof(kLipoCurve) / sizeof(kLipoCurve[0]); i++)
    {
        if (norm <= kLipoCurve[i].v_norm)
        {
            const CurvePoint &lo = kLipoCurve[i - 1];
            const CurvePoint &hi = kLipoCurve[i];
            float t = (norm - lo.v_norm) / (hi.v_norm - lo.v_norm);
            return (uint8_t)(lo.pct + t * (hi.pct - lo.pct));
        }
    }
    return 100;
}

// Update v_empty from a new discharging reading. v_full is updated separately
// from a post-unplug stable sample (see battery_task). Returns true if v_empty
// changed.
static bool update_v_empty(uint16_t mv, bool charging)
{
    if (mv == 0 || charging) return false;
    if (mv < v_empty_mv && mv >= V_EMPTY_MIN_MV && mv <= V_EMPTY_MAX_MV)
    {
        v_empty_mv = mv;
        return true;
    }
    return false;
}

// Try to push v_full up from a freshly-recorded post-unplug sample. Only
// accepts samples in the realistic cell-voltage band — anything outside it
// is either still-USB or a brown-out reading.
static bool try_set_v_full(uint16_t mv)
{
    if (mv >= V_FULL_MIN_MV && mv <= V_FULL_MAX_MV && mv > v_full_mv)
    {
        v_full_mv = mv;
        return true;
    }
    return false;
}

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

// Rolling history of voltage samples for slope estimation. Buffer length is
// chosen so we cover ~10 s with the 1.6 s task period — short enough to react
// to plug/unplug within a few seconds, long enough to average out load-sag
// noise that can otherwise trick a single-delta detector.
#define BATT_HISTORY_LEN 7

// Slope thresholds, in mV/sec.
//   Charging: typical USB charger pushes 5–15 mV/s into a partly-empty cell.
//   Discharge: a watch under typical load drifts ~0.5–2 mV/s downward.
// Anything in between is "uncertain" — keep the previous state.
#define CHG_RISE_MV_PER_SEC 3.0f
#define CHG_FALL_MV_PER_SEC -1.0f

// Absolute voltage above which we know we're on USB. The cell's resting
// voltage on this board tops out around 3.9 V (Schottky drop between cell
// and divider tap); anything sustainably ≥ 4.0 V is the post-diode USB
// Vbus showing through the divider, which only happens on a charger.
#define CHG_PLATEAU_MV 4000

// Sudden voltage jump (in either direction) — instant override. Plug-in
// gives an immediate ~200–500 mV step as the charger applies regulated
// voltage; unplug shows the inverse step as the cell relaxes.
#define CHG_EDGE_MV 150

void battery_task(void *)
{
    watch.battery.voltage = battery_get_mV();
    watch.battery.percent = voltage_to_percent(watch.battery.voltage);

    uint16_t hist[BATT_HISTORY_LEN] = {0};
    int hist_count = 0;
    int hist_idx = 0;

    uint8_t last_sent_pct = 255;
    bool last_sent_chg = false;
    int64_t last_send_us = 0;
    int persist_countdown = 0;

    // Awake/asleep load-offset learning. When the watch transitions sleep
    // state, the rail rebounds (asleep) or sags (awake). Sample voltage
    // ~3 s after the transition (after the rail settles), compare against
    // the pre-transition reading, and EMA the delta. Apply the learned
    // offset before doing percent / curve learning so we're working with
    // a consistent "asleep" reference.
    bool last_sleeping = watch.sleeping;
    int settle_ticks_remaining = 0;
    uint16_t v_before_transition = 0;
    bool transitioned_to_asleep = false;
    int load_offset_mv = 0; // (asleep_v - awake_v); positive when awake reads lower

    // Post-unplug full-voltage sampling. On the unplug edge we mark a
    // settle window; once it expires, the next reading is treated as the
    // resting cell voltage and gets a shot at updating v_full.
    int post_unplug_settle = 0;

    while (true)
    {
        uint16_t last_voltage = watch.battery.voltage;
        watch.battery.voltage = battery_get_mV();

        // Detect awake/sleep transitions for the load-offset learner.
        if (watch.sleeping != last_sleeping)
        {
            v_before_transition = last_voltage;
            transitioned_to_asleep = watch.sleeping;
            settle_ticks_remaining = 3; // ~5 s at 1.6 s tick
            last_sleeping = watch.sleeping;
        }
        else if (settle_ticks_remaining > 0 && --settle_ticks_remaining == 0
                 && v_before_transition != 0)
        {
            int delta = (int)watch.battery.voltage - (int)v_before_transition;
            // delta is +ve going awake→asleep (rebound) and -ve asleep→awake.
            // Normalise to a single sign: how much does asleep read above awake?
            int sample = transitioned_to_asleep ? delta : -delta;
            // Sanity-clamp so a charging-event jump doesn't poison the EMA.
            if (sample > -150 && sample < 150)
            {
                load_offset_mv = (load_offset_mv * 7 + sample) / 8; // EMA, ~8-tick TC
            }
            v_before_transition = 0;
        }

        // Compensate live readings to "asleep equivalent" so curve learning
        // and percent display don't bounce when the watch's load changes.
        uint16_t v_compensated = watch.battery.voltage;
        if (!watch.sleeping)
        {
            int adj = (int)v_compensated + load_offset_mv;
            if (adj < 0) adj = 0;
            if (adj > 0xFFFF) adj = 0xFFFF;
            v_compensated = (uint16_t)adj;
        }

        // Edge detection: a sudden jump is an unambiguous plug/unplug event.
        int32_t diff = (int32_t)watch.battery.voltage - (int32_t)last_voltage;
        if (diff > CHG_EDGE_MV)
            watch.battery.charging = true;
        else if (diff < -CHG_EDGE_MV)
        {
            watch.battery.charging = false;
            // Voltage will overshoot downward briefly as the rail collapses
            // off the charger before the cell settles. Wait a few ticks
            // before sampling for the new resting full-voltage.
            post_unplug_settle = 5; // ~8 s
        }

        // Push the new sample into the ring buffer (use the raw reading so
        // edge detection above stays consistent across awake/asleep).
        hist[hist_idx] = watch.battery.voltage;
        hist_idx = (hist_idx + 1) % BATT_HISTORY_LEN;
        if (hist_count < BATT_HISTORY_LEN) hist_count++;

        if (hist_count == BATT_HISTORY_LEN)
        {
            float n = BATT_HISTORY_LEN;
            float sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
            for (int i = 0; i < BATT_HISTORY_LEN; i++)
            {
                int read_idx = (hist_idx + i) % BATT_HISTORY_LEN;
                float x = (float)i;
                float y = (float)hist[read_idx];
                sum_x += x;
                sum_y += y;
                sum_xy += x * y;
                sum_xx += x * x;
            }
            float slope_per_sample = (n * sum_xy - sum_x * sum_y) /
                                     (n * sum_xx - sum_x * sum_x);
            float slope_mV_s = slope_per_sample / 1.6f;

            if (watch.battery.voltage >= CHG_PLATEAU_MV)
                watch.battery.charging = true;
            else if (slope_mV_s > CHG_RISE_MV_PER_SEC)
                watch.battery.charging = true;
            else if (slope_mV_s < CHG_FALL_MV_PER_SEC)
                watch.battery.charging = false;
        }

        // Anchor learning: v_empty ratchets down on any discharging
        // reading; v_full is set from a post-unplug settle sample.
        bool anchors_changed = update_v_empty(v_compensated, watch.battery.charging);
        if (post_unplug_settle > 0 && !watch.battery.charging)
        {
            if (--post_unplug_settle == 0)
            {
                // First clean reading after the unplug edge — this is the
                // cell's resting voltage, modulo our awake/asleep offset
                // compensation. If it's the highest we've ever seen, it's
                // the new full-voltage anchor.
                if (try_set_v_full(v_compensated))
                {
                    anchors_changed = true;
                    ESP_LOGI(TAG, "learned v_full=%u (post-unplug)", v_full_mv);
                }
            }
        }

        watch.battery.percent = voltage_to_percent(v_compensated);

        // Persist the learned anchors lazily — every ~5 minutes if they
        // moved. NVS writes are slow (~ms) and wear-limited, so don't
        // do them on every tick.
        if (anchors_changed)
            persist_countdown = 0;
        else if (++persist_countdown == (5 * 60 * 1000) / 1600)
        {
            watch.settings.writeUint16("bat_full", v_full_mv);
            watch.settings.writeUint16("bat_empty", v_empty_mv);
            persist_countdown = -1; // marker: saved, don't save again until anchors move
        }

        // Push status to Gadgetbridge when the percent changes by ≥1, the
        // charging flag flips, or every 60 s as a heartbeat. send_gb is
        // a no-op when no peer is subscribed, so this is cheap if nobody's
        // listening.
        int64_t now = esp_timer_get_time();
        bool pct_changed = watch.battery.percent != last_sent_pct;
        bool chg_changed = watch.battery.charging != last_sent_chg;
        bool heartbeat = (now - last_send_us) > 60LL * 1000 * 1000;
        if (ble.connected() && (pct_changed || chg_changed || heartbeat))
        {
            ble.send_status();
            last_sent_pct = watch.battery.percent;
            last_sent_chg = watch.battery.charging;
            last_send_us = now;
        }

        vTaskDelay(pdMS_TO_TICKS(1600));
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

    // Curve-fitting calibration scheme (ESP32-S3 has eFuse-burned cal data,
    // so DEFAULT_VREF / line-fitting from the old API isn't needed).
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = BAT_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc_cali_handle));

    // Pull learned anchors out of NVS (or fall back to defaults). If a
    // previous firmware version saved values outside the now-current sane
    // band, reset them — otherwise the v_full ratchet (which only moves
    // upward) would never catch up to the real cell voltage.
    v_full_mv = watch.settings.readUint16("bat_full", V_FULL_DEFAULT_MV);
    v_empty_mv = watch.settings.readUint16("bat_empty", V_EMPTY_DEFAULT_MV);
    if (v_full_mv < V_FULL_MIN_MV || v_full_mv > V_FULL_MAX_MV)
        v_full_mv = V_FULL_DEFAULT_MV;
    if (v_empty_mv < V_EMPTY_MIN_MV || v_empty_mv > V_EMPTY_MAX_MV)
        v_empty_mv = V_EMPTY_DEFAULT_MV;

    watch.battery.voltage = UINT16_MAX;
    watch.battery.percent = UINT8_MAX;

    xTaskCreate(battery_task, "battery_task", 1024 * 3, NULL, 2, NULL);
#else
#error "Please add battery code for this new hardware"
#endif
}
