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

// Drop the IMU to slow ODRs / suspend the polling task while asleep
// (saves ~3 mA + lets the chip light-sleep without I²C jitter).
void imu_sleep();
void imu_wake();