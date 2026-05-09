# GWatch-Paris

ESP-IDF firmware for a custom round-LCD smartwatch built around a Waveshare
ESP32-S3-Touch-LCD-1.28 module (GC9A01 + CST816S + QMI8658 IMU + LiPo + CH343P
USB-UART), with a 3D-printed shell, vibration motor, and an LVGL 9 UI.

This is the user's third rewrite of the project. It's a personal project, not
a product — there is no test suite, and "stability" is judged by whether the
watch boots and the watch face renders for hours without resetting.

## Build & flash

ESP-IDF v5.5 at `/Users/garrett/esp/v5.5/esp-idf`. The user's IDF venv has
broken before (homebrew python@3.13 → 3.14 churn) — if `idf.py` says the venv
is missing, recreate it with:

```sh
/Library/Frameworks/Python.framework/Versions/3.13/bin/python3 -m venv --clear \
  /Users/garrett/.espressif/python_env/idf5.5_py3.13_env
PATH=/Library/Frameworks/Python.framework/Versions/3.13/bin:$PATH \
  bash /Users/garrett/esp/v5.5/esp-idf/install.sh esp32s3
```

Then in any new shell:

```sh
PATH=/Library/Frameworks/Python.framework/Versions/3.13/bin:$PATH
. /Users/garrett/esp/v5.5/esp-idf/export.sh
idf.py build         # or `ninja -C build` to skip idf.py's python-version check
idf.py -p /dev/tty.wchusbserial578E0235041 flash monitor
```

`-O3` (`COMPILER_OPTIMIZATION_PERF`) and 64-byte data-cache lines are on, so
clean builds are slow. Avoid `idf.py fullclean` unless something is genuinely
busted.

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

## Code map

```
main/
  main.c          Entry; calls watch_init().
  watch.cpp/hpp   Watch class — orchestrates all subsystems, owns the
                  pm task (light-sleep gating) and sleep/wake state.
                  Comment "DO NOT TOUCH, IS A CAREFULLY BALANCED PILE OF
                  LOGIC" on sleep()/wakeup() is real — those interact with
                  esp_pm_lock + lvgl_port + display + IMU wake.
  pins.h          All GPIO assignments. ENV_WAVESHARE define gates them.
  display/
    display.cpp/hpp  Display class. SPI + GC9A01 panel via esp_lcd_gc9a01,
                     touch via esp_lcd_touch_cst816s, LVGL pipeline via
                     esp_lvgl_port (ping-pong DMA buffers, swap_bytes on
                     the LCD peripheral, LVGL pinned to APP_CPU).
                     Custom indev (lvgl_touch_read) so we can suppress the
                     wake-press; touches reset Watch::sleep_time on every
                     press to keep the pm task from idling us out.
  hardware/
    battery.*    ADC-based battery voltage reading.
    imu.*        QMI8658 6-axis (wake-on-tilt and step counting).
    motor.*      Two-pin haptic motor (S3 GPIOs cap at 40 mA, motor wants
                 100 mA, so it's wired across two pins driven in parallel).
    drivers/qmi8658.* Vendor IMU driver, lifted from cfscn/sensorlib.
  system/
    wifi.*       NVS-backed creds + connect/scan; one-shot SNTP for the
                 RTC after associate.
    schedule.*   Static daily timetable (compile-time data in
                 schedule.hpp).
    settings.*   NVS-backed user settings (sleep timeout, brightness, etc).
  ui/
    ui.cpp/hpp        Builds the screen tree at boot: vertical layer
                      (quicksettings ↑ / main_screen / apps ↓), horizontal
                      layer of "screens" (watchface, stopwatch, timer,
                      IMU debug, calculator, apps grid). Scroll-snap +
                      infinite-scroll on the horizontal layer.
    faces/            Watch faces (analog is the active one).
    screens/          Individual app screens.

components/
  fonts/        Custom fonts (Product Sans + Font Awesome glyphs).
  cfscn__sensorlib/  Vendored sensor lib (mostly unused now, kept for
                     historical IMU paths).
  watchui/      Older monolithic UI component, currently disabled in
                main/CMakeLists.txt (`# watchui`).
```

LVGL 9 + esp_lvgl_port 2.7. The LVGL task is owned by esp_lvgl_port — you do
not call `lv_timer_handler()` directly anywhere. `Display::refresh()` is just
`lv_obj_invalidate(active_screen)`.

## Conventions and gotchas

- **Cross-task LVGL mutation must hold `lvgl_port_lock`.** Anything that
  touches `lv_*` from a non-LVGL task (the pm_update task on core 0, the
  backlight task, IMU callbacks, anywhere with `xTaskCreatePinnedToCore`)
  needs `lvgl_port_lock(0) … lvgl_port_unlock()` around the LVGL call. The
  example pattern is in `Watch::wakeup()`. Event callbacks fired from inside
  LVGL itself (LV_EVENT_CLICKED, LV_EVENT_SCROLL, etc.) are already on the
  LVGL task and do not need the lock.
- **Watch::sleep / wakeup is order-sensitive.** Both functions stop /
  resume `esp_lvgl_port`, release / acquire two pm locks, and toggle the
  panel via `esp_lcd_panel_disp_on_off`. Don't reorder.
- **`set_wakeup_touch(true)` before sleeping** so the touch that wakes the
  watch doesn't also fire as a click in the UI underneath. The flag is
  cleared in `lvgl_touch_read` once the finger lifts.
- **GC9A01 panel driver does not implement `disp_sleep`.** `disp_on_off(false)`
  + LEDC backlight = 0 is what actually blanks the panel. Calling
  `esp_lcd_panel_disp_sleep()` will print an error and do nothing.
- **The CH343P over the WCH driver is reliable at 460800 baud** for flashing.
  `idf.flashBaudRate` in `.vscode/settings.json` is unset → defaults to
  460800. Don't drop it for "reliability" — that doesn't help and slows
  iteration.
- **Don't introduce blocking calls into LVGL event handlers.** They run on
  the LVGL task; blocking it stalls rendering and touch reads. Defer to
  `xTaskCreate` or `lv_async_call` instead.
- **Don't `lv_task_handler()` directly anywhere.** It races with the
  esp_lvgl_port task. The old `display.refresh()` did this and would
  intermittently panic.
- **Auto-reset over CH343P sometimes gets stuck after a long-held connection.**
  Power-cycle the bridge by unplugging USB and replugging. Failing that,
  hold BOOT and power-cycle to enter the ROM bootloader manually.

## Flash layout

Custom 16 MB partition table at `partitions.csv`:

| Partition | Type        | Offset    | Size    | Notes                                  |
|-----------|-------------|-----------|---------|----------------------------------------|
| nvs       | data/nvs    | 0x9000    | 64 KB   | WiFi creds, settings, schedule         |
| phy_init  | data/phy    | 0x19000   | 4 KB    | RF calibration                         |
| factory   | app/factory | 0x20000   | 13 MB   | Single app, no OTA                     |
| storage   | data/spiffs | 0xD20000  | 2.875 MB| Wired in table; not yet mounted in code|

Last byte at `0x1000000` exactly — fills the 16 MB chip. Any change to
partition offsets wipes existing NVS contents (saved WiFi, settings, etc).

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

## Power management

`pm_init()` enables `light_sleep_enable=true` with min freq 10 MHz, max 240.
Two PM locks are taken at boot (`pm_freq_lock`, `pm_sleep_lock`); they're
released in `Watch::sleep()` and reacquired in `Watch::wakeup()`. While
those locks are held the chip will not light-sleep and its frequency stays
at max.

`CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y` is set so that **if** the chip ever
ends up using its native USB-Serial-JTAG (not currently — flashing goes
through the CH343P), it would still keep USB awake when a host is plugged
in. Leaving the flag on is harmless either way.

## Things that should not be added

- A README is `README.md`, not `CLAUDE.md`. Don't write user-facing prose
  here; this file is the agent's quick-orientation only.
- No new test framework; this project has none and isn't getting one.
- No background "wake LVGL on demand" hacks — the existing `lvgl_port_stop /
  resume` flow inside `Display::sleep / wake` is correct and matches IDF's
  documented pattern. Reach for the same pattern if you add another reason
  to pause LVGL.
