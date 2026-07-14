# Simulator

Builds the watch's LVGL UI as a desktop SDL window so screens, watch faces,
and scroll/touch behaviour can be iterated on without flashing the board.

The `main/ui/**` sources compile **unchanged** — every ESP-IDF header they
include is shimmed under `shim/include/`, and the watch subsystem classes
(`Watch`, `Display`, `BLE`, `Settings`, `IMU`, motor, battery) have host
stubs in `src/stubs.cpp`. Touch the real `main/ui/` files and the change
shows up next rebuild.

## What works

- Full screen tree: watchface, horizontal screens (stopwatch / timer /
  alarm / IMU / calculator / apps), vertical (quicksettings ↑, weather /
  notifications / music ↓), settings, schedule, dice, unistroke.
- Touch via SDL mouse; scrolling via mouse wheel.
- Fonts, watchface images (`main/assets/`), weather icons
  (`components/images/`) — all linked from the real generated `.c` files.
- **Round-LCD mask**: black anti-aliased ring on `lv_layer_top` clips the
  square SDL window into a 240-pixel circle that matches the GC9A01.
- **Settings persist** to `~/.gwatch_sim_nvs.bin` across launches — every
  `nvs_set_blob` rewrites the file. `rm ~/.gwatch_sim_nvs.bin` to reset.
- FreeRTOS tasks spawn detached pthreads, so the alarm/timer/export
  background workers run.

## What doesn't

- **No BLE.** `ble.connected()` is permanently false, no notifications
  appear, music metadata is empty. The notifications and music screens
  render with empty state.
- **No real IMU.** `accel_read()` returns `(0, 0, 1)` — flat on its back.
- **No haptic feedback.** Motor calls no-op.
- **Battery is fixed at 87%.**
- **Sleep / wake** is a no-op state flip; the display isn't actually
  cycled.

## Build

```sh
cd simulator
cmake -B build
cmake --build build -j
./build/gwatch_sim
```

Dependencies on macOS:

```sh
brew install sdl2 cmake
```

LVGL is pulled via CMake `FetchContent` at configure time, pinned to
`v9.5.0` to match the firmware's IDF component lock. First-run downloads
take ~30 s; subsequent builds reuse the cached checkout under
`build/_deps/`. No external paths required.

## Layout

```
simulator/
  CMakeLists.txt
  main.cpp              entry: init LVGL + SDL, kick the LVGL handler loop
  hal_sdl.{c,h}         SDL window + mouse / keyboard / mousewheel indevs
  mouse_cursor_icon.c   unused (kept for parity with the upstream template)
  lv_conf.h             LVGL config — copied from the upstream template
  src/
    stubs.cpp           every host-side Watch / BLE / Display / NVS impl
  shim/include/
    esp_*.h             ESP-IDF API shims (timer, log, heap_caps, system, …)
    freertos/*.h        FreeRTOS shims (task → pthread, semphr → mutex)
    nvs.h, nvs_flash.h  in-memory NVS
    driver/i2c_master.h opaque type for member declarations only
    esp_lcd_*.h         opaque types for Display member declarations
```

## Web build (share with a URL)

The same source builds to a static HTML/JS/WASM bundle via Emscripten.

```sh
# one-time: install the Emscripten SDK
git clone https://github.com/emscripten-core/emsdk ~/emsdk
~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest

# every shell:
source ~/emsdk/emsdk_env.sh

# build
simulator/web/build.sh
```

Outputs `simulator/build-web/gwatch_sim.{html,js,wasm}` — upload that
folder to any static host (GitHub Pages, Vercel, Netlify) or preview:

```sh
cd simulator/build-web && python3 -m http.server 8000
# open http://localhost:8000/gwatch_sim.html
```

The web HTML shell (`simulator/web/shell.html`) renders one 240×240
canvas plus HTML sidebar buttons (Notify / Dismiss / Clear / Charge /
Battery). Buttons call into `simulator/src/sim_controls_web.cpp` via
`Module.ccall`.

**Web-specific limitations:**

- **No FreeRTOS tasks.** The web build is single-threaded — `xTaskCreate`
  is a no-op. The two consumers (`alarm_task`, `timer_task`) build their
  UI but their countdowns don't tick.
- **No NVS persistence.** `~/.gwatch_sim_nvs.bin` lands on Emscripten's
  MEMFS, which is wiped on reload. Easy to switch to IDBFS later.
- **No window position memory** (one canvas, no windows).

## Notes

- `LV_USE_ASSERT_OBJ` is left on in `lv_conf.h`. The analog watchface's
  periodic update fires before all its objects exist, which produces a
  flood of `obj != NULL` warnings on launch. They're cosmetic — same
  behaviour as on-device — and a useful tripwire for real UI bugs.
- `M_TWOPI` is supplied via a compile define — ESP-IDF's `<math.h>`
  ships it, macOS clang's doesn't.
