// Web build's sim controls. The native build draws LVGL buttons in a
// second SDL window (sim_controls.cpp); under Emscripten there's a single
// canvas, so the equivalent buttons live in plain HTML around the canvas
// and call into the WASM module via the EMSCRIPTEN_KEEPALIVE'd functions
// below.
//
// Each entry point mutates the same shared state the native sidebar does
// (ble.notifications_mut(), watch.battery.*) so the watch UI reacts
// identically across builds.

#include <emscripten.h>

#include "watch.hpp"
#include "ble.hpp"
#include "esp_timer.h"

#include <string>
#include <utility>

namespace {

uint32_t g_next_id = 1000;

const char *const SAMPLE_SRC[]   = { "Slack",    "Messages", "Calendar",  "GitHub"      };
const char *const SAMPLE_TITLE[] = { "@channel", "Mom",      "Standup",   "PR #4218"    };
const char *const SAMPLE_BODY[]  = {
    "lunch at 12?",
    "call me when you can",
    "starts in 5 min",
    "alyssa requested your review",
};

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
void sim_notify(void)
{
    static int i = 0;
    int slot = i++ % 4;

    Notification n;
    n.id      = g_next_id++;
    n.src     = SAMPLE_SRC[slot];
    n.title   = SAMPLE_TITLE[slot];
    n.body    = SAMPLE_BODY[slot];
    n.when_ms = esp_timer_get_time() / 1000;
    n.reply   = false;

    ble.notifications_mut().push_back(std::move(n));
    ble.bump_version();
}

EMSCRIPTEN_KEEPALIVE
void sim_dismiss(void)
{
    auto &q = ble.notifications_mut();
    if (!q.empty()) q.pop_back();
    ble.bump_version();
}

EMSCRIPTEN_KEEPALIVE
void sim_clear(void)
{
    ble.clear_notifications();
}

EMSCRIPTEN_KEEPALIVE
void sim_toggle_charging(void)
{
    watch.battery.charging = !watch.battery.charging;
}

EMSCRIPTEN_KEEPALIVE
void sim_battery_step(void)
{
    uint8_t p = watch.battery.percent;
    if      (p > 75) p = 75;
    else if (p > 50) p = 50;
    else if (p > 25) p = 25;
    else if (p > 10) p = 10;
    else if (p > 5)  p = 5;
    else             p = 100;
    watch.battery.percent = p;
}

} // extern "C"
