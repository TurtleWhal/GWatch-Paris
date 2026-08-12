#include "ui.hpp"
#include <sys/time.h>
#if LV_USE_SNAPSHOT
#include "draw/snapshot/lv_snapshot.h"
#include "esp_heap_caps.h"
#endif

// Tread face: an odometer clock. A horizontal ribbon of the 12 hour numbers
// rests with the current hour under the frame; two diagonal ribbons show the
// minute tens (0-5) and units (0-9). When a digit advances, its ribbon flicks
// quickly one slot forward (eased over TW_ANIM) so the next number snaps into
// place. treadwatch_update() (called at ~16 ms while the face is visible)
// drives it; nothing runs while the face is off-screen or the watch is asleep.

static constexpr int BG_COLOR = 0x101010;
static constexpr int TREAD_COLOR = 0x000000;
static constexpr int EDGE_COLOR = 0x222222;
static constexpr int FRAME_COLOR = 0x282833;

static constexpr uint8_t TW_COUNT = 12;            // 12 hour numbers
static constexpr int TW_SLOT = 40;                 // px per number
static constexpr int TW_SPAN = TW_COUNT * TW_SLOT; // ring length (480)

static constexpr uint32_t TW_ANIM =
    220; // flick duration for hour/minute ribbons (ms)
static constexpr uint32_t SEC_ANIM =
    120; // snappier flick for the seconds ribbon (ms)
// The hour frame sits over slot index 1 (digit centre at screen x=60, the 2nd
// 40px slot). Label i shows number i+1, so to rest hour H under the frame the
// ribbon's resting slot is (H-1) - HR_FRAME_SLOT.
static constexpr int HR_FRAME_SLOT = 1;

static lv_obj_t *tw_labels[TW_COUNT];
static int tw_last_x[TW_COUNT]; // last pixel we set, to skip no-op moves
static uint32_t tw_last_tick;

// Per-ribbon flick state. Each ribbon rests on the slot that shows the current
// time digit; when that digit advances it flicks forward one slot (eased over
// TW_ANIM), then `slot` increments (mod count).
struct RibbonAnim {
  int slot; // resting slot index (off / slot)
  bool flicking;
  uint32_t flick_start; // tick when the current flick began
};
static RibbonAnim tw_hr_a, tw_m1_a, tw_m2_a;

// --- Minute ribbons: two parallel diagonal tracks (m1box / m2box). They
//     share the hours' rest+flick clock (one step per second). The digits are
//     placed straight onto the screen (not parented to the rotated boxes) so
//     they stay upright while travelling along the -66 degree diagonal — the
//     position is what moves diagonally, not the glyph. Slot spacing sets how
//     many digits land inside the visible diagonal chord (~240 px):
//       m1box = 0-5, spaced so 3 show at once (240/80).
//       m2box = 0-9, spaced so 5 show at once (240/48). ---
static constexpr int M1_COUNT = 6, M1_SLOT = 80;  // 0-5, 3 visible
static constexpr int M2_COUNT = 10, M2_SLOT = 48; // 0-9, 5 visible
static constexpr float MN_ANGLE_DEG =
    -66.0f; // matches m*box transform_rotation (-660)
static constexpr float MN_Y_NUDGE =
    12.0f; // drop the digits this many px lower (vertical),
           // applied along the track so they stay centred on it
static constexpr int MN_LBL_W =
    30; // fixed slot width -> exact horizontal centring
// Track centres = each m*box's screen centre (240px screen centre + its align
// x-offset). Keep in sync with the lv_obj_align calls below.
static constexpr float M1_CX = 116.0f, M1_CY = 120.0f; // m1box align (-4, 0)
static constexpr float M2_CX = 150.0f, M2_CY = 120.0f; // m2box align (30, 0)

static lv_obj_t *tw_m1[M1_COUNT];
static lv_obj_t *tw_m2[M2_COUNT];
static int tw_m1_xy[M1_COUNT][2]; // last (x,y) placed, to skip no-op moves
static int tw_m2_xy[M2_COUNT][2];
static float tw_dx, tw_dy; // screen-space unit dir of +along-track
static float tw_mn_hh; // label-top -> ink-centre offset (minute vertical centre)
static float tw_mn_along; // MN_Y_NUDGE expressed as an along-track shift

// --- Seconds ruler (secbox): a 60-mark ring, one mark per second spaced
//     SEC_SLOT px — a number every 5 s (00,05,..,55) and a plain tick for the
//     seconds between. It scrolls so the current second's mark rests under
//     secarrow, flicking one slot forward each second like the other ribbons.
//     ---
static constexpr int SEC_COUNT = 60, SEC_SLOT = 12;   // one mark per second
static constexpr int SEC_SPAN = SEC_COUNT * SEC_SLOT; // ring length (720)
static constexpr float SEC_ARROW_X =
    178.0f; // secarrow x in secbox-local coords
            // (secframe align +58 -> screen 178; secbox left at 0)
static lv_obj_t *tw_sec[SEC_COUNT];
static int tw_sec_x[SEC_COUNT]; // last x placed, to skip no-op moves
static RibbonAnim tw_sec_a;

#if LV_USE_SNAPSHOT
// One-time ARGB8888 snapshot of the two static diagonal bars (see the bar
// setup in treadwatch_create). Allocated once in PSRAM and reused for the
// process lifetime, exactly like weather.cpp's render cache.
static uint8_t *tw_bar_buf;
static lv_image_dsc_t tw_bar_dsc;
static constexpr uint32_t TW_BAR_W = 240, TW_BAR_H = 240;
static constexpr uint32_t TW_BAR_BYTES = TW_BAR_W * TW_BAR_H * 4;
#endif

// Date + battery labels. Their value changes at most once a day / on a battery
// step, and they're rotated (a set_text triggers a transform re-render), so
// they're refreshed only when the shown value actually changes.
static const char *TW_WDAYS[] = {"SUN", "MON", "TUE", "WED",
                                 "THU", "FRI", "SAT"};
static lv_obj_t *tw_batlbl, *tw_datelbl, *tw_daylbl;
static int tw_last_bat = -1, tw_last_wday = -1, tw_last_mday = -1;

static void tw_update_datebat(const struct tm &t) {
  int pct = watch.battery.percent;
  if (pct != tw_last_bat) {
    tw_last_bat = pct;
    lv_label_set_text_fmt(tw_batlbl, "%d%%", pct);
  }
  if (t.tm_wday != tw_last_wday) {
    tw_last_wday = t.tm_wday;
    lv_label_set_text(tw_datelbl, TW_WDAYS[t.tm_wday]);
  }
  if (t.tm_mday != tw_last_mday) {
    tw_last_mday = t.tm_mday;
    lv_label_set_text_fmt(tw_daylbl, "%d", t.tm_mday);
  }
}

// SquadaOne is an all-caps / numeric display face whose glyphs sit high inside
// the font line box — the line height reserves descent space the digits never
// use. LVGL centres by that line box, so a digit that's LEFT_MID-aligned (or
// centred off half the line height) renders a few px too high: worse the bigger
// the font. This returns how many px to shift a '0'-height digit DOWN so its
// ink centre — not its line-box centre — is what lands centred. Read from the
// live font metrics so it stays correct if a size is regenerated.
static float tw_digit_vcenter(const lv_font_t *font) {
  lv_font_glyph_dsc_t g;
  if (!lv_font_get_glyph_dsc(font, &g, '0', 0))
    return 0.0f;
  // Ink centre measured from the top of the line box: the baseline sits
  // (line_height - base_line) below the top, and the digit's bbox rises box_h
  // from (baseline - ofs_y).
  float baseline = font->line_height - font->base_line;
  float ink_center = baseline - g.ofs_y - g.box_h * 0.5f;
  return font->line_height * 0.5f - ink_center; // +ve => shift down
}

static void tw_make_digits(lv_obj_t *scr, lv_obj_t **arr, int xy[][2],
                           int count) {
  for (int j = 0; j < count; j++) {
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text_fmt(lbl, "%d", j);
    lv_obj_set_style_text_font(lbl, &SquadaOneRegular_24, 0);
    lv_obj_add_style(lbl, &accent_text_style, 0); // theme colour, live-updates
    // Fixed width + centred text gives exact horizontal centring; content
    // height keeps the full glyph. Vertical centring is done in place from
    // the font line height (tw_mn_hh), so no post-layout size query is needed.
    lv_obj_set_size(lbl, MN_LBL_W, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    arr[j] = lbl;
    xy[j][0] = xy[j][1] = -10000; // sentinel: force first placement
  }
}

// Place one diagonal ribbon: digit j sits at along-track distance
// (j*slot - off) from the track centre, wrapped symmetrically so the
// ribbon stays centred, then projected onto the -66 degree screen direction.
static void tw_place_diag(lv_obj_t **arr, int xy[][2], int count, int slot,
                          float cx, float cy, float off) {
  const float span = (float)(count * slot);
  const float half = span * 0.5f;
  for (int j = 0; j < count; j++) {
    float s = (float)(slot * j) - off;
    while (s < -half)
      s += span;
    while (s >= half)
      s -= span;
    // Centre on the track point (fixed width -> half is MN_LBL_W/2; vertical
    // uses the font line height). The along-track shift keeps the ribbon on
    // the centreline while dropping it MN_Y_NUDGE px lower.
    float t = s + tw_mn_along;
    int x = (int)lroundf(cx + t * tw_dx - MN_LBL_W * 0.5f);
    int y = (int)lroundf(cy + t * tw_dy - tw_mn_hh);
    if (x != xy[j][0] || y != xy[j][1]) {
      lv_obj_set_pos(arr[j], x, y);
      xy[j][0] = x;
      xy[j][1] = y;
    }
  }
}

// Place the horizontal hour ribbon at a given offset (in px). Mirrors
// tw_place_diag but on a straight, screen-aligned track.
static void tw_place_hours(float off) {
  for (uint8_t i = 0; i < TW_COUNT; i++) {
    float fx = (float)(TW_SLOT * i) - off;
    if (fx < -TW_SLOT)
      fx += (float)TW_SPAN; // wrapped off the left edge -> back onto the right
    int x = (int)lroundf(fx);
    if (x != tw_last_x[i]) {
      lv_obj_set_x(tw_labels[i], x);
      tw_last_x[i] = x;
    }
  }
}

// Place the seconds ruler so the mark for the current second rests under the
// arrow. `off` is in px; the resting second sits at off/SEC_SLOT. Each mark is
// centred on its ring slot — numbers are 16px wide (half 8), ticks 2px (half
// 1).
static void tw_place_sec(float off) {
  for (int s = 0; s < SEC_COUNT; s++) {
    float rel = (float)(SEC_SLOT * s) - off;
    while (rel < -SEC_SPAN * 0.5f)
      rel += SEC_SPAN; // pick the copy nearest the arrow
    while (rel >= SEC_SPAN * 0.5f)
      rel -= SEC_SPAN;
    int hw = (s % 5 == 0) ? 8 : 1; // number-label vs tick half-width
    int x = (int)lroundf(SEC_ARROW_X + rel - (float)hw);
    if (x != tw_sec_x[s]) {
      lv_obj_set_x(tw_sec[s], x);
      tw_sec_x[s] = x;
    }
  }
}

// Drive one ribbon toward `target` and return its offset in *slots*. At rest
// it sits on `slot`; when the target digit moves ahead it flicks forward one
// slot, easing from slot -> slot+1 over TW_ANIM, then advances `slot`. `snap`
// jumps straight to the target with no animation (used on wake / first show).
static float tw_ribbon(RibbonAnim &a, int count, int target, bool snap,
                       uint32_t anim) {
  int fwd = ((target - a.slot) % count + count) % count; // forward distance
  // Snap on a big/backward jump (wake, time correction) rather than spinning
  // the whole ribbon around; only a single forward step gets the flick.
  if (snap || fwd > 1) {
    a.slot = target;
    a.flicking = false;
    return (float)a.slot;
  }
  if (!a.flicking) {
    if (fwd == 0)
      return (float)a.slot; // already showing the right digit
    a.flicking = true;      // digit advanced -> start a flick
    a.flick_start = lv_tick_get();
  }
  uint32_t el = lv_tick_elaps(a.flick_start);
  if (el >= anim) {
    a.slot = (a.slot + 1) % count; // flick done; a further flick starts next
    a.flicking = false;            // frame if the target is still ahead
    return (float)a.slot;
  }
  float p = (float)el / (float)anim;
  p = p * p * (3.0f - 2.0f * p); // smoothstep ease
  return (float)a.slot + p;      // slot -> slot+1
}

lv_obj_t *treadwatch_create(lv_obj_t *parent) {
  lv_obj_t *scr = create_screen(parent);
  lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
  lv_obj_set_style_bg_color(scr, lv_color_hex(BG_COLOR), 0);

  lv_obj_t *hourbox = lv_obj_create(scr);
  lv_obj_set_size(hourbox, 242, 52);
  lv_obj_center(hourbox);
  lv_obj_set_scroll_dir(hourbox, LV_DIR_HOR);
  lv_obj_set_style_border_width(hourbox, 1, 0);
  lv_obj_set_style_border_color(hourbox, lv_color_hex(EDGE_COLOR), 0);
  lv_obj_set_style_radius(hourbox, 0, 0);
  lv_obj_set_style_bg_color(hourbox, lv_color_hex(TREAD_COLOR), 0);
  lv_obj_set_scrollbar_mode(hourbox, LV_SCROLLBAR_MODE_OFF);
  // Zero the default container padding so a label's content-area x (a
  // multiple of TW_SLOT at rest) maps straight to screen x — otherwise
  // pad_left shifts every resting label off the screen-left 40px grid.
  lv_obj_set_style_pad_all(hourbox, 0, 0);

  for (uint8_t i = 0; i < TW_COUNT; i++) {
    lv_obj_t *lbl = lv_label_create(hourbox);
    lv_label_set_text_fmt(lbl, "%d", i + 1);
    lv_obj_set_style_text_font(lbl, &SquadaOneRegular_48, 0);
    lv_obj_add_style(lbl, &accent_text_style, 0); // theme colour, live-updates
    // Fixed 40px-wide slot (keeps left edges on the screen grid) but height
    // hugs the glyph so LEFT_MID centers the number vertically in the 60px
    // strip. Later lv_obj_set_x only updates the x offset, leaving this
    // LEFT_MID alignment — and thus the vertical centering — intact.
    lv_obj_set_size(lbl, TW_SLOT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, TW_SLOT * i,
                 (int)lroundf(tw_digit_vcenter(&SquadaOneRegular_48)));
    tw_labels[i] = lbl;
    tw_last_x[i] = TW_SLOT * i;
  }

  lv_obj_t *hourframe = lv_obj_create(scr);
  lv_obj_set_size(hourframe, 50, 60);
  lv_obj_set_style_bg_opa(hourframe, 0, 0);
  lv_obj_set_style_border_width(hourframe, 6, 0);
  lv_obj_set_style_border_color(hourframe, lv_color_hex(FRAME_COLOR), 0);
  lv_obj_align(hourframe, LV_ALIGN_CENTER, -120 + TW_SLOT * 1.5, 0);

  // The two diagonal bars are static decoration, but transform_rotation makes
  // each a large rotated layer. LVGL re-renders that layer — tiled, because it
  // exceeds LV_DRAW_LAYER_SIMPLE_BUF_SIZE — whenever a dirty region overlaps
  // its near-full-screen bounding box, so the once-a-second seconds flick
  // dragged both bars into a full-screen redraw every animating frame. They
  // never change, so render them once into an ARGB8888 bitmap and show that
  // flat image instead; blitting it is cheap and never triggers a transform
  // pass. Same rendered-once-cache technique as weather.cpp. If LV_USE_SNAPSHOT
  // is off (or the alloc/snapshot fails) barlayer stays in place as the live
  // bars — correct, just the old expensive path.
  lv_obj_t *barlayer = lv_obj_create(scr);
  lv_obj_remove_style_all(
      barlayer); // transparent -> snapshot holds only the bars
  lv_obj_set_size(barlayer, 240, 240);
  lv_obj_center(barlayer);
  lv_obj_remove_flag(barlayer, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *m1box = lv_obj_create(barlayer);
  lv_obj_set_size(m1box, 280, 26);
  lv_obj_center(m1box);
  lv_obj_set_scroll_dir(m1box, LV_DIR_HOR);
  lv_obj_set_style_border_width(m1box, 1, 0);
  lv_obj_set_style_border_color(m1box, lv_color_hex(EDGE_COLOR), 0);
  lv_obj_set_style_radius(m1box, 0, 0);
  lv_obj_set_style_bg_color(m1box, lv_color_hex(TREAD_COLOR), 0);
  lv_obj_set_scrollbar_mode(m1box, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_transform_pivot_x(m1box, 140, 0);
  lv_obj_set_style_transform_pivot_y(m1box, 13, 0);
  lv_obj_set_style_transform_rotation(m1box, -660, 0);

  lv_obj_t *m2box = lv_obj_create(barlayer);
  lv_obj_set_size(m2box, 280, 26);
  lv_obj_center(m2box);
  lv_obj_set_scroll_dir(m2box, LV_DIR_HOR);
  lv_obj_set_style_border_width(m2box, 1, 0);
  lv_obj_set_style_border_color(m2box, lv_color_hex(EDGE_COLOR), 0);
  lv_obj_set_style_radius(m2box, 0, 0);
  lv_obj_set_style_bg_color(m2box, lv_color_hex(TREAD_COLOR), 0);
  lv_obj_set_scrollbar_mode(m2box, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_transform_pivot_x(m2box, 140, 0);
  lv_obj_set_style_transform_pivot_y(m2box, 13, 0);
  lv_obj_set_style_transform_rotation(m2box, -660, 0);

  lv_obj_align(m1box, LV_ALIGN_CENTER, -4, 0);
  lv_obj_align(m2box, LV_ALIGN_CENTER, 30, 0);

#if LV_USE_SNAPSHOT
  if (!tw_bar_buf)
    tw_bar_buf = (uint8_t *)heap_caps_malloc(TW_BAR_BYTES, MALLOC_CAP_SPIRAM |
                                                               MALLOC_CAP_8BIT);
  if (tw_bar_buf) {
    lv_obj_update_layout(barlayer);
    lv_draw_buf_t dbuf = {};
    if (lv_draw_buf_init(&dbuf, TW_BAR_W, TW_BAR_H, LV_COLOR_FORMAT_ARGB8888,
                         /*stride auto*/ 0, tw_bar_buf,
                         TW_BAR_BYTES) == LV_RESULT_OK &&
        lv_snapshot_take_to_draw_buf(barlayer, LV_COLOR_FORMAT_ARGB8888,
                                     &dbuf) == LV_RESULT_OK) {
      tw_bar_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
      tw_bar_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
      tw_bar_dsc.header.w = TW_BAR_W;
      tw_bar_dsc.header.h = TW_BAR_H;
      tw_bar_dsc.header.stride = TW_BAR_W * 4;
      tw_bar_dsc.data_size = TW_BAR_BYTES;
      tw_bar_dsc.data = tw_bar_buf;

      lv_obj_t *barimg = lv_image_create(scr);
      lv_obj_center(barimg);
      lv_obj_remove_flag(barimg, LV_OBJ_FLAG_CLICKABLE);
      lv_image_set_src(barimg, &tw_bar_dsc);
      lv_obj_delete(barlayer); // drop the live transformed bars
    }
  }
#endif

  lv_obj_t *minframe = lv_obj_create(scr);
  lv_obj_set_size(minframe, 64, 36);
  lv_obj_set_style_bg_opa(minframe, 0, 0);
  lv_obj_set_style_border_width(minframe, 6, 0);
  lv_obj_set_style_border_color(minframe, lv_color_hex(FRAME_COLOR), 0);
  lv_obj_align(minframe, LV_ALIGN_CENTER, 8, 12);
  lv_obj_set_scrollbar_mode(minframe, LV_SCROLLBAR_MODE_OFF);

  // Digit ribbons for the two diagonal tracks. Created on scr (not the boxes)
  // so they render upright on top of the bars; positioned along the diagonal.
  tw_dx = cosf(DEG2RAD * MN_ANGLE_DEG);
  tw_dy = sinf(DEG2RAD * MN_ANGLE_DEG);
  // Distance from a digit label's top to the point that should sit on the
  // track. That target is the digit's *ink* centre, so start from half the
  // line height and pull it up by the font's high-glyph bias (tw_digit_vcenter)
  // — otherwise the minute digits ride a couple px above the diagonal.
  tw_mn_hh = lv_font_get_line_height(&SquadaOneRegular_24) * 0.5f -
             tw_digit_vcenter(&SquadaOneRegular_24);
  // A vertical drop of MN_Y_NUDGE, achieved by moving along the track (dy is
  // the track's vertical component) so the digits stay on the centreline.
  tw_mn_along = MN_Y_NUDGE / tw_dy;
  tw_make_digits(scr, tw_m1, tw_m1_xy, M1_COUNT);
  tw_make_digits(scr, tw_m2, tw_m2_xy, M2_COUNT);

  lv_obj_t *secbox = lv_obj_create(scr);
  lv_obj_set_size(secbox, 240, 26);
  lv_obj_align(secbox, LV_ALIGN_CENTER, 0, 46);
  lv_obj_set_scroll_dir(secbox, LV_DIR_HOR);
  lv_obj_set_style_border_width(secbox, 0, 0);
  lv_obj_set_style_radius(secbox, 0, 0);
  lv_obj_set_style_bg_color(secbox, lv_color_hex(TREAD_COLOR), 0);
  // Fully opaque + square + untransformed => LVGL's cover check lets the
  // seconds-ribbon redraws skip everything beneath secbox, so the rotated
  // m1/m2 bars (whose huge transformed bounding boxes overlap this strip)
  // don't get re-rendered every animating frame.
  lv_obj_set_style_bg_opa(secbox, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(secbox, 1, 0);
  lv_obj_set_style_border_color(secbox, lv_color_hex(EDGE_COLOR), 0);
  lv_obj_set_scrollbar_mode(secbox, LV_SCROLLBAR_MODE_OFF);
  // Zero the default container padding so a label's content-area x (a
  // multiple of TW_SLOT at rest) maps straight to screen x — otherwise
  // pad_left shifts every resting label off the screen-left 40px grid.
  lv_obj_set_style_pad_all(secbox, 0, 0);

  // Build the 60 second-marks into tw_sec[] indexed by second: a number label
  // every 5 s (index 5i) and a tick for the four seconds between. LEFT_MID
  // keeps them vertically centred; tw_place_sec sets the real x each frame.
  for (uint8_t i = 0; i < 12; i++) {
    lv_obj_t *lbl = lv_label_create(secbox);
    lv_label_set_text_fmt(lbl, "%02d", i * 5);
    lv_obj_set_style_text_font(lbl, &SquadaOneRegular_18, 0);
    lv_obj_add_style(lbl, &accent_text_style, 0); // theme colour, live-updates
    lv_obj_set_size(lbl, 16, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    // Round the correction DOWN: at 18px it lands on a half-pixel, and leaving
    // the number a hair high (like the hour/minute digits) reads better than
    // rounding up and dropping it below the centre line.
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0,
                 (int)floorf(tw_digit_vcenter(&SquadaOneRegular_18)));
    tw_sec[i * 5] = lbl;
    tw_sec_x[i * 5] = -10000; // sentinel: force first placement

    for (uint8_t j = 0; j < 4; j++) {
      lv_obj_t *line = lv_obj_create(secbox);
      lv_obj_set_size(line, 2, 8);
      lv_obj_set_style_border_width(line, 0, 0);
      lv_obj_add_style(line, &accent_bg_style, 0); // theme colour, live-updates
      lv_obj_align(line, LV_ALIGN_LEFT_MID, 0, 0);
      tw_sec[i * 5 + j + 1] = line;
      tw_sec_x[i * 5 + j + 1] = -10000;
    }
  }

  lv_obj_t *secframe = lv_obj_create(scr);
  lv_obj_set_size(secframe, 64, 36);
  lv_obj_set_style_bg_opa(secframe, 0, 0);
  lv_obj_set_style_border_width(secframe, 6, 0);
  lv_obj_set_style_border_color(secframe, lv_color_hex(FRAME_COLOR), 0);
  lv_obj_align(secframe, LV_ALIGN_CENTER, 58, 46);
  lv_obj_set_style_pad_all(secframe, 0, 0);
  lv_obj_set_scrollbar_mode(secframe, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *secarrow = lv_obj_create(secframe);
  lv_obj_set_size(secarrow, 6, 6);
  lv_obj_set_style_border_width(secarrow, 0, 0);
  lv_obj_set_style_bg_color(secarrow, lv_color_hex(FRAME_COLOR), 0);
  lv_obj_align(secarrow, LV_ALIGN_BOTTOM_MID, 0, 3);
  lv_obj_set_style_transform_pivot_x(secarrow, 3, 0);
  lv_obj_set_style_transform_pivot_y(secarrow, 3, 0);
  lv_obj_set_style_transform_rotation(secarrow, 450, 0);
  lv_obj_set_style_radius(secarrow, 0, 0);

  tw_batlbl = lv_label_create(scr);
  lv_obj_set_style_text_font(tw_batlbl, &SquadaOneRegular_16, 0);
  lv_obj_add_style(tw_batlbl, &accent_text_style, 0); // theme colour, live-updates
  lv_obj_set_style_transform_rotation(tw_batlbl, -660, 0);
  lv_obj_align(tw_batlbl, LV_ALIGN_BOTTOM_LEFT, 46, -16);

//   lv_obj_t *baticon = lv_label_create(scr);
//   lv_obj_add_style(baticon, &accent_text_style, 0);
//   lv_obj_set_style_transform_rotation(baticon, -660, 0);
//   lv_obj_align(baticon, LV_ALIGN_BOTTOM_LEFT, 31, -26);
//   SET_SYMBOL_14(baticon, FA_BATTERY_EMPTY);

  tw_datelbl = lv_label_create(scr);
  lv_obj_set_style_text_font(tw_datelbl, &SquadaOneRegular_18, 0);
  lv_obj_add_style(tw_datelbl, &accent_text_style,
                   0); // theme colour, live-updates
  lv_obj_set_style_transform_rotation(tw_datelbl, -660, 0);
  lv_obj_align(tw_datelbl, LV_ALIGN_TOP_RIGHT, -35, 84);

  tw_daylbl = lv_label_create(scr);
  lv_obj_set_style_text_font(tw_daylbl, &SquadaOneRegular_24, 0);
  lv_obj_add_style(tw_daylbl, &accent_text_style, 0); // theme colour, live-updates
  lv_obj_align(tw_daylbl, LV_ALIGN_TOP_RIGHT, -18, 70);
  lv_obj_set_style_text_align(tw_daylbl, LV_TEXT_ALIGN_CENTER, 0);

  // Rest every ribbon on the current time so nothing flicks at boot.
  struct timeval tv;
  gettimeofday(&tv, NULL);
  struct tm t;
  localtime_r(&tv.tv_sec, &t);
  int h12 = t.tm_hour % 12;
  if (h12 == 0)
    h12 = 12;
  tw_hr_a.slot = ((h12 - 1) - HR_FRAME_SLOT + TW_COUNT) % TW_COUNT;
  tw_m1_a.slot = t.tm_min / 10;
  tw_m2_a.slot = t.tm_min % 10;
  tw_sec_a.slot = t.tm_sec % SEC_COUNT;
  tw_hr_a.flicking = tw_m1_a.flicking = tw_m2_a.flicking = tw_sec_a.flicking =
      false;

  tw_place_hours((float)tw_hr_a.slot * TW_SLOT);
  tw_place_diag(tw_m1, tw_m1_xy, M1_COUNT, M1_SLOT, M1_CX, M1_CY,
                (float)tw_m1_a.slot * M1_SLOT);
  tw_place_diag(tw_m2, tw_m2_xy, M2_COUNT, M2_SLOT, M2_CX, M2_CY,
                (float)tw_m2_a.slot * M2_SLOT);
  tw_place_sec((float)tw_sec_a.slot * SEC_SLOT);

  // Force-populate the date/battery labels (guards are stale from a previous
  // build of the face, and these are freshly created objects).
  tw_last_bat = tw_last_wday = tw_last_mday = -1;
  tw_update_datebat(t);

  tw_last_tick = lv_tick_get();

  return scr;
}

void treadwatch_update() {
  // A long gap means the face was off-screen or the watch slept: jump the
  // ribbons straight to the current time instead of flicking across it.
  uint32_t dt = lv_tick_elaps(tw_last_tick);
  tw_last_tick = lv_tick_get();
  bool snap = (dt > 100);

  struct timeval tv;
  gettimeofday(&tv, NULL);
  struct tm t;
  localtime_r(&tv.tv_sec, &t);
  int h12 = t.tm_hour % 12;
  if (h12 == 0)
    h12 = 12;

  int hr_target = ((h12 - 1) - HR_FRAME_SLOT + TW_COUNT) % TW_COUNT;
  int m1_target = t.tm_min / 10;         // minute tens (0-5)
  int m2_target = t.tm_min % 10;         // minute units (0-9)
  int sec_target = t.tm_sec % SEC_COUNT; // 0-59 (fold a leap-second 60 to 0)

  float hr_off =
      tw_ribbon(tw_hr_a, TW_COUNT, hr_target, snap, TW_ANIM) * (float)TW_SLOT;
  float m1_off =
      tw_ribbon(tw_m1_a, M1_COUNT, m1_target, snap, TW_ANIM) * (float)M1_SLOT;
  float m2_off =
      tw_ribbon(tw_m2_a, M2_COUNT, m2_target, snap, TW_ANIM) * (float)M2_SLOT;
  float sec_off = tw_ribbon(tw_sec_a, SEC_COUNT, sec_target, snap, SEC_ANIM) *
                  (float)SEC_SLOT;

  tw_place_hours(hr_off);
  tw_place_diag(tw_m1, tw_m1_xy, M1_COUNT, M1_SLOT, M1_CX, M1_CY, m1_off);
  tw_place_diag(tw_m2, tw_m2_xy, M2_COUNT, M2_SLOT, M2_CX, M2_CY, m2_off);
  tw_place_sec(sec_off);

  tw_update_datebat(t);
}
