#ifndef SYSTEM_H
#define SYSTEM_H

#include "stdint.h"

#include "esp_task.h"

#include "battery.h"
#include "wifitime.h"
#include "ui.h"
#include "display.h"

struct BatteryInfo
{
    uint16_t voltage;
    uint8_t percent;
    bool charging;
};

struct SystemInfo
{
    struct BatteryInfo bat;
};

extern struct SystemInfo sysinfo;

#endif // SYSTEM_H