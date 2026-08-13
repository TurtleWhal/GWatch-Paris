#include "ui.hpp"

#include "cJSON.h"
#include <climits>
#include <string>
#include <sys/time.h>
#include <time.h>

const char *alarmhourticks[] = {"12", "1", "2", "3", "4", "5", "6",
                                "7", "8", "9", "10", "11", NULL};

const char *alarmminuteticks[] = {"00", "05", "10", "15", "20", "25", "30", "35", "40", "45", "50", "55", NULL};

static lv_point_precise_t line_points[] = {
    {120, 120},
    {120, 43}};

lv_obj_t *setalarmscr;

lv_obj_t *alarmarc;
lv_obj_t *alarmline;
lv_obj_t *alarmscale;

lv_obj_t *hourbox;
lv_obj_t *minutebox;

lv_obj_t *alarmhour;
lv_obj_t *alarmminute;

lv_obj_t *pmlbl;
lv_obj_t *amlbl;

bool sethour = true;
bool am = true;

// Stored alarms. In-memory only for now — wire to Settings/NVS if you
// want them to survive a reboot.
struct Alarm
{
    uint8_t hour;   // 1..12
    uint8_t minute; // 0..59
    bool am;
    bool enabled;
};

static constexpr int N_ALARMS = 4;
Alarm alarms[N_ALARMS] = {
    {12, 0, true, false},
    {12, 0, true, false},
    {12, 0, true, false},
    {12, 0, true, false},
};

// NVS persistence. Each alarm packs into one uint16:
//   bit 11    : enabled
//   bit 10    : am
//   bits 6..9 : hour   (1..12, stored as-is)
//   bits 0..5 : minute (0..59)
// Stored under keys "alarm0" .. "alarm3".
static uint16_t pack_alarm(const Alarm &a)
{
    return (uint16_t)((a.enabled ? (1u << 11) : 0) |
                      (a.am ? (1u << 10) : 0) |
                      ((uint16_t)(a.hour & 0x0F) << 6) |
                      (uint16_t)(a.minute & 0x3F));
}

static Alarm unpack_alarm(uint16_t v)
{
    Alarm a;
    a.enabled = (v >> 11) & 1;
    a.am = (v >> 10) & 1;
    a.hour = (v >> 6) & 0x0F;
    a.minute = v & 0x3F;
    // Sanity-clamp if NVS came back with garbage.
    if (a.hour < 1 || a.hour > 12)
        a.hour = 12;
    if (a.minute > 59)
        a.minute = 0;
    return a;
}

static void save_alarm(int idx)
{
    char key[8];
    snprintf(key, sizeof(key), "alarm%d", idx);
    watch.settings.writeUint16(key, pack_alarm(alarms[idx]));
}

// Find the enabled alarm that will fire next (soonest positive delta
// from now, wrapping to tomorrow if all remaining alarms today are
// past) and format it as "H:MM MDI_ALARM" for the info-stack row.
// Leaves `out` empty if no alarm is enabled.
void alarm_next_infostack_text(std::string &out)
{
    out.clear();

    time_t now = time(nullptr);
    struct tm nowtm;
    localtime_r(&now, &nowtm);
    int now_min = nowtm.tm_hour * 60 + nowtm.tm_min;

    int best_delta = INT_MAX;
    const Alarm *best = nullptr;
    for (int i = 0; i < N_ALARMS; i++) {
        if (!alarms[i].enabled) continue;
        // hour is stored 1..12 with am flag; convert to 24h minute-
        // of-day for chronological compare.
        int h24 = alarms[i].hour % 12;      // 12 → 0
        if (!alarms[i].am) h24 += 12;       // pm → 12..23
        int amin = h24 * 60 + alarms[i].minute;
        int delta = amin - now_min;
        if (delta <= 0) delta += 24 * 60;   // already past today → tomorrow
        if (delta < best_delta) {
            best_delta = delta;
            best = &alarms[i];
        }
    }

    if (!best) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d %s", best->hour, best->minute, MDI_ALARM);
    out.assign(buf);
}

static void load_alarms()
{
    for (int i = 0; i < N_ALARMS; i++)
    {
        char key[8];
        snprintf(key, sizeof(key), "alarm%d", i);
        // Default falls back to the in-memory default if NVS has no entry.
        uint16_t v = watch.settings.readUint16(key, pack_alarm(alarms[i]));
        alarms[i] = unpack_alarm(v);
    }
}

// Per-row widgets we need to update from the editor's "Set" handler.
static lv_obj_t *alarm_row_lbls[N_ALARMS] = {};
static lv_obj_t *alarm_row_switches[N_ALARMS] = {};

static int editing_alarm = -1;      // index currently open in setalarmscr
static lv_obj_t *prev_alarm_screen; // screen to return to from setalarmscr
// AM/PM labels toggle between accent and a neutral gray at click time;
// read the current accent from the global so it tracks live theme
// changes (vs. snapshotting at screen-create time as we used to).

// Firing screen + cross-task signal. The background alarm_task runs even
// while the LVGL task is suspended for light sleep, so when it detects a
// match it sets `pending_alarm_idx` and calls watch.wakeup(); the LVGL
// `alarm_check_pending` timer then picks the signal up after LVGL
// resumes, fades to the ring screen, and starts the haptic.
static lv_obj_t *alarm_ring_screen = nullptr;
static lv_obj_t *alarm_ring_time_lbl = nullptr;     // shows the fired alarm's HH:MM AM/PM
static lv_timer_t *alarm_auto_stop_timer = nullptr; // one-shot, 60s
volatile int pending_alarm_idx = -1;
static uint32_t last_fired_minute[N_ALARMS] = {};

// FreeRTOS task: poll local time once per second, fire any matching
// enabled alarm by signalling pending_alarm_idx and waking the watch.
// Runs in the background and stays alive through light sleep, so an
// alarm whose time arrives while the watch is asleep still triggers.
static void alarm_task(void *)
{
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Wait until the current pending fire has been handled by the
        // LVGL side before scanning for a new one — keeps the ring
        // screen from getting overwritten by another simultaneous fire.
        if (pending_alarm_idx >= 0)
            continue;

        struct timeval tv;
        gettimeofday(&tv, NULL);
        // Skip until Gadgetbridge has synced the clock; otherwise a 12:00 AM
        // alarm would fire constantly against the 1970-epoch default time.
        if (tv.tv_sec < 1700000000)
            continue;

        struct tm t;
        localtime_r(&tv.tv_sec, &t);
        uint32_t current_minute = (uint32_t)(tv.tv_sec / 60);

        for (int i = 0; i < N_ALARMS; i++)
        {
            if (!alarms[i].enabled)
                continue;
            if (last_fired_minute[i] == current_minute)
                continue;
            // Alarm is stored in 12-hour clock; convert to 24-hour for the
            // comparison. 12 AM → 0, 12 PM → 12.
            int alarm_h24 = alarms[i].hour % 12;
            if (!alarms[i].am)
                alarm_h24 += 12;
            if (alarm_h24 == t.tm_hour && alarms[i].minute == t.tm_min)
            {
                last_fired_minute[i] = current_minute;
                pending_alarm_idx = i;
                watch.wakeup();
                break;
            }
        }
    }
}

// Auto-stop timer callback. Fires once, 60s after the alarm started
// ringing — silences the haptic and asks the pm task to sleep. The
// ring screen stays the active LVGL screen (preserve_screen_on_sleep
// is left set), so when the user next touches the watch they wake up
// directly onto the ring screen showing which alarm fired.
static void alarm_auto_stop_cb(lv_timer_t *t)
{
    haptic_stop();
    alarm_auto_stop_timer = nullptr;
    lv_timer_delete(t);
    watch.request_sleep();
}

// LVGL timer: catches the signal set by alarm_task, fades the ring
// screen in, and starts the haptic loop. Runs on the LVGL task so all
// LVGL/haptic calls below stay on the right thread.
static void alarm_check_pending(lv_timer_t *)
{
    if (pending_alarm_idx < 0)
        return;
    int idx = pending_alarm_idx;
    pending_alarm_idx = -1;

    // Surface which alarm fired on the ring screen.
    if (alarm_ring_time_lbl && idx >= 0 && idx < N_ALARMS)
    {
        const Alarm &a = alarms[idx];
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d %s",
                 a.hour, a.minute, a.am ? "AM" : "PM");
        lv_label_set_text(alarm_ring_time_lbl, buf);
    }

    if (alarm_ring_screen)
        lv_screen_load_anim(alarm_ring_screen,
                            LV_SCREEN_LOAD_ANIM_FADE_IN, 100, 0, false);
    haptic_play(true, 800, 800, 800, 800, 800, 2160, 0);

    // Stay-awake hold for 60s, then auto-stop. preserve_screen_on_sleep
    // keeps the ring screen as the active LVGL screen across sleep so
    // the next wake puts the user back on it.
    watch.prevent_sleep_until_ms = esp_timer_get_time() / 1000 + 60000;
    watch.preserve_screen_on_sleep = true;
    if (alarm_auto_stop_timer)
        lv_timer_delete(alarm_auto_stop_timer);
    alarm_auto_stop_timer = lv_timer_create(alarm_auto_stop_cb, 60000, NULL);
    lv_timer_set_repeat_count(alarm_auto_stop_timer, 1);
}

static void refresh_alarm_row(int idx)
{
    if (!alarm_row_lbls[idx])
        return;
    const Alarm &a = alarms[idx];
    lv_label_set_text_fmt(alarm_row_lbls[idx], "%d:%02d %s",
                          a.hour, a.minute, a.am ? "AM" : "PM");
    if (a.enabled)
        lv_obj_add_state(alarm_row_switches[idx], LV_STATE_CHECKED);
    else
        lv_obj_remove_state(alarm_row_switches[idx], LV_STATE_CHECKED);
}

// Receive a Gadgetbridge {t:"alarm","d":[…]} array and overwrite our
// N_ALARMS slots with it. Each incoming entry looks like:
//   { "h": 6, "m": 30, "rep": 51, "on": true }
// where h is 24-hour (0..23), m is minute, on is enabled, and rep is
// a weekday-mask for repeats (ignored for now — the local alarm task
// fires by wall-clock hour+minute regardless of day). Extra entries
// past N_ALARMS are dropped; short arrays leave trailing slots
// disabled. Called from the BLE rx task, so we grab lvgl_port_lock
// before touching row widgets.
extern "C" void alarm_apply_from_gb(cJSON *arr)
{
    if (!cJSON_IsArray(arr))
        return;

    // Reset every slot first so a shorter incoming array cleanly
    // disables the tail rather than leaving stale local alarms armed.
    for (int i = 0; i < N_ALARMS; i++) {
        alarms[i] = {12, 0, true, false};
    }

    int i = 0;
    cJSON *ent;
    cJSON_ArrayForEach(ent, arr) {
        if (i >= N_ALARMS) break;
        if (!cJSON_IsObject(ent)) { i++; continue; }

        cJSON *h  = cJSON_GetObjectItemCaseSensitive(ent, "h");
        cJSON *m  = cJSON_GetObjectItemCaseSensitive(ent, "m");
        cJSON *on = cJSON_GetObjectItemCaseSensitive(ent, "on");

        int h24 = cJSON_IsNumber(h) ? (int)h->valuedouble : 0;
        int mm  = cJSON_IsNumber(m) ? (int)m->valuedouble : 0;
        if (h24 < 0 || h24 > 23) h24 = 0;
        if (mm  < 0 || mm  > 59) mm  = 0;

        // 24h → 12h + am/pm. 0 = 12 AM, 12 = 12 PM.
        bool am    = h24 < 12;
        int  h12   = h24 % 12;
        if (h12 == 0) h12 = 12;

        alarms[i].hour    = (uint8_t)h12;
        alarms[i].minute  = (uint8_t)mm;
        alarms[i].am      = am;
        alarms[i].enabled = cJSON_IsBool(on) ? cJSON_IsTrue(on) : false;
        i++;
    }

    // Persist + refresh any live row labels — but NOT here on the
    // caller's task. The BLE-rx task's stack lives in PSRAM (see
    // ble_init in ble.cpp: internal SRAM is too tight for a 6 KB
    // contiguous chunk after the BT controller + LVGL take theirs),
    // and NVS writes go through the SPI flash driver which disables
    // the flash cache mid-write. A PSRAM-backed stack disappears
    // from the CPU's view the moment the cache goes off, and
    // esp_task_stack_is_sane_cache_disabled() asserts on entry to
    // the flash op — panic during any {t:"alarm"} sync. Defer to
    // lv_async_call, which fires on the LVGL task; that task's stack
    // is in internal SRAM (see display.cpp's lvgl_port_init — no
    // task_stack_caps override → default heap = internal).
    lv_async_call(
        [](void *) {
            for (int j = 0; j < N_ALARMS; j++) {
                save_alarm(j);
                refresh_alarm_row(j);
            }
        },
        nullptr);
}

// Pre-load setalarmscr with alarms[idx] and switch to it.
static void open_alarm_editor(int idx)
{
    editing_alarm = idx;
    const Alarm &a = alarms[idx];

    // AM/PM state + label colors.
    am = a.am;
    lv_obj_set_style_text_color(amlbl,
                                am ? g_accent_color : lv_color_hex(0x444444), 0);
    lv_obj_set_style_text_color(pmlbl,
                                am ? lv_color_hex(0x444444) : g_accent_color, 0);

    // Send a click on hourbox to put the arc into hour-edit mode (this also
    // resets the arc range, ticks, and styles); then override the arc value
    // with the alarm's actual hour. The arc's VALUE_CHANGED handler will
    // refresh the hour label for us.
    sethour = true;
    lv_obj_send_event(hourbox, LV_EVENT_CLICKED, NULL);
    int hour_val = (a.hour == 12) ? 0 : a.hour;
    lv_arc_set_value(alarmarc, hour_val);
    lv_obj_send_event(alarmarc, LV_EVENT_VALUE_CHANGED, NULL);

    // The arc event only updates whichever label matches `sethour`, so the
    // minute label needs to be set manually here.
    lv_label_set_text_fmt(alarmminute, "%02d", a.minute);

    prev_alarm_screen = lv_screen_active();
    lv_screen_load_anim(setalarmscr, LV_SCREEN_LOAD_ANIM_FADE_IN, 200, 0, false);
}

lv_obj_t *alarm_create(lv_obj_t *scr, int idx)
{
    lv_obj_t *alarm = lv_obj_create(scr);
    lv_obj_set_size(alarm, 170, 36);
    lv_obj_set_style_radius(alarm, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(alarm, 0, 0);
    lv_obj_set_scroll_dir(alarm, LV_DIR_NONE);
    lv_obj_set_flag(alarm, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_set_user_data(alarm, (void *)(intptr_t)idx);

    lv_obj_t *alarmlbl = lv_label_create(alarm);
    lv_obj_align(alarmlbl, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_font(alarmlbl, &ProductSansBold_20, 0);
    alarm_row_lbls[idx] = alarmlbl;

    lv_obj_t *alarmswitch = lv_switch_create(alarm);
    lv_obj_align(alarmswitch, LV_ALIGN_RIGHT_MID, 10, 0);
    lv_obj_set_size(alarmswitch, 50, 30);
    lv_obj_set_style_bg_color(alarmswitch, lv_color_hex(0x222222), 0);
    lv_obj_set_user_data(alarmswitch, (void *)(intptr_t)idx);
    alarm_row_switches[idx] = alarmswitch;

    // Switch toggles enabled state for this alarm.
    lv_obj_add_event_cb(
        alarmswitch,
        [](lv_event_t *e)
        {
            lv_obj_t *sw = lv_event_get_target_obj(e);
            int i = (int)(intptr_t)lv_obj_get_user_data(sw);
            alarms[i].enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
            save_alarm(i);
        },
        LV_EVENT_VALUE_CHANGED, NULL);

    // Tapping the row (outside the switch) opens the editor for this alarm.
    lv_obj_add_event_cb(
        alarm,
        [](lv_event_t *e)
        {
            lv_obj_t *row = lv_event_get_target_obj(e);
            int i = (int)(intptr_t)lv_obj_get_user_data(row);
            open_alarm_editor(i);
        },
        LV_EVENT_CLICKED, NULL);

    refresh_alarm_row(idx);
    return alarm;
}

lv_obj_t *alarmscr_create(lv_obj_t *parent)
{
    // Restore persisted alarms before the rows render or the watcher
    // task starts checking the clock.
    load_alarms();

    lv_color_t gray = lv_theme_get_color_secondary(parent);

    lv_obj_t *scr = create_screen(parent);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_track_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_main_place(scr, LV_FLEX_ALIGN_START, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_flag(scr, LV_OBJ_FLAG_SCROLL_ELASTIC, true);

    lv_obj_set_style_pad_bottom(scr, 40, 0);
    lv_obj_set_style_pad_row(scr, 8, 0);

    lv_obj_t *appslabel = lv_label_create(scr);
    lv_label_set_text(appslabel, "Alarms");
    lv_obj_set_style_text_font(appslabel, &ProductSansRegular_20, 0);
    lv_obj_set_style_text_color(appslabel, lv_color_white(), 0);

    lv_obj_set_style_pad_top(appslabel, 12, 0);
    lv_obj_set_style_pad_bottom(appslabel, 8, 0);

    for (int i = 0; i < N_ALARMS; i++)
        alarm_create(scr, i);

    setalarmscr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(setalarmscr, lv_color_black(), 0);

    alarmarc = lv_arc_create(setalarmscr);
    lv_obj_set_size(alarmarc, 202, 202);
    lv_obj_set_align(alarmarc, LV_ALIGN_CENTER);

    lv_arc_set_bg_angles(alarmarc, 0, 360);
    lv_arc_set_rotation(alarmarc, 270);

    lv_arc_set_range(alarmarc, 0, 12);
    lv_arc_set_value(alarmarc, 12);

    /* Hide bg + indicator */
    lv_obj_set_style_arc_opa(alarmarc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(alarmarc, LV_OPA_TRANSP, LV_PART_INDICATOR);

    /* Style knob */
    lv_obj_set_style_bg_opa(alarmarc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_add_style(alarmarc, &accent_border_style, LV_PART_KNOB);
    lv_obj_set_style_border_width(alarmarc, 2, LV_PART_KNOB);
    lv_obj_set_style_border_opa(alarmarc, LV_OPA_COVER, LV_PART_KNOB);

    lv_obj_set_style_pad_all(alarmarc, 12, LV_PART_KNOB);
    lv_arc_set_change_rate(alarmarc, UINT32_MAX);

    lv_obj_set_flag(alarmarc, LV_OBJ_FLAG_ADV_HITTEST, true);

    alarmscale = lv_scale_create(setalarmscr);

    lv_obj_set_flag(alarmscale, LV_OBJ_FLAG_CLICKABLE, false);

    lv_obj_set_size(alarmscale, 145, 145);
    lv_scale_set_mode(alarmscale, LV_SCALE_MODE_ROUND_OUTER);
    lv_obj_set_style_bg_opa(alarmscale, 0, 0);
    lv_obj_set_align(alarmscale, LV_ALIGN_CENTER);
    lv_obj_set_style_arc_width(alarmscale, 0, 0);

    lv_scale_set_angle_range(alarmscale, 354);
    lv_scale_set_rotation(alarmscale, 270);
    lv_scale_set_range(alarmscale, 0, 60);
    lv_scale_set_label_show(alarmscale, true);
    lv_scale_set_total_tick_count(alarmscale, 60);
    lv_scale_set_major_tick_every(alarmscale, 5);
    lv_scale_set_text_src(alarmscale, alarmhourticks);

    lv_obj_set_style_text_font(alarmscale, &ProductSansBold_24, 0);
    lv_obj_set_style_text_color(alarmscale, gray, 0);

    lv_obj_set_style_line_color(alarmscale, gray, LV_PART_INDICATOR);
    lv_obj_set_style_length(alarmscale, 8, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(alarmscale, 2, LV_PART_INDICATOR);

    lv_obj_set_style_line_color(alarmscale, gray, LV_PART_ITEMS);
    lv_obj_set_style_length(alarmscale, 4, LV_PART_ITEMS);
    lv_obj_set_style_line_width(alarmscale, 2, LV_PART_ITEMS);

    alarmline = lv_line_create(setalarmscr);
    lv_line_set_points(alarmline, line_points, 2);
    lv_obj_set_size(alarmline, 240, 240);
    lv_obj_add_style(alarmline, &accent_line_style, 0);
    lv_obj_set_style_line_width(alarmline, 2, 0);
    lv_obj_set_style_line_rounded(alarmline, false, 0);

    // Center cap
    lv_obj_t *c = lv_obj_create(setalarmscr);
    lv_obj_set_size(c, 8, 8);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(c, &accent_bg_style, 0);
    lv_obj_center(c);

    hourbox = lv_obj_create(setalarmscr);
    lv_obj_align(hourbox, LV_ALIGN_CENTER, -35, 0);
    lv_obj_set_size(hourbox, 45, 30);
    lv_obj_set_scroll_dir(hourbox, LV_DIR_NONE);

    alarmhour = lv_label_create(hourbox);
    lv_obj_set_style_text_font(alarmhour, &ProductSansBold_20, 0);
    lv_obj_align(alarmhour, LV_ALIGN_RIGHT_MID, 8, 0);
    lv_label_set_text(alarmhour, "12");

    minutebox = lv_obj_create(setalarmscr);
    lv_obj_align(minutebox, LV_ALIGN_CENTER, 35, 0);
    lv_obj_set_size(minutebox, 45, 30);
    lv_obj_set_scroll_dir(minutebox, LV_DIR_NONE);

    lv_obj_add_event_cb(hourbox, [](lv_event_t *e)
                        {
                            sethour = true;

                            lv_obj_set_style_border_color(lv_event_get_target_obj(e), g_accent_color, 0);
                            lv_obj_set_style_border_color(minutebox, lv_color_hex(0x2f3237), 0);

                            lv_arc_set_max_value(alarmarc, 12);
                            lv_arc_set_value(alarmarc, (uint8_t)atoi(lv_label_get_text(alarmhour)));

                            lv_scale_set_text_src(alarmscale, alarmhourticks);
                            lv_obj_send_event(alarmarc, LV_EVENT_VALUE_CHANGED, NULL); }, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(minutebox, [](lv_event_t *e)
                        {
                            sethour = false;

                            lv_obj_set_style_border_color(lv_event_get_target_obj(e), g_accent_color, 0);
                            lv_obj_set_style_border_color(hourbox, lv_color_hex(0x2f3237), 0);

                            lv_arc_set_max_value(alarmarc, 60);
                            lv_arc_set_value(alarmarc, (uint8_t)atoi(lv_label_get_text(alarmminute)));

                            lv_scale_set_text_src(alarmscale, alarmminuteticks);
                            lv_obj_send_event(alarmarc, LV_EVENT_VALUE_CHANGED, NULL); }, LV_EVENT_CLICKED, NULL);

    alarmminute = lv_label_create(minutebox);
    lv_obj_set_style_text_font(alarmminute, &ProductSansBold_20, 0);
    lv_obj_align(alarmminute, LV_ALIGN_LEFT_MID, -8, 0);
    lv_label_set_text(alarmminute, "00");

    lv_obj_add_event_cb(alarmarc, [](lv_event_t *e)
                        {
                            uint8_t selected = lv_arc_get_value(lv_event_get_target_obj(e));

                            uint16_t angle = selected * 360 / lv_arc_get_max_value(lv_event_get_target_obj(e));
                            line_points[1] = {120.0f + 77 * sinf(angle * M_PI / 180), 120.0f - 77 * cosf(angle * M_PI / 180)};
                            lv_line_set_points(alarmline, line_points, 2);

                            haptic_play(false, 15, 0);

                            if (sethour)
                            {
                                if (selected == 0)
                                    selected = 12;

                                lv_label_set_text_fmt(alarmhour, "%d", selected);
                            }

                            if (!sethour)
                            {
                                if (selected == 60)
                                    selected = 0;

                                lv_label_set_text_fmt(alarmminute, "%02d", selected);
                            } }, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *btn = lv_button_create(setalarmscr);
    lv_obj_set_size(btn, 78, 30);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 37);
    lv_obj_set_style_border_color(btn, lv_color_black(), 0);
    lv_obj_set_style_border_width(btn, 2, 0);

    lv_obj_t *setlbl = lv_label_create(btn);
    lv_obj_center(setlbl);
    lv_obj_set_style_text_font(setlbl, &ProductSansBold_20, 0);
    lv_label_set_text(setlbl, "Set");

    // Save the picked time back into the alarm being edited, refresh that
    // row, then fade back to whichever screen launched the editor.
    lv_obj_add_event_cb(
        btn,
        [](lv_event_t *e)
        {
            if (editing_alarm < 0)
                return;
            Alarm &a = alarms[editing_alarm];
            a.hour = (uint8_t)atoi(lv_label_get_text(alarmhour));
            a.minute = (uint8_t)atoi(lv_label_get_text(alarmminute));
            a.am = am;
            a.enabled = true; // setting an alarm implies turning it on
            save_alarm(editing_alarm);
            refresh_alarm_row(editing_alarm);
            editing_alarm = -1;
            if (prev_alarm_screen)
                lv_screen_load_anim(prev_alarm_screen,
                                    LV_SCREEN_LOAD_ANIM_FADE_OUT, 200, 0, false);
        },
        LV_EVENT_CLICKED, NULL);

    lv_obj_t *amlblshadow = lv_label_create(setalarmscr);
    lv_obj_align(amlblshadow, LV_ALIGN_CENTER, -19, -36);
    lv_obj_set_style_text_font(amlblshadow, &ProductSansBold_20, 0);
    lv_label_set_text(amlblshadow, "AM");
    lv_obj_set_style_text_color(amlblshadow, lv_color_black(), 0);

    amlbl = lv_label_create(setalarmscr);
    lv_obj_align(amlbl, LV_ALIGN_CENTER, -20, -37);
    lv_obj_set_style_text_font(amlbl, &ProductSansBold_20, 0);
    lv_label_set_text(amlbl, "AM");
    lv_obj_set_style_text_color(amlbl, g_accent_color, 0);
    lv_obj_set_flag(amlbl, LV_OBJ_FLAG_CLICKABLE, true);

    lv_obj_t *pmlblshadow = lv_label_create(setalarmscr);
    lv_obj_align(pmlblshadow, LV_ALIGN_CENTER, 21, -36);
    lv_obj_set_style_text_font(pmlblshadow, &ProductSansBold_20, 0);
    lv_label_set_text(pmlblshadow, "PM");
    lv_obj_set_style_text_color(pmlblshadow, lv_color_black(), 0);

    pmlbl = lv_label_create(setalarmscr);
    lv_obj_align(pmlbl, LV_ALIGN_CENTER, 20, -37);
    lv_obj_set_style_text_font(pmlbl, &ProductSansBold_20, 0);
    lv_label_set_text(pmlbl, "PM");
    lv_obj_set_style_text_color(pmlbl, lv_color_hex(0x444444), 0);
    lv_obj_set_flag(pmlbl, LV_OBJ_FLAG_CLICKABLE, true);

    lv_obj_add_event_cb(amlbl, [](lv_event_t *e)
                        {
                            am = true;
                            lv_obj_set_style_text_color(amlbl, g_accent_color, 0);
                            lv_obj_set_style_text_color(pmlbl, lv_color_hex(0x444444), 0); }, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(pmlbl, [](lv_event_t *e)
                        {
                            am = false;
                            lv_obj_set_style_text_color(pmlbl, g_accent_color, 0);
                            lv_obj_set_style_text_color(amlbl, lv_color_hex(0x444444), 0); }, LV_EVENT_CLICKED, NULL);

    lv_obj_send_event(hourbox, LV_EVENT_CLICKED, NULL);

    // Ring screen — shown when an alarm fires. Shows "Alarm" + the
    // time the fired alarm was set for + a Stop button.
    alarm_ring_screen = lv_obj_create(NULL);

    lv_obj_t *ringlbl = lv_label_create(alarm_ring_screen);
    lv_obj_set_style_text_font(ringlbl, &ProductSansRegular_20, 0);
    lv_obj_align(ringlbl, LV_ALIGN_CENTER, 0, -60);
    lv_obj_set_style_text_color(ringlbl, lv_color_hex(0x6699ff), 0);
    lv_label_set_text(ringlbl, "Alarm");

    alarm_ring_time_lbl = lv_label_create(alarm_ring_screen);
    lv_obj_set_style_text_font(alarm_ring_time_lbl, &ProductSansBold_42, 0);
    lv_obj_align(alarm_ring_time_lbl, LV_ALIGN_CENTER, 0, -15);
    lv_label_set_text(alarm_ring_time_lbl, "");

    lv_obj_t *stopbtn = lv_button_create(alarm_ring_screen);
    lv_obj_align(stopbtn, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_radius(stopbtn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_ver(stopbtn, 16, 0);
    lv_obj_set_style_pad_hor(stopbtn, 36, 0);

    lv_obj_t *stoplbl = lv_label_create(stopbtn);
    lv_obj_set_style_text_font(stoplbl, &ProductSansBold_30, 0);
    lv_label_set_text(stoplbl, "Stop");

    lv_obj_add_event_cb(
        stopbtn,
        [](lv_event_t *)
        {
            // Cancel the 60s auto-stop, release the stay-awake hold,
            // stop preserving the ring screen, silence haptic, return
            // to the watch face.
            if (alarm_auto_stop_timer)
            {
                lv_timer_delete(alarm_auto_stop_timer);
                alarm_auto_stop_timer = nullptr;
            }
            watch.prevent_sleep_until_ms = 0;
            watch.preserve_screen_on_sleep = false;
            haptic_stop();
            lv_screen_load_anim(main_screen,
                                LV_SCREEN_LOAD_ANIM_FADE_OUT, 100, 0, false);
        },
        LV_EVENT_PRESSED, NULL);

    // LVGL-side timer that hands off the cross-task fire signal to the
    // UI; background task that actually watches the clock.
    lv_timer_create(alarm_check_pending, 100, NULL);
    // 8 KB stack: watch.wakeup() runs synchronously on the caller's
    // stack and does a full lv_refr_now (display re-init, LCD render of
    // a 240×240 frame) plus IMU reconfig — the default 3 KB blew up
    // when the alarm fired during light sleep. Stack in PSRAM (no DMA
    // / ISR usage on this task) so the 8 KB doesn't tax internal SRAM.
    xTaskCreateWithCaps(alarm_task, "alarm_task", 1024 * 8, NULL, 2, NULL,
                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    return scr;
}
