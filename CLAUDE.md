# GWatch-Paris

ESP-IDF firmware for a custom round-LCD smartwatch built around a Waveshare
ESP32-S3-Touch-LCD-1.28 module (GC9A01 + CST816S + QMI8658 IMU + LiPo + CH343P
USB-UART), with a 3D-printed shell, vibration motor, and an LVGL 9 UI.

This is the user's third rewrite of the project. It's a personal project, not
a product — there is no test suite, and "stability" is judged by whether the
watch boots and the watch face renders for hours without resetting.

WiFi has been removed. All phone connectivity is over BLE via the Bangle.js /
Gadgetbridge protocol — notifications, time sync, battery status, find phone.

## Build & flash

ESP-IDF v6.0 at `/Users/garrett/esp/v6.0/esp-idf`. The user's IDF venv has
broken before (homebrew python@3.13 → 3.14 churn) — there are two venvs at
`/Users/garrett/.espressif/python_env/` (`idf6.0_py3.13_env` is symlink-broken,
`idf6.0_py3.14_env` works). The toolchain expects python3.14 from
`/opt/homebrew/bin`, so put that on PATH before sourcing export.sh.

```sh
export PATH=/opt/homebrew/bin:$PATH
. /Users/garrett/esp/v6.0/esp-idf/export.sh
idf.py build
idf.py -p /dev/tty.wchusbserial578E0235041 flash monitor
```

If `idf.py build` dies after two lines with "'…/bin/python' is currently
active … while the project was configured with '…/bin/python3'" (exit 2),
the build dir was configured by the VS Code extension via the venv's
`python3` and export.sh activates `python`. Don't fullclean — invoke idf.py
through the interpreter the cache expects:

```sh
/Users/garrett/.espressif/python_env/idf6.0_py3.14_env/bin/python3 \
  /Users/garrett/esp/v6.0/esp-idf/tools/idf.py build
```

If `idf.py reconfigure` fails after switching IDF versions, wipe
`build/bootloader build/bootloader-prefix` (the bootloader subproject caches
the old IDF path).

`-O3` (`COMPILER_OPTIMIZATION_PERF`) and 64-byte data-cache lines are on, so
clean builds are slow. Avoid `idf.py fullclean` unless something is genuinely
busted.

## v6.0 migration notes (in case you ever go back to v5.5 or move forward)

- **`json` component is gone from the IDF tree** — pulled from the component
  registry as `espressif/cjson` via `main/idf_component.yml`. Don't put `json`
  in REQUIRES.
- **`driver/*` headers are split per-peripheral.** REQUIRES needs
  `esp_driver_ledc esp_driver_i2c esp_driver_spi esp_driver_gpio` (the
  umbrella `driver` component is now an empty compat shim).
- **`esp_adc_cal.h` is removed.** Battery uses `esp_adc/adc_cali.h` curve-
  fitting scheme; ESP32-S3 has eFuse-burned cal so `DEFAULT_VREF` isn't a
  thing anymore. `ADC_ATTEN_DB_11` was renamed to `ADC_ATTEN_DB_12`.
- **GPIO num enum is no longer implicitly convertible from int** —
  `pins.h` casts `LCD_CS/DC/RST` to `gpio_num_t`. Plain `int` macros for
  pins still work where the consumer accepts ints (LCD_MOSI/CLK/BLK).
- **Linker rejects non-static `TAG` in main** because NimBLE's
  `ble_sm_alg.c` exports a non-static `TAG` of its own. All file-scope
  `const char *TAG` in our code must be `static`.
- **`gpio_sleep_sel_dis` is required on LCD SPI pins.** v6.0 auto-selects
  `CONFIG_PM_SLP_DISABLE_GPIO=y` via `CONFIG_ESP_SLEEP_GPIO_RESET_WORKAROUND`,
  which isolates GPIOs during light sleep. Without exempting MOSI/CLK/CS/DC
  the LCD's SPI bus glitches and the panel stops accepting pixel writes.

## Hardware quirks (host-side)

- **CH343P USB-UART** ⇒ board needs the WCH driver on macOS. macOS's built-in
  CDC handles steady-state data but garbles esptool's high-baud stub upload
  because the CH343 uses vendor-specific baud control. Keep the WCH driver
  enabled. Device shows up as `/dev/tty.wchusbserial*`.
- **Battery + USB-C-to-C cable + macOS** = USB-C role-detect failure.
  Workaround is a USB-C → USB-A → USB-C adapter chain. Phones and Windows do
  not have this issue. Not fixable in firmware.
- **"Port is busy/unavailable" within ~0.2s of plug-in** has so far always
  been a stale `idf.py monitor` from a previous shell session holding the
  port. Diagnose with `lsof | grep wchusb`, kill the offender.

## Hardware quirks (board)

- **BAT_ADC reads through a Schottky-style drop.** The divider (R13=200K /
  R14=100K, ratio 3.0) sees the system rail, which is whichever of cell or
  USB-Vbus-after-diode is higher. Cell at "full" reads ~3.9 V; USB plugged
  in reads 3.7–4.5 V (Vbus minus diode); brown-out floor is ~2.6 V. The
  cell is never directly observable while plugged in.
- **CST816S touch INT (GPIO5) latches LOW after a touch** until something
  reads the touch-data register. While LVGL is running the polling read
  clears it; after `vTaskSuspend(taskLVGL)` it stays asserted forever. This
  matters for using GPIO5 as a sleep wake source — `display.reset_touch()`
  must be called immediately before arming the wake source, otherwise the
  level-low wake condition is already met and sleep entry is rejected.
- **CST816S DisAutoSleep=1** is set at init. Without it, polling reads NACK
  after a few hundred ms of idle and the LVGL touch indev panics. Putting
  the chip into auto-sleep mode (`0xFE = 0`) was tried; on this CST816S
  variant it didn't make GPIO5 a clean wake source — keep DisAutoSleep=1.
- **QMI8658 Wake-on-Motion does not work reliably** on this firmware
  revision. `configWakeOnMotion` puts the IMU into a state where after-the-
  fact reconfiguration via `CTRL7` writes is silently ignored — gyro data
  registers stay at zero. `qmi.reset()` clears it. We don't use WoM;
  tilt-to-wake is the polling-based accel wrist-raise detector in imu.cpp
  (pm_update polls it at 10 Hz while asleep; gyro is fully off in sleep).
- **GC9A01 panel driver does not implement `disp_sleep`.** `disp_on_off(false)`
  + LEDC backlight = 0 is what blanks the panel. Sending raw SLPIN/SLPOUT
  via `panel_io_tx_param` is also unhelpful — the panel ends up showing
  garbage on subsequent wake. Stick with `disp_on_off`.
- **The CH343P over the WCH driver is reliable at 460800 baud** for flashing.
  `idf.flashBaudRate` in `.vscode/settings.json` is unset → defaults to
  460800. Don't drop it for "reliability" — that doesn't help and slows
  iteration.
- **Auto-reset over CH343P sometimes gets stuck after a long-held connection.**
  Power-cycle the bridge by unplugging USB and replugging. Failing that,
  hold BOOT and power-cycle to enter the ROM bootloader manually.

## Code map

```
main/
  main.c          Entry; calls watch_init().
  watch.cpp/hpp   Watch class — orchestrates all subsystems, owns the
                  pm task (light-sleep gating) and sleep/wake state.
                  Comment "DO NOT TOUCH, IS A CAREFULLY BALANCED PILE OF
                  LOGIC" on sleep()/wakeup() is real and now even more so —
                  the wake path includes IMU re-config, LVGL task resume,
                  CST816S re-arm, GC9A01 re-init + invert_color, LVGL
                  invalidate + lv_refr_now, and backlight restore, all in
                  a specific order. See "Light sleep wake order" below.
  pins.h          All GPIO assignments. ENV_WAVESHARE define gates them.
  display/
    display.cpp/hpp  Display class. SPI + GC9A01 panel via esp_lcd_gc9a01,
                     touch via esp_lcd_touch_cst816s, LVGL pipeline via
                     esp_lvgl_port (ping-pong DMA buffers, LVGL renders
                     RGB565_SWAPPED natively so no flush-time byte-swap
                     pass runs, LVGL pinned to APP_CPU).
                     Custom indev (lvgl_touch_read) suppresses the wake-
                     press; `gpio_sleep_sel_dis` on the four SPI pins.
                     `set_touch_active(bool)` toggles CST816S DisAutoSleep
                     (currently always true; left in for future use).
  hardware/
    battery.*    ADC + curve-fitting cal + voltage→percent with learned
                 anchors and awake/asleep load-offset compensation.
                 Anchors persist in NVS (`bat_full`, `bat_empty`).
    imu.*        QMI8658 6-axis (step counting + accel-only wrist-raise
                 wake detector polled by pm_update during sleep). WoM
                 removed.
    motor.*      Two-pin haptic motor (S3 GPIOs cap at 40 mA, motor wants
                 100 mA, so it's wired across two pins driven in parallel).
  system/
    ble.cpp/hpp  NimBLE peripheral; advertises as "Bangle.js xxxx" so
                 Gadgetbridge auto-detects. Nordic UART Service with
                 incoming JSON command dispatcher (setTime, GB(notify),
                 musicstate, find, is_gps_active) and outgoing helpers
                 (status, music control, notification action, find phone).
                 Notification queue lives here.
    schedule.*   Static daily timetable (compile-time data in
                 schedule.hpp).
    settings.*   NVS-backed user settings (sleep timeout, brightness,
                 battery anchors, etc). Has read/writeUint8 + Uint16.
  ui/
    ui.cpp/hpp        Builds the screen tree at boot. Vertical layer
                      (quicksettings ↑ / main_screen / apps ↓), horizontal
                      layer of "screens" (watchface, stopwatch, timer,
                      IMU debug, calculator, apps grid). Scroll-snap +
                      infinite-scroll on the horizontal layer.
    faces/            Watch faces (analog is the active one). All show
                      `watch.battery.percent` (not voltage).
    screens/          Individual app screens. notifications.cpp shows
                      Gadgetbridge notifications from `ble.notifications()`.

components/
  fonts/        Custom fonts (Product Sans + Font Awesome glyphs).
                Regenerate with `python3 fonts.py` after editing
                symbols.json (requires `lv_font_conv` from npm).
  cfscn__sensorlib/  Vendored sensor lib for QMI8658. v6.0 needs
                     `esp_driver_i2c esp_driver_spi` in REQUIRES.
  watchui/      Older monolithic UI component, currently disabled in
                main/CMakeLists.txt (`# watchui`).
```

LVGL 9 + esp_lvgl_port 2.7. The LVGL task is owned by esp_lvgl_port — you do
not call `lv_timer_handler()` directly. `Display::refresh()` is just
`lv_obj_invalidate(active_screen)`.

**LVGL's object heap lives in PSRAM.** `CONFIG_LV_USE_CUSTOM_MALLOC=y` +
`main/system/lv_mem_psram.c` route every `lv_malloc()` through
`heap_caps_malloc(MALLOC_CAP_SPIRAM)`. This is load-bearing, not an
optimization: with the default CLIB malloc LVGL widgets come from internal
SRAM (small allocs, and `SPIRAM_MALLOC_ALWAYSINTERNAL=1024` pins sub-1 KB
allocs there), and the screens built at boot dropped free internal SRAM to
~9 KB — a fast notification burst then ran the render path out of memory and
it wedged holding `lvgl_port_lock` (silent hang: awake, BLE still up, screen
frozen). On PSRAM the boot baseline is ~66 KB free internal. The ping-pong
DMA **draw buffers stay internal** — esp_lvgl_port allocates those separately
(`buff_dma=true`), not via `lv_malloc`, so they're unaffected. Note
`main/CMakeLists.txt` has a `-u lv_malloc_core` link flag: libmain is scanned
before liblvgl, so the linker won't pull our allocator's translation unit on
its own — don't remove it or the build fails with `undefined reference to
lv_malloc_core`. (Diagnosed via a `{t:"reboot"}` handler that logs task states
+ free internal heap before restarting — kept in `ble.cpp`.)

## Conventions and gotchas

- **Cross-task LVGL mutation must hold `lvgl_port_lock`.** Anything that
  touches `lv_*` from a non-LVGL task (the pm_update task on core 0, the
  backlight task, IMU callbacks, anywhere with `xTaskCreatePinnedToCore`)
  needs `lvgl_port_lock(0) … lvgl_port_unlock()` around the LVGL call. The
  example pattern is in `Watch::wakeup()`. Event callbacks fired from inside
  LVGL itself (LV_EVENT_CLICKED, LV_EVENT_SCROLL, etc.) are already on the
  LVGL task and do not need the lock.
- **The notification queue (`ble.notifications()`) is shared across tasks and
  the writers must hold `lvgl_port_lock`.** The LVGL task iterates it in
  `rebuild_notification_list` holding a `const Notification &` to each element
  while it builds that card's labels/image. The BLE-rx-task writers
  (`push_notification`, `dismiss_notification`, `clear_notifications`,
  `attach_notification_image`) therefore take `lvgl_port_lock` around every
  mutation — otherwise a fast `push_back`/`erase` reallocs the vector under the
  reader and it dereferences freed memory (`LoadProhibited` in
  `lv_label_set_text` under a notification burst). The reader is covered by the
  port task's implicit lock; the writers have to opt in. Same rule for any
  future BLE-owned state the UI reads (see also the two-stage album-art handoff
  in `ble.cpp`, which dodges this with its own mutex instead).
- **`lvgl_port_lock(0)` is *not* a try-lock.** The IDF API maps timeout=0
  to `portMAX_DELAY` (infinite wait). If the LVGL task was suspended while
  holding the mutex, the caller deadlocks. `Watch::wakeup()` resumes the
  LVGL task before any `lvgl_port_lock` call for this reason.
- **`Watch::sleep` / `wakeup` is order-sensitive.** Order in `sleep`:
  1. `display.sleep()` → `lvgl_port_stop()` then `vTaskSuspend("taskLVGL")`.
     Stopping the LVGL tick alone isn't enough — its task body has an
     unconditional `vTaskDelay(1)` that prevents tickless idle from ever
     entering light sleep. The full suspend is what actually drops average
     current from 24 → 12 mA.
  2. `imu_sleep()` lowers ODRs and suspends `imu_task`.
  3. `display.reset_touch()` to drain the latched CST816S INT.
  4. Arm GPIO5 wake source (level-low) **and** the negedge ISR for it.
  5. Release `pm_freq_lock` and `pm_sleep_lock`.
  Order in `wakeup`:
  1. Disarm GPIO5 (intr_disable + wakeup_disable). Clear `touch_interrupt`.
  2. Acquire pm locks.
  3. `imu_wake()` re-configures accel/gyro at full ODR.
  4. `vTaskResume("taskLVGL")`. **Before** any `lvgl_port_lock`.
  5. lvgl scroll-to-view (under lock) for whichever screen should be active.
  6. `display.wake()` — runs SLPOUT-equivalent, but more importantly calls
     `esp_lcd_panel_init(panel)` followed by `esp_lcd_panel_invert_color(panel, true)`.
     The panel reset is required: light sleep cycles between calls leave
     the GC9A01 in a state where it acks commands but ignores RAMWR pixel
     writes. The invert_color is required because `panel_init` resets it.
  7. `display.set_rotation`.
  8. `lv_obj_invalidate(lv_screen_active())` + `lv_refr_now(NULL)` under lock —
     forces an immediate full repaint. Without this LVGL has no dirty
     regions and nothing draws even though the panel is now alive.
  9. `display.set_backlight(prevBrightness)`.
- **`gpio_intr_disable` inside ISRs that pair with a level-low sleep wake
  source.** `gpio_wakeup_enable(LOW_LEVEL)` reconfigures the regular IRQ
  intr_type to level-low for sleep purposes; after wake the IRQ would re-
  fire continuously until the source line goes high. The ISR must mask
  itself; pm_update re-enables only after the source has been read/cleared.
  The touch ISR in `watch.cpp` does this; any future GPIO wake source must
  follow the same pattern.
- **`set_wakeup_touch(true)` before sleeping** so the touch that wakes the
  watch doesn't also fire as a click in the UI underneath. The flag is
  cleared in `lvgl_touch_read` once the finger lifts.
- **Don't introduce blocking calls into LVGL event handlers.** They run on
  the LVGL task; blocking it stalls rendering and touch reads. Defer to
  `xTaskCreate` or `lv_async_call` instead.
- **Don't `lv_task_handler()` directly anywhere.** It races with the
  esp_lvgl_port task. The old `display.refresh()` did this and would
  intermittently panic.
- **Don't call invalidating LVGL setters every frame with an unchanged
  value.** `lv_arc_set_*_angle`, `lv_label_set_text*`, etc. dirty the widget's
  region unconditionally — a per-tick call re-flushes that area 60×/sec even
  when nothing changed. This is not just wasteful: a tiny/degenerate flush
  (e.g. a 1-px arc-knob or line-tip region) queued tight behind a larger async
  SPI DMA raced the SPI bus-lock release and **intermittently deadlocked the
  LCD flush** — `spi_device_acquire_bus` never returns, taskLVGL wedges holding
  `lvgl_port_lock`, and the whole watch freezes (looked like a memory/PSRAM/
  sleep bug for a long time; it wasn't). The analog face's timer-arc did
  exactly this at pixel (120,3); the fix is a change-guard (only call the setter
  when the value differs). The info widgets (battery/steps/glance) already guard
  their setters — any new per-tick UI must too.

## Power management

`pm_init()` enables `light_sleep_enable=true` with min freq 10 MHz, max 240.
Two PM locks are taken at boot (`pm_freq_lock`, `pm_sleep_lock`). They are
released in `Watch::sleep()` and reacquired in `Watch::wakeup()`. While held,
the chip stays at max freq and cannot light-sleep.

Light sleep current with display off and BLE connected: **~12 mA average**
(spikes to 70 mA on BLE radio events). Awake with display on: ~50–80 mA.

Critical sdkconfig settings for this to work:

- `CONFIG_BT_ENABLED=y`, `CONFIG_BT_NIMBLE_ENABLED=y`, peripheral+broadcaster
  only, `BT_NIMBLE_PINNED_TO_CORE_0` (LVGL owns core 1), MTU 247.
- `CONFIG_BT_CTRL_MODEM_SLEEP=y`, `CONFIG_BT_CTRL_MODEM_SLEEP_MODE_1=y`.
- **`CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL=y`** + **`CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP=y`**.
  The internal RC slow oscillator (`LPCLK_SEL_RTC_SLOW`) is well above the
  500 ppm BLE spec — using it caused connection drops within seconds even
  while the chip was awake. Main XTAL stays alive through light sleep at a
  small power cost.
- **`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y`** — push NimBLE mbufs into
  PSRAM. Without this, internal SRAM is too tight (BT controller takes ~80 KB)
  and even basic operations OOM.
- **`CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP=n`** — leaving CPU power-down
  on saves ~650 µA but the SPI master / esp_lcd state doesn't survive the
  retention. Wake leaves the LCD pipeline silently dropping every pixel
  write, screen stays black despite LVGL still running.
- `CONFIG_ESP_WIFI_ENABLED=n` — WiFi removed entirely.

After connect, NimBLE asks the central for relaxed connection params
(itvl 100–200 ms, latency 4, supervision 15 s) — see `ble_gap_event_handler`
in `ble.cpp`. Phones may negotiate down but typically accept these.

## BLE / Gadgetbridge

The watch advertises as `Bangle.js xxxx` (last 4 hex of MAC). Gadgetbridge
auto-detects anything starting with `Bangle.js`. Nordic UART Service:

```
service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
  RX:    6E400002-...  (write,  phone → watch)
  TX:    6E400003-...  (notify, watch → phone)
```

**Outgoing message format** (`BLE::send_gb`):
```
\r\n<json>\r\n\x1e
```
The `\x1e` (ASCII Record Separator) is the actual line terminator
Gadgetbridge keys off — plain `\n` produces "unterminated string" parse
errors at the message boundary. Leading `\r\n` flushes any half-line residue.

**Incoming format**:
- `\x10setTime(<ts>);…E.setTimeZone(<offset>);…` → `settimeofday()` + tzset.
- `\x10GB({"t":"notify",...})` → push to notification queue + buzz haptic.
- `\x10GB({"t":"notify-","id":N})` → dismiss from queue.
- `\x10GB({"t":"find","n":true|false})` → buzz haptic / silence.
- `\x10GB({"t":"musicstate"|"musicinfo",...})` → update `BLE::music()`.
- `\x10GB({"t":"is_gps_active"})` → reply `{t:"gps_power", status: false}`
  (note JS-object syntax with unquoted keys; Gadgetbridge's parser is the
  lenient `JSONTokener` variant that handles those).

The `\x10` echo-off prefix is harmless — `BLE::handle_line` strips leading
control chars.

## Battery

Voltage reading uses 16× ADC oversampling per call (single reads have ~10 mV
peak-peak noise). The divider gives the system rail (cell or USB-rail-after-
diode), not the raw cell — see "Hardware quirks (board)".

Two anchors are learned and persisted to NVS (`bat_full`, `bat_empty`):

- **`v_full_mv`** is set from the *post-unplug* settle sample. On a downward
  voltage edge (`diff < −150 mV`), `battery_task` waits 5 ticks (~8 s) for
  the rail to collapse off the charger, then captures the next reading.
  If it's higher than the current `v_full_mv` and inside `[3700, 4000] mV`,
  it becomes the new anchor. Anything outside that band is rejected as
  USB-rail noise or a bad reading.
- **`v_empty_mv`** ratchets down on any discharging reading inside
  `[2600, 3500] mV`. Charging readings are ignored.

Defaults are `v_full=3900`, `v_empty=2800` (this board's actual range).
NVS values from a previous firmware revision get reset at boot if they
fall outside the current sane band.

**Awake/asleep load-offset compensation.** The cell's voltage rebounds when
the watch sleeps and sags when it wakes. `battery_task` watches
`watch.sleeping` for transitions; on each one it captures pre-transition
voltage, waits ~5 s for the rail to settle, and EMA-tracks the delta
(τ ≈ 8 transitions, ±150 mV sanity clamp). The compensated voltage
(`raw + load_offset` while awake) is what feeds the percent calc and curve
learning, so the percent display doesn't bounce when the watch wakes.

**Charging detection** is hybrid:
1. *Edge* — single-sample `±150 mV` jump flips the state immediately.
2. *Plateau* — voltage `≥ 4000 mV` always = charging (cell can't physically
   reach this off-charger; this short-circuits the slope check, since slope
   noise around the plateau would otherwise flip the state).
3. *Slope* — least-squares fit over a 7-sample (~10 s) ring buffer.
   `> +3 mV/s` = charging, `< −1 mV/s` = discharging, in-between keeps state.

`BLE::send_status()` is called automatically when percent or charging flag
changes, with a 60 s heartbeat. `Gadgetbridge` shows the resulting bat/chg/volt.

## Flash layout

Custom 16 MB partition table at `partitions.csv`:

| Partition | Type        | Offset    | Size    | Notes                                  |
|-----------|-------------|-----------|---------|----------------------------------------|
| nvs       | data/nvs    | 0x9000    | 64 KB   | settings, schedule, battery anchors    |
| phy_init  | data/phy    | 0x19000   | 4 KB    | RF calibration                         |
| factory   | app/factory | 0x20000   | 13 MB   | Single app, no OTA                     |
| storage   | data/spiffs | 0xD20000  | 2.875 MB| Wired in table; not yet mounted in code|

Last byte at `0x1000000` exactly — fills the 16 MB chip. Any change to
partition offsets wipes existing NVS contents (settings, learned battery
anchors, etc.).

To use the SPIFFS partition, register it as:

```cpp
esp_vfs_spiffs_conf_t conf = {
    .base_path = "/storage",
    .partition_label = "storage",
    .max_files = 8,
    .format_if_mount_failed = true,
};
esp_vfs_spiffs_register(&conf);
```

Or swap subtype to a reserved range and use `joltwallet/littlefs`.

## Things that should not be added

- A README is `README.md`, not `CLAUDE.md`. Don't write user-facing prose
  here; this file is the agent's quick-orientation only.
- No new test framework; this project has none and isn't getting one.
- **No WiFi.** It was removed because internal SRAM couldn't fit WiFi + BT
  controller + LVGL/PSRAM together (esf_buf_setup_static would OOM at
  esp_wifi_init). Time sync now comes from Gadgetbridge over BLE.
- **No QMI8658 Wake-on-Motion.** Tried multiple approaches; the chip's
  data registers stay zero after `configWakeOnMotion` even after `reset()`,
  and the GPIO INT signaling was unreliable across light sleep transitions.
  Tilt-to-wake is polling-based instead (accel wrist-raise state machine
  in imu.cpp) — keep it that way.
- **No raw SLPIN/SLPOUT to the GC9A01.** `disp_on_off(false/true)` paired
  with the `panel_init` re-init in wake is what actually works.
- No background "wake LVGL on demand" hacks — the existing
  `vTaskSuspend`/`vTaskResume("taskLVGL")` pattern in `Display::sleep/wake`
  is what makes light sleep effective. Reach for the same pattern if you
  add another reason to pause LVGL.
