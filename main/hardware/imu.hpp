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

// Accel-only wrist-raise (glance) detector — see the block comment in
// imu.cpp. pm_update polls this every 100 ms while the watch sleeps;
// true means "the user just raised and is looking at the watch".
// State resets on imu_sleep() so a watch that dozes off while face-up
// can't immediately re-wake itself.
bool imu_wrist_raise_poll();
void imu_wrist_raise_reset();