#include "ui.hpp"
#include <sys/time.h>

const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
const char *wdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

static uint8_t last_sec = 255, last_min = 255, last_hour = 255;
static uint8_t last_day = 255, last_month = 255;

lv_obj_t *timelabel;
lv_obj_t *datelabel;
// lv_obj_t *secondscale;
lv_obj_t *secondslabel;

lv_obj_t *baticon;
lv_obj_t *battery;
lv_obj_t *steps;

lv_obj_t *wifiicon;

lv_obj_t *timescreen_create(lv_obj_t *parent)
{
    lv_color_t accent = lv_theme_get_color_primary(parent);
    lv_color_t gray = lv_theme_get_color_secondary(parent);

    lv_obj_t *scr = create_screen(parent);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    datelabel = lv_label_create(scr);
    lv_obj_set_style_text_font(datelabel, &ProductSansBold_20, 0);
    lv_obj_align(datelabel, LV_ALIGN_CENTER, 0, -48);
    lv_obj_set_style_text_color(datelabel, accent, 0);

    lv_obj_t *timebox = lv_obj_create(scr);
    lv_obj_set_style_bg_opa(timebox, 0, 0);
    lv_obj_set_size(timebox, 240, 96);
    lv_obj_align(timebox, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_scroll_dir(timebox, LV_DIR_NONE);
    lv_obj_set_style_border_width(timebox, 0, 0);
    lv_obj_set_style_pad_all(timebox, 0, 0);
    lv_obj_set_style_pad_left(timebox, 8, 0);

    lv_obj_set_flex_flow(timebox, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_track_place(timebox, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(timebox, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_main_place(timebox, LV_FLEX_ALIGN_START, 0);

    timelabel = lv_label_create(timebox);
    // lv_obj_set_style_text_font(timelabel, &BadeenDisplay_84, 0);
    lv_obj_set_style_text_font(timelabel, &SirinStencil_92, 0);

    secondslabel = lv_label_create(timebox);
    lv_obj_set_style_text_font(secondslabel, &SirinStencil_32, 0);
    lv_obj_set_style_text_color(secondslabel, accent, 0);
    lv_obj_set_style_text_color(secondslabel, lv_color_hex(0x999999), 0);
    lv_obj_set_style_text_line_space(secondslabel, 6, 0);

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

    lv_obj_add_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);

    baticon = lv_label_create(infobox);
    SET_SYMBOL_16(baticon, FA_BATTERY);

    battery = lv_label_create(infobox);
    lv_obj_set_style_text_font(battery, &ProductSansRegular_16, 0);
    lv_label_set_text_fmt(battery, "%1.3fV", 1.234);

    lv_obj_t *stepicon = lv_label_create(infobox);
    SET_SYMBOL_16(stepicon, FA_STEPS);

    steps = lv_label_create(infobox);
    lv_obj_set_style_text_font(steps, &ProductSansRegular_16, 0);
    lv_label_set_text_fmt(steps, "%d", 5678);

    return scr;
}

void timescreen_update()
{
    struct timeval tv;
    gettimeofday(&tv, NULL); // tv_sec (seconds), tv_usec (microseconds)

    struct tm t;
    localtime_r(&tv.tv_sec, &t);

    int ms = tv.tv_usec / 1000; // milliseconds (0–999)

    if (t.tm_sec != last_sec)
    {
        last_sec = t.tm_sec;

        lv_label_set_text_fmt(secondslabel, "%s\n%02d", t.tm_hour > 12 ? "PM" : "AM", last_sec);
    }

    /* Update minute/hour only when changed */
    if (t.tm_min != last_min)
    {
        last_min = t.tm_min;

        uint8_t h = t.tm_hour % 12;

        if (h == 0)
            h = 12;

        lv_label_set_text_fmt(timelabel, "%d:%02d", h, last_min);
        lv_obj_set_style_pad_left(lv_obj_get_parent(timelabel), h < 10 ? 8 : 2, 0);
    }

    if (t.tm_hour != last_hour)
    {
        last_hour = t.tm_hour % 12;
        if (last_hour == 0)
            last_hour = 12;
    }

    /* Update date once a day */
    if (t.tm_mday != last_day)
    {
        last_day = t.tm_mday;

        lv_label_set_text_fmt(datelabel, "%s %s %02d", wdays[t.tm_wday], months[t.tm_mon], t.tm_mday);
    }

    if ((t.tm_mon + 1) != last_month)
    {
        last_month = t.tm_mon + 1;
    }

    /* Battery (assume sysinfo.bat is still valid) */
    lv_label_set_text_fmt(battery, "%1.3fV", watch.battery.voltage / 1000.0);

    lv_label_set_text_fmt(steps, "%ld", watch.imu.steps);

    switch (watch.wifi.status)
    {
    case WIFI_CONNECTED:
        SET_SYMBOL_14(wifiicon, FA_WIFI);
        lv_obj_remove_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);
        break;
    case WIFI_CONNECTING:
        SET_SYMBOL_14(wifiicon, FA_CONNECTING);
        lv_obj_remove_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);
        break;
    case WIFI_DISCONNECTED:
        lv_obj_add_flag(wifiicon, LV_OBJ_FLAG_HIDDEN);
        break;
    }

    SET_SYMBOL_16(baticon,
                  watch.battery.charging ? FA_LIGHTNING : FA_BATTERY);
}
