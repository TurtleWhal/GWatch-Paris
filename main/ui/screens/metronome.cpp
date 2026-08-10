#include "ui.hpp"

#define MIN_BPM 30
#define MAX_BPM 240
#define DEFAULT_BPM 60

#define FLASH_MS 100

static int bpm = DEFAULT_BPM;
static bool running = false;

// Scheduled monotonic tick time. Advanced by interval_us on each tick
// regardless of when the timer actually fired — so the timer fires late,
// the next tick still lands at the original cadence and the metronome
// doesn't drift.
static int64_t next_tick_us = 0;
static int64_t flash_off_us = 0;

static lv_obj_t *flash_overlay = nullptr;
static lv_obj_t *bpm_label = nullptr;
static lv_obj_t *play_icon = nullptr;
static lv_obj_t *metronome_scr = nullptr;

static int64_t interval_us() { return 60000000LL / bpm; }

static void update_bpm_label() { lv_label_set_text_fmt(bpm_label, "%d", bpm); }

static void schedule_first_tick()
{
    // Fire immediately on play, then advance by interval thereafter.
    next_tick_us = esp_timer_get_time();
}

static void set_play_icon()
{
    SET_SYMBOL_32(play_icon, running ? FA_PAUSE : FA_PLAY);
}

static void stop_metronome()
{
    if (!running) return;
    running = false;
    set_play_icon();
    lv_obj_add_flag(flash_overlay, LV_OBJ_FLAG_HIDDEN);
    flash_off_us = 0;
}

static void start_metronome()
{
    if (running) return;
    running = true;
    schedule_first_tick();
    set_play_icon();
}

void metronome_update(lv_timer_t *timer)
{
    int64_t now = esp_timer_get_time();

    if (flash_off_us != 0 && now >= flash_off_us)
    {
        lv_obj_add_flag(flash_overlay, LV_OBJ_FLAG_HIDDEN);
        flash_off_us = 0;
    }

    if (!running) return;

    // Auto-stop if the user navigated away from the metronome screen —
    // otherwise the haptic keeps buzzing on whatever screen they swiped to.
    if (lv_screen_active() != metronome_scr)
    {
        stop_metronome();
        return;
    }

    if (now < next_tick_us) return;

    int64_t step = interval_us();
    next_tick_us += step;
    // If the timer has fallen behind by more than one interval (e.g. the
    // BPM was just raised, or LVGL stalled), snap forward so we don't fire
    // a burst of catch-up ticks.
    if (next_tick_us < now) next_tick_us = now + step;

    haptic_play(false, 40, 0);

    lv_obj_remove_flag(flash_overlay, LV_OBJ_FLAG_HIDDEN);
    flash_off_us = now + FLASH_MS * 1000;

    // Roll the no-sleep deadline forward — without this the pm task puts
    // the watch to sleep after the normal idle timeout even while ticking.
    watch.prevent_sleep_until_ms = now / 1000 + 10000;
}

static void on_bpm_delta(int delta)
{
    int next = bpm + delta;
    if (next < MIN_BPM) next = MIN_BPM;
    if (next > MAX_BPM) next = MAX_BPM;
    if (next == bpm) return;
    bpm = next;
    update_bpm_label();
    // Re-anchor next tick to "now + new interval" so the rate change feels
    // immediate instead of waiting out the old interval.
    if (running) next_tick_us = esp_timer_get_time() + interval_us();
}

lv_obj_t *metronome_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    metronome_scr = scr;

    flash_overlay = lv_obj_create(scr);
    lv_obj_set_size(flash_overlay, 240, 240);
    lv_obj_center(flash_overlay);
    lv_obj_set_style_radius(flash_overlay, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(flash_overlay, 0, 0);
    lv_obj_set_style_bg_color(flash_overlay, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(flash_overlay, LV_OPA_30, 0);
    lv_obj_remove_flag(flash_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(flash_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(flash_overlay, LV_OBJ_FLAG_HIDDEN);

    bpm_label = lv_label_create(scr);
    lv_obj_set_style_text_font(bpm_label, &ProductSansBold_76, 0);
    lv_obj_set_style_text_color(bpm_label, lv_color_white(), 0);
    lv_obj_align(bpm_label, LV_ALIGN_CENTER, 0, -20);
    update_bpm_label();

    lv_obj_t *bpm_sub = lv_label_create(scr);
    lv_obj_set_style_text_font(bpm_sub, &ProductSansRegular_16, 0);
    lv_obj_set_style_text_color(bpm_sub, lv_color_hex(0x888888), 0);
    lv_label_set_text(bpm_sub, "BPM");
    lv_obj_align(bpm_sub, LV_ALIGN_CENTER, 0, 26);

    lv_obj_t *minus = lv_button_create(scr);
    lv_obj_set_size(minus, 44, 44);
    lv_obj_align(minus, LV_ALIGN_LEFT_MID, 16, -10);
    lv_obj_set_style_radius(minus, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(minus, lv_color_hex(0x333333), 0);
    lv_obj_t *minus_lbl = lv_label_create(minus);
    lv_obj_set_style_text_font(minus_lbl, &ProductSansBold_36, 0);
    lv_label_set_text(minus_lbl, "−");
    lv_obj_center(minus_lbl);
    lv_obj_add_event_cb(minus, [](lv_event_t *) { on_bpm_delta(-1); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(minus, [](lv_event_t *) { on_bpm_delta(-5); }, LV_EVENT_LONG_PRESSED_REPEAT, NULL);
    lv_obj_add_event_cb(minus, [](lv_event_t *) { haptic_play(false, 25, 0); }, LV_EVENT_PRESSED, NULL);

    lv_obj_t *plus = lv_button_create(scr);
    lv_obj_set_size(plus, 44, 44);
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, -16, -10);
    lv_obj_set_style_radius(plus, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(plus, lv_color_hex(0x333333), 0);
    lv_obj_t *plus_lbl = lv_label_create(plus);
    lv_obj_set_style_text_font(plus_lbl, &ProductSansBold_36, 0);
    lv_label_set_text(plus_lbl, "+");
    lv_obj_center(plus_lbl);
    lv_obj_add_event_cb(plus, [](lv_event_t *) { on_bpm_delta(1); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(plus, [](lv_event_t *) { on_bpm_delta(5); }, LV_EVENT_LONG_PRESSED_REPEAT, NULL);
    lv_obj_add_event_cb(plus, [](lv_event_t *) { haptic_play(false, 25, 0); }, LV_EVENT_PRESSED, NULL);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 100, 48);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -32);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);

    play_icon = lv_label_create(btn);
    SET_SYMBOL_32(play_icon, FA_PLAY);
    lv_obj_center(play_icon);

    lv_obj_add_event_cb(btn, [](lv_event_t *) {
        if (running) stop_metronome(); else start_metronome();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn, [](lv_event_t *) { haptic_play(false, 40, 0); }, LV_EVENT_PRESSED, NULL);

    // 10 ms gives us ±10 ms tick jitter at the worst (≤4% of a 240 BPM
    // interval), which is well under the perception threshold for a
    // metronome. The next-tick scheduling in metronome_update ensures
    // jitter doesn't accumulate into drift.
    lv_timer_create(metronome_update, 10, scr);

    return scr;
}
