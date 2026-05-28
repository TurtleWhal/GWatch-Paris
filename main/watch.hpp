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
#include "freertos/idf_additions.h"  // xTaskCreateWithCaps for PSRAM stacks

#include <driver/i2c_master.h>

#include "pins.h"

#include "settings.hpp"
#include "battery.hpp"
#include "motor.hpp"
#include "imu.hpp"
#include "ble.hpp"
#include "display.hpp"
#include "schedule.hpp"

#define DEFAULT_SLEEP_TIME 15000

struct SystemInfo
{
    uint16_t sleeptime;
    bool dosleep;
};

struct Chronology
{
    bool timerrunning;
    int64_t timertime;

    bool stopwatchrunning;
    int64_t stopwatchstarttime;
};

#ifdef __cplusplus

/** Main watch class to link and provide access to all subsystems */
class Watch
{
private:
    esp_pm_lock_handle_t pm_freq_lock;
    esp_pm_lock_handle_t pm_sleep_lock;
    i2c_master_bus_handle_t i2c_bus;

    TaskHandle_t pm_task;

    uint32_t sleep_time;

    void pm_init();
    void iic_init();
    void i2c_scan();

    void pm_update();

public:
    // struct TimeInfo time;
    struct SystemInfo system = {DEFAULT_SLEEP_TIME, true};
    struct BatteryInfo battery;
    struct IMUInfo imu;
    struct Schedule schedule;
    struct Chronology chrono;

    Settings settings;

    Display display;

    bool sleeping;
    bool goingtosleep;

    bool donotdisturb = false;

    // While `esp_timer_get_time()/1000 < prevent_sleep_until_ms` the pm
    // task skips its idle-sleep check. Set this as a deadline (e.g. a
    // 60-second hold from "now") to keep the watch awake regardless of
    // touch activity.
    int64_t prevent_sleep_until_ms = 0;

    // When true, Watch::sleep() leaves lv_screen_active() alone
    // instead of swapping it back to main_screen. Used by the alarm /
    // timer ring screens so that after they auto-stop the watch goes
    // to sleep but stays *on* the ring screen — when the user wakes
    // it they see the alarm/timer that fired.
    bool preserve_screen_on_sleep = false;

    void init();

    void sleep();
    void wakeup();

    // Trip the pm idle check on the next pm_task iteration. Used by the
    // alarm/timer ring screens once their stay-awake hold has elapsed —
    // calling sleep() directly from the LVGL task would block LVGL for
    // the full backlight-fade window, so we hand off to the pm task.
    void request_sleep();
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