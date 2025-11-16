#include "ui.hpp"

lv_obj_t *h1, *h2, *m1, *m2, *s1, *s2;

// lv_obj_t *stopwatchlbl;
// int64_t stopwatchstarttime = 0;
// bool stopwatchrunning = false;

// void update_stopwatch_label()
// {
//     // all in microseconds
//     int64_t current = esp_timer_get_time();
//     int64_t diff = current - stopwatchstarttime;

//     if (stopwatchstarttime == 0)
//     {
//         diff = 0;
//     }
//     else if (!stopwatchrunning)
//     {
//         diff = stopwatchstarttime;
//     }

//     int64_t h = diff / 1000 / 1000 / 60 / 60;
//     int64_t m = diff / 1000 / 1000 / 60;
//     int64_t s = diff / 1000 / 1000;
//     int64_t ms = diff / 1000;

//     if (h) // if h is not 0, display it
//         lv_label_set_text_fmt(stopwatchlbl, "%02lli:%02lli:%02lli", h, m % 60, s % 60);
//     else
//         lv_label_set_text_fmt(stopwatchlbl, "%02lli:%02lli.%02lli", m % 60, s % 60, ((ms % 1000) / 10) % 100);
// }

// void stopwatch_update(lv_timer_t *timer)
// {
//     lv_obj_t *scr = (lv_obj_t *)lv_timer_get_user_data(timer);
//     lv_obj_t *parent = lv_obj_get_parent(scr);

//     if (lv_obj_get_scroll_x(parent) > lv_obj_get_x(scr) - 240 && lv_obj_get_scroll_x(parent) < lv_obj_get_x(scr) + 240)
//     {
//         if (stopwatchrunning)
//         {
//             update_stopwatch_label();
//         }
//     }
// }

lv_obj_t *timerscr_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);

    // lv_obj_t *scrlbl = lv_label_create(scr);
    // lv_label_set_text(scrlbl, "Stopwatch");
    // lv_obj_set_style_text_font(scrlbl, &ProductSansRegular_20, 0);
    // lv_obj_set_style_text_color(scrlbl, lv_color_white(), 0);
    // lv_obj_align(scrlbl, LV_ALIGN_TOP_MID, 0, 10);

    // stopwatchlbl = lv_label_create(scr);
    // lv_obj_set_style_text_font(stopwatchlbl, &GoogleSansCode_46, 0);
    // lv_label_set_text(stopwatchlbl, "00:00.00");
    // lv_obj_set_style_text_color(stopwatchlbl, lv_color_white(), 0);

    // lv_obj_align(stopwatchlbl, LV_ALIGN_CENTER, 0, -20);

    static lv_style_t style;
    lv_style_init(&style);

    lv_style_set_text_font(&style, &GoogleSansCode_46);
    lv_style_set_border_width(&style, 0);

    // Create all rollers
    // Create colins first so that they are in the back
    lv_obj_t *c1 = lv_roller_create(scr);
    lv_roller_set_options(c1, ":", LV_ROLLER_MODE_NORMAL);
    lv_obj_add_style(c1, &style, 0);

    lv_obj_t *c2 = lv_roller_create(scr);
    lv_roller_set_options(c2, ":", LV_ROLLER_MODE_NORMAL);
    lv_obj_add_style(c2, &style, 0);

    h1 = lv_roller_create(scr);
    lv_roller_set_options(h1, "0\n1\n2\n3\n4\n5\n6\n7\n8\n9", LV_ROLLER_MODE_INFINITE);
    lv_obj_add_style(h1, &style, 0);

    h2 = lv_roller_create(scr);
    lv_roller_set_options(h2, "0\n1\n2\n3\n4\n5\n6\n7\n8\n9", LV_ROLLER_MODE_INFINITE);
    lv_obj_add_style(h2, &style, 0);

    m1 = lv_roller_create(scr);
    lv_roller_set_options(m1, "0\n1\n2\n3\n4\n5", LV_ROLLER_MODE_INFINITE);
    lv_obj_add_style(m1, &style, 0);

    m2 = lv_roller_create(scr);
    lv_roller_set_options(m2, "0\n1\n2\n3\n4\n5\n6\n7\n8\n9", LV_ROLLER_MODE_INFINITE);
    lv_obj_add_style(m2, &style, 0);

    s1 = lv_roller_create(scr);
    lv_roller_set_options(s1, "0\n1\n2\n3\n4\n5", LV_ROLLER_MODE_INFINITE);
    lv_obj_add_style(s1, &style, 0);

    s2 = lv_roller_create(scr);
    lv_roller_set_options(s2, "0\n1\n2\n3\n4\n5\n6\n7\n8\n9", LV_ROLLER_MODE_INFINITE);
    lv_obj_add_style(s2, &style, 0);

    lv_obj_set_style_radius(s2, 4, LV_PART_INDICATOR);
    lv_obj_set_style_radius(h1, 4, LV_PART_INDICATOR);

    // Size all Roller
    lv_obj_set_size(h1, 30, 50);
    lv_obj_set_size(h2, 30, 50);
    lv_obj_set_size(c1, 30, 50);
    lv_obj_set_size(m1, 30, 50);
    lv_obj_set_size(m2, 30, 50);
    lv_obj_set_size(c2, 30, 50);
    lv_obj_set_size(s1, 30, 50);
    lv_obj_set_size(s2, 30, 50);

#define SPACING 30

    // Position all Rollers
    lv_obj_align(h1, LV_ALIGN_CENTER, 0 - 85, -40);
    lv_obj_align(h2, LV_ALIGN_CENTER, 0 - 55, -40);
    lv_obj_align(c1, LV_ALIGN_CENTER, 0 - 35, -40);
    lv_obj_align(m1, LV_ALIGN_CENTER, 0 - 15, -40);
    lv_obj_align(m2, LV_ALIGN_CENTER, 0 + 15, -40);
    lv_obj_align(c2, LV_ALIGN_CENTER, 0 + 35, -40);
    lv_obj_align(s1, LV_ALIGN_CENTER, 0 + 55, -40);
    lv_obj_align(s2, LV_ALIGN_CENTER, 0 + 85, -40);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 120, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, -34, 50);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *playicon = lv_label_create(btn);
    SET_SYMBOL_32(playicon, FA_PLAY);
    lv_obj_center(playicon);

    lv_obj_t *reset = lv_button_create(scr);
    lv_obj_set_size(reset, 60, 60);
    lv_obj_align(reset, LV_ALIGN_CENTER, 64, 50);
    lv_obj_set_style_radius(reset, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(reset, lv_color_hex(0x333333), 0);

    lv_obj_t *reseticon = lv_label_create(reset);
    SET_SYMBOL_32(reseticon, FA_ROTATE);
    lv_obj_center(reseticon);

    // lv_obj_add_event_cb(btn, [](lv_event_t *e)
    //                     {
    //                         if (stopwatchrunning)
    //                         {
    //                             int64_t current = esp_timer_get_time();
    //                             int64_t diff = current - stopwatchstarttime;
    //                             stopwatchstarttime = diff;
    //                             SET_SYMBOL_32((lv_obj_t*)lv_event_get_user_data(e), FA_PLAY);
    //                         }
    //                         else
    //                         {
    //                             if (stopwatchstarttime == 0)
    //                             {
    //                                 stopwatchstarttime = esp_timer_get_time();
    //                             }
    //                             else
    //                             {
    //                                 stopwatchstarttime = esp_timer_get_time() - stopwatchstarttime;
    //                             }
    //                             SET_SYMBOL_32((lv_obj_t*)lv_event_get_user_data(e), FA_PAUSE);
    //                         }
    //                         stopwatchrunning = !stopwatchrunning;
    //                         update_stopwatch_label(); }, LV_EVENT_CLICKED, playicon);

    // lv_obj_add_event_cb(reset, [](lv_event_t *e)
    //                     {
    //                             stopwatchrunning = false;
    //                             stopwatchstarttime = 0;
    //                             update_stopwatch_label();
    //                             SET_SYMBOL_32((lv_obj_t*)lv_event_get_user_data(e), FA_PLAY); }, LV_EVENT_CLICKED, playicon);

    lv_obj_add_event_cb(btn, [](lv_event_t *e)
                        { haptic_play(false, 80, 0); }, LV_EVENT_PRESSED, NULL);

    lv_obj_add_event_cb(reset, [](lv_event_t *e)
                        { haptic_play(false, 80, 0); }, LV_EVENT_PRESSED, NULL);

    // lv_timer_create(stopwatch_update, 33, scr); // 30 times per second (not an even number so that milliseconds do not alternate)

    return scr;
}