#include "esp_adc/adc_oneshot.h"
#include "esp_adc_cal.h"
#include "esp_log.h"

#include "battery.h"

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t adc1_handle;
esp_adc_cal_characteristics_t adc_chars;

void battery_init(void)
{
    // --- Configure ADC1 ---
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc1_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12-bit
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC1_CHANNEL_0, &chan_cfg));

    // Characterize ADC for voltage calibration
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
}

uint32_t battery_get_mV(void)
{
    int raw;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC1_CHANNEL_0, &raw));

    uint32_t voltage = esp_adc_cal_raw_to_voltage(raw, &adc_chars);

    // 🤷‍♂️ I honestly don't know but it was in the old code and it works
    uint32_t bat_voltage = ((voltage * 3300 * 3) / 4096) + 200;
    return bat_voltage;
}