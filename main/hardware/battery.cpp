#include "watch.hpp"
#include "esp_adc_cal.h"
#include <esp_adc/adc_oneshot.h>
#include "esp_log.h"

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t adc1_handle;
static esp_adc_cal_characteristics_t adc_chars;

#define BAT_ADC_CHANNEL ADC_CHANNEL_0 // GPIO1
#define BAT_DIVIDER_RATIO 3.0f        // (R13 + R14) / R14 = 3.0
#define DEFAULT_VREF 1100             // mV

/** Get current battery voltage
 * @returns current battery voltage in millivolts
 */
uint32_t battery_get_mV(void)
{
#ifdef ENV_WAVESHARE

    int raw = 0;
    esp_err_t err = adc_oneshot_read(adc1_handle, BAT_ADC_CHANNEL, &raw);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(err));
        return 0;
    }

    // Convert raw reading to voltage (in mV)
    uint32_t adc_voltage = esp_adc_cal_raw_to_voltage(raw, &adc_chars);

    // Scale up according to the resistor divider
    uint32_t battery_voltage = (uint32_t)(adc_voltage * BAT_DIVIDER_RATIO);

    return battery_voltage;

#else
    return 0;
#endif
}

/** Battery monitoring task */
void battery_task(void *)
{
    watch.battery.voltage = battery_get_mV();

    while (true)
    {
        uint16_t last_voltage = watch.battery.voltage;
        watch.battery.voltage = battery_get_mV();

        int32_t diff = (int32_t)watch.battery.voltage - (int32_t)last_voltage;

        if (diff < -200 && watch.battery.charging)
        { // sudden voltage drop indicates unplugged
            watch.battery.charging = false;
        }
        else if (diff > 200 && !watch.battery.charging)
        { // vice versa
            watch.battery.charging = true;
            // watch.vibrate(100);
        }

        vTaskDelay(pdMS_TO_TICKS(1600));
    }
}

/** Initialise the battery subsystem */
void battery_init(void)
{
#ifdef ENV_WAVESHARE
    // --- Configure ADC1 ---
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc1_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_11,         // up to ~3.3V input
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12-bit
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, BAT_ADC_CHANNEL, &chan_cfg));

    // Characterize ADC for voltage calibration
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, DEFAULT_VREF, &adc_chars);

    watch.battery.voltage = UINT16_MAX;
    watch.battery.percent = UINT8_MAX;

    xTaskCreate(battery_task, "battery_task", 1024, NULL, 2, NULL);
#else
#error "Please add battery code for this new hardware"
#endif
}
