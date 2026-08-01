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

// ---------------- Wrist-raise (glance) wake detection ----------------
//
// Accel-only orientation state machine, polled by pm_update every 100 ms
// while the watch sleeps. Replaces the old single-sample gyro threshold
// (>250 dps on one instantaneous reading per 200 ms): a wrist-raise's
// rotation peak lasts only ~100–200 ms, so a sparse instantaneous poll
// usually landed outside it and only exaggerated motions tripped it. A
// glance is better characterized by its END STATE — the screen turns
// face-up and STAYS there while the user reads it — and an end state
// that persists for hundreds of ms cannot be missed by a 10 Hz poll.
//
// Detection is DELTA-based on a FILTERED GRAVITY estimate: fire when
// the screen is face-up and settled NOW, and its face-up-ness ROSE
// materially within the last ~1.5 s, starting from OUTSIDE the face-up
// zone. Each piece exists to fix an observed failure mode:
//
// * Gravity EMA, magnitude-gated. Raw az mixes gravity with linear
//   acceleration — a straight-line shove along Z faked an orientation
//   dip into the history, and the stillness right after read as a
//   "raise" (wakes on pure linear motion). `up` is now computed from an
//   EMA gravity estimate that only accepts samples whose |a| is within
//   WRIST_STATIC_LO..HI of 1 g: during a shove |a| deviates, the
//   estimate freezes, and no fake dip enters the history. Normalizing
//   by |g_est| makes `up` pure direction cosine.
//
// * Raise measured vs the window MINIMUM (not the oldest sample), so
//   slow lazy raises stay detectable — the low point a raise started
//   from lingers in the window regardless of how long the raise takes.
//
// * The raise must START below the face-up zone (window_min <
//   WRIST_FACE_UP_G). This is the anti-loop rule: a watch resting
//   face-up whose `up` jitters around the threshold can NEVER fire (its
//   window minimum sits inside the zone), and a watch that times out
//   and sleeps while being looked at can't immediately re-wake itself.
//   Without it, threshold-straddling micro-motion produced sleep→wake→
//   sleep loops on a completely resting arm. The flip side — no wake
//   when the pose was already within ~50° of face-up — costs nothing
//   real: from there the raise gesture barely exists physically.
//
// * The raise itself is recognized through TWO parallel paths, both
//   measured against history samples that started outside the face-up
//   zone (industry practice — Bosch's wrist-wear-wakeup engine keys on
//   "attitude change within a 1 s window", InfiniTime on "degrees
//   rolled" over its stats window):
//     - component path: `up` rose by ≥ WRIST_RAISE_DELTA. Sensitive to
//       small pitch-toward-face glances near the zone edge, where the
//       total 3-D rotation is only 10–15°.
//     - attitude path: the 3-D angle between the current gravity vector
//       and a history vector is ≥ ~30° (dot ≤ WRIST_RAISE_ANGLE_COS).
//       Catches supination-heavy twists, where the wrist rolls a lot
//       but the z-component (and thus `up`) changes little.
//
// * Directional focus pose (Bosch bounds its focus box per direction;
//   ours bounds the one direction that matters): the end pose must not
//   LEAN AWAY from the user by more than ~10°. "Toward the user" in the
//   sensor frame is derived per display rotation from the same
//   empirically-verified table as the gyro fade-cancel in watch.cpp.
//   A pose 50° toward your face is a glance; 50° tilted away is you
//   showing the screen to someone else, or an arm resting pointed away
//   — those no longer wake.
//
//   FIRE ⇔  up > WRIST_FACE_UP_G                   (facing the user, ~≤52°)
//        ∧  lean_toward_user ≥ WRIST_LEAN_MIN      (not tilted away >~10°)
//        ∧  |a| within WRIST_STATIC_LO..HI of 1 g  (settled, not mid-swing)
//        ∧  all of the above for WRIST_CONFIRM_SAMPLES consecutive polls
//        ∧  ∃ history sample h with up_h < WRIST_FACE_UP_G  (started off-face)
//             where  up − up_h ≥ WRIST_RAISE_DELTA          (component path)
//                 ∨  angle(g_now, g_h) ≥ ~30°               (attitude path)
//
// The gyro plays no part, so imu_sleep powers it down entirely; the
// accel already runs at 125 Hz for the pedometer, making the detector's
// marginal sensor cost zero — the only cost is one 6-byte I²C read per
// poll.
//
// Axis note: the display normal is along Z in the sensor frame no
// matter what UI rotation is configured (rotation spins the image
// within the screen plane), so az alone answers "face-up?" — unlike the
// gyro fade-cancel in watch.cpp, no rotation mapping is needed. On THIS
// board the QMI8658 mounts with +Z out the BACK of the watch: face-up
// on the wrist reads az ≈ −1 g, so the code below uses `up = -az`.
// (Symptom when this sign is wrong: wake fires on tilting the screen
// AWAY from you, and never fires on a glance — because "away" then
// matches screen-vertical postures and "face-up" is actually
// face-down. Verified empirically on-wrist.)

static constexpr float WRIST_FACE_UP_G = 0.62f;   // up above ⇒ facing user (≈≤52°)
static constexpr float WRIST_RAISE_DELTA = 0.15f; // min rise of `up` vs window low
static constexpr float WRIST_RAISE_ANGLE_COS = 0.866f; // 3-D attitude path: ≥30°
static constexpr float WRIST_LEAN_MIN = -0.17f;   // ≥ ~10° leaned AWAY ⇒ not a glance
static constexpr float WRIST_STATIC_LO = 0.75f;   // |a| gate: quasi-static only,
static constexpr float WRIST_STATIC_HI = 1.25f;   // rejects mid-swing samples
static constexpr int WRIST_CONFIRM_SAMPLES = 2;   // 200 ms face-up dwell to fire
static constexpr int WRIST_HISTORY = 15;          // 1.5 s rolling attitude window
static constexpr float WRIST_EMA_ALPHA = 0.5f;    // gravity LPF, ~200 ms @ 10 Hz

static float wr_grav[3];        // EMA gravity estimate, g units
static bool wr_grav_valid = false;
static float wr_hist[WRIST_HISTORY][3]; // normalized gravity vectors
static int wr_hist_head = 0; // next write slot (= oldest sample once full)
static int wr_hist_fill = 0; // valid entries; gates firing until history exists
static int wr_confirm_cnt = 0;

void imu_wrist_raise_reset() {
  wr_grav_valid = false;
  wr_hist_head = 0;
  wr_hist_fill = 0;
  wr_confirm_cnt = 0;
}

// Signed "leaning toward the user" component of the (normalized) gravity
// vector, selected per display rotation. Derived from the gyro fade-cancel
// table in watch.cpp (whose signs are verified on-wrist): rotating the
// display normal by each rotation's wake axis shows which sensor-frame
// direction the user's face lies in; the gravity component along that
// direction goes negative when the pose leans AWAY. If lean-gating ever
// blocks glances after a rotation/wearing change, this mapping (not the
// threshold) is the suspect — see the "blocked by lean" log below.
static float wrist_lean_toward_user(float gx, float gy) {
  switch (lv_display_get_rotation(NULL)) {
  case LV_DISPLAY_ROTATION_0:
    return -gx;
  case LV_DISPLAY_ROTATION_90:
    return gy;
  case LV_DISPLAY_ROTATION_180:
    return gx;
  case LV_DISPLAY_ROTATION_270:
    return -gy;
  }
  return 0;
}

bool imu_wrist_raise_poll() {
  Acceleration a = {};
  // Failed read (I²C hiccup): hold state rather than feeding the
  // detector a zero vector, which would look like a deep "low point"
  // and prime a false raise-delta.
  if (!qmi.getAccelerometer(a.x, a.y, a.z))
    return false;

  float mag = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  bool mag_ok = mag > WRIST_STATIC_LO && mag < WRIST_STATIC_HI;

  // Gravity EMA — accepts only quasi-static samples so linear
  // acceleration can't masquerade as orientation change; the estimate
  // freezes during shoves/swings and resumes tracking once |a| ≈ 1 g
  // again. Seeded directly from the first clean sample after a reset.
  if (mag_ok) {
    if (!wr_grav_valid) {
      wr_grav[0] = a.x;
      wr_grav[1] = a.y;
      wr_grav[2] = a.z;
      wr_grav_valid = true;
    } else {
      wr_grav[0] += WRIST_EMA_ALPHA * (a.x - wr_grav[0]);
      wr_grav[1] += WRIST_EMA_ALPHA * (a.y - wr_grav[1]);
      wr_grav[2] += WRIST_EMA_ALPHA * (a.z - wr_grav[2]);
    }
  }
  if (!wr_grav_valid)
    return false; // nothing clean seen yet (continuous motion)

  float gmag = sqrtf(wr_grav[0] * wr_grav[0] + wr_grav[1] * wr_grav[1] +
                     wr_grav[2] * wr_grav[2]);
  if (gmag < 0.5f)
    return false; // degenerate estimate; wait for it to recover

  // Normalized gravity direction. up: direction cosine of the display
  // normal vs "up" — +Z points out the back of the watch on this board,
  // see axis note above.
  float gu[3] = {wr_grav[0] / gmag, wr_grav[1] / gmag, wr_grav[2] / gmag};
  float up = -gu[2];

  // Scan history (before pushing the current sample, so the current
  // pose can't shrink its own deltas) for the best raise evidence among
  // samples that started OUTSIDE the face-up zone — the anti-loop rule:
  // poses that were face-up all along contribute nothing.
  float up_min = 1.0f;   // lowest off-face `up` (component path)
  float dot_min = 1.0f;  // most-different off-face attitude (3-D path)
  for (int i = 0; i < wr_hist_fill; i++) {
    float up_h = -wr_hist[i][2];
    if (up_h >= WRIST_FACE_UP_G)
      continue;
    if (up_h < up_min)
      up_min = up_h;
    float d = gu[0] * wr_hist[i][0] + gu[1] * wr_hist[i][1] +
              gu[2] * wr_hist[i][2];
    if (d < dot_min)
      dot_min = d;
  }
  bool started_off_face = up_min < WRIST_FACE_UP_G;

  bool history_full = wr_hist_fill >= WRIST_HISTORY;
  wr_hist[wr_hist_head][0] = gu[0];
  wr_hist[wr_hist_head][1] = gu[1];
  wr_hist[wr_hist_head][2] = gu[2];
  wr_hist_head = (wr_hist_head + 1) % WRIST_HISTORY;
  if (wr_hist_fill < WRIST_HISTORY)
    wr_hist_fill++;

  float lean = wrist_lean_toward_user(gu[0], gu[1]);
  bool settled = up > WRIST_FACE_UP_G && lean >= WRIST_LEAN_MIN && mag_ok;
  if (!settled) {
    // Diagnostic: a dwelled pose that passes everything EXCEPT the lean
    // gate would have fired without it — worth seeing on the monitor.
    // If this logs on every real glance, the rotation mapping in
    // wrist_lean_toward_user is wrong for how the watch is being worn.
    // Own counter (not wr_confirm_cnt, which a steady lean-block never
    // increments); logs once per blocked episode.
    static int lean_blocked_cnt = 0;
    if (up > WRIST_FACE_UP_G && mag_ok && lean < WRIST_LEAN_MIN) {
      if (++lean_blocked_cnt == WRIST_CONFIRM_SAMPLES)
        ESP_LOGI(TAG, "wrist-raise blocked by lean=%.2f", (double)lean);
    } else {
      lean_blocked_cnt = 0;
    }
    wr_confirm_cnt = 0;
    return false;
  }

  if (++wr_confirm_cnt < WRIST_CONFIRM_SAMPLES)
    return false;

  // history_full also debounces sleep entry: for the first 1.5 s of a
  // sleep the window is still priming and nothing can fire. Two raise
  // paths — see block comment: component delta for shallow
  // pitch-toward-face glances, 3-D attitude angle for supination-heavy
  // twists that barely move `up`.
  if (history_full && started_off_face &&
      ((up - up_min) >= WRIST_RAISE_DELTA ||
       dot_min <= WRIST_RAISE_ANGLE_COS)) {
    imu_wrist_raise_reset();
    return true;
  }
  return false;
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
  // Gyro: fully OFF while asleep (~0.6 mA saved — a MEMS gyro's drive
  // oscillator draws that much at any ODR). The wrist-raise wake
  // detector below is accel-only, so nothing needs the gyro until the
  // watch is awake again. This is a plain CTRL7 sensor-disable — NOT
  // the broken WoM mode CLAUDE.md warns about.
  qmi.disableGyroscope();
  imu_wrist_raise_reset();
  if (imu_task_handle)
    vTaskSuspend(imu_task_handle);
}

void imu_wake() {
  if (imu_task_handle)
    vTaskResume(imu_task_handle);
  qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,
                      SensorQMI8658::GYR_ODR_224_2Hz);
  qmi.enableGyroscope();
  // Catch up the step counter to whatever the chip accumulated while
  // we were asleep. Without this the displayed count stays frozen at
  // the pre-sleep value until the next interrupt fires through
  // qmi.update(), which can be a long time post-wake if the user just
  // glances at the watch and goes back to sleep.
  watch.imu.steps = qmi.getPedometerCounter();
}
