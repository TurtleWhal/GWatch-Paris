#include "ui.hpp"

#include <cstring>
#include <strings.h>  // strcasecmp

// Theme color palette. Config.json stores the currently-selected colour
// as a hex string like "#03a9f4" under settings.themecolor; the swatch
// index and the hex form are two views of the same table. Order is also
// the order the swatches appear on the settings screen. Kept here so
// ui_init can call settings_color_at(idx) without duplicating the
// table.
static const lv_color_t SETTINGS_PALETTE[] = {
    LV_COLOR_MAKE(0x03, 0xA9, 0xF4), // Blue (default)
    LV_COLOR_MAKE(0xF4, 0x43, 0x36), // Red
    LV_COLOR_MAKE(0xFF, 0x50, 0x00), // Orange
    LV_COLOR_MAKE(0xFF, 0x98, 0x00), // Gold
    LV_COLOR_MAKE(0x4C, 0xAF, 0x50), // Green
    LV_COLOR_MAKE(0x00, 0x96, 0x88), // Turquoise
    LV_COLOR_MAKE(0xE0, 0x40, 0xFB), // Purple
    // LV_COLOR_MAKE(0xFF, 0xEB, 0x3B), // Yellow
    // LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), // White
};
static const char *const SETTINGS_PALETTE_HEX[] = {
    "#03A9F4",
    "#F44336",
    "#FF5000",
    "#FF9800",
    "#4CAF50",
    "#009688",
    "#E040FB",
};
static constexpr uint8_t SETTINGS_PALETTE_N =
    sizeof(SETTINGS_PALETTE) / sizeof(SETTINGS_PALETTE[0]);
static_assert(sizeof(SETTINGS_PALETTE_HEX) / sizeof(SETTINGS_PALETTE_HEX[0])
                  == SETTINGS_PALETTE_N,
              "palette hex table must match SETTINGS_PALETTE length");

uint8_t settings_color_count() { return SETTINGS_PALETTE_N; }
lv_color_t settings_color_at(uint8_t idx) {
  if (idx >= SETTINGS_PALETTE_N)
    idx = 0;
  return SETTINGS_PALETTE[idx];
}
const char *settings_color_hex_at(uint8_t idx) {
  if (idx >= SETTINGS_PALETTE_N)
    idx = 0;
  return SETTINGS_PALETTE_HEX[idx];
}
// Case-insensitive so a user-edited config with lowercase like
// "#03a9f4" still round-trips. Falls back to 0 if the hex doesn't
// match any palette entry (e.g. an old / unknown colour).
uint8_t settings_color_idx_from_hex(const char *hex) {
  if (!hex) return 0;
  for (uint8_t i = 0; i < SETTINGS_PALETTE_N; i++) {
    if (strcasecmp(hex, SETTINGS_PALETTE_HEX[i]) == 0)
      return i;
  }
  return 0;
}

lv_obj_t *create_setting(lv_obj_t *parent, const char *name, bool state,
                         lv_event_cb_t event_cb) {
  lv_obj_t *setting = lv_button_create(parent);
  lv_obj_set_size(setting, 200, 44);
  lv_obj_set_style_bg_color(setting, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(setting, LV_RADIUS_CIRCLE, 0);

  lv_obj_t *label = lv_label_create(setting);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
  lv_label_set_text(label, name);
  lv_obj_set_style_text_font(label, &ProductSansRegular_16, 0);

  lv_obj_t *sw = lv_switch_create(setting);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 6, 0);

  lv_obj_set_state(sw, LV_STATE_CHECKED, state);

  if (event_cb != nullptr) {
    lv_obj_add_event_cb(sw, event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(
        setting,
        [](lv_event_t *e) {
          lv_obj_set_state(
              (lv_obj_t *)lv_event_get_user_data(e), LV_STATE_CHECKED,
              !(lv_obj_get_state((lv_obj_t *)lv_event_get_user_data(e)) &
                LV_STATE_CHECKED));
          lv_obj_send_event((lv_obj_t *)lv_event_get_user_data(e),
                            LV_EVENT_CLICKED, NULL);
        },
        LV_EVENT_CLICKED, sw);

    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
  } else {
    lv_obj_set_style_opa(setting, LV_OPA_50, 0);
    lv_obj_set_flag(setting, LV_OBJ_FLAG_CLICKABLE, false);
  }

  return setting;
}

// -------------------- Theme color row --------------------

// Per-swatch context. The button's user_data points at one of these.
struct ColorSwatchCtx {
  uint8_t idx;
  lv_obj_t *check; // checkmark label, hidden unless this idx is active
};

// Re-apply the theme with a new primary color. The shared accent_*_style
// instances in ui.cpp do the live propagation — every widget that calls
// lv_obj_add_style on one of them repaints when apply_accent_color
// reports the style change. lv_theme_default_init is called again as
// well so any future widget creation that consults lv_theme_get_color_*
// sees the new primary; existing widgets that captured the old color
// into a private style won't update without going through the shared
// style or g_accent_color.
static void apply_theme_color(uint8_t idx) {
  lv_color_t primary = settings_color_at(idx);
  lv_theme_t *th = lv_theme_default_init(lv_display_get_default(), primary,
                                         lv_color_hex(0x607D8B), true,
                                         &ProductSansRegular_14);
  lv_display_set_theme(lv_display_get_default(), th);
  apply_accent_color(primary);
}

static lv_obj_t *build_color_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  // Wrap + content-sized height so adding more swatches just spills onto
  // a new line instead of overflowing the round screen's edges.
  lv_obj_set_size(row, 220, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 4, 0);
  lv_obj_set_style_pad_row(row, 4, 0);
  lv_obj_set_scroll_dir(row, LV_DIR_NONE);

  // Read the persisted colour as a hex string from config.json and
  // map it back to a palette index for the UI. Unknown hex → index 0.
  std::string cur_hex = watch.settings.readString("settings", "themecolor",
                                                  settings_color_hex_at(0));
  uint8_t current = settings_color_idx_from_hex(cur_hex.c_str());

  for (uint8_t i = 0; i < settings_color_count(); i++) {
    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_size(btn, 22, 22);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, settings_color_at(i), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(
        btn, i == current ? lv_color_white() : lv_color_hex(0x222222), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    // Stash the swatch index in the LVGL user_data. The click handler
    // walks every sibling to clear the previously-active border, so
    // it doesn't need a back-pointer to the per-row context.
    lv_obj_set_user_data(btn, (void *)(uintptr_t)i);

    lv_obj_add_event_cb(
        btn,
        [](lv_event_t *e) {
          lv_obj_t *clicked = lv_event_get_target_obj(e);
          uint8_t picked = (uint8_t)(uintptr_t)lv_obj_get_user_data(clicked);
          lv_obj_t *r = lv_obj_get_parent(clicked);
          for (uint32_t k = 0; k < lv_obj_get_child_count(r); k++) {
            lv_obj_t *sib = lv_obj_get_child(r, k);
            uint8_t si = (uint8_t)(uintptr_t)lv_obj_get_user_data(sib);
            lv_obj_set_style_border_color(
                sib, si == picked ? lv_color_white() : lv_color_hex(0x222222),
                0);
          }
          watch.settings.writeString("settings", "themecolor",
                                     settings_color_hex_at(picked));
          apply_theme_color(picked);
          haptic_play(false, 30, 0);
        },
        LV_EVENT_CLICKED, NULL);
  }

  return row;
}

// -------------------- Rotation row --------------------

// Apply a rotation and persist. Shared by the settings-screen buttons
// below and the quick-settings rotate icon. LVGL's rotation enum
// (0..3) maps directly to (0, 90, 180, 270) degrees — write the
// user-facing degree value into config.json so the file stays
// intuitive to hand-edit.
void settings_apply_rotation(lv_display_rotation_t rot) {
  watch.display.set_rotation(rot);
  watch.settings.writeInt("settings", "rotation", (int)rot * 90);
}

static lv_obj_t *build_rotation_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 220, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_set_style_pad_row(row, 4, 0);
  lv_obj_set_scroll_dir(row, LV_DIR_NONE);

  static const char *labels[4] = {"0", "90", "180", "270"};
  uint8_t current = (uint8_t)lv_display_get_rotation(NULL);

  for (uint8_t i = 0; i < 4; i++) {
    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_size(btn, 40, 30);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
    // Selected button gets the accent bg via the shared style under
    // the CHECKED state, so swapping the theme color live updates
    // whichever rotation button is currently selected.
    lv_obj_add_style(btn, &accent_bg_style, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_pad_all(btn, 0, 0);
    if (i == current)
      lv_obj_add_state(btn, LV_STATE_CHECKED);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_center(lbl);
    lv_label_set_text(lbl, labels[i]);
    lv_obj_set_style_text_font(lbl, &ProductSansRegular_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);

    lv_obj_set_user_data(btn, (void *)(uintptr_t)i);

    lv_obj_add_event_cb(
        btn,
        [](lv_event_t *e) {
          lv_obj_t *clicked = lv_event_get_target_obj(e);
          uint8_t picked = (uint8_t)(uintptr_t)lv_obj_get_user_data(clicked);
          lv_obj_t *r = lv_obj_get_parent(clicked);
          for (uint32_t k = 0; k < lv_obj_get_child_count(r); k++) {
            lv_obj_t *sib = lv_obj_get_child(r, k);
            uint8_t si = (uint8_t)(uintptr_t)lv_obj_get_user_data(sib);
            if (si == picked)
              lv_obj_add_state(sib, LV_STATE_CHECKED);
            else
              lv_obj_remove_state(sib, LV_STATE_CHECKED);
          }
          settings_apply_rotation((lv_display_rotation_t)picked);
          haptic_play(false, 30, 0);
        },
        LV_EVENT_CLICKED, NULL);
  }

  return row;
}

// -------------------- Watch face row --------------------

static lv_obj_t *build_watchface_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 220, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_set_style_pad_row(row, 4, 0);
  lv_obj_set_scroll_dir(row, LV_DIR_NONE);

  uint8_t current = watchface_active_idx();
  uint8_t count = watchface_count();

  for (uint8_t i = 0; i < count; i++) {
    lv_obj_t *btn = lv_button_create(row);
    // 66 wide fits 3 pills + 2×6 gap = 210, leaves 5 px margin in the
    // 220-wide row. With ROW_WRAP, 4+ pills spill onto a second line.
    lv_obj_set_size(btn, 66, 30);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
    lv_obj_add_style(btn, &accent_bg_style, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_pad_all(btn, 0, 0);
    if (i == current)
      lv_obj_add_state(btn, LV_STATE_CHECKED);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_center(lbl);
    lv_label_set_text(lbl, watchface_name_at(i));
    lv_obj_set_style_text_font(lbl, &ProductSansRegular_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);

    lv_obj_set_user_data(btn, (void *)(uintptr_t)i);

    lv_obj_add_event_cb(
        btn,
        [](lv_event_t *e) {
          lv_obj_t *clicked = lv_event_get_target_obj(e);
          uint8_t picked = (uint8_t)(uintptr_t)lv_obj_get_user_data(clicked);
          lv_obj_t *r = lv_obj_get_parent(clicked);
          for (uint32_t k = 0; k < lv_obj_get_child_count(r); k++) {
            lv_obj_t *sib = lv_obj_get_child(r, k);
            uint8_t si = (uint8_t)(uintptr_t)lv_obj_get_user_data(sib);
            if (si == picked)
              lv_obj_add_state(sib, LV_STATE_CHECKED);
            else
              lv_obj_remove_state(sib, LV_STATE_CHECKED);
          }
          watchface_set_active(picked);
          haptic_play(false, 30, 0);
        },
        LV_EVENT_CLICKED, NULL);
  }

  return row;
}

// -------------------- Section header --------------------

static lv_obj_t *section_label(lv_obj_t *parent, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &ProductSansRegular_14, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0x999999), 0);
  lv_obj_set_style_pad_top(l, 6, 0);
  return l;
}

// -------------------- Uptime / build labels --------------------

static lv_obj_t *uptime;

static void settings_update(lv_timer_t *timer) {
  if (lv_screen_active() == lv_timer_get_user_data(timer)) {
    uint64_t t = esp_timer_get_time();
    uint64_t ms = t / 1000;
    uint64_t s = ms / 1000;
    uint64_t m = s / 60;
    uint64_t h = m / 60;
    uint64_t d = h / 24;

    if (d > 0)
      lv_label_set_text_fmt(uptime, "Uptime: %lld Days %02lld:%02lld:%02lld", d,
                            h % 24, m % 60, s % 60);
    else
      lv_label_set_text_fmt(uptime, "Uptime: %02lld:%02lld:%02lld", h % 24,
                            m % 60, s % 60);
  }
}

lv_obj_t *settingsscreen_create() {
  lv_obj_t *scr = create_screen(NULL);

  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_flex_track_place(scr, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_set_style_flex_cross_place(scr, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

  // Inset the column ends so the first/last row don't get clipped by
  // the round panel — top entry sits below the curve, bottom entry
  // above it. The rest of the rows are scrollable in between.
  lv_obj_set_style_pad_top(scr, 50, 0);
  lv_obj_set_style_pad_bottom(scr, 50, 0);
  lv_obj_set_style_pad_row(scr, 4, 0);

  lv_obj_t *scrlbl = lv_label_create(scr);
  lv_label_set_text(scrlbl, "Settings");
  lv_obj_set_style_text_font(scrlbl, &ProductSansRegular_20, 0);
  lv_obj_set_style_text_color(scrlbl, lv_color_white(), 0);
  lv_obj_set_style_pad_bottom(scrlbl, 8, 0);

  section_label(scr, "Watch Face");
  build_watchface_row(scr);

  section_label(scr, "Theme Color");
  build_color_row(scr);

  section_label(scr, "Tilt to Wake");
  // Both persisted (unlike Disable Sleep, which is deliberately a
  // per-session override): tilt behaviour is a lasting preference.
  create_setting(scr, "Tilt to Wake", watch.system.tiltwake, [](lv_event_t *e) {
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    watch.system.tiltwake = on;
    watch.settings.writeBool("settings", "tiltwake", on);
  });
  create_setting(
      scr, "Vibrate on Wake", watch.system.tiltwake_buzz, [](lv_event_t *e) {
        bool on =
            lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
        watch.system.tiltwake_buzz = on;
        watch.settings.writeBool("settings", "vibrateontilt", on);
      });

  section_label(scr, "Rotation");
  build_rotation_row(scr);

  section_label(scr, "Power");
  create_setting(
      scr, "Disable Sleep", !watch.system.dosleep, [](lv_event_t *e) {
        watch.system.dosleep =
            !lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
      });

  lv_obj_set_style_text_align(scr, LV_TEXT_ALIGN_CENTER, 0);

  uptime = lv_label_create(scr);
  lv_obj_set_style_text_font(uptime, &ProductSansRegular_14, 0);
  lv_obj_set_style_text_color(uptime, lv_color_hex(0xaaaaaa), 0);
  lv_obj_set_style_pad_top(uptime, 8, 0);

  lv_obj_t *build = lv_label_create(scr);
  lv_label_set_text_fmt(build, "Firmware Compiled on\n%s at %s", __DATE__,
                        __TIME__);
  lv_obj_set_style_text_font(build, &ProductSansRegular_14, 0);
  lv_obj_set_style_text_color(build, lv_color_hex(0x888888), 0);

  lv_timer_create(settings_update, 1000, scr);

  return scr;
}
