#include "ui.hpp"
#include <sys/time.h>
#include <math.h>
#include <stdio.h>

#define DEG2RAD (M_PI / 180.0f)

// Clock-face convention: t is degrees clockwise from 12 o'clock.
//   t = 0   → 12 o'clock (top)
//   t = 90  → 3 o'clock  (right)
//   t = 180 → 6 o'clock  (bottom)
//   t = 270 → 9 o'clock  (left)
#define POS_RADIAL(r, t) ((r) * sinf((t) * DEG2RAD)), (-(r) * cosf((t) * DEG2RAD))

// Odd height so the apex lands on an exact center row (y=8) — an even
// height has no integer center, and `lv_point_from_precise` truncates float
// vertex coords to int, which would break the apex symmetry.
#define HAND_H 17
#define HAND_TAIL 12                          // gray tail behind the pivot
#define HAND_GRAY_LEN 54                     // gray base width (canvas x = 0..47)
// Triangle tip length. Apex angle ≈ 2·atan(half_base / altitude); the +1-row
// bottom-vertex overshoot used in draw_hand_shapes shifts the measured angle
// up by ~3°.
//   (HAND_H+1)/2 = 9  → altitude 8, half_base 8 → ≈90° (the old value)
//                 11  → altitude 10, half_base 8 → ≈80°  (pointier)
#define HAND_TRIANGLE_LEN 11
#define HAND_BORDER 2

// Screen-coords offset of every hand's shadow from its own hand. Stays fixed
// regardless of rotation because each shadow is rendered by a sibling obj
// whose rotation pivot sits at watch_center + (OFFSET, OFFSET) instead of at
// watch_center itself.
#define HAND_SHADOW_OFFSET 2

#define SECOND_HAND_BODY 46
#define MINUTE_HAND_BODY 42
#define HOUR_HAND_BODY 16

#define MINUTE_HAND_W (HAND_GRAY_LEN + MINUTE_HAND_BODY + HAND_TRIANGLE_LEN) // 104
#define HOUR_HAND_W (HAND_GRAY_LEN + HOUR_HAND_BODY + HAND_TRIANGLE_LEN)     // 72

static uint8_t last_sec = 255, last_min = 255, last_hour = 255;
static uint8_t last_day = 255, last_month = 255;
static uint32_t last_battery_check = 0;
static uint32_t last_battery_mv = 0;

LV_DRAW_BUF_DEFINE_STATIC(minute_hand_buf,        MINUTE_HAND_W, HAND_H, LV_COLOR_FORMAT_ARGB8888);
LV_DRAW_BUF_DEFINE_STATIC(hour_hand_buf,          HOUR_HAND_W,   HAND_H, LV_COLOR_FORMAT_ARGB8888);
LV_DRAW_BUF_DEFINE_STATIC(minute_hand_shadow_buf, MINUTE_HAND_W, HAND_H, LV_COLOR_FORMAT_ARGB8888);
LV_DRAW_BUF_DEFINE_STATIC(hour_hand_shadow_buf,   HOUR_HAND_W,   HAND_H, LV_COLOR_FORMAT_ARGB8888);

// File-scope statics so they don't collide with the identically-named globals
// in analogwatch.cpp / rotarywatch.cpp / timewatch.cpp now that watchface.cpp
// links every face into the binary.
static lv_obj_t *secondhand;
static lv_obj_t *secondhand_shadow;
static lv_obj_t *minutehand;
static lv_obj_t *minutehand_shadow;
static lv_obj_t *hourhand;
static lv_obj_t *hourhand_shadow;
static lv_obj_t *date_label;

// lv_obj_t *schedulelabel;
// lv_obj_t *datelabel;

// lv_obj_t *timerarc;

// lv_obj_t *baticon;
// lv_obj_t *battery;
// lv_obj_t *steps;
// lv_obj_t *glance;

// lv_obj_t *wifiicon;

// Draw the hand's outer outline (gray base + outer pentagon = body rect +
// triangle) into `layer` at offset (ox, oy) using a single color/opa. This
// is what we lay down twice as shape-following drop shadows under the real
// hand for the minute hand.
static void draw_hand_silhouette(lv_layer_t *layer, int ox, int oy, int W,
                                 lv_color_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = color;
    rect_dsc.bg_opa = opa;
    rect_dsc.border_width = 0;
    rect_dsc.radius = 0;

    const int outer_base_x = W - HAND_TRIANGLE_LEN;

    // Body silhouette: gray-base and outer-pentagon-body fuse into one rect.
    lv_area_t body = {ox, oy, ox + outer_base_x - 1, oy + HAND_H - 1};
    lv_draw_rect(layer, &rect_dsc, &body);

    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.color = color;
    tri_dsc.opa = opa;
    tri_dsc.p[0].x = ox + outer_base_x; tri_dsc.p[0].y = oy + 0;
    tri_dsc.p[1].x = ox + W - 1;        tri_dsc.p[1].y = oy + (HAND_H - 1) / 2;
    tri_dsc.p[2].x = ox + outer_base_x; tri_dsc.p[2].y = oy + HAND_H;
    lv_draw_triangle(layer, &tri_dsc);
}

// Draw the actual hand (gray base + outer light-gray pentagon + inner white
// pentagon) into `layer` at offset (ox, oy).
static void draw_hand_shapes(lv_layer_t *layer, int ox, int oy, int W)
{
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.border_width = 0;
    rect_dsc.radius = 0;

    // Gray base (fixed length on every hand).
    rect_dsc.bg_color = lv_color_hex(0x333333);
    lv_area_t base_area = {ox, oy, ox + HAND_GRAY_LEN - 1, oy + HAND_H - 1};
    lv_draw_rect(layer, &rect_dsc, &base_area);

    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.opa = LV_OPA_COVER;

    const int outer_base_x = W - HAND_TRIANGLE_LEN;
    const int outer_apex_x = W - 1;
    const int outer_apex_y = (HAND_H - 1) / 2;

    // Outer (light gray) pentagon: body rect + 90° triangle tip.
    rect_dsc.bg_color = lv_color_hex(0xaaaaaa);
    lv_area_t outer_body = {ox + HAND_GRAY_LEN, oy, ox + outer_base_x - 1, oy + HAND_H - 1};
    lv_draw_rect(layer, &rect_dsc, &outer_body);

    tri_dsc.color = lv_color_hex(0xaaaaaa);
    tri_dsc.p[0].x = ox + outer_base_x; tri_dsc.p[0].y = oy + 0;
    tri_dsc.p[1].x = ox + outer_apex_x; tri_dsc.p[1].y = oy + outer_apex_y;
    // Bottom vertex pushed 1px past the hand's bottom row so LVGL's edge
    // rule doesn't light up the row below the apex (keeps the tip 1 px wide).
    tri_dsc.p[2].x = ox + outer_base_x; tri_dsc.p[2].y = oy + HAND_H;
    lv_draw_triangle(layer, &tri_dsc);

    // Inner (white) pentagon, one extra px shorter than the outer (see
    // earlier discussion of diagonal border thickness).
    const int inner_base_x = outer_base_x - 1;
    const int inner_apex_x = outer_apex_x - HAND_BORDER - 1;

    rect_dsc.bg_color = lv_color_hex(0xffffff);
    lv_area_t inner_body = {
        ox + HAND_GRAY_LEN + HAND_BORDER,
        oy + HAND_BORDER,
        ox + inner_base_x - 1,
        oy + HAND_H - 1 - HAND_BORDER,
    };
    lv_draw_rect(layer, &rect_dsc, &inner_body);

    tri_dsc.color = lv_color_hex(0xffffff);
    tri_dsc.p[0].x = ox + inner_base_x; tri_dsc.p[0].y = oy + HAND_BORDER;
    tri_dsc.p[1].x = ox + inner_apex_x; tri_dsc.p[1].y = oy + outer_apex_y;
    tri_dsc.p[2].x = ox + inner_base_x; tri_dsc.p[2].y = oy + HAND_H - 1 - HAND_BORDER;
    lv_draw_triangle(layer, &tri_dsc);
}

// Create a watch hand. `W` is the hand's logical width (gray + body + tip).
// The canvas is positioned so its rotation pivot (canvas-local HAND_TAIL,
// HAND_H/2) lands at the watch center. Shadows are handled by a separate
// `create_hand_shadow` canvas — see below.
static lv_obj_t *create_hand(lv_obj_t *parent, lv_draw_buf_t *buf, int W)
{
    const int pivot_x = HAND_TAIL;
    const int pivot_y = HAND_H / 2;

    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(canvas, buf);
    lv_obj_align(canvas, LV_ALIGN_CENTER, W / 2 - pivot_x, HAND_H / 2 - pivot_y);

    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    draw_hand_shapes(&layer, 0, 0, W);
    lv_canvas_finish_layer(canvas, &layer);

    lv_obj_set_style_transform_pivot_x(canvas, pivot_x, 0);
    lv_obj_set_style_transform_pivot_y(canvas, pivot_y, 0);
    return canvas;
}

// Create a sibling shadow canvas for `create_hand`. Identical canvas size
// and pivot, but the canvas alignment is offset by HAND_SHADOW_OFFSET in
// screen coords, so the pivot in screen lands at watch_center + (OFFSET,
// OFFSET) instead of watch_center. Apply the same rotation as the hand and
// the shadow always trails the hand by (OFFSET, OFFSET) in screen pixels,
// regardless of rotation angle.
static lv_obj_t *create_hand_shadow(lv_obj_t *parent, lv_draw_buf_t *buf, int W)
{
    const int pivot_x = HAND_TAIL;
    const int pivot_y = HAND_H / 2;

    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(canvas, buf);
    lv_obj_align(canvas, LV_ALIGN_CENTER,
                 W / 2 - pivot_x + HAND_SHADOW_OFFSET,
                 HAND_H / 2 - pivot_y + HAND_SHADOW_OFFSET);

    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    draw_hand_silhouette(&layer, 0, 0, W, lv_color_black(), LV_OPA_50);
    lv_canvas_finish_layer(canvas, &layer);

    lv_obj_set_style_transform_pivot_x(canvas, pivot_x, 0);
    lv_obj_set_style_transform_pivot_y(canvas, pivot_y, 0);
    return canvas;
}

lv_style_t rtick_style;
lv_style_t ctick_style;

lv_obj_t *divewatch_create(lv_obj_t *parent)
{
    lv_color_t gray = lv_theme_get_color_secondary(parent);

    lv_obj_t *scr = create_screen(parent);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x222222), 0);

    lv_style_init(&rtick_style);
    lv_style_set_bg_color(&rtick_style, lv_color_hex(0xffffff));
    lv_style_set_bg_opa(&rtick_style, LV_OPA_COVER);
    lv_style_set_radius(&rtick_style, 0);
    lv_style_set_border_color(&rtick_style, lv_color_hex(0xaaaaaa));
    lv_style_set_border_width(&rtick_style, 2);

    lv_obj_t *h12 = lv_obj_create(scr);
    lv_obj_set_size(h12, 10, 18);
    lv_obj_align(h12, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_add_style(h12, &rtick_style, 0);
    lv_obj_set_scroll_dir(h12, LV_DIR_NONE);

    lv_obj_t *h3 = lv_obj_create(scr);
    lv_obj_set_size(h3, 18, 10);
    lv_obj_align(h3, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_style(h3, &rtick_style, 0);
    lv_obj_set_scroll_dir(h3, LV_DIR_NONE);

    lv_obj_t *h6 = lv_obj_create(scr);
    lv_obj_set_size(h6, 10, 18);
    lv_obj_align(h6, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_add_style(h6, &rtick_style, 0);
    lv_obj_set_scroll_dir(h6, LV_DIR_NONE);

    lv_obj_t *h9 = lv_obj_create(scr);
    lv_obj_set_size(h9, 18, 10);
    lv_obj_align(h9, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_add_style(h9, &rtick_style, 0);
    lv_obj_set_scroll_dir(h9, LV_DIR_NONE);

    lv_style_init(&ctick_style);
    lv_style_set_bg_color(&ctick_style, lv_color_hex(0xffffff));
    lv_style_set_bg_opa(&ctick_style, LV_OPA_COVER);
    lv_style_set_radius(&ctick_style, LV_RADIUS_CIRCLE);
    lv_style_set_border_color(&ctick_style, lv_color_hex(0xaaaaaa));
    lv_style_set_border_width(&ctick_style, 2);
    lv_style_set_size(&ctick_style, 20, 20);

    lv_obj_t *c1 = lv_obj_create(scr);
    lv_obj_align(c1, LV_ALIGN_CENTER, POS_RADIAL(100, 30));
    lv_obj_add_style(c1, &ctick_style, 0);
    lv_obj_set_scroll_dir(c1, LV_DIR_NONE);

    lv_obj_t *c2 = lv_obj_create(scr);
    lv_obj_align(c2, LV_ALIGN_CENTER, POS_RADIAL(100, 60));
    lv_obj_add_style(c2, &ctick_style, 0);
    lv_obj_set_scroll_dir(c2, LV_DIR_NONE);

    lv_obj_t *c4 = lv_obj_create(scr);
    lv_obj_align(c4, LV_ALIGN_CENTER, POS_RADIAL(100, 120));
    lv_obj_add_style(c4, &ctick_style, 0);
    lv_obj_set_scroll_dir(c4, LV_DIR_NONE);

    lv_obj_t *c5 = lv_obj_create(scr);
    lv_obj_align(c5, LV_ALIGN_CENTER, POS_RADIAL(100, 150));
    lv_obj_add_style(c5, &ctick_style, 0);
    lv_obj_set_scroll_dir(c5, LV_DIR_NONE);

    lv_obj_t *c7 = lv_obj_create(scr);
    lv_obj_align(c7, LV_ALIGN_CENTER, POS_RADIAL(100, 210));
    lv_obj_add_style(c7, &ctick_style, 0);
    lv_obj_set_scroll_dir(c7, LV_DIR_NONE);

    lv_obj_t *c8 = lv_obj_create(scr);
    lv_obj_align(c8, LV_ALIGN_CENTER, POS_RADIAL(100, 240));
    lv_obj_add_style(c8, &ctick_style, 0);
    lv_obj_set_scroll_dir(c8, LV_DIR_NONE);

    lv_obj_t *c10 = lv_obj_create(scr);
    lv_obj_align(c10, LV_ALIGN_CENTER, POS_RADIAL(100, 300));
    lv_obj_add_style(c10, &ctick_style, 0);
    lv_obj_set_scroll_dir(c10, LV_DIR_NONE);

    lv_obj_t *c11 = lv_obj_create(scr);
    lv_obj_align(c11, LV_ALIGN_CENTER, POS_RADIAL(100, 330));
    lv_obj_add_style(c11, &ctick_style, 0);
    lv_obj_set_scroll_dir(c11, LV_DIR_NONE);

    lv_obj_t *l12 = lv_label_create(scr);
    lv_label_set_text(l12, "12");
    lv_obj_align(l12, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_text_color(l12, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(l12, &LexendExaSemiBold_48, 0);
    
    lv_obj_t *l3 = lv_label_create(scr);
    lv_label_set_text(l3, "3");
    lv_obj_align(l3, LV_ALIGN_RIGHT_MID, -36, 0);
    lv_obj_set_style_text_color(l3, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(l3, &LexendExaSemiBold_48, 0);

    lv_obj_t *l6 = lv_label_create(scr);
    lv_label_set_text(l6, "6");
    lv_obj_align(l6, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_obj_set_style_text_color(l6, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(l6, &LexendExaSemiBold_48, 0);

    lv_obj_t *l9 = lv_label_create(scr);
    lv_label_set_text(l9, "9");
    lv_obj_align(l9, LV_ALIGN_LEFT_MID, 36, 0);
    lv_obj_set_style_text_color(l9, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(l9, &LexendExaSemiBold_48, 0);

    lv_obj_t *datebox = lv_obj_create(scr);
    lv_obj_set_size(datebox, 24, 16);
    // Clock-angle 135° = midway between 4 (120°) and 5 (150°). Radius 80
    // matches the original spacing — keeps the box safely inside the radius-
    // 100 tick ring so the 45° rotation (which makes the box's long axis
    // radial) doesn't push its far corner visually past the 5 tick.
    lv_obj_align(datebox, LV_ALIGN_CENTER, POS_RADIAL(80, 135));
    lv_obj_set_scroll_dir(datebox, LV_DIR_NONE);
    lv_obj_set_style_radius(datebox, 1, 0);
    lv_obj_set_style_border_width(datebox, 1, 0);
    lv_obj_set_style_border_color(datebox, lv_color_hex(0x666666), 0);
    lv_obj_set_style_bg_color(datebox, lv_color_hex(0x222222), 0);

    // Hardcoded pivot — `lv_obj_get_width(datebox)` returns 0 here because
    // LVGL hasn't refreshed the layout yet. That left the pivot at (0, 0),
    // and a 45° rotation around the box's top-left shifts the visible center
    // ~12 px down-left, landing the box at clock-angle ~144° instead of 135°.
    lv_obj_set_style_transform_pivot_x(datebox, 28 / 2, 0);
    lv_obj_set_style_transform_pivot_y(datebox, 16 / 2, 0);
    // Absolute screen rotation (CW), independent of POS_RADIAL's angle.
    lv_obj_set_style_transform_rotation(datebox, 45 * 10, 0);

    date_label = lv_label_create(datebox);
    lv_obj_center(date_label);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xeeeeee), 0);
    lv_obj_set_style_text_font(date_label, &LexendExaSemiBold_14, 0);

    LV_DRAW_BUF_INIT_STATIC(hour_hand_buf);
    LV_DRAW_BUF_INIT_STATIC(minute_hand_buf);
    LV_DRAW_BUF_INIT_STATIC(hour_hand_shadow_buf);
    LV_DRAW_BUF_INIT_STATIC(minute_hand_shadow_buf);

    // Each hand has a shadow twin created right before it (so the shadow
    // sits one layer below the hand). The twin is offset HAND_SHADOW_OFFSET
    // px down-right in screen coords; both rotate together, so the offset
    // direction stays fixed in screen space even as the hands sweep.
    hourhand_shadow   = create_hand_shadow(scr, &hour_hand_shadow_buf,   HOUR_HAND_W);
    hourhand          = create_hand       (scr, &hour_hand_buf,          HOUR_HAND_W);
    minutehand_shadow = create_hand_shadow(scr, &minute_hand_shadow_buf, MINUTE_HAND_W);
    minutehand        = create_hand       (scr, &minute_hand_buf,        MINUTE_HAND_W);

    // Second hand. Total width = HAND_TAIL (sticking out behind the pivot) +
    // HAND_GRAY_LEN + SECOND_HAND_BODY (the forward visible portion). Pivot
    // lives at obj-x = HAND_TAIL, so HAND_TAIL px hangs off the back side of
    // the pivot and the rest extends forward.
    const int second_hand_w = HAND_GRAY_LEN + SECOND_HAND_BODY + HAND_TAIL;
    const int second_hand_h = 14;

    // Shadow container for the second hand, created before `secondhand` so it
    // renders underneath. Same size and pivot, but aligned +OFFSET px down-
    // right in screen coords so its rotation pivot lands at watch_center +
    // (OFFSET, OFFSET) rather than at watch_center itself. Apply the same
    // rotation to both and the shadow trails by exactly (OFFSET, OFFSET) in
    // screen space at any angle.
    secondhand_shadow = lv_obj_create(scr);
    lv_obj_set_size(secondhand_shadow, second_hand_w, second_hand_h);
    lv_obj_set_style_border_width(secondhand_shadow, 0, 0);
    lv_obj_set_style_radius(secondhand_shadow, 0, 0);
    lv_obj_set_style_bg_opa(secondhand_shadow, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(secondhand_shadow, LV_DIR_NONE);
    lv_obj_set_style_pad_all(secondhand_shadow, 0, 0);
    lv_obj_align(secondhand_shadow, LV_ALIGN_CENTER,
                 second_hand_w / 2 - HAND_TAIL + HAND_SHADOW_OFFSET,
                 HAND_SHADOW_OFFSET);
    lv_obj_set_style_transform_pivot_x(secondhand_shadow, HAND_TAIL, 0);
    lv_obj_set_style_transform_pivot_y(secondhand_shadow, second_hand_h / 2, 0);

    // Shadow children mirror the real second hand's line + box. The cyan tip
    // is omitted because, like the gray line, it darkens to the same flat
    // shadow color anyway — one rectangle is enough.
    lv_obj_t *shadow_secondline = lv_obj_create(secondhand_shadow);
    lv_obj_set_size(shadow_secondline, second_hand_w, 2);
    lv_obj_set_style_border_width(shadow_secondline, 0, 0);
    lv_obj_set_style_radius(shadow_secondline, 0, 0);
    lv_obj_set_style_bg_color(shadow_secondline, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(shadow_secondline, LV_OPA_50, 0);
    lv_obj_set_scroll_dir(shadow_secondline, LV_DIR_NONE);
    lv_obj_align(shadow_secondline, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *shadow_secondbox = lv_obj_create(secondhand_shadow);
    lv_obj_set_size(shadow_secondbox, 14, 14);
    lv_obj_set_style_border_width(shadow_secondbox, 0, 0);
    lv_obj_set_style_radius(shadow_secondbox, 0, 0);
    lv_obj_set_style_bg_color(shadow_secondbox, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(shadow_secondbox, LV_OPA_50, 0);
    lv_obj_set_scroll_dir(shadow_secondbox, LV_DIR_NONE);
    lv_obj_align(shadow_secondbox, LV_ALIGN_RIGHT_MID, -SECOND_HAND_BODY / 2 + 7, 0);

    // Center cap shadow — sits at canvas-local (HAND_TAIL, second_hand_h/2),
    // which is the shadow container's pivot. In screen coords the shadow's
    // pivot lands at watch_center + (OFFSET, OFFSET), so this dot is the
    // shadow of the real center cap below.
    lv_obj_t *shadow_centerdot = lv_obj_create(secondhand_shadow);
    lv_obj_set_size(shadow_centerdot, 5, 5);
    lv_obj_set_style_border_width(shadow_centerdot, 0, 0);
    lv_obj_set_style_radius(shadow_centerdot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(shadow_centerdot, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(shadow_centerdot, LV_OPA_50, 0);
    lv_obj_set_scroll_dir(shadow_centerdot, LV_DIR_NONE);
    lv_obj_align(shadow_centerdot, LV_ALIGN_CENTER, HAND_TAIL - second_hand_w / 2, 0);

    // The real second hand on top of its shadow. No shadow styles on any of
    // these children — all shadow handling lives in secondhand_shadow above.
    secondhand = lv_obj_create(scr);
    lv_obj_set_size(secondhand, second_hand_w, second_hand_h);
    lv_obj_set_style_border_width(secondhand, 0, 0);
    lv_obj_set_style_radius(secondhand, 0, 0);
    lv_obj_set_style_bg_opa(secondhand, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(secondhand, LV_DIR_NONE);
    lv_obj_set_style_pad_all(secondhand, 0, 0);
    lv_obj_align(secondhand, LV_ALIGN_CENTER, second_hand_w / 2 - HAND_TAIL, 0);
    lv_obj_set_style_transform_pivot_x(secondhand, HAND_TAIL, 0);
    lv_obj_set_style_transform_pivot_y(secondhand, second_hand_h / 2, 0);

    lv_obj_t *secondline = lv_obj_create(secondhand);
    lv_obj_set_size(secondline, second_hand_w, 2);
    lv_obj_set_style_border_width(secondline, 0, 0);
    lv_obj_set_style_radius(secondline, 0, 0);
    lv_obj_set_style_bg_color(secondline, lv_color_hex(0x333333), 0);
    lv_obj_set_scroll_dir(secondline, LV_DIR_NONE);
    lv_obj_align(secondline, LV_ALIGN_LEFT_MID, 0, 0);

    
    lv_obj_t *secondend = lv_obj_create(secondhand);
    lv_obj_set_size(secondend, SECOND_HAND_BODY, 2);
    lv_obj_set_style_border_width(secondend, 0, 0);
    lv_obj_set_style_radius(secondend, 0, 0);
    lv_obj_add_style(secondend, &accent_bg_style, 0);
    lv_obj_set_scroll_dir(secondend, LV_DIR_NONE);
    lv_obj_align(secondend, LV_ALIGN_RIGHT_MID, 0, 0);
    
    lv_obj_t *secondbox = lv_obj_create(secondhand);
    lv_obj_set_size(secondbox, 14, 14);
    lv_obj_set_style_border_width(secondbox, 2, 0);
    lv_obj_add_style(secondbox, &accent_border_style, 0);
    lv_obj_set_style_radius(secondbox, 0, 0);
    lv_obj_set_style_bg_color(secondbox, lv_color_hex(0x333333), 0);
    lv_obj_set_scroll_dir(secondbox, LV_DIR_NONE);
    lv_obj_align(secondbox, LV_ALIGN_RIGHT_MID, -SECOND_HAND_BODY / 2 + 7, 0);

    // Center cap, as the last child of secondhand so it draws on top of the
    // line/end/box. Positioned at canvas-local (HAND_TAIL, second_hand_h/2) —
    // exactly the rotation pivot — so the cap stays pinned to the watch
    // center while the rest of the hand sweeps around it. Same base color as
    // the second hand's line (0x333333).
    lv_obj_t *centerdot = lv_obj_create(secondhand);
    lv_obj_set_size(centerdot, 5, 5);
    lv_obj_set_style_border_width(centerdot, 0, 0);
    lv_obj_set_style_radius(centerdot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(centerdot, lv_color_hex(0x333333), 0);
    lv_obj_set_scroll_dir(centerdot, LV_DIR_NONE);
    lv_obj_align(centerdot, LV_ALIGN_CENTER, HAND_TAIL - second_hand_w / 2, 0);

    return scr;
}

void divewatch_update()
{
        struct timeval tv;
        gettimeofday(&tv, NULL);

        struct tm t;
        localtime_r(&tv.tv_sec, &t);
        int ms = tv.tv_usec / 1000;

        // Calculate angles (0° = 12 o'clock, increasing clockwise)
        float sangle = (t.tm_sec + ms / 1000.0f) * 6.0f;

        // Convert angles so 0° is at 12 o'clock (subtract 90°)
        // float srad = (sangle - 90.0f) * DEG2RAD;

        const int32_t srot = (int32_t)((270 + sangle) * 10);
        lv_obj_set_style_transform_rotation(secondhand, srot, 0);
        lv_obj_set_style_transform_rotation(secondhand_shadow, srot, 0);

        // Update minute and hour when needed
        if (t.tm_sec != last_sec)
        {
            last_sec = t.tm_sec;

            float mangle = (t.tm_min + t.tm_sec / 60.0f) * 6.0f;
            // float mrad = (mangle - 90.0f) * DEG2RAD;

            const int32_t mrot = (int32_t)((270 + mangle) * 10);
            lv_obj_set_style_transform_rotation(minutehand, mrot, 0);
            lv_obj_set_style_transform_rotation(minutehand_shadow, mrot, 0);

            if (t.tm_min != last_min)
            {
                last_min = t.tm_min;

                float hangle = ((t.tm_hour % 12) + t.tm_min / 60.0f) * 30.0f;

                const int32_t hrot = (int32_t)((270 + hangle) * 10);
                lv_obj_set_style_transform_rotation(hourhand, hrot, 0);
                lv_obj_set_style_transform_rotation(hourhand_shadow, hrot, 0);

                if (t.tm_hour != last_hour)
                {
                    last_hour = t.tm_hour;

                    if (t.tm_mday != last_day)
                    {
                        last_day = t.tm_mday;

                        lv_label_set_text_fmt(date_label, "%02d", t.tm_mday);

                        if ((t.tm_mon + 1) != last_month)
                            last_month = t.tm_mon + 1;
                    }
                }
            }
        }
}