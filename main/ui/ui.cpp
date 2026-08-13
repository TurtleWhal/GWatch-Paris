#include "ui.hpp"

// For lv_obj_scroll_by_raw, used by the infinite-scroll wrap below. The
// public lv_obj_scroll_to/by(..., LV_ANIM_OFF) can't be used there: see
// the comment on scroll_shift_raw.
#include "lvgl_private.h"

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
lv_obj_t *homeassistant_screen;
lv_obj_t *units_screen;

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

// Text font with FontAwesome fallback so FA_* glyphs can sit inline in a
// normal text string (see ui.hpp).
lv_font_t ProductSansRegular_16_fa;
lv_font_t ProductSansRegular_24_fa;

lv_font_t SquadaOneRegular_18_mdi;

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

// True while a pointer is actively dragging `obj`, as opposed to a scroll
// animation coasting it after release.
static bool indev_is_dragging(lv_obj_t *obj) {
  for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i))
    if (lv_indev_get_scroll_obj(i) == obj)
      return true;
  return false;
}

// Shift `cont`'s scroll position by `delta` px silently. The public
// lv_obj_scroll_to/by(..., LV_ANIM_OFF) is NOT silent: it lv_anim_delete()s
// any running scroll animation — killing a thrown snap mid-flight and firing
// the anim's deleted_cb (a spurious LV_EVENT_SCROLL_END) — and its ANIM_OFF
// path then sends SCROLL_BEGIN + SCROLL_END unconditionally. Those
// mid-motion SCROLL_ENDs made force_black_at_rest repaint the highlight
// target black between the SCROLL-event grays: the black/gray flicker seen
// while a fast flick settled across the wrap point. scroll_by_raw fires one
// SCROLL event and touches nothing else. Sign: positive x in scroll_by_raw
// DECREASES scroll_x, hence the negation.
static void scroll_shift_raw(lv_obj_t *cont, int32_t delta) {
  lv_obj_scroll_by_raw(cont, -delta, 0);
}

static void scroll_loop_event_cb(lv_event_t *e) {
  static bool is_adjusting = false;
  lv_obj_t *cont = lv_event_get_current_target_obj(e);

  // While a snap/throw animation is coasting, leave the row alone — the
  // animation drives ABSOLUTE scroll positions, so a ±240 wrap shift here
  // would be undone on its next frame (re-triggering the wrap, forever).
  // Nothing is lost by waiting: with SCROLL_ONE the anim target lands
  // exactly on the wrap boundary, so the wrap is only actually needed once
  // the scroll settles — this callback is registered for SCROLL_END too,
  // which is where the deferred wrap then runs. Finger drags (elastic-off
  // pins the scroll at the edge until the row is rotated) still wrap
  // immediately.
  if (lv_obj_is_scrolling(cont) && !indev_is_dragging(cont))
    return;

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
      // Settle the reshuffled children's coords before shifting so the
      // SCROLL event fired by the shift computes highlight colors from
      // real positions instead of stale pre-layout ones.
      lv_obj_update_layout(cont);
      scroll_shift_raw(cont, item_width);
    } else if (scroll_x >= content_w - cont_w) {
      lv_obj_t *first_child = lv_obj_get_child(cont, 0);
      lv_obj_move_to_index(first_child,
                           (int32_t)(lv_obj_get_child_count(cont) - 1));
      lv_obj_update_layout(cont);
      scroll_shift_raw(cont, -item_width);
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

  ProductSansRegular_16_fa = ProductSansRegular_16;
  ProductSansRegular_16_fa.fallback = &FontAwesome_16;
  ProductSansRegular_24_fa = ProductSansRegular_24;
  ProductSansRegular_24_fa.fallback = &FontAwesome_24;

  SquadaOneRegular_18_mdi = SquadaOneRegular_18;
  SquadaOneRegular_18_mdi.fallback = &MaterialDesignIcons_16;

  // Primary colour comes from settings.themecolor in config.json.
  // Parsed as an arbitrary hex string, not looked up in the palette,
  // so a picker-chosen (or hand-edited) colour survives boot too.
  // https://vuetifyjs.com/en/styles/colors/#material-colors
  std::string saved_hex = watch.settings.readString(
      "settings", "themecolor", settings_color_hex_at(0));
  lv_color_t accent = settings_color_from_hex(saved_hex.c_str());
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
  // SCROLL_END too: the wrap is deferred while a snap animation is coasting
  // (see scroll_loop_event_cb) and runs here once the scroll has settled.
  // Registered BEFORE the force_black_at_rest handlers below so the row is
  // already rotated when they wipe the resting bg.
  lv_obj_add_event_cb(hor_layer, scroll_loop_event_cb, LV_EVENT_SCROLL_END,
                      NULL);

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

  // Rest-state internal scroll positions.
  //   - Quicksettings should always open scrolled to the BOTTOM.
  //   - Notifications should always open scrolled to the TOP.
  // Resetting these whenever the user scrolls AWAY on ver_layer means
  // they can never come back to a mid-list state. Runs on SCROLL_END
  // (after the snap animation has settled) so the reset happens on
  // the OTHER page while the user is centered on their target — the
  // scroll they just left invisibly rewinds before the next visit.
  static struct VerPages {
    lv_obj_t *quicksettings;
    lv_obj_t *notifications;
  } ver_pages = {quicksettings, notifications};

  lv_obj_add_event_cb(
      ver_layer,
      [](lv_event_t *e) {
        lv_obj_t *layer = lv_event_get_current_target_obj(e);
        // Skip while a snap is still coasting — SCROLL_END also fires
        // when a scroll anim is canceled mid-flight (user grabs the
        // screen). Real rest-state means nothing is moving.
        if (lv_obj_is_scrolling(layer))
          return;

        int32_t scroll_y = lv_obj_get_scroll_y(layer);
        int32_t qs_y     = lv_obj_get_y(ver_pages.quicksettings);
        int32_t notif_y  = lv_obj_get_y(ver_pages.notifications);

        // Reset quicksettings to the bottom unless the user is
        // currently viewing it. Snap on ver_layer lands exactly at
        // the child's y so equality is a safe check. INT32_MAX gets
        // clamped by LVGL to the widget's max scroll position — no
        // need to compute it here.
        if (scroll_y != qs_y)
          lv_obj_scroll_to_y(ver_pages.quicksettings, INT32_MAX, LV_ANIM_OFF);

        // Reset notifications to the top unless it's the active page.
        if (scroll_y != notif_y)
          lv_obj_scroll_to_y(ver_pages.notifications, 0, LV_ANIM_OFF);
      },
      LV_EVENT_SCROLL_END, NULL);

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

  // Neither weather nor music are created at boot. Both are created on
  // demand by the visibility tick below:
  //   - weather when ble.weather().version > 0 (the phone has pushed at
  //     least one weather packet), kept for the rest of the session.
  //   - music while state=="play", torn down after 5 min silence.
  // Priority for the LV_DIR_LEFT wrap-target (the "first screen to the
  // left of the watch face" when the user swipes right past it):
  //   music if alive > weather if alive > apps as fallback
  // The layout is kept in that same priority order — music is the last
  // hor_layer child when it exists, then weather, then apps — so the
  // wrap-around scroll physically lands on the same screen the
  // highlight is showing.

  static ScrollEventData scroll_dataR = {stopwatch, LV_DIR_RIGHT};
  lv_obj_add_event_cb(hor_layer, screen_scroll_highlight_event_cb,
                      LV_EVENT_SCROLL, &scroll_dataR);

  // Highlight defaults to appsscreen — the natural last child before
  // weather / music show up. The tick below repoints it as those
  // screens come and go.
  static ScrollEventData scroll_dataL = {appsscreen, LV_DIR_LEFT};
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
    // SCROLL_END is NOT only "the scroll settled": LVGL also fires it
    // whenever a scroll animation is deleted mid-flight (e.g. the user
    // grabs the screen while a snap is still coasting). Painting black on
    // those would flash against the grays the SCROLL events keep
    // computing — only treat SCROLL_END as rest once nothing is moving.
    if (lv_obj_is_scrolling(lv_event_get_current_target_obj(e)))
      return;
    ScrollEventData *data = (ScrollEventData *)lv_event_get_user_data(e);
    if (data && data->obj)
      lv_obj_set_style_bg_color(data->obj, lv_color_black(), 0);
  };
  lv_obj_add_event_cb(hor_layer, force_black_at_rest, LV_EVENT_SCROLL_END,
                      &scroll_dataR);
  lv_obj_add_event_cb(hor_layer, force_black_at_rest, LV_EVENT_SCROLL_END,
                      &scroll_dataL);

  lv_obj_send_event(hor_layer, LV_EVENT_SCROLL, NULL);
  // After the initial SCROLL pass, also send a SCROLL_END to lock the
  // resting bg colors. lv_obj_send_event(hor_layer, LV_EVENT_SCROLL,
  // NULL) above may run with partial-layout positions; the SCROLL_END
  // pass runs after the lv_obj_scroll_to_view_recursive(watchscr)
  // call at the end of ui_init has settled the layout, ensuring
  // weather/stopwatch start out crisply black on first boot before
  // the user has touched anything.
  lv_obj_send_event(hor_layer, LV_EVENT_SCROLL_END, NULL);

  // Dynamic-screen visibility state. Both weather and music are
  // created / destroyed on demand by the 1 s tick below.
  //   - appsscreen is always alive and acts as the fallback screen
  //     immediately-left-of-watchface when neither weather nor music
  //     is present.
  //   - weather is created the first time ble.weather().version > 0.
  //     Kept for the rest of the session (weather data doesn't really
  //     "stop"; unlike music there's no natural expiry).
  //   - music is created while state=="play", destroyed after 5 min of
  //     no music-channel traffic AND the user isn't currently viewing it.
  //
  // Desired circular order (right-swipe from watchface):
  //   watchface → music → weather → apps → alarm → timer → stopwatch
  //
  // Layout is maintained in that order by inserting each new dynamic
  // screen IMMEDIATELY LEFT of watchface in the flex row (at
  // watchscr's current index) — that lands it in the exact slot the
  // wrap-around scroll physically reveals when the user swipes right
  // from the watchface, regardless of how the infinite-scroll callback
  // has rotated children since boot. When both music and weather are
  // alive, weather is inserted at MUSIC's index (music shifts +1)
  // so the final order stays [..., weather, music, watchscr, ...].
  static struct ScreenVis {
    lv_obj_t *appsscreen;
    lv_obj_t *weather;
    lv_obj_t *music;
    ScrollEventData *scroll_data_L;
  } screen_vis = {};
  screen_vis.appsscreen = appsscreen;
  screen_vis.scroll_data_L = &scroll_dataL;

  // 1 s tick. Cheap (a couple of int compares + one string compare on
  // typical passes). Runs on the LVGL task so no lvgl_port_lock is
  // needed for the create/delete calls, and ble.music()/ble.weather()
  // are touched without a lock — same pattern as music_update /
  // weather_update; the worst race is one missed tick, which the next
  // pass corrects.
  lv_timer_create(
      [](lv_timer_t *) {
        const MusicState &m = ble.music();
        bool playing = (m.state == "play");
        int64_t now_ms = esp_timer_get_time() / 1000;

        // Helper: repoint the DIR_LEFT highlight to the highest-
        // priority screen currently alive and repaint immediately so
        // the highlight follows the new target on the next frame.
        //
        // Also unconditionally wipes the apps + weather bgs back to
        // black. The SCROLL handler that paints the highlight
        // gradient can leave one of these two screens frozen with a
        // mid-gradient gray if the previous highlight target got
        // swapped out mid-transition: an in-flight SCROLL callback
        // set apps' (or weather's) bg to a highlight tint, then
        // scroll_data_L->obj flips before the next SCROLL fires, so
        // the old target's else-branch never runs to clear it.
        // Wiping both to black on every music/weather add-or-remove
        // guarantees a clean rest state — cheap since
        // lv_obj_set_style_bg_color is a no-op when the color hasn't
        // changed.
        auto refresh_wrap_target = []() {
          lv_obj_t *target = screen_vis.music     ? screen_vis.music
                             : screen_vis.weather ? screen_vis.weather
                                                  : screen_vis.appsscreen;
          if (screen_vis.scroll_data_L->obj != target) {
            screen_vis.scroll_data_L->obj = target;
            lv_obj_send_event(hor_layer, LV_EVENT_SCROLL, NULL);
          }
          if (screen_vis.appsscreen)
            lv_obj_set_style_bg_color(screen_vis.appsscreen,
                                      lv_color_black(), 0);
          if (screen_vis.weather)
            lv_obj_set_style_bg_color(screen_vis.weather,
                                      lv_color_black(), 0);
        };

        // Helper: after lv_obj_create appended new_screen at the end,
        // move it to sit immediately BEFORE `anchor` in the flex row.
        // Everything at anchor's index and beyond shifts right by 240.
        // If the user's viewport was showing any of those shifted
        // widgets, bump scroll_x by +240 so the visible screen doesn't
        // jump. Boundary case: viewing exactly the anchor (scroll_x ==
        // anchor_x) counts as "past the insertion point" — the anchor
        // itself shifts +240 so we need the shift to stay on it.
        auto insert_before = [](lv_obj_t *new_screen, lv_obj_t *anchor) {
          int32_t target_idx = lv_obj_get_index(anchor);
          int32_t target_x = target_idx * 240;
          int32_t saved_scroll = lv_obj_get_scroll_x(hor_layer);
          lv_obj_move_to_index(new_screen, target_idx);
          if (saved_scroll >= target_x)
            lv_obj_scroll_to_x(hor_layer, saved_scroll + 240, LV_ANIM_OFF);
        };

        // -- Create the weather screen once the phone has pushed data.
        if (!screen_vis.weather && ble.weather().version > 0) {
          screen_vis.weather = weather_create(hor_layer);
          // Insert weather at (music's index if music exists, else
          // watchscr's index) so the circular order stays
          //   [..., weather, music, watchscr, ...]  or
          //   [..., weather, watchscr, ...] when music is absent.
          lv_obj_t *anchor = screen_vis.music ? screen_vis.music : watchscr;
          insert_before(screen_vis.weather, anchor);
          refresh_wrap_target();
        }

        // -- Create the music screen when playback starts. Anchor is
        // always watchscr — music must be watchscr's immediate left
        // neighbour to be "the one right-swipe away".
        if (playing && !screen_vis.music) {
          screen_vis.music = music_create(hor_layer);
          insert_before(screen_vis.music, watchscr);
          refresh_wrap_target();
        }

        // -- Retire music after a long silence on the music channel.
        // Keyed off last_msg_ms (stamped on every musicstate /
        // musicinfo arrival) rather than pause time, so the screen
        // sticks around while the phone is still updating volume /
        // scrub position — it only dies once the media app has gone
        // fully dormant. Never yank the screen out while the user is
        // looking at it.
        if (!playing && screen_vis.music) {
          constexpr int64_t IDLE_TIMEOUT_MS = 5LL * 60 * 1000;
          if (now_ms - m.last_msg_ms >= IDLE_TIMEOUT_MS) {
            int32_t mx =
                lv_obj_get_x(screen_vis.music) - lv_obj_get_scroll_x(hor_layer);
            bool on_music = (mx > -120 && mx < 120);
            if (!on_music) {
              music_destroy();
              screen_vis.music = nullptr;
              refresh_wrap_target();
            }
          }
        }
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

  // Watchface visibility: hidden unless watchscr is at least partly
  // on-screen. Two axes have to line up for that:
  //   - hor_layer's scroll is within ±240 px of watchscr's x
  //   - ver_layer's scroll is within ±240 px of hor_layer's y
  // Any full transition off either axis (onto quicksettings /
  // notifications / a horizontally-adjacent screen) hides it, so the
  // watchface's per-tick draw work stops and its widgets stop
  // rendering underneath the opaque overlay pages. Called from both
  // hor and ver scroll handlers because either axis can change what
  // the user is looking at.
  static auto update_watchface_vis = []() {
    if (!ver_layer || !hor_layer || !watchscr || !watchface)
      return;
    int32_t hy = lv_obj_get_y(hor_layer);
    int32_t vy = lv_obj_get_scroll_y(ver_layer);
    bool ver_shows = (vy > hy - 240 && vy < hy + 240);

    int32_t hx = lv_obj_get_scroll_x(hor_layer);
    int32_t wx = lv_obj_get_x(watchscr);
    bool hor_shows = (hx > wx - 240 && hx < wx + 240);

    lv_obj_set_flag(watchface, LV_OBJ_FLAG_HIDDEN,
                    !(ver_shows && hor_shows));
  };

  lv_obj_add_event_cb(
      hor_layer,
      [](lv_event_t *e) {
        lv_obj_t *scr = (lv_obj_t *)lv_event_get_user_data(e);
        int32_t scroll = lv_obj_get_scroll_x(lv_event_get_target_obj(e));
        int32_t pos = lv_obj_get_x(scr);

        // hor_layer's own bg toggles between transparent (letting the
        // watchface show through) and opaque black (clipping any
        // watchface bleed) purely from the horizontal distance to
        // watchscr — that's this layer's own concern.
        if (scroll - 240 < pos && scroll + 240 > pos)
          lv_obj_set_style_bg_opa(lv_event_get_target_obj(e), LV_OPA_0, 0);
        else
          lv_obj_set_style_bg_opa(lv_event_get_target_obj(e), LV_OPA_COVER, 0);

        update_watchface_vis();
        update_ver_scroll();
      },
      LV_EVENT_SCROLL, watchscr);

  // Also reevaluate on every ver_layer scroll event so moving onto
  // the upper / lower row re-enables vertical immediately (and hides
  // the watchface immediately), without having to wait for the next
  // hor_layer scroll.
  lv_obj_add_event_cb(
      ver_layer,
      [](lv_event_t *) {
        update_watchface_vis();
        update_ver_scroll();
      },
      LV_EVENT_SCROLL, NULL);

  create_app(appsscreen, FA_STOPWATCH, "Stopwatch", stopwatch);
  create_app(appsscreen, FA_TIMER, "Timer", timer);
  create_app(appsscreen, FA_ALARM, "Alarm", alarm);
  create_app(appsscreen, FA_CALCULATOR, "Calculator", calculator_screen, true);

  units_screen = units_create(NULL);
  create_app(appsscreen, FA_RULER, "Unit Conversion", units_screen,
             true);

  // Unistroke's app entry is registered post-BLE-init from watch.cpp.
  // Even just adding the button widget here was enough to push LVGL's
  // internal-SRAM use past the threshold the BT controller's malloc
  // needs at boot. unistroke_register_app() runs after ble.init() has
  // claimed its memory, and the screen widgets behind the button are
  // lazy-built on first tap (no boot cost at all).
  g_appsscreen = appsscreen;

  create_app(appsscreen, FA_IMU, "IMU", imu_screen_create(NULL), true);

  schedule_screen = schedule_screen_create(NULL);
  create_app(appsscreen, FA_CALENDAR, "Schedule", schedule_screen, true);

  homeassistant_screen = homeassistant_create(NULL);
  lv_obj_t *haapp = create_app(appsscreen, FA_SMARTHOME, "Home Assistant",
                               homeassistant_screen, true);
  SET_MDI_SYMBOL_22(lv_obj_get_child(haapp, 0), MDI_HOME_ASSISTANT);

  create_app(appsscreen, FA_DICE, "Dice", dice_create(NULL), true);

  metronome_screen = metronome_create(NULL);
  create_app(appsscreen, FA_METRONOME, "Metronome", metronome_screen, true);

  settings_screen = settingsscreen_create();
  create_app(appsscreen, FA_SETTINGS, "Settings", settings_screen, true);

  lv_screen_load(main_screen);

  lv_obj_scroll_to_view_recursive(watchscr, LV_ANIM_OFF);

  // lv_screen_load_anim(units_screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 100, 0, false);
}