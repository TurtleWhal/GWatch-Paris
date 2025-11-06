#include "watch.hpp"

#include "../components/cfscn__sensorlib/src/SensorQMI8658.hpp"
#include <math.h>
#include <driver/gpio.h>

const char *TAG = "QMI8658";

SensorQMI8658 qmi;

volatile bool step_interrupt;

void set_flag(void *arg)
{
    step_interrupt = true;
    ESP_LOGI(TAG, "flag");
}

void pedometer_event()
{
    uint32_t val = qmi.getPedometerCounter();
    ESP_LOGI(TAG, "Pedometer: %d", val);

    watch.imu.steps = val;
}

void imu_task(void *pvParamaters)
{
    while (true)
    {
        // if (step_interrupt)
        // {
        //     step_interrupt = false;
        //     qmi.update();
        // }
        qmi.update();

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void imu_init(i2c_master_bus_handle_t bus)
{
    bool err = qmi.begin(bus, QMI8658_L_SLAVE_ADDRESS);

    if (!err)
    {
        ESP_LOGW(TAG, "Failed to find QMI8658");
    }

    /* Get chip id*/
    ESP_LOGI(TAG, "Device ID: %x", qmi.getChipID());

    // Equipped with acceleration sensor, 2G, ORR62.5HZ
    // qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_2G, SensorQMI8658::ACC_ODR_62_5Hz);
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_2G, SensorQMI8658::ACC_ODR_125Hz);

    // Enable the accelerometer
    qmi.enableAccelerometer();

#define PED_SENSITIVITY 2

    //* Indicates the count of sample batch/window for calculation
    uint16_t ped_sample_cnt = 50; // 50 samples
    //* Indicates the threshold of the valid peak-to-peak detection
    uint16_t ped_fix_peak2peak = 200 / PED_SENSITIVITY; // 200mg
    //* Indicates the threshold of the peak detection comparing to average
    uint16_t ped_fix_peak = 100 / PED_SENSITIVITY; // 100mg
    //* Indicates the maximum duration (timeout window) for a step.
    //* Reset counting calculation if no peaks detected within this duration.
    uint16_t ped_time_up = 200; // 200 samples 4s
    //* Indicates the minimum duration for a step.
    //* The peaks detected within this duration (quiet time) is ignored.
    uint8_t ped_time_low = 20; // 20 samples
    //*   Indicates the minimum continuous steps to start the valid step counting.
    //*   If the continuously detected steps is lower than this count and timeout,the steps will not be take into account;
    //*   if yes, the detected steps will all be taken into account and counting is started to count every following step before timeout.
    //*   This is useful to screen out the fake steps detected by non-step vibrations
    //*   The timeout duration is defined by ped_time_up.
    uint8_t ped_time_cnt_entry = 10; // 10 steps entry count
    //*   Recommended 0
    uint8_t ped_fix_precision = 0;
    //*   The amount of steps when to update the pedometer output registers.
    uint8_t ped_sig_count = 4; // Every 4 valid steps is detected, update the registers once (added by 4).

    qmi.configPedometer(ped_sample_cnt,
                        ped_fix_peak2peak,
                        ped_fix_peak,
                        ped_time_up,
                        ped_time_low,
                        ped_time_cnt_entry,
                        ped_fix_precision,
                        ped_sig_count);

    // Enable the step counter and enable the interrupt
    if (!qmi.enablePedometer(SensorQMI8658::INTERRUPT_PIN_1))
    {
        ESP_LOGW(TAG, "Enable pedometer failed!");
    }

    // Set the step counter callback function
    qmi.setPedometerEventCallBack(pedometer_event);

    gpio_isr_handler_add(IMU_INT1, set_flag, NULL);

    xTaskCreate(imu_task, "imu_task", 1024 * 4, NULL, 4, NULL);
}

Acceleration imu_read()
{
    Acceleration a;

    qmi.getAccelerometer(a.x, a.y, a.z);

    return a;
}