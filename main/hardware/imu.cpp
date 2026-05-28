#include "watch.hpp"

#include "../components/cfscn__sensorlib/src/SensorQMI8658.hpp"
#include <math.h>
#include <driver/gpio.h>

static const char *TAG = "QMI8658";

SensorQMI8658 qmi;

static TaskHandle_t imu_task_handle = NULL;

volatile bool step_interrupt;

static void pedometer_event();

// Set up accel + gyro + pedometer at full rates. Called from imu_init and
// from imu_wake().
static void imu_configure_normal()
{
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_16G,
                            SensorQMI8658::ACC_ODR_125Hz);
    qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,
                        SensorQMI8658::GYR_ODR_224_2Hz);
    qmi.enableAccelerometer();
    qmi.enableGyroscope();

#define PED_SENSITIVITY 2
    uint16_t ped_sample_cnt = 50;
    uint16_t ped_fix_peak2peak = 200 / PED_SENSITIVITY;
    uint16_t ped_fix_peak = 100 / PED_SENSITIVITY;
    uint16_t ped_time_up = 200;
    uint8_t ped_time_low = 20;
    uint8_t ped_time_cnt_entry = 10;
    uint8_t ped_fix_precision = 0;
    uint8_t ped_sig_count = 4;
    qmi.configPedometer(ped_sample_cnt, ped_fix_peak2peak, ped_fix_peak,
                        ped_time_up, ped_time_low, ped_time_cnt_entry,
                        ped_fix_precision, ped_sig_count);
    qmi.enablePedometer(SensorQMI8658::INTERRUPT_PIN_1);
    qmi.setPedometerEventCallBack(pedometer_event);
}

void set_flag(void *arg)
{
    step_interrupt = true;
    ESP_LOGI(TAG, "flag");
}

static void pedometer_event()
{
    uint32_t val = qmi.getPedometerCounter();
    watch.imu.steps = val;
}

void imu_task(void *pvParamaters)
{
    while (true)
    {
        qmi.update();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void imu_init(i2c_master_bus_handle_t bus)
{
    bool err = qmi.begin(bus, QMI8658_L_SLAVE_ADDRESS);

    if (!err)
    {
        ESP_LOGW(TAG, "Failed to find QMI8658");
    }

    ESP_LOGI(TAG, "Device ID: %x", qmi.getChipID());

    imu_configure_normal();

    // Stack in PSRAM — I2C reads use the i2c master driver's own
    // buffers; the task itself is pure math + queue posts. Frees 4 KB
    // of internal SRAM.
    xTaskCreateWithCaps(imu_task, "imu_task", 1024 * 4, NULL, 4,
                        &imu_task_handle,
                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

Acceleration accel_read()
{
    Acceleration a;
    qmi.getAccelerometer(a.x, a.y, a.z);
    return a;
}

GyroData gyro_read()
{
    GyroData a;
    qmi.getGyroscope(a.x, a.y, a.z);
    return a;
}

void imu_sleep()
{
    // Drop accel + gyro to slow ODRs while the watch is asleep. Suspend the
    // polling task so its 100 ms I²C burst doesn't keep the chip from
    // light-sleeping. Pedometer counter freezes while asleep.
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_16G,
                            SensorQMI8658::ACC_ODR_31_25Hz);
    qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,
                        SensorQMI8658::GYR_ODR_56_05Hz);
    if (imu_task_handle) vTaskSuspend(imu_task_handle);
}

void imu_wake()
{
    if (imu_task_handle) vTaskResume(imu_task_handle);
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_16G,
                            SensorQMI8658::ACC_ODR_125Hz);
    qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,
                        SensorQMI8658::GYR_ODR_224_2Hz);
}
