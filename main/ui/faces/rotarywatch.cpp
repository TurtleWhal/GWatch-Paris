#include "ui.hpp"
#include <sys/time.h>

LV_IMAGE_DECLARE(croppedoverlay);

// All file-scope decls here are `static` — every face .cpp tends to use the
// same widget names (battery, steps, wifiicon, ...) and once watchface.cpp
// links all faces together those collide at link time.
static const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char *wdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

static const char *minute_ticks[] = {"00", "55", "50", "45", "40", "35", "30",
                                     "25", "20", "15", "10", "05", NULL};

static const char *second_ticks[] = {"00", "55", "50", "45", "40", "35", "30",
                                     "25", "20", "15", "10", "05", NULL};

static lv_obj_t *minutescale;
static lv_obj_t *secondscale;

static lv_obj_t *hour;
static lv_obj_t *minute;

static lv_obj_t *mday;
static lv_obj_t *wday;
static lv_obj_t *date;

static lv_obj_t *battery;
static lv_obj_t *steps;

static lv_obj_t *wifiicon;

static uint8_t last_sec = 255, last_min = 255, last_hour = 255;
static uint8_t last_day = 255, last_month = 255;
static uint32_t last_battery_check = 0;
static uint32_t last_battery_mv = 0;

lv_obj_t *rotarywatch_create(lv_obj_t *parent)
{
    lv_color_t gray = lv_theme_get_color_secondary(parent);

    lv_obj_t *scr = create_screen(parent);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    lv_obj_set_style_radius(scr, 0, 0);

    /* Minute scale */
    minutescale = lv_scale_create(scr);
    lv_obj_set_size(minutescale, 164, 164);
    lv_scale_set_mode(minutescale, LV_SCALE_MODE_ROUND_INNER);
    // lv_obj_set_style_bg_opa(minutescale, LV_OPA_0, 0);
    lv_obj_set_style_bg_color(minutescale, lv_color_black(), 0);
    lv_obj_set_style_radius(minutescale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(minutescale, true, 0);
    lv_obj_align(minutescale, LV_ALIGN_LEFT_MID, 30 - (164 / 2), 0);
    lv_obj_set_style_arc_width(minutescale, 0, 0);

    lv_scale_set_angle_range(minutescale, 354);
    lv_scale_set_rotation(minutescale, 0);
    lv_scale_set_range(minutescale, 0, 60);
    lv_scale_set_label_show(minutescale, true);
    lv_scale_set_total_tick_count(minutescale, 60);
    lv_scale_set_major_tick_every(minutescale, 5);
    lv_scale_set_text_src(minutescale, minute_ticks);

    lv_obj_set_style_text_font(minutescale, &ProductSansRegular_14, 0);
    lv_obj_set_style_text_color(minutescale, gray, 0);

    lv_obj_set_style_line_color(minutescale, gray, LV_PART_INDICATOR);
    lv_obj_set_style_length(minutescale, 8, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(minutescale, 2, LV_PART_INDICATOR);

    lv_obj_set_style_line_color(minutescale, gray, LV_PART_ITEMS);
    lv_obj_set_style_length(minutescale, 4, LV_PART_ITEMS);
    lv_obj_set_style_line_width(minutescale, 2, LV_PART_ITEMS);

    /* Second scale */
    secondscale = lv_scale_create(scr);
    lv_obj_set_size(secondscale, 234, 234);
    lv_scale_set_mode(secondscale, LV_SCALE_MODE_ROUND_INNER);
    lv_obj_set_style_bg_opa(secondscale, LV_OPA_0, 0);
    lv_obj_set_style_radius(secondscale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(secondscale, true, 0);
    lv_obj_align(secondscale, LV_ALIGN_LEFT_MID, 30 - (234 / 2), 0);
    lv_obj_set_style_arc_width(secondscale, 0, 0);

    lv_scale_set_angle_range(secondscale, 354);
    lv_scale_set_rotation(secondscale, 0);
    lv_scale_set_range(secondscale, 0, 60);
    lv_scale_set_label_show(secondscale, true);
    lv_scale_set_total_tick_count(secondscale, 60);
    lv_scale_set_major_tick_every(secondscale, 5);
    lv_scale_set_text_src(secondscale, second_ticks);

    lv_obj_set_style_text_font(secondscale, &ProductSansRegular_14, 0);
    lv_obj_add_style(secondscale, &accent_text_style, 0);

    lv_obj_set_style_line_color(secondscale, gray, LV_PART_INDICATOR);
    lv_obj_set_style_length(secondscale, 8, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(secondscale, 2, LV_PART_INDICATOR);

    lv_obj_set_style_line_color(secondscale, gray, LV_PART_ITEMS);
    lv_obj_set_style_length(secondscale, 4, LV_PART_ITEMS);
    lv_obj_set_style_line_width(secondscale, 2, LV_PART_ITEMS);

    lv_obj_t *overlayimg = lv_img_create(scr);
    lv_image_set_src(overlayimg, &croppedoverlay);
    lv_obj_align(overlayimg, LV_ALIGN_LEFT_MID, -2, 0);

    lv_obj_t *minutemask = lv_obj_create(scr);
    lv_obj_set_size(minutemask, 38, 36);
    lv_obj_align(minutemask, LV_ALIGN_LEFT_MID, 60, 0);
    lv_obj_set_style_bg_color(minutemask, lv_color_black(), 0);
    lv_obj_set_style_border_opa(minutemask, LV_OPA_0, 0);
    lv_obj_set_style_radius(minutemask, 0, 0);

    lv_obj_t *bound = lv_obj_create(scr);
    lv_obj_set_size(bound, 92, 36);
    lv_obj_set_style_bg_opa(bound, LV_OPA_0, 0);
    lv_obj_set_style_radius(bound, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(bound, &accent_border_style, 0);
    lv_obj_set_style_border_width(bound, 2, 0);
    lv_obj_align(bound, LV_ALIGN_LEFT_MID, 60, 0);

    /* Hour + minute text */
    hour = lv_label_create(scr);
    lv_label_set_text(hour, "09");
    lv_obj_set_style_text_color(hour, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(hour, &ProductSansBold_42, 0);
    lv_obj_align(hour, LV_ALIGN_LEFT_MID, 4, 0);

    minute = lv_label_create(scr);
    lv_label_set_text(minute, "17");
    lv_obj_set_style_text_color(minute, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(minute, &ProductSansBold_24, 0);
    lv_obj_align(minute, LV_ALIGN_LEFT_MID, 66, 0);

    /* Steps */
    steps = create_valuearc(scr, FA_STEPS);
    lv_obj_align(steps, LV_ALIGN_RIGHT_MID, -40, -68);
    lv_arc_set_range(steps, 0, 6500);
    lv_arc_set_value(steps, 1234);
    lv_label_set_text_fmt(lv_obj_get_child_by_name(steps, "text"), "%d", 1234);

    /* Date */
    // lv_obj_t *dateicon = lv_label_create(scr);
    // SET_SYMBOL_14(dateicon, FA_CALENDAR);
    // lv_obj_align(dateicon, LV_ALIGN_CENTER, 80, -22);
    // lv_obj_set_style_text_color(dateicon, accent, 0);

    wday = lv_label_create(scr);
    lv_obj_align(wday, LV_ALIGN_CENTER, 80, -20);
    lv_obj_set_style_text_font(wday, &ProductSansBold_16, 0);
    lv_obj_add_style(wday, &accent_text_style, 0);
    lv_label_set_text(wday, "WED");

    mday = lv_label_create(scr);
    lv_obj_align(mday, LV_ALIGN_CENTER, 80, 0);
    lv_obj_set_style_text_font(mday, &ProductSansBold_20, 0);
    lv_obj_set_style_text_color(mday, lv_color_white(), 0);
    lv_label_set_text(mday, "DEC");

    date = lv_label_create(scr);
    lv_obj_align(date, LV_ALIGN_CENTER, 80, 20);
    lv_obj_set_style_text_font(date, &ProductSansRegular_16, 0);
    lv_obj_add_style(date, &accent_text_style, 0);
    lv_label_set_text(date, "12/03/25");

    // lv_obj_add_flag(wday, LV_OBJ_FLAG_CLICKABLE);
    // lv_obj_add_flag(month, LV_OBJ_FLAG_CLICKABLE);
    // lv_obj_add_flag(day, LV_OBJ_FLAG_CLICKABLE);
    // lv_obj_add_flag(date, LV_OBJ_FLAG_CLICKABLE);

    // lv_obj_add_flag(date, LV_OBJ_FLAG_HIDDEN);

    // auto change_date_visibility = [](lv_event_t *e)
    // {
    //     printf("Date clicked\n");
    //     lv_obj_set_flag(wday, LV_OBJ_FLAG_HIDDEN, !lv_obj_has_flag(wday, LV_OBJ_FLAG_HIDDEN));
    //     lv_obj_set_flag(month, LV_OBJ_FLAG_HIDDEN, !lv_obj_has_flag(month, LV_OBJ_FLAG_HIDDEN));
    //     lv_obj_set_flag(day, LV_OBJ_FLAG_HIDDEN, !lv_obj_has_flag(day, LV_OBJ_FLAG_HIDDEN));
    //     lv_obj_set_flag(date, LV_OBJ_FLAG_HIDDEN, !lv_obj_has_flag(date, LV_OBJ_FLAG_HIDDEN));
    // };

    // lv_obj_add_event_cb(wday, change_date_visibility, LV_EVENT_CLICKED, NULL);
    // lv_obj_add_event_cb(month, change_date_visibility, LV_EVENT_CLICKED, NULL);
    // lv_obj_add_event_cb(day, change_date_visibility, LV_EVENT_CLICKED, NULL);
    // lv_obj_add_event_cb(date, change_date_visibility, LV_EVENT_CLICKED, NULL);

    /* Battery */
    battery = create_valuearc(scr, FA_BATTERY_FULL);
    lv_obj_align(battery, LV_ALIGN_RIGHT_MID, -40, 68);
    lv_arc_set_range(battery, 0, 100);
    lv_arc_set_value(battery, 100);
    lv_label_set_text_fmt(lv_obj_get_child_by_name(battery, "text"), "%d%%", 100);

    wifiicon = lv_label_create(scr);
    // lv_obj_set_pos(wifiicon, POLAR(120, 90));
    lv_obj_align(wifiicon, LV_ALIGN_CENTER, POLAR(110, 90));

    lv_obj_set_style_text_color(wifiicon, lv_color_white(), 0);
    SET_SYMBOL_14(wifiicon, FA_WIFI);

    lv_obj_add_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);

    // Force a full draw now: reset the change-guards so the immediate update
    // below repaints every field. Without this, switching to this face leaves
    // it blank until the minute/day/battery happens to change.
    last_sec = last_min = last_hour = last_day = last_month = 255;
    last_battery_check = 0;
    last_battery_mv = 0;
    rotarywatch_update();

    return scr;
}

void rotarywatch_update()
{
    struct timeval tv;
    gettimeofday(&tv, NULL); // tv_sec (seconds), tv_usec (microseconds)

    struct tm t;
    localtime_r(&tv.tv_sec, &t);

    int ms = tv.tv_usec / 1000; // milliseconds (0–999)

    /* Rotate seconds smoothly */
    uint16_t sangle = (ms * 360 / 1000) / 60 + (t.tm_sec * 6);
    lv_scale_set_rotation(secondscale, sangle);

    if (t.tm_sec != last_sec)
    {
        last_sec = t.tm_sec;

        uint16_t mangle = t.tm_sec * (360 / 60) / 60 + t.tm_min * (360 / 60);
        lv_scale_set_rotation(minutescale, mangle);
    }

    /* Update minute/hour only when changed */
    if (t.tm_min != last_min)
    {
        last_min = t.tm_min;
        char buf[4];
        snprintf(buf, sizeof(buf), "%02d", last_min);
        lv_label_set_text(minute, buf);
    }

    if (t.tm_hour != last_hour)
    {
        last_hour = t.tm_hour % 12;
        if (last_hour == 0)
            last_hour = 12;
        char buf[4];
        snprintf(buf, sizeof(buf), "%02d", last_hour);
        lv_label_set_text(hour, buf);
    }

    /* Update date once a day */
    if (t.tm_mday != last_day)
    {
        last_day = t.tm_mday;

        lv_label_set_text_fmt(mday, "%s %02d", months[t.tm_mon], t.tm_mday);

        lv_label_set_text(wday, wdays[t.tm_wday]);

        lv_label_set_text_fmt(date, "%02d/%02d/%02d", t.tm_mon + 1, t.tm_mday, (t.tm_year + 1900) % 100);
    }

    if ((t.tm_mon + 1) != last_month)
    {
        last_month = t.tm_mon + 1;
    }

    /* Battery */
    lv_arc_set_value(battery, watch.battery.percent);
    lv_label_set_text_fmt(lv_obj_get_child_by_name(battery, "text"),
                          "%d%%", watch.battery.percent);

    lv_arc_set_value(steps, watch.imu.steps);
    lv_label_set_text_fmt(lv_obj_get_child_by_name(steps, "text"),
                          "%li", watch.imu.steps);

    if (ble.connected())
    {
        SET_SYMBOL_14(wifiicon, FA_BLUETOOTH);
        lv_obj_remove_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);
    }

    SET_SYMBOL_14(lv_obj_get_child_by_name(battery, "icon"),
                  getbaticon(watch.battery.charging, watch.battery.percent));
}
