#include "ui.hpp"

lv_obj_t *h1, *h2, *m1, *m2, *s1, *s2;

// int64_t watch.chrono.timertime = 0;
// bool watch.chrono.timerrunning = false;

TaskHandle_t taskhandle;

lv_obj_t *playicon;

lv_obj_t *alarmscr;

void update_timer_label()
{
    int64_t sec = watch.chrono.timertime / 1000000;

    int64_t h = sec / 3600;
    int64_t m = (sec % 3600) / 60;
    int64_t s = sec % 60;

    if (lv_roller_get_selected(s2) != s % 10)
        lv_roller_set_selected(s2, s % 10, LV_ANIM_ON);

    if (lv_roller_get_selected(s1) != s / 10)
        lv_roller_set_selected(s1, s / 10, LV_ANIM_ON);

    if (lv_roller_get_selected(m2) != m % 10)
        lv_roller_set_selected(m2, m % 10, LV_ANIM_ON);

    if (lv_roller_get_selected(m1) != m / 10)
        lv_roller_set_selected(m1, m / 10, LV_ANIM_ON);

    if (lv_roller_get_selected(h2) != h % 10)
        lv_roller_set_selected(h2, h % 10, LV_ANIM_ON);

    if (lv_roller_get_selected(h1) != h / 10)
        lv_roller_set_selected(h1, h / 10, LV_ANIM_ON);
}

void read_rollers(lv_event_t *)
{
    int h = (lv_roller_get_selected(h1) * 10) + lv_roller_get_selected(h2);
    int m = (lv_roller_get_selected(m1) * 10) + lv_roller_get_selected(m2);
    int s = (lv_roller_get_selected(s1) * 10) + lv_roller_get_selected(s2);

    watch.chrono.timertime = ((((h * 60) + m) * 60) + s) * 1000ll * 1000ll + (watch.chrono.timertime % (1000ll * 1000ll));
}

void timer_update(lv_timer_t *timer)
{
    lv_obj_t *scr = (lv_obj_t *)lv_timer_get_user_data(timer);
    lv_obj_t *parent = lv_obj_get_parent(scr);

    if (watch.chrono.timerrunning && watch.chrono.timertime == 0)
    {
        watch.chrono.timerrunning = false;
        SET_SYMBOL_32(playicon, FA_PLAY);

        lv_screen_load_anim(alarmscr, LV_SCREEN_LOAD_ANIM_FADE_IN, 100, 0, false);

        haptic_play(true, 800, 800, 800, 800, 800, 2160, 0);
    }

    if (lv_obj_get_scroll_x(parent) > lv_obj_get_x(scr) - 240 && lv_obj_get_scroll_x(parent) < lv_obj_get_x(scr) + 240)
    {
        if (watch.chrono.timerrunning)
        {
            update_timer_label();
        }
    }
}

void timer_task(void *arg)
{
    int64_t lasttime = esp_timer_get_time();

    while (true)
    {
        int64_t now = esp_timer_get_time();
        int64_t diff = now - lasttime;
        lasttime = now;

        if (watch.chrono.timerrunning)
        {
            watch.chrono.timertime -= diff;

            // ESP_LOGI("timer", "%lld", watch.chrono.timertime);

            if (watch.chrono.timertime <= 0)
            {
                watch.chrono.timertime = 0;
                watch.wakeup();
            }
        }

        vTaskDelay(100);
    }
}

lv_obj_t *timerscr_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);

    // lv_obj_t *scrlbl = lv_label_create(scr);
    // lv_label_set_text(scrlbl, "Timer");
    // lv_obj_set_style_text_font(scrlbl, &ProductSansRegular_20, 0);
    // lv_obj_set_style_text_color(scrlbl, lv_color_white(), 0);
    // lv_obj_align(scrlbl, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *c = lv_obj_create(scr);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_size(c, 200, 50);

    lv_obj_set_scroll_dir(c, LV_DIR_NONE);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_clip_corner(c, true, 0);
    lv_obj_set_style_radius(c, 8, 0);

    static lv_style_t style;
    lv_style_init(&style);

    lv_style_set_text_font(&style, &GoogleSansCode_46);
    lv_style_set_border_width(&style, 0);

    // Create all rollers
    // Create colins first so that they are in the back
    lv_obj_t *c1 = lv_roller_create(c);
    lv_roller_set_options(c1, ":", LV_ROLLER_MODE_NORMAL);
    lv_obj_add_style(c1, &style, 0);

    lv_obj_t *c2 = lv_roller_create(c);
    lv_roller_set_options(c2, ":", LV_ROLLER_MODE_NORMAL);
    lv_obj_add_style(c2, &style, 0);

    h1 = lv_roller_create(c);
    lv_roller_set_options(h1, "0\n1\n2\n3\n4\n5\n6\n7\n8\n9", LV_ROLLER_MODE_INFINITE);
    lv_obj_add_style(h1, &style, 0);

    h2 = lv_roller_create(c);
    lv_roller_set_options(h2, "0\n1\n2\n3\n4\n5\n6\n7\n8\n9", LV_ROLLER_MODE_INFINITE);
    lv_obj_add_style(h2, &style, 0);

    m1 = lv_roller_create(c);
    lv_roller_set_options(m1, "0\n1\n2\n3\n4\n5", LV_ROLLER_MODE_INFINITE);
    lv_obj_add_style(m1, &style, 0);

    m2 = lv_roller_create(c);
    lv_roller_set_options(m2, "0\n1\n2\n3\n4\n5\n6\n7\n8\n9", LV_ROLLER_MODE_INFINITE);
    lv_obj_add_style(m2, &style, 0);

    s1 = lv_roller_create(c);
    lv_roller_set_options(s1, "0\n1\n2\n3\n4\n5", LV_ROLLER_MODE_INFINITE);
    lv_obj_add_style(s1, &style, 0);

    s2 = lv_roller_create(c);
    lv_roller_set_options(s2, "0\n1\n2\n3\n4\n5\n6\n7\n8\n9", LV_ROLLER_MODE_INFINITE);
    lv_obj_add_style(s2, &style, 0);

    // Size all Roller
    lv_obj_set_size(h1, 30, 50);
    lv_obj_set_size(h2, 30, 50);
    lv_obj_set_size(c1, 30, 50);
    lv_obj_set_size(m1, 30, 50);
    lv_obj_set_size(m2, 30, 50);
    lv_obj_set_size(c2, 30, 50);
    lv_obj_set_size(s1, 30, 50);
    lv_obj_set_size(s2, 30, 50);

    // Position all Rollers
    lv_obj_align(h1, LV_ALIGN_CENTER, 0 - 85, 0);
    lv_obj_align(h2, LV_ALIGN_CENTER, 0 - 55, 0);
    lv_obj_align(c1, LV_ALIGN_CENTER, 0 - 35, 0);
    lv_obj_align(m1, LV_ALIGN_CENTER, 0 - 15, 0);
    lv_obj_align(m2, LV_ALIGN_CENTER, 0 + 15, 0);
    lv_obj_align(c2, LV_ALIGN_CENTER, 0 + 35, 0);
    lv_obj_align(s1, LV_ALIGN_CENTER, 0 + 55, 0);
    lv_obj_align(s2, LV_ALIGN_CENTER, 0 + 85, 0);

    lv_obj_add_event_cb(h1, read_rollers, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(h2, read_rollers, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(m1, read_rollers, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(m2, read_rollers, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s1, read_rollers, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s2, read_rollers, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 120, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, -34, 50);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);

    playicon = lv_label_create(btn);
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

    lv_obj_add_event_cb(btn, [](lv_event_t *e)
                        {
                            if (watch.chrono.timerrunning)
                            {
                                SET_SYMBOL_32((lv_obj_t*)lv_event_get_user_data(e), FA_PLAY);
                            }
                            else
                            {
                                SET_SYMBOL_32((lv_obj_t*)lv_event_get_user_data(e), FA_PAUSE);
                            }
                            watch.chrono.timerrunning = !watch.chrono.timerrunning;
                            update_timer_label(); }, LV_EVENT_CLICKED, playicon);

    lv_obj_add_event_cb(reset, [](lv_event_t *e)
                        {
                                watch.chrono.timerrunning = false;
                                watch.chrono.timertime = 0;
                                update_timer_label();
                                SET_SYMBOL_32((lv_obj_t*)lv_event_get_user_data(e), FA_PLAY); }, LV_EVENT_CLICKED, playicon);

    lv_obj_add_event_cb(btn, [](lv_event_t *e)
                        { haptic_play(false, 80, 0); }, LV_EVENT_PRESSED, NULL);

    lv_obj_add_event_cb(reset, [](lv_event_t *e)
                        { haptic_play(false, 80, 0); }, LV_EVENT_PRESSED, NULL);

    alarmscr = lv_obj_create(NULL);

    lv_obj_t *lbl = lv_label_create(alarmscr);
    lv_obj_set_style_text_font(lbl, &ProductSansBold_42, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -30);
    lv_label_set_text(lbl, "Timer");

    lv_obj_t *stopbtn = lv_button_create(alarmscr);
    lv_obj_align(stopbtn, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_radius(stopbtn, LV_RADIUS_CIRCLE, 0);

    lv_obj_set_style_pad_ver(stopbtn, 20, 0);
    lv_obj_set_style_pad_hor(stopbtn, 40, 0);

    lv_obj_t *btnlbl = lv_label_create(stopbtn);
    lv_obj_set_style_text_font(btnlbl, &ProductSansBold_36, 0);
    lv_label_set_text(btnlbl, "Stop");

    lv_obj_add_event_cb(stopbtn, [](lv_event_t *e)
                        {
                                haptic_stop();
                                update_timer_label();
                                lv_screen_load_anim(main_screen, LV_SCREEN_LOAD_ANIM_FADE_OUT, 100, 0, false); }, LV_EVENT_PRESSED, NULL);

    lv_timer_create(timer_update, 33, scr);

    xTaskCreate(timer_task, "timer_task", 1024 * 3, NULL, 2, &taskhandle);

    return scr;
}