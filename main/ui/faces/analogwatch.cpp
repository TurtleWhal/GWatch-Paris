#include "ui.hpp"
#include <sys/time.h>
#include <math.h>
#include <stdio.h>

#define DEG2RAD (M_PI / 180.0f)

static const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char *wdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

static uint8_t last_sec = 255, last_min = 255, last_hour = 255;
static uint8_t last_day = 255, last_month = 255, last_wday = 255;
static uint32_t last_battery_check = 0;
static uint32_t last_battery_mv = 0;

// Change-guards for the info widgets. lv_label_set_text* always invalidates,
// even when the text is identical — without these the battery/steps/BLE
// labels would dirty their regions on every update tick and force pointless
// partial redraws at the full tick rate.
static uint8_t last_bat_pct = 255;
static bool last_bat_charging = false;
static uint32_t last_steps = UINT32_MAX;
static int8_t last_ble_state = -1;
static int64_t last_glance_s = -1; // -1 = glance is blank
// Timer-arc bg-end angle change guard. lv_arc_set_bg_end_angle invalidates the
// arc's knob region (at 12 o'clock = pixel 120,3) every call, even when the
// value is unchanged. This update runs every frame, so calling it
// unconditionally re-flushed a 1x1 area at (120,3) 60x/sec — a degenerate flush
// queued tight behind the big second-hand DMA. That "big async DMA then acquire
// the bus ~2 ms later" pattern raced the SPI bus-lock release and occasionally
// deadlocked it (the intermittent freeze). -1 forces the first set after create.
static int32_t last_arc_bg_end = -1;

// File-scope statics — other faces (divewatch, rotarywatch, timewatch) reuse
// the same names for their own widgets, and once watchface.cpp registers
// every face's create/update the linker pulls all of them in together.
static lv_obj_t *secondhand;
static lv_obj_t *minutehand;
static lv_obj_t *hourhand;
static lv_obj_t *time_label;

static lv_obj_t *schedulelabel;
static lv_obj_t *datelabel;

static lv_obj_t *timerarc;

static lv_obj_t *baticon;
static lv_obj_t *battery;
static lv_obj_t *steps;
static lv_obj_t *glance;

static lv_obj_t *wifiicon;

static lv_point_precise_t second_hand_points[] = {
    {120, 120},
    {220, 120}};

static lv_point_precise_t minute_hand_points[] = {
    {120, 120},
    {220, 120}};

// Move a hand's lv_line so the *object* hugs the center→tip segment instead
// of covering the full 240x240 face. lv_line_set_points invalidates the whole
// object, so a full-screen line forces a full-screen repaint on every tick —
// with the sweep second hand that was ~100% of the idle render load. The
// tight bbox keeps the dirty region to the pixels the hand can actually
// touch. `pad` covers half the line width, the rounded caps and a pixel of
// anti-aliasing. set_pos/set_size invalidate both the old and new areas, so
// the previous hand position is repainted without any extra bookkeeping.
static void position_hand(lv_obj_t *hand, lv_point_precise_t *pts,
                          float tip_x, float tip_y, int32_t pad)
{
    const float cx = 120.0f, cy = 120.0f; // dial center (pts[0])

    int32_t minx = (int32_t)floorf(fminf(cx, tip_x)) - pad;
    int32_t miny = (int32_t)floorf(fminf(cy, tip_y)) - pad;
    int32_t maxx = (int32_t)ceilf(fmaxf(cx, tip_x)) + pad;
    int32_t maxy = (int32_t)ceilf(fmaxf(cy, tip_y)) + pad;

    pts[0].x = cx - minx;
    pts[0].y = cy - miny;
    pts[1].x = tip_x - minx;
    pts[1].y = tip_y - miny;

    lv_obj_set_pos(hand, minx, miny);
    lv_obj_set_size(hand, maxx - minx, maxy - miny);
    lv_line_set_points(hand, pts, 2);
}

lv_obj_t *analogwatch_create(lv_obj_t *parent)
{
    lv_color_t gray = lv_theme_get_color_secondary(parent);

    lv_obj_t *scr = create_screen(parent);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    lv_obj_set_style_radius(scr, 0, 0);

    timerarc = lv_arc_create(parent);
    lv_obj_set_size(timerarc, 234, 234);
    lv_obj_center(timerarc);

    lv_arc_set_mode(timerarc, LV_ARC_MODE_NORMAL);
    lv_arc_set_bg_start_angle(timerarc, 0);
    lv_arc_set_bg_end_angle(timerarc, 0);
    lv_arc_set_rotation(timerarc, -90);

    lv_obj_set_style_arc_width(timerarc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(timerarc, 0, LV_PART_INDICATOR);

    lv_obj_set_style_arc_color(timerarc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(timerarc, false, LV_PART_MAIN);
    lv_obj_remove_style(timerarc, NULL, LV_PART_KNOB);
    lv_obj_add_flag(timerarc, LV_OBJ_FLAG_ADV_HITTEST);

    lv_obj_set_style_line_rounded(timerarc, true, 0);

    schedulelabel = lv_label_create(scr);
    lv_obj_set_style_text_font(schedulelabel, &ProductSansRegular_16, 0);
    lv_obj_align(schedulelabel, LV_ALIGN_CENTER, 0, -70);
    lv_obj_set_style_text_color(schedulelabel, gray, 0);

    datelabel = lv_label_create(scr);
    lv_obj_set_style_text_font(datelabel, &ProductSansBold_20, 0);
    lv_obj_align(datelabel, LV_ALIGN_CENTER, 0, -48);
    lv_obj_add_style(datelabel, &accent_text_style, 0);

    // Center digital time label
    time_label = lv_label_create(scr);
    lv_obj_set_style_text_font(time_label, &BadeenDisplay_84, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0x444444), 0);
    lv_obj_center(time_label);

    lv_obj_t *infobox = lv_obj_create(scr);

    lv_obj_set_style_bg_opa(infobox, 0, 0);
    lv_obj_set_size(infobox, 240, 96);
    lv_obj_align(infobox, LV_ALIGN_CENTER, 0, 48);
    lv_obj_set_scroll_dir(infobox, LV_DIR_NONE);
    lv_obj_set_style_border_width(infobox, 0, 0);
    lv_obj_set_style_pad_all(infobox, 0, 0);

    lv_obj_set_flex_flow(infobox, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_track_place(infobox, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(infobox, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_main_place(infobox, LV_FLEX_ALIGN_CENTER, 0);

    wifiicon = lv_label_create(infobox);
    SET_SYMBOL_14(wifiicon, FA_WIFI);

    // lv_obj_add_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);

    baticon = lv_label_create(infobox);
    SET_SYMBOL_16(baticon, FA_BATTERY_EMPTY);

    battery = lv_label_create(infobox);
    lv_obj_set_style_text_font(battery, &ProductSansRegular_16, 0);
    lv_label_set_text_fmt(battery, "%d%%", 100);

    lv_obj_t *stepicon = lv_label_create(infobox);
    SET_SYMBOL_16(stepicon, FA_STEPS);

    steps = lv_label_create(infobox);
    lv_obj_set_style_text_font(steps, &ProductSansRegular_16, 0);
    lv_label_set_text_fmt(steps, "%d", 5678);

    glance = lv_label_create(scr);
    lv_obj_align(glance, LV_ALIGN_CENTER, 0, 68);
    lv_obj_set_style_text_color(glance, gray, 0);
    lv_obj_set_style_text_font(glance, &ProductSansRegular_16, 0);
    lv_label_set_text(glance, "");

    hourhand = lv_obj_create(scr);
    lv_obj_set_size(hourhand, 65 + 18, 18);
    lv_obj_align(hourhand, LV_ALIGN_CENTER, 65 / 2, 0);
    lv_obj_set_style_transform_pivot_x(hourhand, 9, 0);
    lv_obj_set_style_transform_pivot_y(hourhand, 9, 0);

    lv_obj_set_style_radius(hourhand, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(hourhand, 0, 0);
    lv_obj_set_style_border_width(hourhand, 2, 0);
    lv_obj_add_style(hourhand, &accent_border_style, 0);

    lv_obj_set_scroll_dir(hourhand, LV_DIR_NONE);

    // Minute hand
    minutehand = lv_line_create(scr);
    lv_line_set_points(minutehand, minute_hand_points, 2);
    lv_obj_set_size(minutehand, 240, 240);
    lv_obj_set_style_line_color(minutehand, lv_color_white(), 0);
    lv_obj_set_style_line_width(minutehand, 6, 0);
    lv_obj_set_style_line_rounded(minutehand, true, 0);

    // Second hand
    secondhand = lv_line_create(scr);
    lv_line_set_points(secondhand, second_hand_points, 2);
    lv_obj_set_size(secondhand, 240, 240);
    lv_obj_set_style_line_color(secondhand, lv_color_hex(0x888888), 0);
    lv_obj_set_style_line_width(secondhand, 2, 0);
    lv_obj_set_style_line_rounded(secondhand, true, 0);

    // Center cap
    lv_obj_t *c = lv_obj_create(scr);
    lv_obj_set_size(c, 6, 6);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x888888), 0);
    lv_obj_center(c);

    // Force a full draw now: reset the change-guards so the immediate update
    // below repaints every field. Without this, switching to this face leaves
    // it blank until the minute/day/battery happens to change.
    last_sec = last_min = last_hour = last_day = last_month = last_wday = 255;
    last_arc_bg_end = -1;
    last_battery_check = 0;
    last_battery_mv = 0;
    last_bat_pct = 255;
    last_bat_charging = false;
    last_steps = UINT32_MAX;
    last_ble_state = -1;
    last_glance_s = -1;
    analogwatch_update();

    return scr;
}

void analogwatch_update()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm t;
    localtime_r(&tv.tv_sec, &t);
    int ms = tv.tv_usec / 1000;

    // Calculate angles (0° = 12 o'clock, increasing clockwise)
    float sangle = (t.tm_sec + ms / 1000.0f) * 6.0f;

    // Convert angles so 0° is at 12 o'clock (subtract 90°)
    float srad = (sangle - 90.0f) * DEG2RAD;

    // Update second hand continuously (tight-bbox move, see position_hand)
    position_hand(secondhand, second_hand_points,
                  120.5f + cosf(srad) * 100.0f,
                  120.5f + sinf(srad) * 100.0f, 3);

    // Update minute and hour when needed
    if (t.tm_sec != last_sec)
    {
        last_sec = t.tm_sec;

        float mangle = (t.tm_min + t.tm_sec / 60.0f) * 6.0f;
        float mrad = (mangle - 90.0f) * DEG2RAD;

        position_hand(minutehand, minute_hand_points,
                      120.5f + cosf(mrad) * 95.0f,
                      120.5f + sinf(mrad) * 95.0f, 5);

        // Update label (digital time)
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", t.tm_hour > 12 ? t.tm_hour - 12 : t.tm_hour, t.tm_min);
        lv_label_set_text(time_label, buf);

    }

    // Hour hand + schedule text. Checked independently of the sec-change
    // block (and the hour compared alongside the minute) so a Gadgetbridge
    // time sync that jumps the clock repaints them on the next tick even
    // when the new minute value happens to equal the last-seen one.
    if (t.tm_min != last_min || t.tm_hour != last_hour)
    {
        last_min = t.tm_min;
        last_hour = t.tm_hour;

        float hangle = ((t.tm_hour % 12) + t.tm_min / 60.0f) * 30.0f;

        lv_obj_set_style_transform_rotation(hourhand, (270 + hangle) * 10, 0);

        lv_label_set_text(schedulelabel, watch.schedule.getText());
    }

    // Date label. This used to be nested sec-change → min-change →
    // hour-change → day-change, which broke on time syncs: a jump across
    // days that lands on the same hour digit (or waking at the same hour
    // the next day) left the date stale until the next natural hour
    // rollover. Compare everything the label actually shows — weekday,
    // month, day — every tick instead; a sync like Jan 14 → Jul 14 keeps
    // mday equal, so mday alone isn't sufficient either.
    if (t.tm_mday != last_day || (uint8_t)(t.tm_mon + 1) != last_month ||
        t.tm_wday != last_wday)
    {
        last_day = t.tm_mday;
        last_month = t.tm_mon + 1;
        last_wday = t.tm_wday;

        lv_label_set_text_fmt(datelabel, "%s %s %02d", wdays[t.tm_wday], months[t.tm_mon], t.tm_mday);
    }

    // Info widgets only repaint when their value actually changed — label
    // setters invalidate unconditionally, and this runs every tick.
    if (watch.battery.percent != last_bat_pct ||
        watch.battery.charging != last_bat_charging)
    {
        last_bat_pct = watch.battery.percent;
        last_bat_charging = watch.battery.charging;
        lv_label_set_text_fmt(battery, "%d%%", last_bat_pct);
        // lv_label_set_text_fmt(battery, "%dmV", watch.battery.voltage);
        SET_SYMBOL_16(baticon, getbaticon(last_bat_charging, last_bat_pct));
    }

    if (watch.imu.steps != last_steps)
    {
        last_steps = watch.imu.steps;
        lv_label_set_text_fmt(steps, "%lu", (unsigned long)last_steps);
    }

    {
        int8_t ble_state = ble.connected() ? 1 : 0;
        if (ble_state != last_ble_state)
        {
            last_ble_state = ble_state;
            SET_SYMBOL_14(wifiicon, ble_state ? FA_BLUETOOTH : "");
        }
    }

    if (watch.chrono.stopwatchrunning)
    {
        int64_t diff = esp_timer_get_time() - watch.chrono.stopwatchstarttime;
        int64_t s = diff / 1000 / 1000;

        if (s != last_glance_s)
        {
            last_glance_s = s;
            int64_t h = s / 60 / 60;
            int64_t m = s / 60;

            if (h > 0)
                lv_label_set_text_fmt(glance, "%lld:%02lld:%02lld", h, m % 60, s % 60);
            else
                lv_label_set_text_fmt(glance, "%lld:%02lld", m, s % 60);
        }
    }
    else if (last_glance_s != -1)
    {
        last_glance_s = -1;
        lv_label_set_text(glance, "");
    }

    int32_t arc_bg_end = 0;
    if (watch.chrono.timerrunning)
    {
        int64_t t = watch.chrono.timertime;

        int64_t h = t / 1000 / 1000 / 60 / 60;
        int64_t m = t / 1000 / 1000 / 60;
        int64_t s = t / 1000 / 1000;

        arc_bg_end = (h > 0) ? (int32_t)(h * 6)
                     : (m > 0) ? (int32_t)(m * 6)
                               : (int32_t)(s * 6);
    }

    // Only touch the arc when its value actually changes — otherwise this
    // re-invalidates the knob pixel (120,3) every frame. See last_arc_bg_end.
    if (arc_bg_end != last_arc_bg_end)
    {
        last_arc_bg_end = arc_bg_end;
        lv_arc_set_bg_end_angle(timerarc, arc_bg_end);
    }
}