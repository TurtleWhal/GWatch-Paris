// Web build of the FreeRTOS tasks. The firmware uses xTaskCreate with
// `while (true) { …; vTaskDelay(N); }` bodies — a pattern that doesn't
// translate to a single-threaded WASM target without stack-unwinding
// tricks (ASYNCIFY) that destabilise emscripten_set_main_loop.
//
// Instead, stubs.cpp's web xTaskCreate calls into sim_web_schedule_task
// below. We pattern-match on the task name and stand up an lv_timer that
// executes the equivalent body — one tick per timer fire.
//
// Currently wires up:
//   "timer_task"  — countdown in watch.chrono.timertime ticks down
//   "alarm_task"  — poll local time, set pending_alarm_idx on match;
//                   the firmware's LVGL-side alarm_check_pending timer
//                   handles the ring screen + haptic exactly as on-device

#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "lvgl.h"

#include "watch.hpp"
#include "esp_timer.h"

extern "C" void sim_web_schedule_task(const char *name,
                                      void (*fn)(void *),
                                      void *arg);

// Mirror of the layout in main/ui/screens/alarm.cpp. Keep in sync:
// any field change there has to land here too. POD layout means linking
// against the firmware's `Alarm alarms[]` is well-defined.
struct Alarm {
    uint8_t hour;
    uint8_t minute;
    bool    am;
    bool    enabled;
};
constexpr int N_ALARMS = 4;  // matches alarm.cpp's static constexpr

extern Alarm alarms[N_ALARMS];
extern volatile int pending_alarm_idx;

namespace {

int64_t  g_timer_lasttime = 0;
uint32_t g_alarm_last_fired_minute[N_ALARMS] = {};

void timer_task_tick(lv_timer_t * /*t*/)
{
    int64_t now = esp_timer_get_time();
    if (g_timer_lasttime == 0) g_timer_lasttime = now;
    int64_t diff = now - g_timer_lasttime;
    g_timer_lasttime = now;

    if (watch.chrono.timerrunning) {
        watch.chrono.timertime -= diff;
        if (watch.chrono.timertime <= 0) {
            watch.chrono.timertime = 0;
            watch.wakeup();
        }
    }
}

void alarm_task_tick(lv_timer_t * /*t*/)
{
    /* Wait for the firmware's LVGL-side `alarm_check_pending` timer to
     * drain a previous fire before scanning again — matches alarm.cpp's
     * alarm_task behaviour. */
    if (pending_alarm_idx >= 0) return;

    struct timeval tv;
    gettimeofday(&tv, nullptr);

    struct tm t;
    localtime_r(&tv.tv_sec, &t);
    uint32_t current_minute = (uint32_t)(tv.tv_sec / 60);

    for (int i = 0; i < N_ALARMS; i++) {
        if (!alarms[i].enabled) continue;
        if (g_alarm_last_fired_minute[i] == current_minute) continue;

        int alarm_h24 = alarms[i].hour % 12;
        if (!alarms[i].am) alarm_h24 += 12;

        if (alarm_h24 == t.tm_hour && alarms[i].minute == t.tm_min) {
            g_alarm_last_fired_minute[i] = current_minute;
            pending_alarm_idx = i;
            watch.wakeup();
            break;
        }
    }
}

}  // namespace

extern "C" void sim_web_schedule_task(const char *name,
                                      void (*fn)(void *),
                                      void *arg)
{
    (void)fn; (void)arg;
    if (!name) return;

    if (strcmp(name, "timer_task") == 0) {
        /* 100ms matches the firmware's vTaskDelay(100). */
        lv_timer_create(timer_task_tick, 100, nullptr);
    } else if (strcmp(name, "alarm_task") == 0) {
        /* 1s matches the firmware's vTaskDelay(pdMS_TO_TICKS(1000)). */
        lv_timer_create(alarm_task_tick, 1000, nullptr);
    }
    /* Other tasks (unistroke export, …) silently dropped. */
}
