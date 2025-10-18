#include "watch.hpp"

#include "esp_adc_cal.h"
#include <esp_adc/adc_oneshot.h>

static adc_oneshot_unit_handle_t adc1_handle;
esp_adc_cal_characteristics_t adc_chars;

/** Get curent battery voltage
 * @returns current battery voltage in millivolts
 */
uint32_t battery_get_mV(void)
{
#ifdef ENV_WAVESHARE

    int raw;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &raw));

    uint32_t voltage = esp_adc_cal_raw_to_voltage(raw, &adc_chars);

    // 🤷‍♂️ I honestly don't know but it was in the old code and it works
    uint32_t bat_voltage = ((voltage * 3300 * 3) / 4096) + 200;
    return bat_voltage;

#endif // ENV_WAVESHARE

    return 0;
}

/** Update task for battery */
void battery_task(void *)
{
    watch.battery.voltage = battery_get_mV();

    while (true)
    {
        uint16_t lastvoltage = watch.battery.voltage;
        watch.battery.voltage = battery_get_mV();

        int32_t diff = watch.battery.voltage - lastvoltage;

        if (diff < -100)
        { // sudden voltage drop indicates unplugged
            watch.battery.charging = false;
        }
        else if (diff > 100)
        { // vice versa
            watch.battery.charging = true;
        }

        vTaskDelay(pdMS_TO_TICKS(1600));
    }
}

/** Initialise the battery susbsystem */
void battery_init(void)
{
    // --- Configure ADC1 ---
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc1_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12-bit
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &chan_cfg));

    // Characterize ADC for voltage calibration
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    watch.battery.voltage = UINT16_MAX;
    watch.battery.percent = UINT8_MAX;

    xTaskCreate(battery_task, "battery_task", 1 * 1024, NULL, 2, NULL);
}

#ifndef ENV_WAVESHARE
#error "Please add battery code for this new hardware"
#endif