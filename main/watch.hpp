#ifndef WATCH_H
#define WATCH_H

#include "stdint.h"

#include "esp_task.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_pm.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <driver/i2c_master.h>

#include "pins.h"

#include "battery.h"
#include "wifi.hpp"
#include "ui.h"
#include "display.hpp"

struct TimeInfo
{
    int year;  // full year, e.g., 2025
    int month; // 1..12
    int day;   // 1..31
    int hour;  // 0..23
    int min;   // 0..59
    int sec;   // 0..59
    int ms;    // 0..999
};

#ifdef __cplusplus

/** Main watch class to link and provide access to all subsystems */
class Watch
{
private:
    esp_pm_lock_handle_t pm_freq_lock;
    esp_pm_lock_handle_t pm_sleep_lock;
    i2c_master_bus_handle_t i2c_bus;

    uint32_t sleep_time;

    bool goingtosleep;

    void pm_init();
    void iic_init();

    void pm_update();

public:
    struct TimeInfo time;
    struct BatteryInfo battery;
    struct WiFi wifi;

    Display display;

    bool sleeping;

    void init();

    void sleep();
    void wakeup();
};

extern Watch watch;

#endif // __cplusplus

#ifdef __cplusplus
extern "C"
{
#endif

    void watch_init();

#ifdef __cplusplus
}
#endif

// uint32_t millis()
// {
//     return esp_timer_get_time() / 1000;
// }

#endif // WATCH_H