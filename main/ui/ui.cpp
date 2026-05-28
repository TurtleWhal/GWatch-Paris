#include "ui.hpp"

lv_obj_t *main_screen;
lv_obj_t *ver_layer;
lv_obj_t *hor_layer;
lv_obj_t *lower_layer;

// Captured at the end of ui_init so post-boot code (e.g. unistroke_register_app)
// can append entries after BLE has claimed its boot-time SRAM allocations.
lv_obj_t *g_appsscreen = nullptr;

// RAM wrappers around the generated `const lv_font_t` fonts in flash.
// LVGL's fallback chain is a per-font field; the generator emits the
// originals into .rodata so we can't write to them — instead we copy
// the structs into RAM at boot and patch in NotoEmoji as the fallback.
// Call sites that render user-supplied text (notifications, music
// metadata) use these wrappers so emoji codepoints fall through to
// NotoEmoji instead of dropping out as missing-glyph boxes.
lv_font_t ProductSansBold_30_emoji;
lv_font_t ProductSansBold_24_emoji;
lv_font_t ProductSansRegular_20_emoji;
lv_font_t ProductSansBold_16_emoji;
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

lv_obj_t *create_screen(lv_obj_t *parent)
{
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

lv_obj_t *create_valuearc(lv_obj_t *parent, const char *symbol)
{
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
static void scroll_loop_event_cb(lv_event_t *e)
{
    static bool is_adjusting = false;
    lv_obj_t *cont = lv_event_get_current_target_obj(e);

    if (!is_adjusting)
    {
        is_adjusting = true;
        int32_t scroll_x = lv_obj_get_scroll_x(cont);
        int32_t cont_w = lv_obj_get_width(cont);
        int32_t content_w = (int32_t)lv_obj_get_child_count(cont) * 240;

        /* Use ITEM_SIZE as horizontal item width */
        const int32_t item_width = 240;

        if (scroll_x <= 0)
        {
            lv_obj_t *last_child = lv_obj_get_child(cont, (int32_t)(lv_obj_get_child_count(cont) - 1));
            lv_obj_move_to_index(last_child, 0);
            lv_obj_scroll_to_x(cont, scroll_x + item_width, LV_ANIM_OFF);
        }
        else if (scroll_x >= content_w - cont_w)
        {
            lv_obj_t *first_child = lv_obj_get_child(cont, 0);
            lv_obj_move_to_index(first_child, (int32_t)(lv_obj_get_child_count(cont) - 1));
            lv_obj_scroll_to_x(cont, scroll_x - item_width, LV_ANIM_OFF);
        }
        is_adjusting = false;
    }
}

struct ScrollEventData
{
    lv_obj_t *obj;
    lv_dir_t direction;
};

void screen_scroll_highlight_event_cb(lv_event_t *e)
{
    ScrollEventData *data = (ScrollEventData *)lv_event_get_user_data(e);

    switch (data->direction)
    {
    case LV_DIR_TOP:
    {
        lv_obj_t *scr = data->obj;

        int32_t pos = lv_obj_get_y(scr);
        int32_t scroll = lv_obj_get_scroll_y(lv_event_get_target_obj(e));
        int32_t height = lv_obj_get_height(scr);

        int32_t v = scroll - pos;
        if (v > 0 && v < height)
            lv_obj_set_style_bg_color(scr, lv_color_hsv_to_rgb(180, 0, (v * 20) / height), 0);
        else
            lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    }
    break;
    case LV_DIR_BOTTOM:
    {
        lv_obj_t *scr = data->obj;

        int32_t pos = lv_obj_get_y(scr);                                  // Position of the screen in its container
        int32_t scroll = lv_obj_get_scroll_y(lv_event_get_target_obj(e)); // current scroll of container
        int32_t height = lv_obj_get_height(scr);                          // height of screen (always 240)

        int32_t v = height - (scroll - (pos - height));
        if (v > 0 && v < height)
            lv_obj_set_style_bg_color(scr, lv_color_hsv_to_rgb(180, 0, (v * 20) / height), 0);
        else
            lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    }
    break;
    case LV_DIR_LEFT:
    {
        lv_obj_t *scr = data->obj;

        int32_t pos = lv_obj_get_x(scr);                                  // Position of the screen in its container
        int32_t scroll = lv_obj_get_scroll_x(lv_event_get_target_obj(e)); // current scroll of container
        int32_t width = lv_obj_get_width(scr);                            // width of screen (always 240)

        int32_t v = scroll - pos;
        if (v > 0 && v < width)
            lv_obj_set_style_bg_color(scr, lv_color_hsv_to_rgb(180, 0, (v * 20) / width), 0);
        else
            lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    }
    break;
    case LV_DIR_RIGHT:
    {
        lv_obj_t *scr = data->obj;

        int32_t pos = lv_obj_get_x(scr);                                  // Position of the screen in its container
        int32_t scroll = lv_obj_get_scroll_x(lv_event_get_target_obj(e)); // current scroll of container
        int32_t width = lv_obj_get_width(scr);                            // width of screen (always 240)

        int32_t v = width - (scroll - (pos - width));
        if (v > 0 && v < width)
            lv_obj_set_style_bg_color(scr, lv_color_hsv_to_rgb(180, 0, (v * 20) / width), 0);
        else
            lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    }
    break;
    default:
        break;
    }
}

lv_obj_t *watchscr;  // invisible layer to show watch face
lv_obj_t *watchface; // actual watch face

// Push a new accent color into all shared styles and trigger a repaint
// of every widget that has any of them attached. Safe to call any time
// after ui_init has run (which is what lv_style_init's the structs).
void apply_accent_color(lv_color_t c)
{
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

char *getbaticon(bool charging, uint8_t percent)
{
    if (watch.battery.charging)
    {
        return FA_CHARGING;
    }
    else
    {
        if (watch.battery.percent > 75)
        {
            return FA_BATTERY_FULL;
        }
        else if (watch.battery.percent > 50)
        {
            return FA_BATTERY_75;
        }
        else if (watch.battery.percent > 25)
        {
            return FA_BATTERY_50;
        }
        else if (watch.battery.percent > 10)
        {
            return FA_BATTERY_25;
        }
        else if (watch.battery.percent > 5)
        {
            return FA_BATTERY_10;
        }
        else
        {
            return FA_BATTERY_EMPTY;
        }
    }
}

void Display::ui_init()
{
    ProductSansBold_30_emoji = ProductSansBold_30;
    ProductSansBold_30_emoji.fallback = &NotoEmojiRegular_20;
    ProductSansBold_24_emoji = ProductSansBold_24;
    ProductSansBold_24_emoji.fallback = &NotoEmojiRegular_20;
    ProductSansRegular_20_emoji = ProductSansRegular_20;
    ProductSansRegular_20_emoji.fallback = &NotoEmojiRegular_20;
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
    lv_theme_t *th = lv_theme_default_init(lv_display_get_default(),
                                           accent,
                                           lv_color_hex(0x607D8B),
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

    lv_display_set_theme(lv_display_get_default(), th); /* Assign theme to display */

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

    lv_obj_set_style_margin_all(ver_layer, 0, 0);
    lv_obj_set_style_pad_all(ver_layer, 0, 0);

    lv_obj_t *quicksettings = quicksettings_create(ver_layer);

    static ScrollEventData scroll_dataT = {quicksettings, LV_DIR_TOP};
    lv_obj_add_event_cb(ver_layer, screen_scroll_highlight_event_cb, LV_EVENT_SCROLL, &scroll_dataT);

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

    lower_layer = lv_obj_create(ver_layer);
    lv_obj_set_size(lower_layer, 240, 240);
    lv_obj_set_style_bg_color(lower_layer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lower_layer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(lower_layer, 0, 0);
    lv_obj_set_style_radius(lower_layer, LV_RADIUS_CIRCLE, 0);

    lv_obj_set_flex_flow(lower_layer, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_snap_x(lower_layer, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flag(lower_layer, LV_OBJ_FLAG_SCROLL_ELASTIC, false);
    lv_obj_set_scrollbar_mode(lower_layer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_row(lower_layer, 0, 0);
    lv_obj_set_style_pad_column(lower_layer, 0, 0);
    lv_obj_set_flag(lower_layer, LV_OBJ_FLAG_SCROLL_ONE, true);

    lv_obj_set_style_margin_all(lower_layer, 0, 0);
    lv_obj_set_style_pad_all(lower_layer, 0, 0);

    lv_obj_add_event_cb(lower_layer, scroll_loop_event_cb, LV_EVENT_SCROLL, NULL);

    lv_obj_t *weather = weather_create(lower_layer);
    lv_obj_set_style_bg_opa(weather, 0, 0);
    
    lv_obj_t *notifications = notifications_screen_create(lower_layer);
    lv_obj_set_style_bg_opa(notifications, 0, 0);
    
    lv_obj_t *music = music_create(lower_layer);
    lv_obj_set_style_bg_opa(music, 0, 0);

    lv_obj_scroll_to_view(notifications, LV_ANIM_OFF);

    static ScrollEventData scroll_dataB = {lower_layer, LV_DIR_BOTTOM};
    lv_obj_add_event_cb(ver_layer, screen_scroll_highlight_event_cb, LV_EVENT_SCROLL, &scroll_dataB);

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
    lv_obj_t *imuscreen = imu_screen_create(hor_layer);
    lv_obj_t *calcscreen = calculator_create(hor_layer);
    lv_obj_t *appsscreen = apps_screen_create(hor_layer);

    static ScrollEventData scroll_dataR = {stopwatch, LV_DIR_RIGHT};
    lv_obj_add_event_cb(hor_layer, screen_scroll_highlight_event_cb, LV_EVENT_SCROLL, &scroll_dataR);

    static ScrollEventData scroll_dataL = {appsscreen, LV_DIR_LEFT};
    lv_obj_add_event_cb(hor_layer, screen_scroll_highlight_event_cb, LV_EVENT_SCROLL, &scroll_dataL);

    lv_obj_send_event(hor_layer, LV_EVENT_SCROLL, NULL);

    // lv_obj_t *appsscreen = apps_screen_create(ver_layer);

    // static ScrollEventData scroll_dataB = {appsscreen, LV_DIR_BOTTOM};
    // lv_obj_add_event_cb(ver_layer, screen_scroll_highlight_event_cb, LV_EVENT_SCROLL, &scroll_dataB);

    lv_obj_add_event_cb(hor_layer, [](lv_event_t *e)
                        {
        lv_obj_t *scr = (lv_obj_t *)lv_event_get_user_data(e);
        int32_t scroll = lv_obj_get_scroll_x(lv_event_get_target_obj(e)); // scroll of screens layer
        int32_t pos = lv_obj_get_x(scr);

        if (scroll - 240 < pos && scroll + 240 > pos)
        { // watch face visible
            lv_obj_set_style_bg_opa(lv_event_get_target_obj(e), LV_OPA_0, 0);
            lv_obj_set_flag(watchface, LV_OBJ_FLAG_HIDDEN, false);

            if (scroll == pos)
            { // if exactly centered
                lv_obj_set_scroll_dir(ver_layer, LV_DIR_VER);
            }
        }
        else
        {
            lv_obj_set_flag(watchface, LV_OBJ_FLAG_HIDDEN, true);
            lv_obj_set_style_bg_opa(lv_event_get_target_obj(e), LV_OPA_COVER, 0);
            lv_obj_set_scroll_dir(ver_layer, LV_DIR_NONE);
        } }, LV_EVENT_SCROLL, watchscr);

    create_app(appsscreen, FA_STOPWATCH, "Stopwatch", stopwatch);
    create_app(appsscreen, FA_TIMER, "Timer", timer);
    create_app(appsscreen, FA_ALARM, "Alarm", alarm);
    create_app(appsscreen, FA_CALCULATOR, "Calculator", calcscreen);

    // Unistroke's app entry is registered post-BLE-init from watch.cpp.
    // Even just adding the button widget here was enough to push LVGL's
    // internal-SRAM use past the threshold the BT controller's malloc
    // needs at boot. unistroke_register_app() runs after ble.init() has
    // claimed its memory, and the screen widgets behind the button are
    // lazy-built on first tap (no boot cost at all).
    g_appsscreen = appsscreen;

    create_app(appsscreen, FA_FLASHLIGHT, "Flashlight", [](lv_event_t *)
               {
                   static uint16_t flashlight_prev;

                   static lv_obj_t *flashlight_screen = lv_obj_create(NULL);
                   lv_obj_set_style_bg_color(flashlight_screen, lv_color_white(), 0);

                   flashlight_prev = watch.display.get_brightness();

                   lv_screen_load_anim(flashlight_screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 100, 0, false);

                   watch.display.set_backlight(100);

                   lv_obj_add_event_cb(flashlight_screen, [](lv_event_t *e)
                                       {
                                           watch.display.set_backlight(*(uint16_t *)lv_event_get_user_data(e));
                                           lv_screen_load_anim(main_screen, LV_SCREEN_LOAD_ANIM_FADE_OUT, 100, 0, false);
                                       },
                                       LV_EVENT_CLICKED, &flashlight_prev); });

    // create_app(appsscreen, FA_IMU, "Accelerometer", imuscreen); // Accelerometer is too long
    create_app(appsscreen, FA_IMU, "IMU", imuscreen);

    create_app(appsscreen, FA_CALENDAR, "Schedule", schedule_screen_create(NULL), true);

    create_app(appsscreen, FA_DICE, "Dice", dice_create(NULL), true);

    lv_obj_t *settings = settingsscreen_create();
    create_app(appsscreen, FA_SETTINGS, "Settings", settings, true);

    // create_app(appsscreen, FA_METRONOME, "Metronome");

    lv_screen_load(main_screen);

    lv_obj_scroll_to_view_recursive(watchscr, LV_ANIM_OFF);
}