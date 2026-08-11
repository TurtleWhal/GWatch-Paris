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
// "#03a9f4" still round-trips. Returns UINT8_MAX when the hex isn't
// in the palette — used by the swatch row to leave every palette
// entry un-highlighted when a picker-chosen custom colour is active.
uint8_t settings_color_idx_from_hex(const char *hex) {
  if (!hex) return UINT8_MAX;
  for (uint8_t i = 0; i < SETTINGS_PALETTE_N; i++) {
    if (strcasecmp(hex, SETTINGS_PALETTE_HEX[i]) == 0)
      return i;
  }
  return UINT8_MAX;
}

lv_color_t settings_color_from_hex(const char *hex) {
  if (!hex) return SETTINGS_PALETTE[0];
  if (*hex == '#') hex++;
  // Any non-6-hex-char input is treated as malformed. Length is
  // cheaper to check first so we short-circuit on things like ""
  // or "cornflowerblue" before running the per-char decoder.
  size_t n = 0;
  while (hex[n] && n <= 6) n++;
  if (n != 6) return SETTINGS_PALETTE[0];
  uint32_t v = 0;
  for (int i = 0; i < 6; i++) {
    char c = hex[i];
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return SETTINGS_PALETTE[0];
    v = (v << 4) | (uint32_t)d;
  }
  return lv_color_hex(v);
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

// Re-apply the theme with an arbitrary primary color. The shared
// accent_*_style instances in ui.cpp do the live propagation — every
// widget that calls lv_obj_add_style on one of them repaints when
// apply_accent_color reports the style change. lv_theme_default_init is
// called again as well so any future widget creation that consults
// lv_theme_get_color_* sees the new primary; existing widgets that
// captured the old color into a private style won't update without
// going through the shared style or g_accent_color.
//
// Passing hex non-null persists it. The picker calls with a computed
// "#RRGGBB"; palette-swatch clicks pass their static table entry.
static void apply_theme_color(lv_color_t primary, const char *hex) {
  lv_theme_t *th = lv_theme_default_init(lv_display_get_default(), primary,
                                         lv_color_hex(0x607D8B), true,
                                         &ProductSansRegular_14);
  lv_display_set_theme(lv_display_get_default(), th);
  apply_accent_color(primary);
  if (hex)
    watch.settings.writeString("settings", "themecolor", hex);
}

// Custom-swatch marker stored in an object's user_data alongside the
// palette-index integers. UINT8_MAX is never a real palette index (see
// SETTINGS_PALETTE_N bound), so overloading it here is unambiguous —
// the click handler branches on it to open the picker.
static constexpr uintptr_t CUSTOM_SWATCH_TAG = 0xFF;

// Set on first build. The color picker's "Save" path calls back into
// refresh_color_row(hex) to re-run the per-swatch border logic; without
// that pointer we'd need to re-lookup the row every time.
static lv_obj_t *g_color_row = nullptr;
static lv_obj_t *g_custom_swatch = nullptr;

// Update swatch borders + the custom-swatch fill to reflect a new
// active color. Called both when the picker commits a custom colour
// and when a palette swatch is clicked. Idempotent + cheap — no
// re-layout, just style writes on the existing children.
static void refresh_color_row(const char *cur_hex) {
  if (!g_color_row) return;
  uint8_t match = settings_color_idx_from_hex(cur_hex);
  for (uint8_t i = 0; i < settings_color_count(); i++) {
    lv_obj_t *sw = lv_obj_get_child(g_color_row, i);
    lv_obj_set_style_border_color(
        sw, (i == match) ? lv_color_white() : lv_color_hex(0x222222), 0);
  }
  if (g_custom_swatch) {
    bool is_custom = (match == UINT8_MAX);
    lv_color_t cur = settings_color_from_hex(cur_hex);
    // When a palette entry is active, dim the custom swatch to a
    // neutral grey — otherwise the picker button reads as "another
    // selectable swatch" and confuses the highlight semantics.
    lv_obj_set_style_bg_color(
        g_custom_swatch,
        is_custom ? cur : lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_color(
        g_custom_swatch,
        is_custom ? lv_color_white() : lv_color_hex(0x222222), 0);
  }
}

// -------------------- Color picker modal --------------------

// Full-screen R/G/B slider modal. Lazy-built on first open, reused
// after — same reasoning as the find-phone screen: rebuilding every
// invocation would stall the LVGL task and lose the widget-pointer
// captures the sliders + preview need. All state (widget pointers,
// current RGB values) lives in file-static so the captureless-lambda
// slider callbacks can reach it by name.
static lv_obj_t *g_pick_screen = nullptr;
static lv_obj_t *g_pick_preview = nullptr;
static lv_obj_t *g_pick_r = nullptr;
static lv_obj_t *g_pick_g = nullptr;
static lv_obj_t *g_pick_b = nullptr;
static uint8_t   g_pick_rv = 0, g_pick_gv = 0, g_pick_bv = 0;

static void picker_update_preview() {
  if (!g_pick_preview) return;
  lv_color_t c = lv_color_make(g_pick_rv, g_pick_gv, g_pick_bv);
  lv_obj_set_style_bg_color(g_pick_preview, c, 0);
}

// Small helper for the R/G/B rows — a label + slider pair inside a
// horizontal flex container. Returns the slider so the caller can
// stash the pointer in a file-static + wire the value-change event.
static lv_obj_t *picker_slider_row(lv_obj_t *parent, const char *tag,
                                   uint8_t initial) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 200, 24);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, tag);
  lv_obj_set_style_text_font(lbl, &ProductSansRegular_16, 0);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_set_width(lbl, 12);

  lv_obj_t *sld = lv_slider_create(row);
  lv_obj_set_size(sld, 170, 8);
  lv_slider_set_range(sld, 0, 255);
  lv_slider_set_value(sld, initial, LV_ANIM_OFF);
  return sld;
}

static void open_color_picker(lv_color_t initial) {
  if (!g_pick_screen) {
    g_pick_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_pick_screen, 240, 240);
    lv_obj_set_style_bg_color(g_pick_screen, lv_color_black(), 0);
    lv_obj_set_style_pad_all(g_pick_screen, 0, 0);
    lv_obj_set_style_border_width(g_pick_screen, 0, 0);
    lv_obj_set_style_radius(g_pick_screen, 0, 0);
    lv_obj_remove_flag(g_pick_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Layout: title at top, preview swatch under it, three slider
    // rows in a column, OK/Cancel side-by-side at the bottom. All
    // absolute-positioned with LV_ALIGN_CENTER + Y offsets — same
    // pattern as the find-phone modal, which is the only layout
    // that's reliably centered on the round screen in this build.
    lv_obj_t *title = lv_label_create(g_pick_screen);
    lv_label_set_text(title, "Custom color");
    lv_obj_set_style_text_font(title, &ProductSansRegular_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -95);

    g_pick_preview = lv_obj_create(g_pick_screen);
    lv_obj_set_size(g_pick_preview, 80, 20);
    lv_obj_set_style_radius(g_pick_preview, 6, 0);
    lv_obj_set_style_border_width(g_pick_preview, 1, 0);
    lv_obj_set_style_border_color(g_pick_preview, lv_color_hex(0x444444), 0);
    lv_obj_set_style_pad_all(g_pick_preview, 0, 0);
    lv_obj_remove_flag(g_pick_preview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(g_pick_preview, LV_ALIGN_CENTER, 0, -65);

    // Column holding the three slider rows. A single flex container
    // is cheaper than aligning each row individually and keeps the
    // vertical gaps uniform if the row height ever changes.
    lv_obj_t *col = lv_obj_create(g_pick_screen);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 220, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 6, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, -5);

    g_pick_r = picker_slider_row(col, "R", 0);
    g_pick_g = picker_slider_row(col, "G", 0);
    g_pick_b = picker_slider_row(col, "B", 0);

    auto on_slide = [](lv_event_t *e) {
      lv_obj_t *s = lv_event_get_target_obj(e);
      uint8_t v = (uint8_t)lv_slider_get_value(s);
      if (s == g_pick_r) g_pick_rv = v;
      else if (s == g_pick_g) g_pick_gv = v;
      else if (s == g_pick_b) g_pick_bv = v;
      picker_update_preview();
    };
    lv_obj_add_event_cb(g_pick_r, on_slide, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_pick_g, on_slide, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_pick_b, on_slide, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *ok_btn = lv_button_create(g_pick_screen);
    lv_obj_set_size(ok_btn, 80, 36);
    lv_obj_set_style_radius(ok_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(ok_btn, &accent_bg_style, 0);
    lv_obj_align(ok_btn, LV_ALIGN_CENTER, -45, 75);
    lv_obj_t *ok_lbl = lv_label_create(ok_btn);
    lv_obj_center(ok_lbl);
    lv_label_set_text(ok_lbl, "Save");
    lv_obj_set_style_text_color(ok_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(ok_lbl, &ProductSansRegular_16, 0);

    lv_obj_t *cx_btn = lv_button_create(g_pick_screen);
    lv_obj_set_size(cx_btn, 80, 36);
    lv_obj_set_style_radius(cx_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cx_btn, lv_color_hex(0x222222), 0);
    lv_obj_align(cx_btn, LV_ALIGN_CENTER, 45, 75);
    lv_obj_t *cx_lbl = lv_label_create(cx_btn);
    lv_obj_center(cx_lbl);
    lv_label_set_text(cx_lbl, "Cancel");
    lv_obj_set_style_text_color(cx_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(cx_lbl, &ProductSansRegular_16, 0);

    lv_obj_add_event_cb(
        ok_btn,
        [](lv_event_t *) {
          haptic_play(false, 40, 0);
          char hex[8];
          snprintf(hex, sizeof(hex), "#%02X%02X%02X",
                   g_pick_rv, g_pick_gv, g_pick_bv);
          lv_color_t c = lv_color_make(g_pick_rv, g_pick_gv, g_pick_bv);
          apply_theme_color(c, hex);
          refresh_color_row(hex);
          lv_screen_load_anim(main_screen, LV_SCREEN_LOAD_ANIM_FADE_OUT,
                              100, 0, false);
        },
        LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(
        cx_btn,
        [](lv_event_t *) {
          haptic_play(false, 30, 0);
          lv_screen_load_anim(main_screen, LV_SCREEN_LOAD_ANIM_FADE_OUT,
                              100, 0, false);
        },
        LV_EVENT_CLICKED, NULL);
  }

  // Seed the sliders + preview from the current colour every open so
  // the picker starts wherever the user is now, not wherever it was
  // when they last opened it.
  g_pick_rv = (uint8_t)((initial.red));
  g_pick_gv = (uint8_t)((initial.green));
  g_pick_bv = (uint8_t)((initial.blue));
  lv_slider_set_value(g_pick_r, g_pick_rv, LV_ANIM_OFF);
  lv_slider_set_value(g_pick_g, g_pick_gv, LV_ANIM_OFF);
  lv_slider_set_value(g_pick_b, g_pick_bv, LV_ANIM_OFF);
  picker_update_preview();

  lv_screen_load_anim(g_pick_screen, LV_SCREEN_LOAD_ANIM_FADE_IN,
                      100, 0, false);
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
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_set_style_pad_row(row, 6, 0);
  lv_obj_set_scroll_dir(row, LV_DIR_NONE);

  g_color_row = row;

  // Read the persisted colour as a hex string from config.json.
  // settings_color_idx_from_hex returns UINT8_MAX for anything not in
  // the palette (i.e. a custom picker-chosen colour), so no swatch
  // gets a border in that case — the custom swatch below picks it up.
  std::string cur_hex = watch.settings.readString("settings", "themecolor",
                                                  settings_color_hex_at(0));
  uint8_t current = settings_color_idx_from_hex(cur_hex.c_str());

  for (uint8_t i = 0; i < settings_color_count(); i++) {
    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_size(btn, 40, 40);
    lv_obj_set_style_radius(btn, 8, 0);
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
          lv_color_t c = settings_color_at(picked);
          const char *hex = settings_color_hex_at(picked);
          apply_theme_color(c, hex);
          refresh_color_row(hex);
          haptic_play(false, 30, 0);
        },
        LV_EVENT_CLICKED, NULL);
  }

  // Custom-color swatch — trailing entry in the row that opens the
  // R/G/B picker modal. Marked with CUSTOM_SWATCH_TAG in user_data
  // solely so an accidental palette walker would skip it; the click
  // handler here is scoped to this button only.
  bool custom_active = (current == UINT8_MAX);
  lv_color_t cur = settings_color_from_hex(cur_hex.c_str());
  g_custom_swatch = lv_button_create(row);
  lv_obj_set_size(g_custom_swatch, 40, 40);
  lv_obj_set_style_radius(g_custom_swatch, 8, 0);
  lv_obj_set_style_bg_color(
      g_custom_swatch,
      custom_active ? cur : lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_width(g_custom_swatch, 2, 0);
  lv_obj_set_style_border_color(
      g_custom_swatch,
      custom_active ? lv_color_white() : lv_color_hex(0x222222), 0);
  lv_obj_set_style_pad_all(g_custom_swatch, 0, 0);
  lv_obj_set_style_shadow_width(g_custom_swatch, 0, 0);
  lv_obj_set_user_data(g_custom_swatch, (void *)CUSTOM_SWATCH_TAG);

  lv_obj_t *icon = lv_label_create(g_custom_swatch);
  SET_SYMBOL_24(icon, FA_PALETTE);
  lv_obj_set_style_text_color(icon, lv_color_white(), 0);
  lv_obj_center(icon);

  lv_obj_add_event_cb(
      g_custom_swatch,
      [](lv_event_t *) {
        haptic_play(false, 30, 0);
        // Seed picker from the current on-disk colour rather than
        // whatever is on the sliders — the user may have changed the
        // theme via a palette click since the last picker session.
        std::string h = watch.settings.readString(
            "settings", "themecolor", settings_color_hex_at(0));
        open_color_picker(settings_color_from_hex(h.c_str()));
      },
      LV_EVENT_CLICKED, NULL);

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
