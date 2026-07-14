#include "watch.hpp"

#include "../components/cfscn__sensorlib/src/SensorQMI8658.hpp"
#include <driver/gpio.h>
#include <math.h>

static const char *TAG = "QMI8658";

SensorQMI8658 qmi;

static TaskHandle_t imu_task_handle = NULL;

volatile bool step_interrupt;

static void pedometer_event();

// Set up accel + gyro + pedometer at full rates. Called from imu_init and
// from imu_wake().
static void imu_configure_normal() {
  qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_16G,
                          SensorQMI8658::ACC_ODR_125Hz);
  qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,
                      SensorQMI8658::GYR_ODR_224_2Hz);
  qmi.enableAccelerometer();
  qmi.enableGyroscope();

  // Tuned for wrist-worn use. The QMI8658 datasheet defaults
  // (peak2peak=200 mg, peak=100 mg, time_cnt_entry=10) assume hip /
  // pocket mounting where the gait impulse hits the sensor directly.
  // On the wrist the arm absorbs most of the impulse — peak-to-peak
  // accel during a relaxed walking step is closer to 40–80 mg, well
  // under the default. Stock thresholds detect only exaggerated arm
  // swings (running, deliberate gesture), missing most normal steps.
  //
  // All time-valued fields are in sample counts at the *current*
  // accel ODR (datasheet: "80 means 1.6s @ ODR=50Hz"). Configured for
  // 125 Hz; imu_sleep deliberately keeps accel at 125 Hz so these
  // windows stay valid while the watch is asleep.
  uint16_t ped_sample_cnt = 50;
  // 40/20 mg fell below the noise floor of incidental wrist motion —
  // just shaking the watch in your hand racked up steps. 60/30 is the
  // sweet spot for this board: enough to filter rest-state jitter,
  // low enough to catch a normal walking cadence on the wrist.
  uint16_t ped_fix_peak2peak = 60;  // mg
  uint16_t ped_fix_peak = 30;       // mg — half of p2p, per QMI guidance
  uint16_t ped_time_up = 200;       // 1.6s @ 125Hz max gap between steps
  uint8_t ped_time_low = 30;        // 240ms @ 125Hz — rejects back-swing
  // Number of consecutive cadence-consistent peaks the chip must see
  // before it commits to "this is walking" and starts adding to the
  // counter. Higher = more shake rejection (short bursts of wrist
  // motion that aren't gait can't trip a meaningful count) at the
  // cost of throwing away the first N steps of every real walk. 6
  // ≈ 6 wrist swings ≈ 6 seconds before counting engages, which
  // matches how a real walk looks: it ramps in, it doesn't start
  // with one explosive swing.
  uint8_t ped_time_cnt_entry = 6;
  uint8_t ped_fix_precision = 0;
  // Was 4: counter register only updated in 4-step groups, so any
  // walk ending on a non-multiple-of-4 forfeited the trailing 1–3
  // steps when the cadence stopped. 1 = flush every individual step,
  // at the cost of one extra interrupt per step (negligible — ~2 Hz
  // when walking, free when not).
  uint8_t ped_sig_count = 1;

  qmi.configPedometer(ped_sample_cnt, ped_fix_peak2peak, ped_fix_peak,
                      ped_time_up, ped_time_low, ped_time_cnt_entry,
                      ped_fix_precision, ped_sig_count);
  qmi.enablePedometer(SensorQMI8658::INTERRUPT_PIN_1);
  qmi.setPedometerEventCallBack(pedometer_event);
}

void set_flag(void *arg) {
  step_interrupt = true;
  ESP_LOGI(TAG, "flag");
}

static void pedometer_event() {
  uint32_t val = qmi.getPedometerCounter();
  watch.imu.steps = val;
}

void imu_task(void *pvParamaters) {
  while (true) {
    qmi.update();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void imu_init(i2c_master_bus_handle_t bus) {
  bool err = qmi.begin(bus, QMI8658_L_SLAVE_ADDRESS);

  if (!err) {
    ESP_LOGW(TAG, "Failed to find QMI8658");
  }

  ESP_LOGI(TAG, "Device ID: %x", qmi.getChipID());

  imu_configure_normal();

  // Stack in PSRAM — I2C reads use the i2c master driver's own
  // buffers; the task itself is pure math + queue posts. Frees 4 KB
  // of internal SRAM.
  xTaskCreateWithCaps(imu_task, "imu_task", 1024 * 4, NULL, 4, &imu_task_handle,
                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

Acceleration accel_read() {
  Acceleration a;
  qmi.getAccelerometer(a.x, a.y, a.z);
  return a;
}

GyroData gyro_read() {
  GyroData a;
  qmi.getGyroscope(a.x, a.y, a.z);
  return a;
}

void imu_sleep() {
  // Keep accel at the same ODR the pedometer was configured for
  // (125 Hz). The QMI8658's built-in pedometer derives all its time
  // windows (ped_time_up, ped_time_low, ped_sample_cnt) in *sample
  // counts*, so changing the accel ODR while asleep would silently
  // skew them — at 31.25 Hz the 200-sample max-step-gap that meant
  // 1.6 s now means 6.4 s, and detection collapses. The chip keeps
  // counting autonomously while asleep at this ODR; the counter is
  // read out on wake (below) since imu_task is suspended here.
  //
  // Power cost: ACC_ODR_125Hz LP ≈ 30 µA vs 10 µA @ 31.25 Hz. The
  // extra 20 µA is negligible against the ~12 mA light-sleep floor.
  //
  // Gyro: pm_update only takes a single sample per poll (every 200 ms),
  // so any continuous ODR ≥ 5 Hz produces a fresh sample on every poll
  // and we just want the chip's lowest current draw. GYR_ODR_28_025Hz
  // is the floor — drops gyro draw vs 56 Hz with no impact on the
  // single-shot read we actually consume. The 250 dps threshold in
  // tilt_wake_detected sees the same instantaneous value either way.
  qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,
                      SensorQMI8658::GYR_ODR_28_025Hz);
  if (imu_task_handle)
    vTaskSuspend(imu_task_handle);
}

void imu_wake() {
  if (imu_task_handle)
    vTaskResume(imu_task_handle);
  qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,
                      SensorQMI8658::GYR_ODR_224_2Hz);
  // Catch up the step counter to whatever the chip accumulated while
  // we were asleep. Without this the displayed count stays frozen at
  // the pre-sleep value until the next interrupt fires through
  // qmi.update(), which can be a long time post-wake if the user just
  // glances at the watch and goes back to sleep.
  watch.imu.steps = qmi.getPedometerCounter();
}
