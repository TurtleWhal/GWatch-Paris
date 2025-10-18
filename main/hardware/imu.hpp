#include "stdint.h"

struct IMUInfo
{
    uint32_t steps;
};

void imu_init(i2c_master_bus_handle_t bus);