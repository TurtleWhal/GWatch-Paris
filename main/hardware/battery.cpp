#include "watch.hpp"
#include "ble.hpp"
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include "esp_system.h"
#include "esp_log.h"
#include <stdarg.h>
#include <stdio.h>

static const char *TAG = "battery";

// Send a `{"t":"info","msg":"<formatted>"}` GB message so a connected
// Gadgetbridge instance can show learning events (anchor updates) in
// its debug log. Useful when the watch isn't tethered to serial — the
// phone keeps a rolling log of received GB messages. No-op if no peer
// is subscribed.
static void send_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void send_info(const char *fmt, ...)
{
    char body[80];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"t\":\"info\",\"msg\":\"%s\"}", body);
    ble.send_gb(buf);
}

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
#define MIN_VOLTAGE 2780

// brownout set to 2.84V

void battery_task(void *)
{
    while (true)
    {
        uint32_t raw = battery_get_mV() + (watch.sleeping ? 0 : 50);

        watch.battery.voltage = raw;

        int8_t pct = 0;

        if (raw >= CHG_THRESH)
        {
            watch.battery.charging = true;
            pct = (int8_t)((raw - MIN_CHG_VOLTAGE) * 100 / (MAX_CHG_VOLTAGE - MIN_CHG_VOLTAGE));
        }
        else
        {
            watch.battery.charging = false;
            pct = (int8_t)((raw - MIN_VOLTAGE) * 100 / (MAX_VOLTAGE - MIN_VOLTAGE));
        }

        if (pct > 100)
            pct = 100;
        if (pct < 0)
            pct = 0;

        watch.battery.percent = pct;

        ble.send_status();

        vTaskDelay(pdMS_TO_TICKS(1000));
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
