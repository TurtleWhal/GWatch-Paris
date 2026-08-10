// Host-side stub implementations of every ESP-IDF / FreeRTOS API and every
// Watch subsystem (Display, BLE, Settings, IMU, motor, battery) that the
// real main/ui/** code calls into. Method signatures match the headers in
// main/ and main/system/ exactly so the UI translation units compile and
// link unchanged.
//
// Nothing here talks to real hardware — backlight is a single uint, NVS is
// an in-memory map, FreeRTOS tasks become detached pthreads, BLE has no
// peer. The simulator's job is to render the UI; anything past that is
// out of scope.

#include "watch.hpp"
#include "ble.hpp"
#include "esp_system.h"
#include "freertos/semphr.h"

#include <SDL.h>
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// ---- Globals matching the real firmware's extern symbols -----------------
Watch watch;
BLE   ble;

// ===========================================================================
// ESP-IDF API stubs
// ===========================================================================

extern "C" void esp_restart(void)
{
    fprintf(stderr, "[sim] esp_restart() called — exiting\n");
    exit(0);
}

extern "C" int64_t esp_timer_get_time(void)
{
    // SDL ticks are milliseconds since SDL_Init; on-device esp_timer is
    // microseconds since boot. Same monotonic semantic, just scaled.
    return (int64_t)SDL_GetTicks() * 1000;
}

// ---- esp_lvgl_port: recursive mutex around all LVGL access ---------------
static pthread_mutex_t g_lvgl_mtx;
static bool            g_lvgl_mtx_init = false;

static void ensure_lvgl_mtx()
{
    if (g_lvgl_mtx_init) return;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_lvgl_mtx, &a);
    pthread_mutexattr_destroy(&a);
    g_lvgl_mtx_init = true;
}

extern "C" bool lvgl_port_lock(uint32_t /*timeout_ms*/)
{
    ensure_lvgl_mtx();
    pthread_mutex_lock(&g_lvgl_mtx);
    return true;
}

extern "C" void lvgl_port_unlock(void)
{
    pthread_mutex_unlock(&g_lvgl_mtx);
}

// ---- FreeRTOS tasks → detached pthreads (native) / asyncify (web) -------
// Native: spawn a real pthread, detach. The task body just runs.
// Web: spawn the task body via emscripten_async_call so it runs on the
//      browser's main thread. The body's vTaskDelay calls emscripten_sleep,
//      which (with -sASYNCIFY on the link line) yields back to the event
//      loop so the canvas stays responsive. The watch firmware's two task
//      users (alarm_task, timer_task) are simple `while(true) { …;
//      vTaskDelay(N); }` loops that work unchanged under this model.
namespace {
struct PthreadTask { TaskFunction_t fn; void *arg; };
}

#ifndef __EMSCRIPTEN__
static void *task_trampoline(void *p)
{
    auto *t = static_cast<PthreadTask *>(p);
    t->fn(t->arg);
    delete t;
    return nullptr;
}
#endif

extern "C" BaseType_t xTaskCreate(TaskFunction_t fn, const char *name,
                                  uint32_t /*stack*/, void *arg,
                                  UBaseType_t /*prio*/, TaskHandle_t *out)
{
#ifdef __EMSCRIPTEN__
    /* Native pthreads aren't available in the single-threaded WASM build.
     * We special-case the two firmware tasks we actually need (timer +
     * alarm) by name and run them as lv_timer callbacks instead — see
     * sim_tasks_web.cpp. Anything else gets dropped. */
    extern void sim_web_schedule_task(const char *, TaskFunction_t, void *);
    sim_web_schedule_task(name, fn, arg);
    if (out) *out = nullptr;
    return pdPASS;
#else
    (void)name;
    auto *t = new PthreadTask{fn, arg};
    pthread_t pt;
    pthread_create(&pt, nullptr, task_trampoline, t);
    pthread_detach(pt);
    if (out) *out = reinterpret_cast<TaskHandle_t>(pt);
    return pdPASS;
#endif
}

extern "C" BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name,
                                              uint32_t stack, void *arg,
                                              UBaseType_t prio, TaskHandle_t *out,
                                              BaseType_t /*core*/)
{
    return xTaskCreate(fn, name, stack, arg, prio, out);
}

extern "C" BaseType_t xTaskCreateWithCaps(TaskFunction_t fn, const char *name,
                                          uint32_t stack, void *arg,
                                          UBaseType_t prio, TaskHandle_t *out,
                                          uint32_t /*caps*/)
{
    return xTaskCreate(fn, name, stack, arg, prio, out);
}

extern "C" void vTaskDelete(TaskHandle_t /*h*/)
{
#ifndef __EMSCRIPTEN__
    pthread_exit(nullptr);
#endif
}
extern "C" void vTaskDeleteWithCaps(TaskHandle_t h) { vTaskDelete(h); }

extern "C" void vTaskDelay(TickType_t ticks)
{
#ifdef __EMSCRIPTEN__
    /* No-op in web. The two FreeRTOS task bodies (timer_task, alarm_task)
     * are re-implemented as lv_timer callbacks in sim_tasks_web.cpp, so
     * we never actually enter their `while(true) { …; vTaskDelay(); }`
     * loop. Any other vTaskDelay caller would block the JS event loop;
     * the firmware doesn't have any such call sites outside tasks. */
    (void)ticks;
#else
    usleep((useconds_t)ticks * 1000);
#endif
}
extern "C" void vTaskSuspend(TaskHandle_t) {}
extern "C" void vTaskResume(TaskHandle_t)  {}
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(void) { return nullptr; }
extern "C" TaskHandle_t xTaskGetHandle(const char *)    { return nullptr; }

// ---- FreeRTOS semaphores → pthread mutexes -------------------------------
extern "C" SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    auto *m = new pthread_mutex_t;
    pthread_mutex_init(m, nullptr);
    return (SemaphoreHandle_t)m;
}

extern "C" SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return xSemaphoreCreateMutex();
}

extern "C" BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t /*wait*/)
{
    pthread_mutex_lock((pthread_mutex_t *)h);
    return pdPASS;
}

extern "C" BaseType_t xSemaphoreGive(SemaphoreHandle_t h)
{
    pthread_mutex_unlock((pthread_mutex_t *)h);
    return pdPASS;
}

extern "C" void vSemaphoreDelete(SemaphoreHandle_t h)
{
    pthread_mutex_destroy((pthread_mutex_t *)h);
    delete (pthread_mutex_t *)h;
}

// ---- NVS — in-memory key/value store, optionally persisted to disk -------
//
// Backing file format (little-endian throughout):
//   "GWNV" magic                             4 bytes
//   uint32 num_namespaces
//   per namespace:
//     uint8  ns_name_len                     (always < 16 in practice)
//     bytes  ns_name
//     uint32 num_entries
//     per entry:
//       uint8  key_len                       (NVS keys cap at 15 chars)
//       bytes  key
//       uint32 value_len
//       bytes  value
//
// Path: $HOME/.gwatch_sim_nvs.bin so it survives `rm -rf build/`. Delete
// the file to reset settings.
namespace {
struct NvsBlob { std::vector<uint8_t> data; };
using NvsNs = std::unordered_map<std::string, NvsBlob>;
std::unordered_map<std::string, NvsNs>          g_nvs;
std::unordered_map<nvs_handle_t, std::string>   g_nvs_open;
nvs_handle_t                                     g_nvs_next = 1;

NvsNs *ns_for(nvs_handle_t h)
{
    auto it = g_nvs_open.find(h);
    if (it == g_nvs_open.end()) return nullptr;
    return &g_nvs[it->second];
}

std::string nvs_path()
{
    const char *home = getenv("HOME");
    std::string p = (home && *home) ? home : ".";
    p += "/.gwatch_sim_nvs.bin";
    return p;
}

template <typename T>
bool read_le(FILE *f, T *out)
{
    return fread(out, sizeof(T), 1, f) == 1;
}

template <typename T>
void write_le(FILE *f, T v)
{
    fwrite(&v, sizeof(T), 1, f);
}

void nvs_save_to_disk()
{
    FILE *f = fopen(nvs_path().c_str(), "wb");
    if (!f) return;
    fwrite("GWNV", 1, 4, f);
    write_le<uint32_t>(f, (uint32_t)g_nvs.size());
    for (const auto &[ns_name, ns] : g_nvs) {
        write_le<uint8_t>(f, (uint8_t)ns_name.size());
        fwrite(ns_name.data(), 1, ns_name.size(), f);
        write_le<uint32_t>(f, (uint32_t)ns.size());
        for (const auto &[key, blob] : ns) {
            write_le<uint8_t>(f, (uint8_t)key.size());
            fwrite(key.data(), 1, key.size(), f);
            write_le<uint32_t>(f, (uint32_t)blob.data.size());
            fwrite(blob.data.data(), 1, blob.data.size(), f);
        }
    }
    fclose(f);
}
}

extern "C" void sim_nvs_load_from_disk()
{
    FILE *f = fopen(nvs_path().c_str(), "rb");
    if (!f) return;
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "GWNV", 4) != 0) {
        fclose(f);
        return;
    }
    uint32_t nns;
    if (!read_le(f, &nns)) { fclose(f); return; }
    for (uint32_t i = 0; i < nns; ++i) {
        uint8_t ns_len; if (!read_le(f, &ns_len)) break;
        std::string ns_name(ns_len, '\0');
        if (fread(ns_name.data(), 1, ns_len, f) != ns_len) break;
        uint32_t ne; if (!read_le(f, &ne)) break;
        NvsNs &ns = g_nvs[ns_name];
        for (uint32_t k = 0; k < ne; ++k) {
            uint8_t key_len; if (!read_le(f, &key_len)) goto done;
            std::string key(key_len, '\0');
            if (fread(key.data(), 1, key_len, f) != key_len) goto done;
            uint32_t val_len; if (!read_le(f, &val_len)) goto done;
            NvsBlob b; b.data.resize(val_len);
            if (val_len && fread(b.data.data(), 1, val_len, f) != val_len) goto done;
            ns[key] = std::move(b);
        }
    }
done:
    fclose(f);
}

extern "C" esp_err_t nvs_open(const char *name, nvs_open_mode_t /*mode*/, nvs_handle_t *out)
{
    nvs_handle_t h = g_nvs_next++;
    g_nvs_open[h] = name;
    if (out) *out = h;
    return ESP_OK;
}

extern "C" void nvs_close(nvs_handle_t h) { g_nvs_open.erase(h); }

extern "C" esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
    NvsNs *ns = ns_for(h);
    if (!ns) return ESP_ERR_INVALID_STATE;
    auto it = ns->find(key);
    if (it == ns->end()) return ESP_ERR_NOT_FOUND;
    if (!out) { if (len) *len = it->second.data.size(); return ESP_OK; }
    if (!len) return ESP_ERR_INVALID_ARG;
    if (*len < it->second.data.size()) return ESP_ERR_INVALID_ARG;
    memcpy(out, it->second.data.data(), it->second.data.size());
    *len = it->second.data.size();
    return ESP_OK;
}

extern "C" esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *val, size_t len)
{
    NvsNs *ns = ns_for(h);
    if (!ns) return ESP_ERR_INVALID_STATE;
    NvsBlob b;
    b.data.assign((const uint8_t *)val, (const uint8_t *)val + len);
    (*ns)[key] = std::move(b);
    nvs_save_to_disk();
    return ESP_OK;
}

extern "C" esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out)
{
    size_t len = 1; return nvs_get_blob(h, key, out, &len);
}

extern "C" esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t v)
{
    return nvs_set_blob(h, key, &v, 1);
}

extern "C" esp_err_t nvs_get_u16(nvs_handle_t h, const char *key, uint16_t *out)
{
    size_t len = 2; return nvs_get_blob(h, key, out, &len);
}

extern "C" esp_err_t nvs_set_u16(nvs_handle_t h, const char *key, uint16_t v)
{
    return nvs_set_blob(h, key, &v, 2);
}

extern "C" esp_err_t nvs_commit(nvs_handle_t)               { return ESP_OK; }
extern "C" esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    NvsNs *ns = ns_for(h);
    if (!ns) return ESP_ERR_INVALID_STATE;
    ns->erase(key);
    nvs_save_to_disk();
    return ESP_OK;
}
extern "C" esp_err_t nvs_erase_all(nvs_handle_t h)
{
    NvsNs *ns = ns_for(h);
    if (!ns) return ESP_ERR_INVALID_STATE;
    ns->clear();
    nvs_save_to_disk();
    return ESP_OK;
}

// ===========================================================================
// Watch subsystems
// ===========================================================================

// Settings implementation now comes from the real main/system/settings.cpp
// (added to sim sources). The legacy uint8/uint16 methods route through
// the nvs_* fakes above; the new JSON methods read/write fs/config.json
// directly via fopen — same file the ESP flashes into SPIFFS.

// ---- Display -------------------------------------------------------------
// On the host, set_backlight/set_rotation are the only state we actually
// care about. ui_init() lives in main/ui/ui.cpp and builds the screen
// tree from there.
static uint16_t g_brightness = 100;

void Display::init(i2c_master_bus_handle_t /*bus*/)  { ui_init(); }
void Display::init_memory()                          {}
void Display::sleep()                                {}
void Display::wake()                                 {}
void Display::refresh()                              { lv_obj_invalidate(lv_screen_active()); }
bool Display::is_touching()                          { return false; }
void Display::reset_touch()                          {}
void Display::lvgl_done()                            {}

void Display::set_rotation(lv_display_rotation_t rot)
{
    lv_display_set_rotation(lv_display_get_default(), rot);
}

void Display::set_backlight(int16_t v, bool /*stopgrad*/)   { g_brightness = (uint16_t)v; }
void Display::set_backlight_gradual(int16_t v, uint32_t)    { g_brightness = (uint16_t)v; }
uint16_t Display::get_brightness()                          { return g_brightness; }
void Display::set_wakeup_touch(bool)                        {}
void Display::set_touch_active(bool)                        {}

void Display::backlight_update() {}
void Display::init_graphics()    {}

// ---- IMU ------------------------------------------------------------------
void imu_init(i2c_master_bus_handle_t) {}
Acceleration accel_read() { return Acceleration{0.0f, 0.0f, 1.0f}; }
GyroData     gyro_read()  { return GyroData{0.0f, 0.0f, 0.0f}; }
void imu_sleep() {}
void imu_wake()  {}

// ---- Battery --------------------------------------------------------------
void battery_init() {}

// ---- Motor / haptic ------------------------------------------------------
esp_err_t haptic_init()                      { return ESP_OK; }
esp_err_t haptic_play(bool, ...)             { return ESP_OK; }
esp_err_t haptic_play_now(bool, ...)         { return ESP_OK; }
void      haptic_stop()                      {}

// ---- BLE ------------------------------------------------------------------
void BLE::init()                                           {}
void BLE::set_enabled(bool on)                             { enabled = on; }
bool BLE::send_gb(const char *)                            { return false; }
void BLE::send_status()                                    {}
void BLE::send_music_control(const char *)                 {}
void BLE::send_notification_action(uint32_t, const char *) {}
void BLE::send_notification_reply(uint32_t, const char *)  {}
void BLE::send_find_phone(bool)                            {}
void BLE::send_screenshot()                                {}
void BLE::clear_notifications()                            { notifs.clear(); notifs_version++; }
void BLE::start_advertising()                              {}
void BLE::handle_line(const char *, size_t)                {}
void BLE::handle_gb_json(const char *, size_t)             {}
void BLE::push_notification(Notification &&n)              { notifs.push_back(std::move(n)); notifs_version++; }
// Two-stage album art install — staged here, swapped into music_state
// on the next music_update tick. The real BLE module uses this so the
// LVGL task can pick up an image that arrived during light sleep without
// needing lvgl_port_lock from the rx task; on the host the same two-step
// hand-off matters because music_update wipes music_state.album_art on
// every track change and expects the new image via the pending slot.
namespace {
PsramByteVec g_pending_album_art;
uint16_t     g_pending_album_art_w = 0;
uint16_t     g_pending_album_art_h = 0;
}

void BLE::post_pending_album_art(PsramByteVec &&pixels, uint16_t w, uint16_t h)
{
    g_pending_album_art   = std::move(pixels);
    g_pending_album_art_w = w;
    g_pending_album_art_h = h;
}

bool BLE::promote_pending_album_art()
{
    if (g_pending_album_art.empty()) return false;
    set_album_art(std::move(g_pending_album_art),
                  g_pending_album_art_w,
                  g_pending_album_art_h);
    g_pending_album_art_w = 0;
    g_pending_album_art_h = 0;
    return true;
}

void BLE::dismiss_notification(uint32_t id, bool)
{
    for (auto it = notifs.begin(); it != notifs.end(); ++it) {
        if (it->id == id) { notifs.erase(it); break; }
    }
    notifs_version++;
}

// ---- Watch ----------------------------------------------------------------
void Watch::init()
{
    battery.voltage  = 4000;
    battery.percent  = 87;
    battery.charging = false;
    imu.steps        = 0;
    sleeping         = false;
    goingtosleep     = false;
    chrono           = {};

    settings.init();
    // Load schedules from config.json into the Schedule class's
    // in-memory vectors. Must run after settings.init has surfaced the
    // config file path but before anything reads schedule state —
    // watchface_update() calls watch.schedule.getText() every tick
    // once the sim UI is up.
    schedule.init();
    display.init(nullptr);
}

void Watch::sleep()         { sleeping = true; }
void Watch::wakeup()        { sleeping = false; }
void Watch::request_sleep() {}

void Watch::pm_init()  {}
void Watch::iic_init() {}
void Watch::i2c_scan() {}
void Watch::pm_update() {}

extern "C" void watch_init() { watch.init(); }
