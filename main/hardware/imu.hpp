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

typedef struct
{
    float x;
    float y;
    float z;
} GyroData;

void imu_init(i2c_master_bus_handle_t bus);
Acceleration accel_read();
GyroData gyro_read();