#ifndef SYSTEM_H
#define SYSTEM_H

#include "stdint.h"

#include "esp_task.h"

#include "battery.h"
#include "wifitime.h"
#include "ui.h"
#include "display.h"

void wakeup();
void systemsleep();

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

struct BatteryInfo
{
    uint16_t voltage;
    uint8_t percent;
    bool charging;
};

struct WiFiInfo
{
    bool connected;
};

struct SystemInfo
{
    struct TimeInfo time;
    struct BatteryInfo bat;
    struct WiFiInfo wifi;

    bool sleeping; // is system asleep
};

extern struct SystemInfo sysinfo;

#endif // SYSTEM_H