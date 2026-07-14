#include "ble.hpp"
#include "images.hpp"
#include "ui.hpp"
#if LV_USE_SNAPSHOT
#include "draw/snapshot/lv_snapshot.h"
#include "esp_heap_caps.h"
#endif
#include <math.h>
#include <string>

static lv_obj_t *location;
static lv_obj_t *description;

static lv_obj_t *icon;
static lv_obj_t *temp;

static lv_obj_t *hi;
static lv_obj_t *lo;
static lv_obj_t *hum;
static lv_obj_t *humval;
static lv_obj_t *uv;
static lv_obj_t *uvval;

// Humidity and UV arcs share the same angular size; ARC_POS sets where
// they sit on the circle (humidity at lower-right, UV mirrored at
// lower-left). Lifted from the function-local #defines so the UV
// gradient draw callback can reference them at file scope.
static constexpr int32_t W_ARC_SIZE = 25;
static constexpr int32_t W_ARC_POS = 55;
static lv_obj_t *wdir;
static lv_obj_t *wspd;

// Last weather snapshot reflected in the UI. Compared against
// ble.weather().version on each tick so we only repaint on real changes.
static uint32_t last_weather_version = 0;
static std::string last_loc;

// --- Rendered-once cache -------------------------------------------------
// All the weather widgets live under `render_group` rather than directly on
// the screen. Building this screen out of an arched per-glyph location
// label, a 16-slice UV gradient drawn in a per-frame callback, two rotated
// value labels and a scaled+rotated wind arrow makes a *full repaint*
// expensive — and the horizontal scroll layer repaints the whole screen
// every frame while it's sliding into view, which is what made scrolling
// onto this screen stutter.
//
// When LV_USE_SNAPSHOT is available we snapshot `render_group` once per real
// weather update into a PSRAM RGB565 buffer and thereafter display that flat
// image via `cache_img`, hiding `render_group`. A flat 240×240 opaque RGB565
// image takes LVGL's fast blit path (no glyph rasterisation, no arc draws,
// no per-pixel rotation/AA), so every subsequent frame — including every
// frame of a scroll animation — is just one image copy out of PSRAM.
// `render_group` stays laid out (hidden, so it costs nothing to skip) and is
// only un-hidden for the duration of the snapshot, keeping updates
// incremental. If LV_USE_SNAPSHOT is off, render_group is simply left
// visible and the screen renders live (the pre-cache behaviour).
static lv_obj_t *render_group;
#if LV_USE_SNAPSHOT
static lv_obj_t *cache_img;
static uint8_t *cache_buf;       // reused PSRAM RGB565 snapshot target
static lv_image_dsc_t cache_dsc; // wraps cache_buf for cache_img
static constexpr uint32_t CACHE_W = 240;
static constexpr uint32_t CACHE_H = 240;
static constexpr uint32_t CACHE_BYTES = CACHE_W * CACHE_H * 2; // RGB565
#endif

// Sample message from Gadgetbridge that this screen consumes:
// {"t":"weather","v":1,"temp":290,"hi":297,"lo":285,"hum":73,"rain":4,
//  "uv":1,"code":800,"txt":"Clear Sky","wind":4.67,"wdir":321,
//  "loc":"My Location"}

// Build the arched per-character location label across the top of the
// circle. Clears any previous children of `container` so we can rebuild
// on a location change. Factored out because the same code runs at
// creation and from weather_update().
static void build_arched_location(lv_obj_t *container, const char *text) {
  lv_obj_clean(container);
  if (!text || !*text)
    return;

  const lv_font_t *loc_font = &ProductSansRegular_20;
  const float R = 106.0f;
  const float deg_per_rad = 180.0f / (float)M_PI;

  int total_w = 0;
  for (const char *p = text; *p; p++) {
    char c[2] = {*p, 0};
    lv_point_t sz;
    lv_text_get_size(&sz, c, loc_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    total_w += sz.x;
  }

  float angle_deg = -90.0f - (total_w * 0.5f / R) * deg_per_rad;
  for (const char *p = text; *p; p++) {
    char c[2] = {*p, 0};
    lv_point_t sz;
    lv_text_get_size(&sz, c, loc_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

    float center_deg = angle_deg + (sz.x * 0.5f / R) * deg_per_rad;
    float a = center_deg / deg_per_rad;
    int cx = 120 + (int)lroundf(R * cosf(a));
    int cy = 120 + (int)lroundf(R * sinf(a));

    lv_obj_t *lbl = lv_label_create(container);
    lv_obj_set_style_text_font(lbl, loc_font, 0);
    lv_label_set_text(lbl, c);
    lv_obj_update_layout(lbl);

    int lw = lv_obj_get_width(lbl);
    int lh = lv_obj_get_height(lbl);
    lv_obj_set_pos(lbl, cx - lw / 2, cy - lh / 2);
    lv_obj_set_style_transform_pivot_x(lbl, lw / 2, 0);
    lv_obj_set_style_transform_pivot_y(lbl, lh / 2, 0);
    lv_obj_set_style_transform_rotation(
        lbl, (int)lroundf((center_deg + 90.0f) * 10.0f), 0);

    angle_deg += (sz.x / R) * deg_per_rad;
  }
}

// OpenWeatherMap condition code → the closest matching icon from
// components/images. Ranges follow the OWM weather-conditions table:
// 2xx storm, 3xx drizzle, 5xx rain, 6xx snow, 7xx atmosphere, 800 clear,
// 80x clouds. Day-only variants for now; night switching would need
// the current local time and the sunrise/sunset hints we don't get
// from this message.
static const lv_image_dsc_t *icon_for_code(uint16_t code) {
  if (code >= 200 && code <= 232)
    return &IMG_STRONG_TSTORMS;
  if (code >= 300 && code <= 321)
    return &IMG_DRIZZLE;
  if (code == 511)
    return &IMG_SLEET_HAIL;
  if (code >= 500 && code <= 504)
    return &IMG_SHOWERS_RAIN;
  if (code >= 520 && code <= 531)
    return &IMG_HEAVY_RAIN;
  if (code == 611 || code == 612 || code == 613 || code == 615 || code == 616)
    return &IMG_SLEET_HAIL;
  if (code == 620 || code == 621 || code == 622)
    return &IMG_SNOW_SHOWERS_SNOW;
  if (code >= 600 && code <= 602)
    return &IMG_HEAVY_SNOW;
  if (code == 781)
    return &IMG_TORNADO;
  if (code >= 701 && code <= 781)
    return &IMG_HAZE_FOG_DUST_SMOKE;
  if (code == 800)
    return &IMG_SUNNY;
  if (code == 801)
    return &IMG_MOSTLY_SUNNY;
  if (code == 802)
    return &IMG_PARTLY_CLOUDY;
  if (code == 803)
    return &IMG_MOSTLY_CLOUDY_DAY;
  if (code == 804)
    return &IMG_CLOUDY;
  return &IMG_PARTLY_CLOUDY; // sensible default for unknown codes
}

// Standard UV-index colour scale, smoothly interpolated between the
// five WHO category anchors so the arc shifts colour gradually as the
// reading rises rather than jumping at each threshold:
//   0–2  Low        green
//   3–5  Moderate   yellow
//   6–7  High       orange
//   8–10 Very High  red
//   11+  Extreme    purple
static lv_color_t uv_color_for(float uv) {
  struct Stop {
    float uv;
    uint32_t rgb;
  };
  // Stops chosen so the gradient lands on saturated reference
  // colours at the band midpoints of the WHO UV scale and ends
  // cleanly at deep red — no purple stop, since the previous
  // red→purple lerp painted the last slice as a washed-out magenta
  // that read as "the dark red looks off".
  static const Stop stops[] = {
      {0.0f, 0x4CAF50},  // low / green
      {3.0f, 0xFFEB3B},  // moderate / yellow
      {6.0f, 0xFF9800},  // high / orange
      {9.0f, 0xF44336},  // very high / red
      {10.0f, 0xB71C1C}, // deep red anchor for the end of the bar
  };
  const int n = (int)(sizeof(stops) / sizeof(stops[0]));
  if (uv <= stops[0].uv)
    return lv_color_hex(stops[0].rgb);
  if (uv >= stops[n - 1].uv)
    return lv_color_hex(stops[n - 1].rgb);

  for (int i = 1; i < n; i++) {
    if (uv <= stops[i].uv) {
      uint32_t a = stops[i - 1].rgb, b = stops[i].rgb;
      float t = (uv - stops[i - 1].uv) / (stops[i].uv - stops[i - 1].uv);
      uint8_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
      uint8_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
      return lv_color_make((uint8_t)(ar + (br - ar) * t),
                           (uint8_t)(ag + (bg - ag) * t),
                           (uint8_t)(ab + (bb - ab) * t));
    }
  }
  return lv_color_hex(stops[n - 1].rgb);
}

// Number of arc slices baked into the gradient. Only the ONE-TIME
// bake pays the per-slice draw cost — after that it's just a bitmap
// blit on every frame regardless of slice count. 48 is fine.
constexpr int UV_SLICES = 48;

// UV gradient canvas geometry. The gradient strip lives in the lower-
// left of the screen (arc spans 112.5°–137.5° at radius 110–116 around
// screen centre 120, 120), painting into roughly x=34..78, y=194..228.
// A 64×48 canvas positioned at (28, 188) covers that with margin for
// the AA edge bleed and the rounded end caps.
//
// Format is LV_COLOR_FORMAT_ARGB8888 (4 B/px = 12288 bytes) — needed
// for per-pixel alpha so the rectangle around the arc strip is fully
// transparent instead of a black block. That means widgets / labels
// behind (or in front, doesn't matter) the canvas area show through
// naturally without depending on the bg colour matching underneath.
#define UV_CANVAS_W 64
#define UV_CANVAS_H 48
#define UV_CANVAS_X 28
#define UV_CANVAS_Y 188
static uint8_t uv_gradient_buf[UV_CANVAS_W * UV_CANVAS_H * 4];

// Paint the gradient into `canvas` exactly once, at create time. The
// canvas is then just a bitmap the renderer composes each frame — no
// draw callback, no per-frame slice work, so updating the arc's value
// (which invalidates only around the knob) doesn't re-render the
// gradient. That's what makes the weather screen stay responsive when
// BLE weather updates land.
static void uv_gradient_bake(lv_obj_t *canvas) {
  // Clear to fully transparent. The subsequent lv_draw_arc calls write
  // opaque pixels for the gradient strip; everywhere else stays
  // transparent so whatever's under the canvas rectangle keeps
  // showing through.
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);

  // The gradient was originally drawn with the arc obj centred on the
  // screen (120, 120). Translate that centre into canvas-local coords
  // — it lands above / outside this small canvas, which is fine:
  // lv_draw_arc clips into the layer's bounds.
  const int32_t cx = 120 - UV_CANVAS_X;
  const int32_t cy = 120 - UV_CANVAS_Y;
  const int32_t arc_w = 6;
  const int32_t radius = 116; // matches side/2 (side = arc obj is 232)

  int32_t a_lo = 180 - W_ARC_POS - W_ARC_SIZE / 2;
  int32_t a_hi = 180 - W_ARC_POS + W_ARC_SIZE / 2;

  for (int i = 0; i < UV_SLICES; i++) {
    float uv_pos = (float)i * 10.0f / (float)(UV_SLICES - 1);

    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = uv_color_for(uv_pos);
    dsc.width = arc_w;
    dsc.center.x = cx;
    dsc.center.y = cy;
    dsc.radius = (uint16_t)radius;
    dsc.start_angle = a_lo + (a_hi - a_lo) * i / UV_SLICES;
    dsc.end_angle = a_lo + (a_hi - a_lo) * (i + 1) / UV_SLICES;
    // Overlap into the next slice by 1° so adjacent AA edges blend
    // instead of showing a thin seam. Skip on the last slice — the
    // rounded cap is already at a_hi and extending it further past
    // that produced the "detached red dot" symptom.
    if (i < UV_SLICES - 1)
      dsc.end_angle += 1;
    dsc.opa = LV_OPA_COVER;
    // Round only the two outward-facing caps — green start and red
    // end. Middle slices have both ends buried under the overlap so
    // rounding them costs cap-render work for no visible benefit.
    dsc.rounded = (i == 0 || i == UV_SLICES - 1) ? 1 : 0;
    lv_draw_arc(&layer, &dsc);
  }

  lv_canvas_finish_layer(canvas, &layer);
}

// Integer Kelvin → Fahrenheit, rounded to nearest. F = (K - 273.15) * 9/5 + 32.
static int k_to_f(int32_t k) {
  return (int)lroundf((k - 273.15f) * 9.0f / 5.0f + 32.0f);
}

// Re-render the (already value-updated) widget tree into the PSRAM cache and
// flip the screen to showing that flat image. Runs on the LVGL task (called
// from the weather_update timer) so no lvgl_port_lock is needed.
//
// render_group must be renderable for the snapshot, so its HIDDEN flag is
// cleared for the duration and re-set afterwards — both happen inside this
// one timer callback, atomically between composited frames, so the user
// never sees render_group directly.
//
// The snapshot area is render_group's coords plus its ext_draw_size; the
// group is a plain opaque container with no shadow/outline/transform of its
// own, so ext_draw_size is 0 and the snapshot is exactly 240×240 — matching
// CACHE_BYTES. lv_snapshot_take_to_draw_buf only reshapes the draw-buf header
// (no realloc) when the size already fits. The LVGL image cache is disabled
// (LV_CACHE_DEF_SIZE=0), so overwriting cache_buf in place and invalidating
// cache_img is enough — no decoded-image cache can go stale.
static void weather_cache_render(void) {
#if LV_USE_SNAPSHOT
  if (!cache_buf)
    return; // alloc failed at create; stay in live-render fallback mode

  lv_obj_remove_flag(render_group, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(render_group);

  lv_draw_buf_t dbuf = {};
  if (lv_draw_buf_init(&dbuf, CACHE_W, CACHE_H, LV_COLOR_FORMAT_RGB565,
                       /*stride auto*/ 0, cache_buf, CACHE_BYTES) ==
          LV_RESULT_OK &&
      lv_snapshot_take_to_draw_buf(render_group, LV_COLOR_FORMAT_RGB565,
                                   &dbuf) == LV_RESULT_OK) {
    lv_obj_remove_flag(cache_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(cache_img);
    lv_obj_add_flag(render_group, LV_OBJ_FLAG_HIDDEN);
  } else {
    // Snapshot failed — leave render_group visible so the screen still
    // shows something (live-rendered, slower, but correct).
    lv_obj_add_flag(cache_img, LV_OBJ_FLAG_HIDDEN);
  }
#endif
  // LV_USE_SNAPSHOT off: no-op; render_group stays visible (live render).
}

void weather_update(lv_timer_t *) {
  const WeatherState &w = ble.weather();
  if (w.version == last_weather_version)
    return;
  last_weather_version = w.version;

  // Arched location: only rebuild when the text actually changed —
  // the per-character layout is expensive (text size per glyph,
  // rotation styles) and would otherwise run every tick.
  if (w.loc != last_loc) {
    last_loc = w.loc;
    build_arched_location(location, w.loc.c_str());
  }

  lv_label_set_text(description, w.txt.c_str());

  char buf[16];
  int t_f = k_to_f(w.temp_k);
  snprintf(buf, sizeof(buf), "%d°", t_f);
  lv_label_set_text(temp, buf);
  // Squeeze the letter-spacing for 3-digit temps so they fit (matches
  // the placeholder note in the original layout).
  lv_obj_set_style_text_letter_space(temp, t_f >= 100 || t_f <= -10 ? -6 : 0,
                                     0);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, (t_f >= 100 || t_f <= -10) ? 0 : 6, 0);

  snprintf(buf, sizeof(buf), "High: %d°", k_to_f(w.hi_k));
  lv_label_set_text(hi, buf);
  snprintf(buf, sizeof(buf), "Low: %d°", k_to_f(w.lo_k));
  lv_label_set_text(lo, buf);

  lv_arc_set_value(hum, w.humidity);
  snprintf(buf, sizeof(buf), "💧%u%%", (unsigned)w.humidity);
  lv_label_set_text(humval, buf);

  uint8_t uv_clamped = w.uv > 10 ? 10 : w.uv;
  // lv_arc_set_value invalidates the obj, which triggers our
  // LV_EVENT_DRAW_MAIN_END callback. The callback reads the current
  // value with lv_arc_get_value and paints the gradient + dims the
  // slices past the knob — no per-update mutation needed here.
  lv_arc_set_value(uv, uv_clamped);
  snprintf(buf, sizeof(buf), "🔆 %u", (unsigned)w.uv);
  lv_label_set_text(uvval, buf);

  lv_image_set_src(icon, icon_for_code(w.code));

  // Wind direction arrow. The arrow image's natural orientation is
  // pointing up = north; rotating by (wdir + 180) makes it point in
  // the direction the wind is GOING (the convention most weather
  // apps display) instead of where it's coming from. Use
  // lv_image_set_rotation (image-API) rather than the obj-style
  // version so the rotation pivots around the source centre and the
  // arrow stays in place as the angle changes. Units: tenths of a
  // degree.
  lv_image_set_rotation(wdir, ((w.wind_dir + 180) % 360) * 10);

  // Wind speed: Gadgetbridge sends the value in km/h despite the
  // field name. Confirmed empirically — a real 2.5 mph wind came
  // through as 4.02 km/h ≈ "4.0" in the JSON, which we were
  // multiplying by 2.23694 (m/s→mph) and displaying as ~9 mph; the
  // 3.6× error is exactly the m/s↔km/h factor. Convert km/h → mph:
  // 1 km/h = 0.621371 mph.
  int mph = (int)lroundf(w.wind_mps * 0.621371f);
  snprintf(buf, sizeof(buf), "%d\nmph", mph);
  lv_label_set_text(wspd, buf);

  // Everything above mutated the (hidden) widget tree; now re-render it into
  // the PSRAM cache and flip the screen to the flat cached image so
  // subsequent frames — especially scroll frames — are a single blit.
  weather_cache_render();
}

lv_obj_t *weather_create(lv_obj_t *parent) {
  lv_obj_t *scr = create_screen(parent);
  lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

  // All weather widgets are built inside render_group rather than directly on
  // the screen so weather_cache_render() can snapshot the whole lot in one
  // shot. Opaque black bg (so the snapshot is fully painted with no
  // transparent gaps) and no shadow/outline/transform of its own (so its
  // ext_draw_size stays 0 and the snapshot is exactly 240×240). See the
  // "Rendered-once cache" note above.
  render_group = lv_obj_create(scr);
  lv_obj_remove_style_all(render_group);
  lv_obj_set_size(render_group, 240, 240);
  lv_obj_align(render_group, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_opa(render_group, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(render_group, lv_color_black(), 0);
  lv_obj_set_flag(render_group, LV_OBJ_FLAG_CLICKABLE, false);
  lv_obj_set_flag(render_group, LV_OBJ_FLAG_SCROLLABLE, false);

  // Arched location label: one rotated label per character laid out along
  // the top of a circle. `location` is the container; children are the
  // per-glyph labels built by build_arched_location.
  location = lv_obj_create(render_group);
  lv_obj_remove_style_all(location);
  lv_obj_set_size(location, 240, 240);
  lv_obj_align(location, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_flag(location, LV_OBJ_FLAG_CLICKABLE, false);
  lv_obj_set_flag(location, LV_OBJ_FLAG_SCROLLABLE, false);
  build_arched_location(location, "—");

  description = lv_label_create(render_group);
  lv_label_set_text(description, "—");

  lv_obj_align(description, LV_ALIGN_TOP_MID, 0, 48);
  lv_obj_set_style_text_font(description, &ProductSansRegular_24, 0);
  lv_obj_set_width(description, 220);
  lv_label_set_long_mode(description, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_align(description, LV_TEXT_ALIGN_CENTER, 0);

  temp = lv_label_create(render_group);
  lv_label_set_text(temp, "—°");
  lv_obj_align(temp, LV_ALIGN_RIGHT_MID, -6, 0);
  lv_obj_set_style_text_font(temp, &ProductSansBold_92, 0);

  icon = lv_image_create(render_group);
  lv_obj_set_size(icon, 64, 64);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, 6, 0);
  lv_image_set_src(icon, &IMG_PARTLY_CLOUDY);
  // lv_image_set_scale(icon, 256 * 64 / 192);

  hi = lv_label_create(render_group);
  lv_obj_align(hi, LV_ALIGN_LEFT_MID, 30, 50);
  lv_obj_set_style_text_align(hi, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(hi, &ProductSansRegular_16, 0);
  lv_label_set_text(hi, "High: —°");

  lo = lv_label_create(render_group);
  lv_obj_align(lo, LV_ALIGN_RIGHT_MID, -30, 50);
  lv_obj_set_style_text_align(lo, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(lo, &ProductSansRegular_16, 0);
  lv_label_set_text(lo, "Low: —°");

  hum = lv_arc_create(render_group);
  lv_obj_set_size(hum, 232, 232);
  lv_obj_align(hum, LV_ALIGN_CENTER, 0, 0);
  lv_arc_set_bg_angles(hum, W_ARC_POS - W_ARC_SIZE / 2,
                       W_ARC_POS + W_ARC_SIZE / 2);
  lv_arc_set_mode(hum, LV_ARC_MODE_REVERSE);
  lv_obj_set_style_bg_opa(hum, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_arc_width(hum, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(hum, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(hum, lv_color_hex(0x002244), LV_PART_MAIN);
  lv_obj_set_style_arc_color(hum, lv_color_hex(0x0088FF), LV_PART_INDICATOR);
  lv_obj_set_flag(hum, LV_OBJ_FLAG_CLICKABLE, false);

  lv_arc_set_range(hum, 0, 100);
  lv_arc_set_value(hum, 0);

  humval = lv_label_create(render_group);
  lv_obj_set_style_text_font(humval, &ProductSansRegular_16_emoji, 0);
  lv_obj_set_style_text_align(humval, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(humval, 100);
  lv_label_set_text(humval, "💧—%");
  lv_obj_align(humval, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_update_layout(humval);

  lv_obj_set_style_transform_pivot_x(humval, 120 - lv_obj_get_x(humval), 0);
  lv_obj_set_style_transform_pivot_y(humval, 120 - lv_obj_get_y(humval), 0);
  lv_obj_set_style_transform_rotation(humval, (-90 + W_ARC_POS) * 10, 0);

  // Pre-baked UV gradient. Rendered exactly once into a small
  // canvas here — no per-frame draw_cb, so weather updates that
  // invalidate the knob region don't re-render 48 anti-aliased arc
  // slices. Added BEFORE the uv arc so it sits below the knob in
  // z-order. The canvas is opaque black outside the arc strip, but
  // that matches scr's own bg so the extra rectangle is invisible;
  // labels / widgets created later in this function still render on
  // top thanks to z-order, so nothing gets occluded.
  {
    lv_obj_t *uv_grad = lv_canvas_create(render_group);
    lv_canvas_set_buffer(uv_grad, uv_gradient_buf, UV_CANVAS_W, UV_CANVAS_H,
                         LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(uv_grad, UV_CANVAS_X, UV_CANVAS_Y);
    lv_obj_set_flag(uv_grad, LV_OBJ_FLAG_CLICKABLE, false);
    uv_gradient_bake(uv_grad);
  }

  uv = lv_arc_create(render_group);
  lv_obj_set_size(uv, 232, 232);
  lv_obj_align(uv, LV_ALIGN_CENTER, 0, 0);
  lv_arc_set_bg_angles(uv, 180 - W_ARC_POS - W_ARC_SIZE / 2,
                       180 - W_ARC_POS + W_ARC_SIZE / 2);
  lv_arc_set_mode(uv, LV_ARC_MODE_NORMAL);
  // Main + indicator arcs are hidden — the gradient track is the
  // pre-baked canvas created just above. Only the knob remains
  // visible from this widget; lv_arc_set_value still moves it along
  // the gradient like a normal slider.
  //
  // Indicator arc_width MUST stay set to the same 6 px we drew the
  // gradient at — lv_arc places the knob at (obj_radius − indicator_
  // arc_width/2), so leaving it at 0 (the default after we hid it)
  // parked the knob at the outer edge of the gradient instead of the
  // centreline. Setting both widths back gives correct knob placement.
  lv_obj_set_style_arc_width(uv, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(uv, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(uv, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(uv, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_flag(uv, LV_OBJ_FLAG_CLICKABLE, false);

  // Knob: 6 px white disc (matching the arc width) with a 1 px black
  // ring around it for visual separation. LVGL knob sizing is
  //   knob_diameter = arc_width + 2 * pad_all
  // and the border is drawn inside the bg, so
  //   white_diameter = knob_diameter − 2 * border_width
  //                  = (arc_width + 2*pad_all) − 2*border
  // For arc_width=6 and white=6, pad_all = border = 1.
  lv_obj_set_style_bg_opa(uv, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_bg_color(uv, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_border_color(uv, lv_color_black(), LV_PART_KNOB);
  lv_obj_set_style_border_width(uv, 1, LV_PART_KNOB);
  lv_obj_set_style_border_opa(uv, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_radius(uv, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_pad_all(uv, 1, LV_PART_KNOB);

  lv_arc_set_range(uv, 0, 10);
  // Mid-range placeholder so the gradient is visible on both sides of
  // the knob before the first BLE update lands.
  lv_arc_set_value(uv, 5);

  // Was hidden as a workaround for the per-frame DRAW_MAIN_BEGIN
  // callback dominating render time. Now the gradient is a passive
  // pre-rendered canvas, so each frame just blits the cached pixels
  // — re-enable the arc so the knob shows.

  uvval = lv_label_create(render_group);
  lv_obj_set_style_text_font(uvval, &ProductSansRegular_16_emoji, 0);
  lv_obj_set_style_text_align(uvval, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(uvval, 100);
  lv_label_set_text(uvval, "🔆 —");
  lv_obj_align(uvval, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_update_layout(uvval);

  lv_obj_set_style_transform_pivot_x(uvval, 120 - lv_obj_get_x(uvval), 0);
  lv_obj_set_style_transform_pivot_y(uvval, 120 - lv_obj_get_y(uvval), 0);
  lv_obj_set_style_transform_rotation(uvval, (90 - W_ARC_POS) * 10, 0);

  // Wind indicator. The arrow image (IMG_ARROW, 64×64, naturally
  // points up = north) sits behind the speed text, both centred on
  // the same screen point — created first so it's drawn beneath the
  // label in z-order. Rotation pivot defaults to the image's own
  // centre in source coords, which is also the point on screen the
  // label is centred on, so the arrow spins cleanly around the text.
  //
  // The 0,84 align offset puts the centre at screen y=204, matching
  // where the original emoji-rotation pivot was.
  wdir = lv_image_create(render_group);
  lv_image_set_src(wdir, &IMG_ARROW);
  // Scale the 64 px source down to ~48 px to match the original
  // wind-dial size. LVGL scale value: 256 = 1:1.
  lv_image_set_scale(wdir, 256 * 48 / 96);
  lv_obj_align(wdir, LV_ALIGN_CENTER, 0, 84);
  // Use lv_image_set_rotation, NOT lv_obj_set_style_transform_rotation.
  // The obj-style version pivots around the obj's top-left by default,
  // which makes the image fly off to the side as the angle changes;
  // the image API uses the image's pivot (defaults to source centre)
  // so the arrow rotates in place. Start pointing down to match the
  // original emoji's initial state. weather_update overwrites this on
  // the first BLE update.
  lv_image_set_rotation(wdir, 210 * 10);

  wspd = lv_label_create(render_group);
  lv_obj_set_style_text_align(wspd, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(wspd, &ProductSansRegular_14, 0);
  lv_obj_set_style_text_line_space(wspd, -4, 0);
  lv_label_set_text(wspd, "—\nmph");
  lv_obj_align(wspd, LV_ALIGN_CENTER, 0, 84);

#if LV_USE_SNAPSHOT
  // Flat cached image, shown in place of render_group once the first snapshot
  // has been taken. Created last so it sits on top of render_group in
  // z-order; starts hidden until weather_cache_render() produces the first
  // snapshot. The PSRAM buffer is allocated once and reused for every update
  // — if the allocation fails we simply never switch to cached mode and keep
  // live-rendering render_group. Buffer is in PSRAM specifically so this
  // caching adds no internal-SRAM pressure (the BT controller is sensitive to
  // it — see ble.cpp / sdkconfig.defaults notes on LV_USE_SNAPSHOT).
  cache_buf = (uint8_t *)heap_caps_malloc(CACHE_BYTES,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  cache_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  cache_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  cache_dsc.header.w = CACHE_W;
  cache_dsc.header.h = CACHE_H;
  cache_dsc.header.stride = CACHE_W * 2;
  cache_dsc.data_size = CACHE_BYTES;
  cache_dsc.data = cache_buf;

  cache_img = lv_image_create(scr);
  lv_obj_align(cache_img, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_flag(cache_img, LV_OBJ_FLAG_CLICKABLE, false);
  lv_obj_add_flag(cache_img, LV_OBJ_FLAG_HIDDEN);
  if (cache_buf)
    lv_image_set_src(cache_img, &cache_dsc);
#endif

  // 1 s tick is plenty — Gadgetbridge weather updates land at most a
  // few times per hour. The version gate in weather_update bails out
  // immediately when there's nothing new, so the per-tick cost is
  // basically a single counter compare.
  lv_timer_create(weather_update, 1000, NULL);

  // lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
  // lv_obj_add_flag(wdir, LV_OBJ_FLAG_HIDDEN);

  return scr;
}
