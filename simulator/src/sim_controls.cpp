// Simulator-only side panel: a column of buttons sitting to the right of the
// watch's 240×240 viewport that inject test events into the watch — fake
// notifications, BLE connect toggle, charging state, etc.
//
// Lives in simulator/src/ so it never reaches the firmware build (which
// globs main/** and doesn't see simulator/). Mutates the shared BLE/Watch
// objects from stubs.cpp via their public interfaces.

#include "lvgl.h"

#include "watch.hpp"
#include "ble.hpp"
#include "esp_timer.h"

#include <cstdio>
#include <string>

namespace {

uint32_t g_next_id = 1000;

const char *const SAMPLE_SRC[]   = { "Slack",       "Messages",  "Calendar",       "GitHub"            };
const char *const SAMPLE_TITLE[] = { "@channel",    "Mom",       "Standup",        "PR #4218"          };
const char *const SAMPLE_BODY[]  = {
    "lunch at 12?",
    "call me when you can",
    "starts in 5 min",
    "alyssa requested your review",
};

void on_notify(lv_event_t *)
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

    /* Event callback runs on the LVGL task — no extra lock needed. */
    ble.notifications_mut().push_back(std::move(n));
    ble.bump_version();
}

void on_dismiss(lv_event_t *)
{
    auto &q = ble.notifications_mut();
    if (!q.empty()) q.pop_back();
    ble.bump_version();
}

void on_clear(lv_event_t *)
{
    ble.clear_notifications();
}

void on_toggle_charging(lv_event_t *)
{
    watch.battery.charging = !watch.battery.charging;
}

void on_battery_step(lv_event_t *)
{
    /* Cycle 100 → 75 → 50 → 25 → 10 → 5 → 100. */
    uint8_t p = watch.battery.percent;
    if      (p > 75) p = 75;
    else if (p > 50) p = 50;
    else if (p > 25) p = 25;
    else if (p > 10) p = 10;
    else if (p > 5)  p = 5;
    else             p = 100;
    watch.battery.percent = p;
}

lv_obj_t *make_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, 34);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

} // anonymous namespace

extern "C" void sim_controls_install(lv_display_t *disp)
{
    /* Build on the sidebar display's active screen — completely separate
     * from the watch display, so the watch's popups and screen swaps
     * never touch this window. */
    lv_obj_t *panel = lv_display_get_screen_active(disp);

    lv_obj_set_style_bg_color(panel, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START,
                                 LV_FLEX_ALIGN_CENTER,
                                 LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_label_create(panel);
    lv_label_set_text(header, "Simulator");
    lv_obj_set_style_text_color(header, lv_color_hex(0x888888), 0);

    make_btn(panel, "Notify",   on_notify);
    make_btn(panel, "Dismiss",  on_dismiss);
    make_btn(panel, "Clear",    on_clear);
    make_btn(panel, "Charge",   on_toggle_charging);
    make_btn(panel, "Battery",  on_battery_step);
}
