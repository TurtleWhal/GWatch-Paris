#ifndef QMI8658_H
#define QMI8658_H

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define QMI8658_LIBRARY_VERSION "1.1.0"

#define QMI8658_ADDRESS_LOW 0x6A
#define QMI8658_ADDRESS_HIGH 0x6B

#define M_PI (3.14159265358979323846f)
#define ONE_G (9.807f)

#define QMI8658_DISABLE_ALL 0x00
#define QMI8658_ENABLE_ACCEL 0x01
#define QMI8658_ENABLE_GYRO 0x02
#define QMI8658_ENABLE_MAG 0x04
#define QMI8658_ENABLE_AE 0x08

// Interrupt flags
#define QMI8658_INT_SIGNIFICANT_MOTION 0x01
#define QMI8658_INT_NO_MOTION 0x02
#define QMI8658_INT_ANY_MOTION 0x04
#define QMI8658_INT_PEDOMETER 0x08
#define QMI8658_INT_HIGH_G 0x10
#define QMI8658_INT_LOW_G 0x20
#define QMI8658_INT_DATA_READY 0x40
#define QMI8658_INT_FIFO_READY 0x80

    typedef enum
    {
        QMI8658_WHO_AM_I = 0x00,
        QMI8658_REVISION = 0x01,
        QMI8658_CTRL1 = 0x02,
        QMI8658_CTRL2 = 0x03,
        QMI8658_CTRL3 = 0x04,
        QMI8658_CTRL4 = 0x05,
        QMI8658_CTRL5 = 0x06,
        QMI8658_CTRL6 = 0x07,
        QMI8658_CTRL7 = 0x08,
        QMI8658_CTRL8 = 0x09,
        QMI8658_CTRL9 = 0x0A,
        QMI8658_CAL1_L = 0x0B,
        QMI8658_CAL1_H = 0x0C,
        QMI8658_CAL2_L = 0x0D,
        QMI8658_CAL2_H = 0x0E,
        QMI8658_CAL3_L = 0x0F,
        QMI8658_CAL3_H = 0x10,
        QMI8658_CAL4_L = 0x11,
        QMI8658_CAL4_H = 0x12,
        QMI8658_FIFO_WTM_TH = 0x13,
        QMI8658_FIFO_CTRL = 0x14,
        QMI8658_FIFO_SAMPLES = 0x15,
        QMI8658_FIFO_STATUS = 0x16,
        QMI8658_FIFO_DATA = 0x17,
        QMI8658_I2CM_STATUS = 0x2C,
        QMI8658_STATUS0 = 0x2E,
        QMI8658_STATUS1 = 0x2F,
        QMI8658_TIMESTAMP_L = 0x30,
        QMI8658_TIMESTAMP_M = 0x31,
        QMI8658_TIMESTAMP_H = 0x32,
        QMI8658_TEMP_L = 0x33,
        QMI8658_TEMP_H = 0x34,
        QMI8658_AX_L = 0x35,
        QMI8658_AX_H = 0x36,
        QMI8658_AY_L = 0x37,
        QMI8658_AY_H = 0x38,
        QMI8658_AZ_L = 0x39,
        QMI8658_AZ_H = 0x3A,
        QMI8658_GX_L = 0x3B,
        QMI8658_GX_H = 0x3C,
        QMI8658_GY_L = 0x3D,
        QMI8658_GY_H = 0x3E,
        QMI8658_GZ_L = 0x3F,
        QMI8658_GZ_H = 0x40,
        QMI8658_MX_L = 0x41,
        QMI8658_MX_H = 0x42,
        QMI8658_MY_L = 0x43,
        QMI8658_MY_H = 0x44,
        QMI8658_MZ_L = 0x45,
        QMI8658_MZ_H = 0x46,
        QMI8658_dQW_L = 0x49,
        QMI8658_dQW_H = 0x4A,
        QMI8658_dQX_L = 0x4B,
        QMI8658_dQX_H = 0x4C,
        QMI8658_dQY_L = 0x4D,
        QMI8658_dQY_H = 0x4E,
        QMI8658_dQZ_L = 0x4F,
        QMI8658_dQZ_H = 0x50,
        QMI8658_dVX_L = 0x51,
        QMI8658_dVX_H = 0x52,
        QMI8658_dVY_L = 0x53,
        QMI8658_dVY_H = 0x54,
        QMI8658_dVZ_L = 0x55,
        QMI8658_dVZ_H = 0x56,
        QMI8658_AE_REG1 = 0x57,
        QMI8658_AE_REG2 = 0x58,
        // Step counter registers
        QMI8658_STEP_CNT_L = 0x07, // Pedometer step counter low byte
        QMI8658_STEP_CNT_M = 0x08, // Pedometer step counter middle byte
        QMI8658_STEP_CNT_H = 0x09, // Pedometer step counter high byte
        // Interrupt and motion detection registers
        QMI8658_TAP_STATUS = 0x59,
        QMI8658_STEP_STATUS = 0x5A,
        QMI8658_INT_STATUS = 0x5B,
        QMI8658_INT_MAP1 = 0x5C,
        QMI8658_INT_MAP2 = 0x5D,
        QMI8658_INT_CONFIG = 0x5E,
        QMI8658_SIG_MOTION_CTRL = 0x5F,
        QMI8658_ANY_MOTION_CTRL = 0x60,
        QMI8658_NO_MOTION_CTRL = 0x61,
        QMI8658_PEDOMETER_CTRL1 = 0x62,
        QMI8658_PEDOMETER_CTRL2 = 0x63,
        QMI8658_PEDOMETER_CTRL3 = 0x64
    } qmi8658_register_t;

    typedef enum
    {
        QMI8658_ACCEL_RANGE_2G = 0x00,
        QMI8658_ACCEL_RANGE_4G = 0x01,
        QMI8658_ACCEL_RANGE_8G = 0x02,
        QMI8658_ACCEL_RANGE_16G = 0x03
    } qmi8658_accel_range_t;

    typedef enum
    {
        QMI8658_ACCEL_ODR_8000HZ = 0x00,
        QMI8658_ACCEL_ODR_4000HZ = 0x01,
        QMI8658_ACCEL_ODR_2000HZ = 0x02,
        QMI8658_ACCEL_ODR_1000HZ = 0x03,
        QMI8658_ACCEL_ODR_500HZ = 0x04,
        QMI8658_ACCEL_ODR_250HZ = 0x05,
        QMI8658_ACCEL_ODR_125HZ = 0x06,
        QMI8658_ACCEL_ODR_62_5HZ = 0x07,
        QMI8658_ACCEL_ODR_31_25HZ = 0x08,
        QMI8658_ACCEL_ODR_LOWPOWER_128HZ = 0x0C,
        QMI8658_ACCEL_ODR_LOWPOWER_21HZ = 0x0D,
        QMI8658_ACCEL_ODR_LOWPOWER_11HZ = 0x0E,
        QMI8658_ACCEL_ODR_LOWPOWER_3HZ = 0x0F
    } qmi8658_accel_odr_t;

    typedef enum
    {
        QMI8658_GYRO_RANGE_32DPS = 0x00,
        QMI8658_GYRO_RANGE_64DPS = 0x01,
        QMI8658_GYRO_RANGE_128DPS = 0x02,
        QMI8658_GYRO_RANGE_256DPS = 0x03,
        QMI8658_GYRO_RANGE_512DPS = 0x04,
        QMI8658_GYRO_RANGE_1024DPS = 0x05,
        QMI8658_GYRO_RANGE_2048DPS = 0x06,
        QMI8658_GYRO_RANGE_4096DPS = 0x07
    } qmi8658_gyro_range_t;

    typedef enum
    {
        QMI8658_GYRO_ODR_8000HZ = 0x00,
        QMI8658_GYRO_ODR_4000HZ = 0x01,
        QMI8658_GYRO_ODR_2000HZ = 0x02,
        QMI8658_GYRO_ODR_1000HZ = 0x03,
        QMI8658_GYRO_ODR_500HZ = 0x04,
        QMI8658_GYRO_ODR_250HZ = 0x05,
        QMI8658_GYRO_ODR_125HZ = 0x06,
        QMI8658_GYRO_ODR_62_5HZ = 0x07,
        QMI8658_GYRO_ODR_31_25HZ = 0x08
    } qmi8658_gyro_odr_t;

    typedef enum
    {
        QMI8658_PRECISION_2 = 2,
        QMI8658_PRECISION_4 = 4,
        QMI8658_PRECISION_6 = 6
    } qmi8658_precision_t;

    typedef enum
    {
        QMI8658_INT_PIN_1 = 0,
        QMI8658_INT_PIN_2 = 1
    } qmi8658_int_pin_t;

    typedef enum
    {
        QMI8658_INT_ACTIVE_LOW = 0,
        QMI8658_INT_ACTIVE_HIGH = 1
    } qmi8658_int_polarity_t;

    typedef enum
    {
        QMI8658_INT_PUSH_PULL = 0,
        QMI8658_INT_OPEN_DRAIN = 1
    } qmi8658_int_output_t;

    typedef enum
    {
        QMI8658_PEDOMETER_MODE_NORMAL = 0,
        QMI8658_PEDOMETER_MODE_ROBUST = 1
    } qmi8658_pedometer_mode_t;

    typedef struct
    {
        float accelX, accelY, accelZ;
        float gyroX, gyroY, gyroZ;
        float temperature;
        uint32_t timestamp;
    } qmi8658_data_t;

    typedef struct
    {
        uint32_t step_count;
        bool step_detected;
        uint32_t step_time;
        float step_frequency;
    } qmi8658_step_data_t;

    typedef struct
    {
        uint8_t interrupt_status;
        bool significant_motion;
        bool any_motion;
        bool no_motion;
        bool pedometer_interrupt;
        bool high_g;
        bool low_g;
        bool data_ready;
        bool fifo_ready;
    } qmi8658_interrupt_status_t;

    typedef struct
    {
        uint8_t threshold; // Motion detection threshold
        uint8_t duration;  // Motion detection duration
        uint8_t count;     // Motion detection count
        bool enable_x;     // Enable X-axis motion detection
        bool enable_y;     // Enable Y-axis motion detection
        bool enable_z;     // Enable Z-axis motion detection
    } qmi8658_motion_config_t;

    typedef struct
    {
        qmi8658_pedometer_mode_t mode; // Pedometer mode
        uint8_t sample_count;          // Sample count for step detection
        uint8_t fix_peak;              // Fixed peak threshold
        uint8_t fix_peak2;             // Second fixed peak threshold
        uint8_t time_up;               // Time up threshold
        uint8_t time_low;              // Time low threshold
        uint8_t time_win;              // Time window
        bool reset_on_read;            // Reset step counter when read
    } qmi8658_pedometer_config_t;

    // Callback function types
    typedef void (*qmi8658_interrupt_callback_t)(qmi8658_interrupt_status_t status, void *user_data);
    typedef void (*qmi8658_step_callback_t)(qmi8658_step_data_t step_data, void *user_data);

    typedef struct
    {
        i2c_master_bus_handle_t bus_handle;
        i2c_master_dev_handle_t dev_handle;
        uint16_t accel_lsb_div;
        uint16_t gyro_lsb_div;
        bool accel_unit_mps2;
        bool gyro_unit_rads;
        int display_precision;
        uint32_t timestamp;

        // Interrupt handling
        gpio_num_t int1_gpio;
        gpio_num_t int2_gpio;
        qmi8658_interrupt_callback_t interrupt_callback;
        qmi8658_step_callback_t step_callback;
        void *callback_user_data;

        // Step counting
        uint32_t last_step_count;
        uint32_t last_step_time;
        bool pedometer_enabled;
        qmi8658_pedometer_config_t pedometer_config;
    } qmi8658_dev_t;

    // Original functions
    esp_err_t qmi8658_init(qmi8658_dev_t *dev, i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr);

    esp_err_t qmi8658_set_accel_range(qmi8658_dev_t *dev, qmi8658_accel_range_t range);
    esp_err_t qmi8658_set_accel_odr(qmi8658_dev_t *dev, qmi8658_accel_odr_t odr);
    esp_err_t qmi8658_set_gyro_range(qmi8658_dev_t *dev, qmi8658_gyro_range_t range);
    esp_err_t qmi8658_set_gyro_odr(qmi8658_dev_t *dev, qmi8658_gyro_odr_t odr);

    esp_err_t qmi8658_enable_accel(qmi8658_dev_t *dev, bool enable);
    esp_err_t qmi8658_enable_gyro(qmi8658_dev_t *dev, bool enable);
    esp_err_t qmi8658_enable_sensors(qmi8658_dev_t *dev, uint8_t enable_flags);

    esp_err_t qmi8658_read_accel(qmi8658_dev_t *dev, float *x, float *y, float *z);
    esp_err_t qmi8658_read_gyro(qmi8658_dev_t *dev, float *x, float *y, float *z);
    esp_err_t qmi8658_read_temp(qmi8658_dev_t *dev, float *temperature);
    esp_err_t qmi8658_read_sensor_data(qmi8658_dev_t *dev, qmi8658_data_t *data);

    esp_err_t qmi8658_read_accel_mg(qmi8658_dev_t *dev, float *x, float *y, float *z);
    esp_err_t qmi8658_read_accel_mps2(qmi8658_dev_t *dev, float *x, float *y, float *z);
    esp_err_t qmi8658_read_gyro_dps(qmi8658_dev_t *dev, float *x, float *y, float *z);
    esp_err_t qmi8658_read_gyro_rads(qmi8658_dev_t *dev, float *x, float *y, float *z);

    esp_err_t qmi8658_is_data_ready(qmi8658_dev_t *dev, bool *ready);
    esp_err_t qmi8658_get_who_am_i(qmi8658_dev_t *dev, uint8_t *who_am_i);
    esp_err_t qmi8658_reset(qmi8658_dev_t *dev);

    void qmi8658_set_accel_unit_mps2(qmi8658_dev_t *dev, bool use_mps2);
    void qmi8658_set_accel_unit_mg(qmi8658_dev_t *dev, bool use_mg);
    void qmi8658_set_gyro_unit_rads(qmi8658_dev_t *dev, bool use_rads);
    void qmi8658_set_gyro_unit_dps(qmi8658_dev_t *dev, bool use_dps);

    void qmi8658_set_display_precision(qmi8658_dev_t *dev, int decimals);
    void qmi8658_set_display_precision_enum(qmi8658_dev_t *dev, qmi8658_precision_t precision);
    int qmi8658_get_display_precision(qmi8658_dev_t *dev);

    bool qmi8658_is_accel_unit_mps2(qmi8658_dev_t *dev);
    bool qmi8658_is_accel_unit_mg(qmi8658_dev_t *dev);
    bool qmi8658_is_gyro_unit_rads(qmi8658_dev_t *dev);
    bool qmi8658_is_gyro_unit_dps(qmi8658_dev_t *dev);

    esp_err_t qmi8658_enable_wake_on_motion(qmi8658_dev_t *dev, uint8_t threshold);
    esp_err_t qmi8658_disable_wake_on_motion(qmi8658_dev_t *dev);

    esp_err_t qmi8658_write_register(qmi8658_dev_t *dev, uint8_t reg, uint8_t value);
    esp_err_t qmi8658_read_register(qmi8658_dev_t *dev, uint8_t reg, uint8_t *buffer, uint8_t length);

    // New step counting functions
    esp_err_t qmi8658_enable_pedometer(qmi8658_dev_t *dev, bool enable);
    esp_err_t qmi8658_configure_pedometer(qmi8658_dev_t *dev, const qmi8658_pedometer_config_t *config);
    esp_err_t qmi8658_reset_step_counter(qmi8658_dev_t *dev);
    esp_err_t qmi8658_read_step_count(qmi8658_dev_t *dev, uint32_t *step_count);
    esp_err_t qmi8658_read_step_data(qmi8658_dev_t *dev, qmi8658_step_data_t *step_data);

    // New interrupt functions
    esp_err_t qmi8658_init_interrupts(qmi8658_dev_t *dev, gpio_num_t int1_gpio, gpio_num_t int2_gpio);
    esp_err_t qmi8658_config_interrupt_pin(qmi8658_dev_t *dev, qmi8658_int_pin_t pin,
                                           qmi8658_int_polarity_t polarity, qmi8658_int_output_t output);
    esp_err_t qmi8658_enable_interrupts(qmi8658_dev_t *dev, uint8_t interrupt_mask, qmi8658_int_pin_t pin);
    esp_err_t qmi8658_disable_interrupts(qmi8658_dev_t *dev, uint8_t interrupt_mask);
    esp_err_t qmi8658_read_interrupt_status(qmi8658_dev_t *dev, qmi8658_interrupt_status_t *status);
    esp_err_t qmi8658_clear_interrupts(qmi8658_dev_t *dev);

    // Motion detection functions
    esp_err_t qmi8658_config_significant_motion(qmi8658_dev_t *dev, const qmi8658_motion_config_t *config);
    esp_err_t qmi8658_config_any_motion(qmi8658_dev_t *dev, const qmi8658_motion_config_t *config);
    esp_err_t qmi8658_config_no_motion(qmi8658_dev_t *dev, const qmi8658_motion_config_t *config);

    // Callback functions
    void qmi8658_set_interrupt_callback(qmi8658_dev_t *dev, qmi8658_interrupt_callback_t callback, void *user_data);
    void qmi8658_set_step_callback(qmi8658_dev_t *dev, qmi8658_step_callback_t callback, void *user_data);

    // ISR handler (should be called from GPIO interrupt)
    void qmi8658_handle_interrupt(qmi8658_dev_t *dev);

#endif // QMI8658_H

#ifdef __cplusplus
}
#endif