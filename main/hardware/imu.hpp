#include "stdint.h"

struct IMUInfo
{
    uint32_t steps;
};

typedef struct
{
    float x;
    float y;
    float z;
} Acceleration;

void imu_init(i2c_master_bus_handle_t bus);
Acceleration imu_read();