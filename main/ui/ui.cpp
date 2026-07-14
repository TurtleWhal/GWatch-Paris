#include "ui.hpp"

lv_obj_t *main_screen;
lv_obj_t *ver_layer;
lv_obj_t *hor_layer;
lv_obj_t *lower_layer;

// Captured at the end of ui_init so post-boot code (e.g.
// unistroke_register_app) can append entries after BLE has claimed its
// boot-time SRAM allocations.
lv_obj_t *g_appsscreen = nullptr;

lv_obj_t *settings_screen;
lv_obj_t *calculator_screen;
lv_obj_t *schedule_screen;
lv_obj_t *metronome_screen;

// RAM wrappers around the generated `const lv_font_t` fonts in flash.
// LVGL's fallback chain is a per-font field; the generator emits the
// originals into .rodata so we can't write to them — instead we copy
// the structs into RAM at boot and patch in NotoEmoji as the fallback.
// Call sites that render user-supplied text (notifications, music
// metadata) use these wrappers so emoji codepoints fall through to
// NotoEmoji instead of dropping out as missing-glyph boxes.
lv_font_t ProductSansBold_30_emoji;
lv_font_t ProductSansBold_24_emoji;
lv_font_t ProductSansBold_16_emoji;
lv_font_t ProductSansRegular_20_emoji;
lv_font_t ProductSansRegular_16_emoji;
lv_font_t ProductSansRegular_14_emoji;

// Shared accent styles. Every widget that wants the theme primary color
// attaches one of these via lv_obj_add_style instead of capturing the
// color value into a local style. apply_accent_color() mutates the one
// property each style holds and calls lv_obj_report_style_change so
// every attached widget repaints — that's what makes the swatch picker
// in the settings screen update the UI live.
//
// Each lv_style_t holds *one* value per property; widgets that need the
// accent in a non-DEFAULT state (e.g. the quick-settings buttons whose
// CHECKED bg is the accent) attach with the appropriate selector.
lv_style_t accent_bg_style;
lv_style_t accent_text_style;
lv_style_t accent_border_style;
lv_style_t accent_line_style;
lv_style_t accent_arc_indicator_style;

// Current accent as a raw color. Used by a handful of toggle-style call
// sites (alarm AM/PM, hourbox vs minutebox focus border) that pick
// between accent and a neutral gray dynamically — those can't use the
// shared style because they need to swap colors at click time. Kept in
// sync with the styles by apply_accent_color().
lv_color_t g_accent_color;

lv_obj_t *create_screen(lv_obj_t *parent) {
  lv_obj_t *scr = lv_obj_create(parent);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_border_width(scr, 0, 0);

  lv_obj_set_size(scr, 240, 240);
  lv_obj_set_style_radius(scr, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_margin_all(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  // lv_obj_set_style_clip_corner(scr, true, 0); // breaks everything somehow?

  return scr;
}

lv_obj_t *create_valuearc(lv_obj_t *parent, const char *symbol) {
  lv_obj_t *arc = lv_arc_create(parent);
  lv_obj_set_size(arc, 60, 60);

  /* Background arc */
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x333333), 0);
  lv_obj_set_style_arc_width(arc, 8, 0);

  /* Indicator arc — accent color via the shared style so it tracks
     the user's theme selection live. */
  lv_obj_add_style(arc, &accent_arc_indicator_style, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);

  /* Knob invisible */
  lv_obj_set_style_opa(arc, LV_OPA_0, LV_PART_KNOB);

  lv_obj_set_flag(arc, LV_OBJ_FLAG_CLICKABLE, false);

  /* Icon */
  lv_obj_t *icon = lv_label_create(arc);
  SET_SYMBOL_14(icon, symbol);
  lv_obj_align(icon, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_text_color(icon, lv_color_white(), 0);
  lv_obj_set_name(icon, "icon");

  /* Value text */
  lv_obj_t *value = lv_label_create(arc);
  lv_obj_align(value, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_color(value, lv_color_white(), 0);
  lv_obj_set_style_text_font(value, &ProductSansRegular_14, 0);
  lv_obj_set_name(value, "text");

  return arc;
}

/* Scroll event callback for row layout */
static void scroll_loop_event_cb(lv_event_t *e) {
  static bool is_adjusting = false;
  lv_obj_t *cont = lv_event_get_current_target_obj(e);

  if (!is_adjusting) {
    is_adjusting = true;
    int32_t scroll_x = lv_obj_get_scroll_x(cont);
    int32_t cont_w = lv_obj_get_width(cont);
    int32_t content_w = (int32_t)lv_obj_get_child_count(cont) * 240;

    /* Use ITEM_SIZE as horizontal item width */
    const int32_t item_width = 240;

    if (scroll_x <= 0) {
      lv_obj_t *last_child =
          lv_obj_get_child(cont, (int32_t)(lv_obj_get_child_count(cont) - 1));
      lv_obj_move_to_index(last_child, 0);
      lv_obj_scroll_to_x(cont, scroll_x + item_width, LV_ANIM_OFF);
    } else if (scroll_x >= content_w - cont_w) {
      lv_obj_t *first_child = lv_obj_get_child(cont, 0);
      lv_obj_move_to_index(first_child,
                           (int32_t)(lv_obj_get_child_count(cont) - 1));
      lv_obj_scroll_to_x(cont, scroll_x - item_width, LV_ANIM_OFF);
    }
    is_adjusting = false;
  }
}

struct ScrollEventData {
  lv_obj_t *obj;
  lv_dir_t direction;
};

void screen_scroll_highlight_event_cb(lv_event_t *e) {
  ScrollEventData *data = (ScrollEventData *)lv_event_get_user_data(e);

// Quadratic ease-out on the value channel: y = 20 * (1 - ((max-v)/max)^2),
// rewritten as 20 * v * (2*max - v) / (max*max) for integer math. The
// linear ramp it replaced felt too gradual — the highlight glow would
// fade evenly across the whole swipe, washing out before the user
// could see where they were heading. The eased curve holds near peak
// (20) for most of the range and only drops sharply in the last
// quarter, so the highlight reads as "yes, this is the destination"
// for almost the entire swipe and only winks out as you actually
// arrive on the screen.
//
//   v / max        linear (old)    eased (new)
//   ─────────      ─────────       ─────────
//   1.00           20              20
//   0.75           15              17.2
//   0.50           10              15.0
//   0.25            5               8.75
//   0.10            2               3.8
//   0.00            0               0
//
// Worst-case intermediate: 20 * max * max for max=240 is 1.15M — fits
// safely in int32_t.
#define SCROLL_COLOR(v, max)                                                   \
  lv_color_hsv_to_rgb(190, 10, (20 * (v) * (2 * (max) - (v))) / ((max) * (max)))

  switch (data->direction) {
  case LV_DIR_TOP: {
    lv_obj_t *scr = data->obj;

    int32_t pos = lv_obj_get_y(scr);
    int32_t scroll = lv_obj_get_scroll_y(lv_event_get_target_obj(e));
    int32_t height = lv_obj_get_height(scr);

    int32_t v = scroll - pos;
    if (v > 0 && v < height)
      lv_obj_set_style_bg_color(scr, SCROLL_COLOR(v, height), 0);
    else
      lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  } break;
  case LV_DIR_BOTTOM: {
    lv_obj_t *scr = data->obj;

    int32_t pos = lv_obj_get_y(scr); // Position of the screen in its container
    int32_t scroll = lv_obj_get_scroll_y(
        lv_event_get_target_obj(e));         // current scroll of container
    int32_t height = lv_obj_get_height(scr); // height of screen (always 240)

    int32_t v = height - (scroll - (pos - height));
    if (v > 0 && v < height)
      lv_obj_set_style_bg_color(scr, SCROLL_COLOR(v, height), 0);
    else
      lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  } break;
  case LV_DIR_LEFT: {
    lv_obj_t *scr = data->obj;

    int32_t pos = lv_obj_get_x(scr); // Position of the screen in its container
    int32_t scroll = lv_obj_get_scroll_x(
        lv_event_get_target_obj(e));       // current scroll of container
    int32_t width = lv_obj_get_width(scr); // width of screen (always 240)

    int32_t v = scroll - pos;
    if (v > 0 && v < width)
      lv_obj_set_style_bg_color(scr, SCROLL_COLOR(v, width), 0);
    else
      lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  } break;
  case LV_DIR_RIGHT: {
    lv_obj_t *scr = data->obj;

    int32_t pos = lv_obj_get_x(scr); // Position of the screen in its container
    int32_t scroll = lv_obj_get_scroll_x(
        lv_event_get_target_obj(e));       // current scroll of container
    int32_t width = lv_obj_get_width(scr); // width of screen (always 240)

    int32_t v = width - (scroll - (pos - width));
    if (v > 0 && v < width)
      lv_obj_set_style_bg_color(scr, SCROLL_COLOR(v, width), 0);
    else
      lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  } break;
  default:
    break;
  }
}

lv_obj_t *watchscr;  // invisible layer to show watch face
lv_obj_t *watchface; // actual watch face

// Push a new accent color into all shared styles and trigger a repaint
// of every widget that has any of them attached. Safe to call any time
// after ui_init has run (which is what lv_style_init's the structs).
void apply_accent_color(lv_color_t c) {
  g_accent_color = c;
  lv_style_set_bg_color(&accent_bg_style, c);
  lv_style_set_text_color(&accent_text_style, c);
  lv_style_set_border_color(&accent_border_style, c);
  lv_style_set_line_color(&accent_line_style, c);
  lv_style_set_arc_color(&accent_arc_indicator_style, c);

  // NULL → refresh anything that uses any style. Cheaper than walking
  // each style individually since changing the theme color touches
  // every screen anyway, and report-change only marks dirty regions
  // it can prove are stale.
  lv_obj_report_style_change(NULL);
}

char *getbaticon(bool charging, uint8_t percent) {
  if (watch.battery.charging) {
    return FA_CHARGING;
  } else {
    if (watch.battery.percent > 75) {
      return FA_BATTERY_FULL;
    } else if (watch.battery.percent > 50) {
      return FA_BATTERY_75;
    } else if (watch.battery.percent > 25) {
      return FA_BATTERY_50;
    } else if (watch.battery.percent > 10) {
      return FA_BATTERY_25;
    } else if (watch.battery.percent > 5) {
      return FA_BATTERY_10;
    } else {
      return FA_BATTERY_EMPTY;
    }
  }
}

void Display::ui_init() {
  ProductSansBold_30_emoji = ProductSansBold_30;
  ProductSansBold_30_emoji.fallback = &NotoEmojiRegular_20;
  ProductSansBold_24_emoji = ProductSansBold_24;
  ProductSansBold_24_emoji.fallback = &NotoEmojiRegular_20;
  ProductSansRegular_20_emoji = ProductSansRegular_20;
  ProductSansRegular_20_emoji.fallback = &NotoEmojiRegular_20;
  ProductSansRegular_16_emoji = ProductSansRegular_16;
  ProductSansRegular_16_emoji.fallback = &NotoEmojiRegular_16;
  ProductSansBold_16_emoji = ProductSansBold_16;
  ProductSansBold_16_emoji.fallback = &NotoEmojiRegular_16;
  ProductSansRegular_14_emoji = ProductSansRegular_14;
  ProductSansRegular_14_emoji.fallback = &NotoEmojiRegular_16;

  // Primary color comes from the persisted "theme_color" setting; the
  // palette table lives in screens/debug.cpp so the settings screen
  // and the boot path agree on the index→color mapping.
  // https://vuetifyjs.com/en/styles/colors/#material-colors
  uint8_t saved_color = watch.settings.readUint8("theme_color", 0);
  lv_color_t accent = settings_color_at(saved_color);
  lv_theme_t *th = lv_theme_default_init(
      lv_display_get_default(), accent, lv_color_hex(0x607D8B),
      true, /* Dark theme?  False = light theme. */
      &ProductSansRegular_14);

  // Initialise the shared accent styles up-front, before any screens
  // get built. Widgets created during ui_init attach these styles and
  // pick up later color changes automatically via the report-change
  // refresh in apply_accent_color.
  lv_style_init(&accent_bg_style);
  lv_style_init(&accent_text_style);
  lv_style_init(&accent_border_style);
  lv_style_init(&accent_line_style);
  lv_style_init(&accent_arc_indicator_style);
  apply_accent_color(accent);

  lv_display_set_theme(lv_display_get_default(),
                       th); /* Assign theme to display */

  main_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_screen, lv_color_black(), 0);

  lv_obj_set_style_margin_all(main_screen, 0, 0);
  lv_obj_set_style_pad_all(main_screen, 0, 0);

  lv_obj_set_style_border_width(main_screen, 0, 0);

  watchface = watchface_create(main_screen);

  ver_layer = lv_obj_create(main_screen);
  lv_obj_set_size(ver_layer, 240, 240);
  lv_obj_set_style_bg_opa(ver_layer, LV_OPA_0, 0);
  lv_obj_set_style_border_width(ver_layer, 0, 0);

  lv_obj_set_flex_flow(ver_layer, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_snap_y(ver_layer, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_flag(ver_layer, LV_OBJ_FLAG_SCROLL_ELASTIC, false);
  lv_obj_set_scrollbar_mode(ver_layer, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_row(ver_layer, 0, 0);
  lv_obj_set_style_pad_column(ver_layer, 0, 0);
  lv_obj_set_flag(ver_layer, LV_OBJ_FLAG_SCROLL_ONE, true);
  lv_obj_set_flag(ver_layer, LV_OBJ_FLAG_SCROLL_CHAIN, false);

  lv_obj_set_style_margin_all(ver_layer, 0, 0);
  lv_obj_set_style_pad_all(ver_layer, 0, 0);

  lv_obj_t *quicksettings = quicksettings_create(ver_layer);

  static ScrollEventData scroll_dataT = {quicksettings, LV_DIR_TOP};
  lv_obj_add_event_cb(ver_layer, screen_scroll_highlight_event_cb,
                      LV_EVENT_SCROLL, &scroll_dataT);

  hor_layer = lv_obj_create(ver_layer);
  lv_obj_set_size(hor_layer, 240, 240);
  lv_obj_set_style_bg_color(hor_layer, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(hor_layer, LV_OPA_0, 0);
  lv_obj_set_style_border_width(hor_layer, 0, 0);

  lv_obj_set_flex_flow(hor_layer, LV_FLEX_FLOW_ROW);
  lv_obj_set_scroll_snap_x(hor_layer, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_flag(hor_layer, LV_OBJ_FLAG_SCROLL_ELASTIC, false);
  lv_obj_set_scrollbar_mode(hor_layer, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_row(hor_layer, 0, 0);
  lv_obj_set_style_pad_column(hor_layer, 0, 0);
  lv_obj_set_flag(hor_layer, LV_OBJ_FLAG_SCROLL_ONE, true);

  lv_obj_set_style_margin_all(hor_layer, 0, 0);
  lv_obj_set_style_pad_all(hor_layer, 0, 0);

  lv_obj_add_event_cb(hor_layer, scroll_loop_event_cb, LV_EVENT_SCROLL, NULL);

  // lv_obj_t *notifications = notifications_screen_create(ver_layer);

  //   lower_layer = lv_obj_create(ver_layer);
  //   lv_obj_set_size(lower_layer, 240, 240);
  //   lv_obj_set_style_bg_color(lower_layer, lv_color_black(), 0);
  //   lv_obj_set_style_bg_opa(lower_layer, LV_OPA_COVER, 0);
  //   lv_obj_set_style_border_width(lower_layer, 0, 0);
  //   lv_obj_set_style_radius(lower_layer, LV_RADIUS_CIRCLE, 0);

  //   lv_obj_set_flex_flow(lower_layer, LV_FLEX_FLOW_ROW);
  //   lv_obj_set_scroll_snap_x(lower_layer, LV_SCROLL_SNAP_CENTER);
  //   lv_obj_set_flag(lower_layer, LV_OBJ_FLAG_SCROLL_ELASTIC, false);
  //   lv_obj_set_scrollbar_mode(lower_layer, LV_SCROLLBAR_MODE_OFF);
  //   lv_obj_set_style_pad_row(lower_layer, 0, 0);
  //   lv_obj_set_style_pad_column(lower_layer, 0, 0);
  //   lv_obj_set_flag(lower_layer, LV_OBJ_FLAG_SCROLL_ONE, true);

  //   lv_obj_set_style_margin_all(lower_layer, 0, 0);
  //   lv_obj_set_style_pad_all(lower_layer, 0, 0);

  //   lv_obj_add_event_cb(lower_layer, scroll_loop_event_cb, LV_EVENT_SCROLL,
  //   NULL);

  //   lv_obj_t *weather = weather_create(lower_layer);
  //   lv_obj_set_style_bg_opa(weather, 0, 0);

  //   lv_obj_t *notifications = notifications_screen_create(lower_layer);
  //   lv_obj_set_style_bg_opa(notifications, 0, 0);

  //   lv_obj_t *music = music_create(lower_layer);
  //   lv_obj_set_style_bg_opa(music, 0, 0);

  // lv_obj_scroll_to_view(notifications, LV_ANIM_OFF);

  // static ScrollEventData scroll_dataB = {lower_layer, LV_DIR_BOTTOM};
  // lv_obj_add_event_cb(ver_layer, screen_scroll_highlight_event_cb,
  //     LV_EVENT_SCROLL, &scroll_dataB);

  lv_obj_t *notifications = notifications_screen_create(ver_layer);
  lv_obj_add_event_cb(notifications, scroll_loop_event_cb, LV_EVENT_SCROLL,
                      NULL);

  static ScrollEventData scroll_dataB = {notifications, LV_DIR_BOTTOM};
  lv_obj_add_event_cb(ver_layer, screen_scroll_highlight_event_cb,
                      LV_EVENT_SCROLL, &scroll_dataB);

  watchscr = lv_obj_create(hor_layer);
  lv_obj_set_size(watchscr, 240, 240);
  lv_obj_set_style_bg_opa(watchscr, LV_OPA_0, 0);
  lv_obj_set_style_border_width(watchscr, 0, 0);
  lv_obj_set_style_radius(watchscr, 0, 0);

  lv_obj_set_style_margin_all(watchscr, 0, 0);
  lv_obj_set_style_pad_all(watchscr, 0, 0);

  lv_obj_t *stopwatch = stopwatch_create(hor_layer);
  lv_obj_t *timer = timerscr_create(hor_layer);
  lv_obj_t *alarm = alarmscr_create(hor_layer);
  // lv_obj_t *imuscreen = imu_screen_create(hor_layer);
  calculator_screen = calculator_create(NULL);
  lv_obj_t *appsscreen = apps_screen_create(hor_layer);

  lv_obj_t *weather = weather_create(hor_layer);
  // Music screen is NOT created at boot. The visibility tick below
  // creates it on demand when the phone reports state == "play" and
  // tears it back down after 5 min of paused/stopped playback. Until
  // then the LV_DIR_LEFT highlight target is weather; when music is
  // alive, the tick repoints the target at music (it's appended as the
  // last hor_layer child, so it becomes the "wraps around from
  // watchface to the left" screen — same UX slot weather occupies the
  // rest of the time).

  static ScrollEventData scroll_dataR = {stopwatch, LV_DIR_RIGHT};
  lv_obj_add_event_cb(hor_layer, screen_scroll_highlight_event_cb,
                      LV_EVENT_SCROLL, &scroll_dataR);

  static ScrollEventData scroll_dataL = {weather, LV_DIR_LEFT};
  lv_obj_add_event_cb(hor_layer, screen_scroll_highlight_event_cb,
                      LV_EVENT_SCROLL, &scroll_dataL);

  // SCROLL_END handlers force the highlight targets back to true
  // black at rest. The per-frame SCROLL events during motion compute
  // the bg via the eased SCROLL_COLOR formula, but the *final* SCROLL
  // event fired by LVGL during snap doesn't always land exactly at
  // the snap point — wrap-around adjustments via lv_obj_scroll_to_x
  // (LV_ANIM_OFF) can leave the last seen scroll position 10–50 px
  // off, where SCROLL_COLOR still evaluates to a dim gray. SCROLL_END
  // fires once the scroll has actually settled, so wiping the bg
  // here guarantees the rest state is unambiguously black, no matter
  // how the snap settled or whether the final SCROLL event fired.
  static auto force_black_at_rest = [](lv_event_t *e) {
    ScrollEventData *data = (ScrollEventData *)lv_event_get_user_data(e);
    if (data && data->obj)
      lv_obj_set_style_bg_color(data->obj, lv_color_black(), 0);
  };
  lv_obj_add_event_cb(hor_layer, force_black_at_rest,
                      LV_EVENT_SCROLL_END, &scroll_dataR);
  lv_obj_add_event_cb(hor_layer, force_black_at_rest,
                      LV_EVENT_SCROLL_END, &scroll_dataL);

  lv_obj_send_event(hor_layer, LV_EVENT_SCROLL, NULL);
  // After the initial SCROLL pass, also send a SCROLL_END to lock the
  // resting bg colors. lv_obj_send_event(hor_layer, LV_EVENT_SCROLL,
  // NULL) above may run with partial-layout positions; the SCROLL_END
  // pass runs after the lv_obj_scroll_to_view_recursive(watchscr)
  // call at the end of ui_init has settled the layout, ensuring
  // weather/stopwatch start out crisply black on first boot before
  // the user has touched anything.
  lv_obj_send_event(hor_layer, LV_EVENT_SCROLL_END, NULL);

  // Music visibility state. Captured here so the LVGL-timer callback
  // below can find weather (to point the highlight back at) and the
  // scroll-data struct (to flip its target between weather and music).
  // Lives at function scope as a static so multiple ui_init calls
  // would reuse the same state — in practice ui_init runs once.
  static struct MusicVis {
    lv_obj_t *weather;
    lv_obj_t *music;
    ScrollEventData *scroll_data_L;
  } music_vis = {};
  music_vis.weather = weather;
  music_vis.scroll_data_L = &scroll_dataL;

  // 1 s tick. Cheap (one string compare + one int64 subtract most
  // ticks). Runs on the LVGL task so no lvgl_port_lock needed for the
  // create/delete calls. ble.music().state is touched without a lock
  // — same pattern as music_update; the worst race is one missed
  // tick, which the next pass corrects.
  lv_timer_create(
      [](lv_timer_t *) {
        const MusicState &m = ble.music();
        bool playing = (m.state == "play");
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (playing) {
          if (!music_vis.music) {
            // Capture the user's current view BEFORE mutating the
            // row. Inserting music at weather+1 pushes everything to
            // the right of weather by 240 px in the flex layout —
            // if the user happened to be viewing one of those
            // screens, scroll_x would still point to the same
            // numeric position but now show a different (shifted)
            // screen. Saving the scroll position now lets us bump
            // it by +240 after the insert so they stay on the same
            // visible content instead of mysteriously jumping to
            // the music screen (or to the screen one slot to the
            // left of where they were).
            int32_t saved_scroll = lv_obj_get_scroll_x(hor_layer);
            int32_t weather_x_before = lv_obj_get_x(music_vis.weather);

            // music_create appends as the last child. That position
            // is meaningful at boot (weather is last, so music ends
            // up "to the right of weather") but the infinite scroll
            // in scroll_loop_event_cb reshuffles children as the
            // user wraps around — by the time music spawns later
            // in the session, "last child" can be any screen, which
            // dumps music in a random slot. Force music to sit
            // immediately after weather regardless of how the row
            // has rotated since boot.
            music_vis.music = music_create(hor_layer);
            int32_t weather_idx = lv_obj_get_index(music_vis.weather);
            lv_obj_move_to_index(music_vis.music, weather_idx + 1);

            // Restore the user's view. Only screens at indices past
            // weather were shifted; if scroll_x was <= weather's x
            // the user was already on weather or to its left and
            // nothing needs to move.
            if (saved_scroll > weather_x_before)
              lv_obj_scroll_to_x(hor_layer, saved_scroll + 240, LV_ANIM_OFF);

            music_vis.scroll_data_L->obj = music_vis.music;
            // Repaint highlights immediately with the new target.
            lv_obj_send_event(hor_layer, LV_EVENT_SCROLL, NULL);
          }
          return;
        }

        // Not playing. Tear down once the phone has been silent on the
        // music channel for >= 5 min AND the user isn't currently
        // looking at the music screen. We key off ble.music().last_msg_ms
        // (stamped on every musicstate/musicinfo arrival) rather than
        // pause time — phones often keep pushing musicstate updates
        // while paused (volume, scrub, etc.), and as long as those
        // keep arriving the user clearly still cares about media. The
        // screen retires only when the music app has gone fully
        // dormant on the phone for the full window.
        if (!music_vis.music)
          return;
        constexpr int64_t IDLE_TIMEOUT_MS = 5LL * 60 * 1000;
        if (now_ms - m.last_msg_ms < IDLE_TIMEOUT_MS)
          return;

        int32_t mx = lv_obj_get_x(music_vis.music) -
                     lv_obj_get_scroll_x(hor_layer);
        bool on_music = (mx > -120 && mx < 120);
        if (on_music)
          return;

        music_destroy();
        music_vis.music = nullptr;
        music_vis.scroll_data_L->obj = music_vis.weather;
        lv_obj_send_event(hor_layer, LV_EVENT_SCROLL, NULL);
      },
      1000, NULL);

  // lv_obj_t *appsscreen = apps_screen_create(ver_layer);

  // static ScrollEventData scroll_dataB = {appsscreen, LV_DIR_BOTTOM};
  // lv_obj_add_event_cb(ver_layer, screen_scroll_highlight_event_cb,
  // LV_EVENT_SCROLL, &scroll_dataB);

  // Whenever EITHER axis scrolls, recompute whether ver_layer is
  // allowed to scroll. Rule:
  //   - If the user isn't centered on the hor_layer row (i.e.
  //     they're on the upper quicksettings or the lower row)
  //     → always allow vertical scroll, so they can escape.
  //   - If they ARE on the hor_layer row → only allow vertical
  //     scroll when hor_layer is EXACTLY centered on the watch
  //     face. Mid-snap settling (off-by-N pixels toward an
  //     adjacent screen) keeps vertical locked so you can't
  //     accidentally jump off-row during a horizontal flick.
  static auto update_ver_scroll = []() {
    if (!ver_layer || !hor_layer || !watchscr)
      return;
    int32_t vy = lv_obj_get_scroll_y(ver_layer);
    int32_t hy = lv_obj_get_y(hor_layer);
    bool on_hor_row = (vy == hy);

    int32_t hx = lv_obj_get_scroll_x(hor_layer);
    int32_t wx = lv_obj_get_x(watchscr);
    bool on_watchface = (hx == wx);

    bool allow = !on_hor_row || on_watchface;
    lv_obj_set_scroll_dir(ver_layer, allow ? LV_DIR_VER : LV_DIR_NONE);
  };

  lv_obj_add_event_cb(
      hor_layer,
      [](lv_event_t *e) {
        lv_obj_t *scr = (lv_obj_t *)lv_event_get_user_data(e);
        int32_t scroll = lv_obj_get_scroll_x(lv_event_get_target_obj(e));
        int32_t pos = lv_obj_get_x(scr);

        // Watchface bg visibility: hidden when fully off, transparent
        // background; otherwise opaque so the watch face shows through.
        if (scroll - 240 < pos && scroll + 240 > pos) {
          lv_obj_set_style_bg_opa(lv_event_get_target_obj(e), LV_OPA_0, 0);
          lv_obj_set_flag(watchface, LV_OBJ_FLAG_HIDDEN, false);
        } else {
          lv_obj_set_flag(watchface, LV_OBJ_FLAG_HIDDEN, true);
          lv_obj_set_style_bg_opa(lv_event_get_target_obj(e), LV_OPA_COVER, 0);
        }

        update_ver_scroll();
      },
      LV_EVENT_SCROLL, watchscr);

  // Also reevaluate on every ver_layer scroll event so moving onto
  // the upper / lower row re-enables vertical immediately, without
  // having to wait for the next hor_layer scroll.
  lv_obj_add_event_cb(
      ver_layer, [](lv_event_t *) { update_ver_scroll(); }, LV_EVENT_SCROLL,
      NULL);

  create_app(appsscreen, FA_STOPWATCH, "Stopwatch", stopwatch);
  create_app(appsscreen, FA_TIMER, "Timer", timer);
  create_app(appsscreen, FA_ALARM, "Alarm", alarm);
  create_app(appsscreen, FA_CALCULATOR, "Calculator", calculator_screen, true);

  // Unistroke's app entry is registered post-BLE-init from watch.cpp.
  // Even just adding the button widget here was enough to push LVGL's
  // internal-SRAM use past the threshold the BT controller's malloc
  // needs at boot. unistroke_register_app() runs after ble.init() has
  // claimed its memory, and the screen widgets behind the button are
  // lazy-built on first tap (no boot cost at all).
  g_appsscreen = appsscreen;

  // create_app(appsscreen, FA_FLASHLIGHT, "Flashlight", [](lv_event_t *)
  //            {
  //                static uint16_t flashlight_prev;

  //                static lv_obj_t *flashlight_screen = lv_obj_create(NULL);
  //                lv_obj_set_style_bg_color(flashlight_screen,
  //                lv_color_white(), 0);

  //                flashlight_prev = watch.display.get_brightness();

  //                lv_screen_load_anim(flashlight_screen,
  //                LV_SCREEN_LOAD_ANIM_FADE_IN, 100, 0, false);

  //                watch.display.set_backlight(100);

  //                lv_obj_add_event_cb(flashlight_screen, [](lv_event_t *e)
  //                                    {
  //                                        watch.display.set_backlight(*(uint16_t
  //                                        *)lv_event_get_user_data(e));
  //                                        lv_screen_load_anim(main_screen,
  //                                        LV_SCREEN_LOAD_ANIM_FADE_OUT, 100,
  //                                        0, false);
  //                                    },
  //                                    LV_EVENT_CLICKED, &flashlight_prev); });

  // create_app(appsscreen, FA_IMU, "IMU", imuscreen);
  create_app(appsscreen, FA_IMU, "IMU", imu_screen_create(NULL), true);

  schedule_screen = schedule_screen_create(NULL);
  create_app(appsscreen, FA_CALENDAR, "Schedule", schedule_screen, true);

  create_app(appsscreen, FA_DICE, "Dice", dice_create(NULL), true);

  metronome_screen = metronome_create(NULL);
  create_app(appsscreen, FA_METRONOME, "Metronome", metronome_screen, true);

  settings_screen = settingsscreen_create();
  create_app(appsscreen, FA_SETTINGS, "Settings", settings_screen, true);

  lv_screen_load(main_screen);

  lv_obj_scroll_to_view_recursive(watchscr, LV_ANIM_OFF);
  // lv_obj_scroll_to_view_recursive(weather, LV_ANIM_OFF);
}