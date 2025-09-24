#include "stdint.h"

struct BatteryInfo
{
    uint16_t voltage;
    uint8_t percent;
    bool charging;
};

void battery_init(void);
uint32_t battery_get_mV(void);