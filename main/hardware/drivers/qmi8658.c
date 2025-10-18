#include "qmi8658.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <math.h>

static const char *TAG = "QMI8658";

// Default pedometer configuration
static const qmi8658_pedometer_config_t default_pedometer_config = {
    .mode = QMI8658_PEDOMETER_MODE_NORMAL,
    .sample_count = 4,
    .fix_peak = 8,
    .fix_peak2 = 6,
    .time_up = 3,
    .time_low = 2,
    .time_win = 5,
    .reset_on_read = false};

// Original functions (keeping all existing functionality)
esp_err_t qmi8658_init(qmi8658_dev_t *dev, i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr)
{
    if (!dev || !bus_handle)
        return ESP_ERR_INVALID_ARG;

    // Initialize all fields
    memset(dev, 0, sizeof(qmi8658_dev_t));

    dev->bus_handle = bus_handle;
    dev->accel_lsb_div = 4096;
    dev->gyro_lsb_div = 64;
    dev->accel_unit_mps2 = false;
    dev->gyro_unit_rads = false;
    dev->display_precision = 6;
    dev->timestamp = 0;

    // Initialize new fields
    dev->int1_gpio = GPIO_NUM_NC;
    dev->int2_gpio = GPIO_NUM_NC;
    dev->interrupt_callback = NULL;
    dev->step_callback = NULL;
    dev->callback_user_data = NULL;
    dev->last_step_count = 0;
    dev->last_step_time = 0;
    dev->pedometer_enabled = false;
    dev->pedometer_config = default_pedometer_config;

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 400000,
        .scl_wait_us = 0,
        .flags.disable_ack_check = false};

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_config, &dev->dev_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add I2C device");
        return ret;
    }

    uint8_t who_am_i;
    ret = qmi8658_get_who_am_i(dev, &who_am_i);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I register");
        return ret;
    }

    if (who_am_i != 0x05)
    {
        ESP_LOGE(TAG, "Invalid WHO_AM_I value: 0x%02X, expected 0x05", who_am_i);
        return ESP_ERR_NOT_FOUND;
    }

    ret = qmi8658_write_register(dev, QMI8658_CTRL1, 0x60);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize sensor");
        return ret;
    }

    ret = qmi8658_set_accel_range(dev, QMI8658_ACCEL_RANGE_8G);
    if (ret != ESP_OK)
        return ret;

    ret = qmi8658_set_accel_odr(dev, QMI8658_ACCEL_ODR_1000HZ);
    if (ret != ESP_OK)
        return ret;

    ret = qmi8658_set_gyro_range(dev, QMI8658_GYRO_RANGE_512DPS);
    if (ret != ESP_OK)
        return ret;

    ret = qmi8658_set_gyro_odr(dev, QMI8658_GYRO_ODR_1000HZ);
    if (ret != ESP_OK)
        return ret;

    ret = qmi8658_enable_sensors(dev, QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO);

    ESP_LOGI(TAG, "QMI8658 initialized successfully");
    return ret;
}

// Keep all existing functions...
esp_err_t qmi8658_set_accel_range(qmi8658_dev_t *dev, qmi8658_accel_range_t range)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    switch (range)
    {
    case QMI8658_ACCEL_RANGE_2G:
        dev->accel_lsb_div = 16384;
        break;
    case QMI8658_ACCEL_RANGE_4G:
        dev->accel_lsb_div = 8192;
        break;
    case QMI8658_ACCEL_RANGE_8G:
        dev->accel_lsb_div = 4096;
        break;
    case QMI8658_ACCEL_RANGE_16G:
        dev->accel_lsb_div = 2048;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    return qmi8658_write_register(dev, QMI8658_CTRL2, (range << 4) | 0x03);
}

esp_err_t qmi8658_set_accel_odr(qmi8658_dev_t *dev, qmi8658_accel_odr_t odr)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    uint8_t current_ctrl2;
    esp_err_t ret = qmi8658_read_register(dev, QMI8658_CTRL2, &current_ctrl2, 1);
    if (ret != ESP_OK)
        return ret;

    uint8_t new_ctrl2 = (current_ctrl2 & 0xF0) | odr;
    return qmi8658_write_register(dev, QMI8658_CTRL2, new_ctrl2);
}

esp_err_t qmi8658_set_gyro_range(qmi8658_dev_t *dev, qmi8658_gyro_range_t range)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    switch (range)
    {
    case QMI8658_GYRO_RANGE_32DPS:
        dev->gyro_lsb_div = 1024;
        break;
    case QMI8658_GYRO_RANGE_64DPS:
        dev->gyro_lsb_div = 512;
        break;
    case QMI8658_GYRO_RANGE_128DPS:
        dev->gyro_lsb_div = 256;
        break;
    case QMI8658_GYRO_RANGE_256DPS:
        dev->gyro_lsb_div = 128;
        break;
    case QMI8658_GYRO_RANGE_512DPS:
        dev->gyro_lsb_div = 64;
        break;
    case QMI8658_GYRO_RANGE_1024DPS:
        dev->gyro_lsb_div = 32;
        break;
    case QMI8658_GYRO_RANGE_2048DPS:
        dev->gyro_lsb_div = 16;
        break;
    case QMI8658_GYRO_RANGE_4096DPS:
        dev->gyro_lsb_div = 8;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    return qmi8658_write_register(dev, QMI8658_CTRL3, (range << 4) | 0x03);
}

esp_err_t qmi8658_set_gyro_odr(qmi8658_dev_t *dev, qmi8658_gyro_odr_t odr)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    uint8_t current_ctrl3;
    esp_err_t ret = qmi8658_read_register(dev, QMI8658_CTRL3, &current_ctrl3, 1);
    if (ret != ESP_OK)
        return ret;

    uint8_t new_ctrl3 = (current_ctrl3 & 0xF0) | odr;
    return qmi8658_write_register(dev, QMI8658_CTRL3, new_ctrl3);
}

esp_err_t qmi8658_enable_accel(qmi8658_dev_t *dev, bool enable)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    uint8_t current_ctrl7;
    esp_err_t ret = qmi8658_read_register(dev, QMI8658_CTRL7, &current_ctrl7, 1);
    if (ret != ESP_OK)
        return ret;

    if (enable)
    {
        current_ctrl7 |= QMI8658_ENABLE_ACCEL;
    }
    else
    {
        current_ctrl7 &= ~QMI8658_ENABLE_ACCEL;
    }

    return qmi8658_write_register(dev, QMI8658_CTRL7, current_ctrl7);
}

esp_err_t qmi8658_enable_gyro(qmi8658_dev_t *dev, bool enable)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    uint8_t current_ctrl7;
    esp_err_t ret = qmi8658_read_register(dev, QMI8658_CTRL7, &current_ctrl7, 1);
    if (ret != ESP_OK)
        return ret;

    if (enable)
    {
        current_ctrl7 |= QMI8658_ENABLE_GYRO;
    }
    else
    {
        current_ctrl7 &= ~QMI8658_ENABLE_GYRO;
    }

    return qmi8658_write_register(dev, QMI8658_CTRL7, current_ctrl7);
}

esp_err_t qmi8658_enable_sensors(qmi8658_dev_t *dev, uint8_t enable_flags)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;
    return qmi8658_write_register(dev, QMI8658_CTRL7, enable_flags & 0x0F);
}

esp_err_t qmi8658_read_accel(qmi8658_dev_t *dev, float *x, float *y, float *z)
{
    if (!dev || !x || !y || !z)
        return ESP_ERR_INVALID_ARG;

    uint8_t buffer[6];
    esp_err_t ret = qmi8658_read_register(dev, QMI8658_AX_L, buffer, 6);
    if (ret != ESP_OK)
        return ret;

    int16_t raw_x = (int16_t)((buffer[1] << 8) | buffer[0]);
    int16_t raw_y = (int16_t)((buffer[3] << 8) | buffer[2]);
    int16_t raw_z = (int16_t)((buffer[5] << 8) | buffer[4]);

    if (dev->accel_unit_mps2)
    {
        *x = (raw_x * ONE_G) / dev->accel_lsb_div;
        *y = (raw_y * ONE_G) / dev->accel_lsb_div;
        *z = (raw_z * ONE_G) / dev->accel_lsb_div;
    }
    else
    {
        *x = (raw_x * 1000.0f) / dev->accel_lsb_div;
        *y = (raw_y * 1000.0f) / dev->accel_lsb_div;
        *z = (raw_z * 1000.0f) / dev->accel_lsb_div;
    }

    return ESP_OK;
}

// NEW INTERRUPT FUNCTIONS

esp_err_t qmi8658_init_interrupts(qmi8658_dev_t *dev, gpio_num_t int1_gpio, gpio_num_t int2_gpio)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    esp_err_t ret = ESP_OK;

    // Configure INT1 GPIO if specified
    if (int1_gpio != GPIO_NUM_NC)
    {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_POSEDGE,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << int1_gpio),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_ENABLE};
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to configure INT1 GPIO");
            return ret;
        }

        dev->int1_gpio = int1_gpio;
        ESP_LOGI(TAG, "INT1 configured on GPIO %d", int1_gpio);
    }

    // Configure INT2 GPIO if specified
    if (int2_gpio != GPIO_NUM_NC)
    {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_POSEDGE,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << int2_gpio),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_ENABLE};
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to configure INT2 GPIO");
            return ret;
        }

        dev->int2_gpio = int2_gpio;
        ESP_LOGI(TAG, "INT2 configured on GPIO %d", int2_gpio);
    }

    // Configure interrupt pins on the sensor (default: active high, push-pull)
    ret = qmi8658_config_interrupt_pin(dev, QMI8658_INT_PIN_1, QMI8658_INT_ACTIVE_HIGH, QMI8658_INT_PUSH_PULL);
    if (ret != ESP_OK)
        return ret;

    ret = qmi8658_config_interrupt_pin(dev, QMI8658_INT_PIN_2, QMI8658_INT_ACTIVE_HIGH, QMI8658_INT_PUSH_PULL);
    if (ret != ESP_OK)
        return ret;

    ESP_LOGI(TAG, "Interrupts initialized");

    return ESP_OK;
}

esp_err_t qmi8658_config_interrupt_pin(qmi8658_dev_t *dev, qmi8658_int_pin_t pin,
                                       qmi8658_int_polarity_t polarity, qmi8658_int_output_t output)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    uint8_t config_val = (polarity << 1) | output;

    if (pin == QMI8658_INT_PIN_1)
    {
        return qmi8658_write_register(dev, QMI8658_INT_CONFIG, config_val);
    }
    else
    {
        return qmi8658_write_register(dev, QMI8658_INT_CONFIG, config_val << 4);
    }
}

esp_err_t qmi8658_enable_interrupts(qmi8658_dev_t *dev, uint8_t interrupt_mask, qmi8658_int_pin_t pin)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    esp_err_t ret;
    qmi8658_register_t map_reg = (pin == QMI8658_INT_PIN_1) ? QMI8658_INT_MAP1 : QMI8658_INT_MAP2;

    // Read current interrupt mapping
    uint8_t current_map;
    ret = qmi8658_read_register(dev, map_reg, &current_map, 1);
    if (ret != ESP_OK)
        return ret;

    // Enable specified interrupts
    uint8_t new_map = current_map | interrupt_mask;
    ret = qmi8658_write_register(dev, map_reg, new_map);
    if (ret != ESP_OK)
        return ret;

    ESP_LOGI(TAG, "Interrupts 0x%02X enabled on pin %d", interrupt_mask, pin);

    return ESP_OK;
}

esp_err_t qmi8658_disable_interrupts(qmi8658_dev_t *dev, uint8_t interrupt_mask)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    esp_err_t ret;

    // Disable from both interrupt pins
    uint8_t current_map1, current_map2;
    ret = qmi8658_read_register(dev, QMI8658_INT_MAP1, &current_map1, 1);
    if (ret != ESP_OK)
        return ret;

    ret = qmi8658_read_register(dev, QMI8658_INT_MAP2, &current_map2, 1);
    if (ret != ESP_OK)
        return ret;

    // Clear specified interrupts from both pins
    uint8_t new_map1 = current_map1 & ~interrupt_mask;
    uint8_t new_map2 = current_map2 & ~interrupt_mask;

    ret = qmi8658_write_register(dev, QMI8658_INT_MAP1, new_map1);
    if (ret != ESP_OK)
        return ret;

    ret = qmi8658_write_register(dev, QMI8658_INT_MAP2, new_map2);
    if (ret != ESP_OK)
        return ret;

    ESP_LOGI(TAG, "Interrupts 0x%02X disabled", interrupt_mask);

    return ESP_OK;
}

esp_err_t qmi8658_read_interrupt_status(qmi8658_dev_t *dev, qmi8658_interrupt_status_t *status)
{
    if (!dev || !status)
        return ESP_ERR_INVALID_ARG;

    esp_err_t ret = qmi8658_read_register(dev, QMI8658_INT_STATUS, &status->interrupt_status, 1);
    if (ret != ESP_OK)
        return ret;

    // Parse individual interrupt flags
    status->significant_motion = (status->interrupt_status & QMI8658_INT_SIGNIFICANT_MOTION) != 0;
    status->no_motion = (status->interrupt_status & QMI8658_INT_NO_MOTION) != 0;
    status->any_motion = (status->interrupt_status & QMI8658_INT_ANY_MOTION) != 0;
    status->pedometer_interrupt = (status->interrupt_status & QMI8658_INT_PEDOMETER) != 0;
    status->high_g = (status->interrupt_status & QMI8658_INT_HIGH_G) != 0;
    status->low_g = (status->interrupt_status & QMI8658_INT_LOW_G) != 0;
    status->data_ready = (status->interrupt_status & QMI8658_INT_DATA_READY) != 0;
    status->fifo_ready = (status->interrupt_status & QMI8658_INT_FIFO_READY) != 0;

    return ESP_OK;
}

esp_err_t qmi8658_clear_interrupts(qmi8658_dev_t *dev)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    // Reading the interrupt status register typically clears the interrupts
    uint8_t dummy;
    return qmi8658_read_register(dev, QMI8658_INT_STATUS, &dummy, 1);
}

// MOTION DETECTION FUNCTIONS

esp_err_t qmi8658_config_significant_motion(qmi8658_dev_t *dev, const qmi8658_motion_config_t *config)
{
    if (!dev || !config)
        return ESP_ERR_INVALID_ARG;

    esp_err_t ret;

    // Configure significant motion threshold and duration
    uint8_t sig_motion_ctrl = (config->threshold & 0x3F) | ((config->duration & 0x03) << 6);
    ret = qmi8658_write_register(dev, QMI8658_SIG_MOTION_CTRL, sig_motion_ctrl);
    if (ret != ESP_OK)
        return ret;

    ESP_LOGI(TAG, "Significant motion configured: threshold=%d, duration=%d",
             config->threshold, config->duration);

    return ESP_OK;
}

esp_err_t qmi8658_config_any_motion(qmi8658_dev_t *dev, const qmi8658_motion_config_t *config)
{
    if (!dev || !config)
        return ESP_ERR_INVALID_ARG;

    esp_err_t ret;

    // Configure any motion detection
    uint8_t any_motion_ctrl = (config->threshold & 0x3F) |
                              ((config->enable_x ? 1 : 0) << 6) |
                              ((config->enable_y ? 1 : 0) << 7);
    ret = qmi8658_write_register(dev, QMI8658_ANY_MOTION_CTRL, any_motion_ctrl);
    if (ret != ESP_OK)
        return ret;

    // Configure duration and Z-axis enable in a second register if needed
    uint8_t duration_ctrl = (config->duration & 0x3F) | ((config->enable_z ? 1 : 0) << 6);
    ret = qmi8658_write_register(dev, QMI8658_ANY_MOTION_CTRL + 1, duration_ctrl);
    if (ret != ESP_OK)
        return ret;

    ESP_LOGI(TAG, "Any motion configured: threshold=%d, duration=%d, axes=%s%s%s",
             config->threshold, config->duration,
             config->enable_x ? "X" : "", config->enable_y ? "Y" : "", config->enable_z ? "Z" : "");

    return ESP_OK;
}

esp_err_t qmi8658_config_no_motion(qmi8658_dev_t *dev, const qmi8658_motion_config_t *config)
{
    if (!dev || !config)
        return ESP_ERR_INVALID_ARG;

    esp_err_t ret;

    // Configure no motion detection
    uint8_t no_motion_ctrl = (config->threshold & 0x3F) |
                             ((config->enable_x ? 1 : 0) << 6) |
                             ((config->enable_y ? 1 : 0) << 7);
    ret = qmi8658_write_register(dev, QMI8658_NO_MOTION_CTRL, no_motion_ctrl);
    if (ret != ESP_OK)
        return ret;

    // Configure duration and count
    uint8_t duration_ctrl = (config->duration & 0x3F) | ((config->enable_z ? 1 : 0) << 6);
    ret = qmi8658_write_register(dev, QMI8658_NO_MOTION_CTRL + 1, duration_ctrl);
    if (ret != ESP_OK)
        return ret;

    ESP_LOGI(TAG, "No motion configured: threshold=%d, duration=%d, axes=%s%s%s",
             config->threshold, config->duration,
             config->enable_x ? "X" : "", config->enable_y ? "Y" : "", config->enable_z ? "Z" : "");

    return ESP_OK;
}

// CALLBACK FUNCTIONS

void qmi8658_set_interrupt_callback(qmi8658_dev_t *dev, qmi8658_interrupt_callback_t callback, void *user_data)
{
    if (dev)
    {
        dev->interrupt_callback = callback;
        dev->callback_user_data = user_data;
    }
}

void qmi8658_set_step_callback(qmi8658_dev_t *dev, qmi8658_step_callback_t callback, void *user_data)
{
    if (dev)
    {
        dev->step_callback = callback;
        dev->callback_user_data = user_data;
    }
}

// ISR HANDLER

void IRAM_ATTR qmi8658_handle_interrupt(qmi8658_dev_t *dev)
{
    if (!dev)
        return;

    // This function should be called from the GPIO interrupt handler
    // It reads the interrupt status and calls the appropriate callbacks

    qmi8658_interrupt_status_t status;
    if (qmi8658_read_interrupt_status(dev, &status) == ESP_OK)
    {

        // Handle step counter interrupt
        if (status.pedometer_interrupt && dev->step_callback)
        {
            qmi8658_step_data_t step_data;
            if (qmi8658_read_step_data(dev, &step_data) == ESP_OK)
            {
                dev->step_callback(step_data, dev->callback_user_data);
            }
        }

        // Handle general interrupt callback
        if (dev->interrupt_callback)
        {
            dev->interrupt_callback(status, dev->callback_user_data);
        }

        // Clear interrupts
        qmi8658_clear_interrupts(dev);
    }
}

esp_err_t qmi8658_read_gyro(qmi8658_dev_t *dev, float *x, float *y, float *z)
{
    if (!dev || !x || !y || !z)
        return ESP_ERR_INVALID_ARG;

    uint8_t buffer[6];
    esp_err_t ret = qmi8658_read_register(dev, QMI8658_GX_L, buffer, 6);
    if (ret != ESP_OK)
        return ret;

    int16_t raw_x = (int16_t)((buffer[1] << 8) | buffer[0]);
    int16_t raw_y = (int16_t)((buffer[3] << 8) | buffer[2]);
    int16_t raw_z = (int16_t)((buffer[5] << 8) | buffer[4]);

    if (dev->gyro_unit_rads)
    {
        *x = (raw_x * M_PI / 180.0f) / dev->gyro_lsb_div;
        *y = (raw_y * M_PI / 180.0f) / dev->gyro_lsb_div;
        *z = (raw_z * M_PI / 180.0f) / dev->gyro_lsb_div;
    }
    else
    {
        *x = (float)raw_x / dev->gyro_lsb_div;
        *y = (float)raw_y / dev->gyro_lsb_div;
        *z = (float)raw_z / dev->gyro_lsb_div;
    }

    return ESP_OK;
}

esp_err_t qmi8658_read_temp(qmi8658_dev_t *dev, float *temperature)
{
    if (!dev || !temperature)
        return ESP_ERR_INVALID_ARG;

    uint8_t buffer[2];
    esp_err_t ret = qmi8658_read_register(dev, QMI8658_TEMP_L, buffer, 2);
    if (ret != ESP_OK)
        return ret;

    int16_t raw_temp = (int16_t)((buffer[1] << 8) | buffer[0]);
    *temperature = (float)raw_temp / 256.0f;

    return ESP_OK;
}

esp_err_t qmi8658_read_sensor_data(qmi8658_dev_t *dev, qmi8658_data_t *data)
{
    if (!dev || !data)
        return ESP_ERR_INVALID_ARG;

    uint8_t timestamp_buffer[3];
    esp_err_t ret = qmi8658_read_register(dev, QMI8658_TIMESTAMP_L, timestamp_buffer, 3);
    if (ret == ESP_OK)
    {
        uint32_t timestamp = ((uint32_t)timestamp_buffer[2] << 16) |
                             ((uint32_t)timestamp_buffer[1] << 8) |
                             timestamp_buffer[0];
        if (timestamp > dev->timestamp)
        {
            dev->timestamp = timestamp;
        }
        else
        {
            dev->timestamp = (timestamp + 0x1000000 - dev->timestamp);
        }
        data->timestamp = dev->timestamp;
    }

    uint8_t sensor_buffer[12];
    ret = qmi8658_read_register(dev, QMI8658_AX_L, sensor_buffer, 12);
    if (ret != ESP_OK)
        return ret;

    int16_t raw_ax = (int16_t)((sensor_buffer[1] << 8) | sensor_buffer[0]);
    int16_t raw_ay = (int16_t)((sensor_buffer[3] << 8) | sensor_buffer[2]);
    int16_t raw_az = (int16_t)((sensor_buffer[5] << 8) | sensor_buffer[4]);

    int16_t raw_gx = (int16_t)((sensor_buffer[7] << 8) | sensor_buffer[6]);
    int16_t raw_gy = (int16_t)((sensor_buffer[9] << 8) | sensor_buffer[8]);
    int16_t raw_gz = (int16_t)((sensor_buffer[11] << 8) | sensor_buffer[10]);

    if (dev->accel_unit_mps2)
    {
        data->accelX = (raw_ax * ONE_G) / dev->accel_lsb_div;
        data->accelY = (raw_ay * ONE_G) / dev->accel_lsb_div;
        data->accelZ = (raw_az * ONE_G) / dev->accel_lsb_div;
    }
    else
    {
        data->accelX = (raw_ax * 1000.0f) / dev->accel_lsb_div;
        data->accelY = (raw_ay * 1000.0f) / dev->accel_lsb_div;
        data->accelZ = (raw_az * 1000.0f) / dev->accel_lsb_div;
    }

    if (dev->gyro_unit_rads)
    {
        data->gyroX = (raw_gx * M_PI / 180.0f) / dev->gyro_lsb_div;
        data->gyroY = (raw_gy * M_PI / 180.0f) / dev->gyro_lsb_div;
        data->gyroZ = (raw_gz * M_PI / 180.0f) / dev->gyro_lsb_div;
    }
    else
    {
        data->gyroX = (float)raw_gx / dev->gyro_lsb_div;
        data->gyroY = (float)raw_gy / dev->gyro_lsb_div;
        data->gyroZ = (float)raw_gz / dev->gyro_lsb_div;
    }

    return qmi8658_read_temp(dev, &data->temperature);
}

// Keep all existing utility functions...
esp_err_t qmi8658_read_accel_mg(qmi8658_dev_t *dev, float *x, float *y, float *z)
{
    bool was_mps2 = dev->accel_unit_mps2;
    dev->accel_unit_mps2 = false;
    esp_err_t ret = qmi8658_read_accel(dev, x, y, z);
    dev->accel_unit_mps2 = was_mps2;
    return ret;
}

esp_err_t qmi8658_read_accel_mps2(qmi8658_dev_t *dev, float *x, float *y, float *z)
{
    bool was_mps2 = dev->accel_unit_mps2;
    dev->accel_unit_mps2 = true;
    esp_err_t ret = qmi8658_read_accel(dev, x, y, z);
    dev->accel_unit_mps2 = was_mps2;
    return ret;
}

esp_err_t qmi8658_read_gyro_dps(qmi8658_dev_t *dev, float *x, float *y, float *z)
{
    bool was_rads = dev->gyro_unit_rads;
    dev->gyro_unit_rads = false;
    esp_err_t ret = qmi8658_read_gyro(dev, x, y, z);
    dev->gyro_unit_rads = was_rads;
    return ret;
}

esp_err_t qmi8658_read_gyro_rads(qmi8658_dev_t *dev, float *x, float *y, float *z)
{
    bool was_rads = dev->gyro_unit_rads;
    dev->gyro_unit_rads = true;
    esp_err_t ret = qmi8658_read_gyro(dev, x, y, z);
    dev->gyro_unit_rads = was_rads;
    return ret;
}

esp_err_t qmi8658_is_data_ready(qmi8658_dev_t *dev, bool *ready)
{
    if (!dev || !ready)
        return ESP_ERR_INVALID_ARG;

    uint8_t status;
    esp_err_t ret = qmi8658_read_register(dev, QMI8658_STATUS0, &status, 1);
    if (ret != ESP_OK)
        return ret;

    *ready = (status & 0x03) != 0;
    return ESP_OK;
}

esp_err_t qmi8658_get_who_am_i(qmi8658_dev_t *dev, uint8_t *who_am_i)
{
    if (!dev || !who_am_i)
        return ESP_ERR_INVALID_ARG;
    return qmi8658_read_register(dev, QMI8658_WHO_AM_I, who_am_i, 1);
}

esp_err_t qmi8658_reset(qmi8658_dev_t *dev)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;
    return qmi8658_write_register(dev, QMI8658_CTRL1, 0x80);
}

void qmi8658_set_accel_unit_mps2(qmi8658_dev_t *dev, bool use_mps2)
{
    if (dev)
        dev->accel_unit_mps2 = use_mps2;
}

void qmi8658_set_accel_unit_mg(qmi8658_dev_t *dev, bool use_mg)
{
    if (dev)
        dev->accel_unit_mps2 = !use_mg;
}

void qmi8658_set_gyro_unit_rads(qmi8658_dev_t *dev, bool use_rads)
{
    if (dev)
        dev->gyro_unit_rads = use_rads;
}

void qmi8658_set_gyro_unit_dps(qmi8658_dev_t *dev, bool use_dps)
{
    if (dev)
        dev->gyro_unit_rads = !use_dps;
}

void qmi8658_set_display_precision(qmi8658_dev_t *dev, int decimals)
{
    if (dev && decimals >= 0 && decimals <= 10)
    {
        dev->display_precision = decimals;
    }
}

void qmi8658_set_display_precision_enum(qmi8658_dev_t *dev, qmi8658_precision_t precision)
{
    if (dev)
        dev->display_precision = (int)precision;
}

int qmi8658_get_display_precision(qmi8658_dev_t *dev)
{
    return dev ? dev->display_precision : 0;
}

bool qmi8658_is_accel_unit_mps2(qmi8658_dev_t *dev)
{
    return dev ? dev->accel_unit_mps2 : false;
}

bool qmi8658_is_accel_unit_mg(qmi8658_dev_t *dev)
{
    return dev ? !dev->accel_unit_mps2 : false;
}

bool qmi8658_is_gyro_unit_rads(qmi8658_dev_t *dev)
{
    return dev ? dev->gyro_unit_rads : false;
}

bool qmi8658_is_gyro_unit_dps(qmi8658_dev_t *dev)
{
    return dev ? !dev->gyro_unit_rads : false;
}

esp_err_t qmi8658_enable_wake_on_motion(qmi8658_dev_t *dev, uint8_t threshold)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    esp_err_t ret = qmi8658_enable_sensors(dev, QMI8658_DISABLE_ALL);
    if (ret != ESP_OK)
        return ret;

    ret = qmi8658_set_accel_range(dev, QMI8658_ACCEL_RANGE_2G);
    if (ret != ESP_OK)
        return ret;

    ret = qmi8658_set_accel_odr(dev, QMI8658_ACCEL_ODR_LOWPOWER_21HZ);
    if (ret != ESP_OK)
        return ret;

    ret = qmi8658_write_register(dev, 0x0B, threshold);
    if (ret != ESP_OK)
        return ret;

    ret = qmi8658_write_register(dev, 0x0C, 0x00);
    if (ret != ESP_OK)
        return ret;

    return qmi8658_enable_sensors(dev, QMI8658_ENABLE_ACCEL);
}

esp_err_t qmi8658_disable_wake_on_motion(qmi8658_dev_t *dev)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    esp_err_t ret = qmi8658_enable_sensors(dev, QMI8658_DISABLE_ALL);
    if (ret != ESP_OK)
        return ret;

    return qmi8658_write_register(dev, 0x0B, 0x00);
}

esp_err_t qmi8658_write_register(qmi8658_dev_t *dev, uint8_t reg, uint8_t value)
{
    if (!dev || !dev->dev_handle)
        return ESP_ERR_INVALID_ARG;

    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(dev->dev_handle, data, 2, 1000);
}

esp_err_t qmi8658_read_register(qmi8658_dev_t *dev, uint8_t reg, uint8_t *buffer, uint8_t length)
{
    if (!dev || !buffer || length == 0 || !dev->dev_handle)
        return ESP_ERR_INVALID_ARG;

    return i2c_master_transmit_receive(dev->dev_handle, &reg, 1, buffer, length, 1000);
}

// NEW STEP COUNTING FUNCTIONS

esp_err_t qmi8658_enable_pedometer(qmi8658_dev_t *dev, bool enable)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    esp_err_t ret;

    if (enable)
    {
        // Configure pedometer with current settings
        ret = qmi8658_configure_pedometer(dev, &dev->pedometer_config);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to configure pedometer");
            return ret;
        }

        // Enable pedometer in CTRL8 register
        ret = qmi8658_write_register(dev, QMI8658_CTRL8, 0x80);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to enable pedometer");
            return ret;
        }

        dev->pedometer_enabled = true;
        ESP_LOGI(TAG, "Pedometer enabled");
    }
    else
    {
        // Disable pedometer
        ret = qmi8658_write_register(dev, QMI8658_CTRL8, 0x00);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to disable pedometer");
            return ret;
        }

        dev->pedometer_enabled = false;
        ESP_LOGI(TAG, "Pedometer disabled");
    }

    return ESP_OK;
}

esp_err_t qmi8658_configure_pedometer(qmi8658_dev_t *dev, const qmi8658_pedometer_config_t *config)
{
    if (!dev || !config)
        return ESP_ERR_INVALID_ARG;

    esp_err_t ret;

    // Copy configuration
    dev->pedometer_config = *config;

    // Configure pedometer control registers
    uint8_t ctrl1_val = (config->mode << 7) | (config->sample_count & 0x0F);
    ret = qmi8658_write_register(dev, QMI8658_PEDOMETER_CTRL1, ctrl1_val);
    if (ret != ESP_OK)
        return ret;

    uint8_t ctrl2_val = (config->fix_peak << 4) | (config->fix_peak2 & 0x0F);
    ret = qmi8658_write_register(dev, QMI8658_PEDOMETER_CTRL2, ctrl2_val);
    if (ret != ESP_OK)
        return ret;

    uint8_t ctrl3_val = (config->time_up << 6) | (config->time_low << 3) | (config->time_win & 0x07);
    ret = qmi8658_write_register(dev, QMI8658_PEDOMETER_CTRL3, ctrl3_val);
    if (ret != ESP_OK)
        return ret;

    ESP_LOGI(TAG, "Pedometer configured: mode=%d, samples=%d", config->mode, config->sample_count);

    return ESP_OK;
}

esp_err_t qmi8658_reset_step_counter(qmi8658_dev_t *dev)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    // Reset step counter by writing to a control register
    esp_err_t ret = qmi8658_write_register(dev, QMI8658_CTRL8, 0x81); // Reset bit + enable
    if (ret != ESP_OK)
        return ret;

    // Wait for reset to complete
    vTaskDelay(pdMS_TO_TICKS(10));

    // Re-enable pedometer
    ret = qmi8658_write_register(dev, QMI8658_CTRL8, 0x80);
    if (ret != ESP_OK)
        return ret;

    dev->last_step_count = 0;
    dev->last_step_time = esp_timer_get_time() / 1000; // Convert to ms

    ESP_LOGI(TAG, "Step counter reset");

    return ESP_OK;
}

esp_err_t qmi8658_read_step_count(qmi8658_dev_t *dev, uint32_t *step_count)
{
    if (!dev || !step_count)
        return ESP_ERR_INVALID_ARG;

    if (!dev->pedometer_enabled)
    {
        ESP_LOGW(TAG, "Pedometer not enabled");
        *step_count = 0;
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t buffer[3];
    esp_err_t ret = qmi8658_read_register(dev, QMI8658_STEP_CNT_L, buffer, 3);
    if (ret != ESP_OK)
        return ret;

    *step_count = ((uint32_t)buffer[2] << 16) | ((uint32_t)buffer[1] << 8) | buffer[0];

    return ESP_OK;
}

esp_err_t qmi8658_read_step_data(qmi8658_dev_t *dev, qmi8658_step_data_t *step_data)
{
    if (!dev || !step_data)
        return ESP_ERR_INVALID_ARG;

    if (!dev->pedometer_enabled)
    {
        ESP_LOGW(TAG, "Pedometer not enabled");
        memset(step_data, 0, sizeof(qmi8658_step_data_t));
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = qmi8658_read_step_count(dev, &step_data->step_count);
    if (ret != ESP_OK)
        return ret;

    uint32_t current_time = esp_timer_get_time() / 1000; // Convert to ms
    step_data->step_time = current_time;

    // Check if new steps detected
    if (step_data->step_count > dev->last_step_count)
    {
        step_data->step_detected = true;

        // Calculate step frequency if we have previous data
        if (dev->last_step_time > 0 && current_time > dev->last_step_time)
        {
            uint32_t time_diff = current_time - dev->last_step_time;
            uint32_t step_diff = step_data->step_count - dev->last_step_count;
            step_data->step_frequency = (float)step_diff * 60000.0f / (float)time_diff; // steps per minute
        }
        else
        {
            step_data->step_frequency = 0.0f;
        }

        // Update last values
        dev->last_step_count = step_data->step_count;
        dev->last_step_time = current_time;

        // Call step callback if registered
        if (dev->step_callback)
        {
            dev->step_callback(*step_data, dev->callback_user_data);
        }
    }
    else
    {
        step_data->step_detected = false;
        step_data->step_frequency = 0.0f;
    }

    return ESP_OK;
}