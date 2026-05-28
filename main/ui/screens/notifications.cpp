#include "ui.hpp"
#include "ble.hpp"
#include <inttypes.h>  // PRIu32 in the reply-chip log line

static lv_obj_t *notif_list_container = nullptr;
static lv_obj_t *empty_label = nullptr;
static uint32_t last_rendered_version = (uint32_t)-1;
// Set to true at the moment a snap-swipe triggers a dismiss; cleared
// when the deferred rebuild runs. Acts as a one-shot gate so that any
// extra SCROLL_END events firing in the same cycle (snap bounce,
// post-deletion layout adjustments, residual indev tracking) can't
// cascade into additional dismisses on neighbouring cards.
static bool dismiss_in_progress = false;

// Render `when_ms` (Notification.when_ms — esp_timer_get_time() / 1000 at
// receipt, monotonic across light sleep) into a short relative-time
// string like "now", "5m ago", "2h ago", "3d ago". The string only
// re-renders when the notification list rebuilds (on add/dismiss), so it
// doesn't tick by itself — fine for this UI since cards already refresh
// whenever notifications mutate.
static void format_age(int64_t when_ms, char *buf, size_t bufsz)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    int64_t delta_s = (now_ms - when_ms) / 1000;
    if (delta_s < 0)
        delta_s = 0;
    if (delta_s < 60)
        snprintf(buf, bufsz, "now");
    else if (delta_s < 3600)
        snprintf(buf, bufsz, "%dm ago", (int)(delta_s / 60));
    else if (delta_s < 86400)
        snprintf(buf, bufsz, "%dh ago", (int)(delta_s / 3600));
    else
        snprintf(buf, bufsz, "%dd ago", (int)(delta_s / 86400));
}

// ----- Popup for newly-arrived notifications -----

static lv_obj_t *popup_screen = nullptr;
static lv_obj_t *popup_src = nullptr;
static lv_obj_t *popup_title = nullptr;
static lv_obj_t *popup_body = nullptr;
static lv_obj_t *popup_replies = nullptr;
static lv_obj_t *popup_icon_box = nullptr; // circle-clip container
static lv_obj_t *popup_icon_img = nullptr; // image inside the container
static lv_obj_t *popup_prev_screen = nullptr;
// What the popup is currently rendering. Used by the icon-refresh timer
// to find the matching Notification, and by the list-tap handler to
// switch the popup to a different (typically older) notification.
static uint32_t popup_last_shown_id = 0;
// Highest id the auto-popup logic has fired for. Decoupled from
// popup_last_shown_id so that tapping an older notification (which
// rewrites popup_last_shown_id to the older id) doesn't make the
// auto-popup think a fresh notification has arrived and immediately
// stack the newest one on top.
static uint32_t popup_seen_latest_id = 0;
static bool popup_icon_applied = false;

static void popup_close(bool animate = true)
{
    if (popup_prev_screen && lv_screen_active() == popup_screen)
        lv_screen_load_anim(popup_prev_screen,
                            animate ? LV_SCREEN_LOAD_ANIM_FADE_OUT
                                    : LV_SCREEN_LOAD_ANIM_NONE,
                            animate ? 200 : 0, 0, false);
}

static lv_obj_t *popup_build()
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_pad_hor(scr, 20, 0);
    lv_obj_set_style_pad_top(scr, 30, 0);
    lv_obj_set_style_pad_bottom(scr, 40, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_track_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_main_place(scr, LV_FLEX_ALIGN_START, 0);
    lv_obj_set_style_pad_row(scr, 8, 0);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(card, 72 + 8 * 2, 0);
    lv_obj_set_style_bg_opa(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_left(card, 8 * 2 + 72, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(card, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(card, 2, 0);

    // Circle-clip container. White-fill is the placeholder shown until a
    // matching notify-img arrives; popup_show() swaps that for the icon
    // image when one is available.
    popup_icon_box = lv_obj_create(card);
    lv_obj_set_size(popup_icon_box, 72, 72);
    lv_obj_set_style_radius(popup_icon_box, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(popup_icon_box, true, 0);
    lv_obj_set_style_border_width(popup_icon_box, 0, 0);
    lv_obj_set_style_pad_all(popup_icon_box, 0, 0);
    lv_obj_set_style_bg_color(popup_icon_box, lv_color_hex(0x222222), 0);
    lv_obj_set_scrollbar_mode(popup_icon_box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flag(popup_icon_box, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_flag(popup_icon_box, LV_OBJ_FLAG_IGNORE_LAYOUT, true);
    lv_obj_set_pos(popup_icon_box, -72 - 8, 0);

    popup_icon_img = lv_image_create(popup_icon_box);
    lv_obj_set_flag(popup_icon_img, LV_OBJ_FLAG_HIDDEN, true);

    popup_src = lv_label_create(card);
    lv_obj_set_style_text_font(popup_src, &ProductSansRegular_14, 0);
    lv_obj_set_style_text_color(popup_src, lv_color_hex(0x6699ff), 0);
    lv_label_set_long_mode(popup_src, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(popup_src, 100, 0);

    popup_title = lv_label_create(card);
    lv_obj_set_style_text_font(popup_title, &ProductSansBold_24_emoji, 0);
    lv_obj_set_style_text_color(popup_title, lv_color_white(), 0);
    lv_label_set_long_mode(popup_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_width(popup_title, 100, 0);

    popup_body = lv_label_create(scr);
    lv_obj_set_style_text_font(popup_body, &ProductSansRegular_20_emoji, 0);
    lv_obj_set_style_text_color(popup_body, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_align(popup_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(popup_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(popup_body, 200);

    popup_replies = lv_obj_create(scr);
    lv_obj_set_style_bg_opa(popup_replies, 0, 0);
    lv_obj_set_style_border_width(popup_replies, 0, 0);
    lv_obj_set_style_pad_all(popup_replies, 0, 0);
    lv_obj_set_width(popup_replies, 200);
    lv_obj_set_height(popup_replies, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(popup_replies, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_flex_main_place(popup_replies, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(popup_replies, 6, 0);
    lv_obj_set_style_pad_column(popup_replies, 6, 0);
    lv_obj_set_style_margin_top(popup_replies, 6, 0);

    // Swipe right to dismiss the popup. After popup_close swaps back to
    // the notifications screen, the user's finger is still down and the
    // residual rightward motion would be picked up by whichever card is
    // now under the touch point — scrolling it onto the left spacer and
    // silently dismissing it via the snap handler. Resetting the indev
    // drops the in-flight touch so the next finger-down starts fresh.
    lv_obj_add_event_cb(scr, [](lv_event_t *)
                        {
                            lv_indev_t *act = lv_indev_active();
                            if (lv_indev_get_gesture_dir(act) == LV_DIR_RIGHT)
                            {
                                popup_close();
                                lv_indev_reset(act, NULL);
                            } }, LV_EVENT_GESTURE, NULL);

    return scr;
}

// Update the popup's icon slot from a notification's current state. Called
// from popup_show() at popup time, and again from the 500 ms refresh timer
// when the matching notify-img lands after the popup is already up.
static void popup_apply_icon(const Notification &n)
{
    if (!n.img.empty() && n.img_w > 0 && n.img_h > 0)
    {
        static lv_image_dsc_t popup_dsc;
        popup_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        popup_dsc.header.w = n.img_w;
        popup_dsc.header.h = n.img_h;
        popup_dsc.header.stride = n.img_w * 2;
        popup_dsc.data_size = n.img.size();
        popup_dsc.data = n.img.data();

        lv_image_set_src(popup_icon_img, &popup_dsc);
        // Default pivot is the image center, which makes the scaled output
        // straddle the container origin; pin to top-left so 0..scaled_size
        // fills the container exactly.
        lv_image_set_pivot(popup_icon_img, 0, 0);
        // Fixed 3:2 upscale (LVGL scale value 384 = 256 * 1.5). With the
        // 48 px notification icons Gadgetbridge sends, this lands at
        // exactly 72 px and fills the 72 px container. Switched from
        // the previous "fit to container" formula (256 * 72 / img_w) so
        // off-spec sources still render at a predictable pixel size
        // instead of getting stretched to fit.
        lv_image_set_scale(popup_icon_img, 256 * 3 / 2);
        lv_obj_set_pos(popup_icon_img, 0, 0);
        lv_obj_set_flag(popup_icon_img, LV_OBJ_FLAG_HIDDEN, false);
        lv_obj_set_style_bg_opa(popup_icon_box, 0, 0);
        popup_icon_applied = true;
    }
    else
    {
        lv_obj_set_flag(popup_icon_img, LV_OBJ_FLAG_HIDDEN, true);
        lv_obj_set_style_bg_opa(popup_icon_box, LV_OPA_COVER, 0);
        popup_icon_applied = false;
    }
}

static void popup_show(const Notification &n, bool animate = true)
{
    if (!popup_screen)
        popup_screen = popup_build();

    lv_label_set_text(popup_src,
                      n.src.empty() ? "Notification" : n.src.c_str());
    lv_label_set_text(popup_title, n.title.empty() ? "" : n.title.c_str());
    lv_label_set_text(popup_body, n.body.empty() ? "" : n.body.c_str());

    lv_obj_clean(popup_replies);

    if (n.reply)
    {
        for (uint8_t i = 0; i < n.replies.size(); i++)
        {
            lv_obj_t *reply = lv_obj_create(popup_replies);
            lv_obj_set_size(reply, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_border_color(reply, lv_color_hex(0x888888), 0);
            lv_obj_set_style_border_width(reply, 2, 0);
            lv_obj_set_style_bg_opa(reply, 0, 0);
            lv_obj_set_style_pad_ver(reply, 4, 0);
            lv_obj_set_style_radius(reply, (lv_font_get_line_height(&ProductSansRegular_20_emoji) + lv_obj_get_style_pad_top(reply, LV_PART_MAIN) * 2 + 4) / 2, 0);
            // lv_obj_set_style_max_width(reply, 200, 0);
            
            lv_obj_t *lbl = lv_label_create(reply);
            lv_obj_set_style_text_font(lbl, &ProductSansRegular_20_emoji, 0);
            lv_label_set_text(lbl, n.replies.at(i).c_str());
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_style_max_width(lbl, 172, 0);

            // Stash the notification id on the chip's user_data; the
            // click handler reads the reply text back off the chip's
            // label child so we don't have to manage per-button string
            // lifetimes (LVGL already owns the label text). Sends the
            // reply, then closes the popup — Gadgetbridge auto-dismisses
            // the notification on its side and the follow-up notify-
            // message will clean it out of the watch queue.
            lv_obj_set_user_data(reply, (void *)(uintptr_t)n.id);
            lv_obj_add_event_cb(reply, [](lv_event_t *e) {
                lv_obj_t *chip = lv_event_get_target_obj(e);
                uint32_t id = (uint32_t)(uintptr_t)lv_obj_get_user_data(chip);
                lv_obj_t *l = lv_obj_get_child(chip, 0);
                const char *text = l ? lv_label_get_text(l) : "";
                ESP_LOGI("popup", "REPLY id=%" PRIu32 " text='%s'", id, text);
                ble.send_notification_reply(id, text);
                haptic_play(false, 40, 0);
                popup_close();
            }, LV_EVENT_CLICKED, NULL);
        }
    }

    popup_apply_icon(n);

    lv_obj_scroll_to_y(popup_body, 0, LV_ANIM_OFF);

    // Don't trample our own popup as the "previous screen" if a second
    // notification arrives while the first popup is still up.
    lv_obj_t *active = lv_screen_active();
    if (active != popup_screen)
        popup_prev_screen = active;

    lv_screen_load_anim(popup_screen,
                        animate ? LV_SCREEN_LOAD_ANIM_FADE_IN
                                : LV_SCREEN_LOAD_ANIM_NONE,
                        animate ? 200 : 0, 0, false);
}

// If the popup is up but its icon hasn't landed yet (notify-img comes in
// a separate BLE message that may arrive after the notify text), scan the
// notification list for the displayed id and apply the icon as soon as it
// shows up. Bails immediately once applied so this stays cheap to call
// from a 500 ms timer.
static void popup_refresh_icon()
{
    if (popup_icon_applied)
        return;
    if (!popup_screen || lv_screen_active() != popup_screen)
        return;
    if (popup_last_shown_id == 0)
        return;
    for (const auto &n : ble.notifications())
    {
        if (n.id == popup_last_shown_id)
        {
            if (!n.img.empty() && n.img_w > 0 && n.img_h > 0)
                popup_apply_icon(n);
            return;
        }
    }
}

// Called from the poll timer in notifications_screen_create — fires the
// popup with a fade animation whenever a notification with an unseen id
// appears at the top of the queue. Wake- and sleep-time popup handling go
// through the explicit *_now() entry points below.
static void popup_check_new()
{
    const auto &list = ble.notifications();
    if (list.empty())
        return;
    uint32_t latest = list.back().id;
    // Compare with `<=` so dismissing the newest notification (which
    // makes list.back() fall back to an older id) doesn't get treated
    // as "a new notification arrived" and auto-pop the one underneath.
    // Gadgetbridge IDs are unix timestamps so they're monotonic.
    if (latest <= popup_seen_latest_id)
        return;
    popup_seen_latest_id = latest;
    popup_last_shown_id = latest;
    popup_icon_applied = false;
    popup_show(list.back(), /*animate=*/true);
}

// Show the latest pending notification (if any) instantly with no animation.
// Called from Watch::wakeup() while the backlight is still off — the screen
// gets painted with the popup as its content, so when the backlight comes
// on the user sees the popup directly without a frame of the watch face.
void notification_popup_present_now()
{
    const auto &list = ble.notifications();
    if (list.empty())
        return;
    uint32_t latest = list.back().id;
    if (latest <= popup_seen_latest_id)
        return;
    popup_seen_latest_id = latest;
    popup_last_shown_id = latest;
    popup_icon_applied = false;
    popup_show(list.back(), /*animate=*/false);
}

// Dismiss any active popup instantly. Called from Watch::sleep() after the
// backlight has fully faded out, so the popup→prev-screen swap is invisible
// to the user (screen is already dark) and on next wake the active screen
// is whatever was underneath the popup, not the popup itself.
void notification_popup_dismiss_now()
{
    popup_close(/*animate=*/false);
}

static void rebuild_notification_list()
{
    if (!notif_list_container)
        return;

    // Bumped by ble.cpp on every mutation (add / dismiss / image attach),
    // so we don't need to diff the queue ourselves.
    uint32_t v = ble.notifications_version();
    if (v == last_rendered_version)
        return;
    last_rendered_version = v;
    const auto &list = ble.notifications();
    size_t count = list.size();

    // Clear the container.
    lv_obj_clean(notif_list_container);

    lv_obj_t *bar = lv_obj_create(notif_list_container);
    lv_obj_set_size(bar, 62, 3);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);

    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
    lv_obj_set_style_margin_bottom(bar, 10, 0);

    if (count == 0)
    {
        // empty_label = lv_label_create(notif_list_container);
        // lv_label_set_text(empty_label, "No notifications");
        // lv_obj_set_style_text_color(empty_label, lv_color_hex(0x888888), 0);
        // lv_obj_set_style_text_font(empty_label, &ProductSansRegular_16, 0);

        empty_label = lv_label_create(notif_list_container);
        lv_label_set_text(empty_label, "No notifications");
        lv_obj_set_style_text_font(empty_label, &ProductSansRegular_20, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0x888888), 0);

        return;
    }

    // Newest first.
    for (auto it = list.rbegin(); it != list.rend(); ++it)
    {
        const Notification &n = *it;

        // Each row is a 3-panel scroll-snap container: empty spacer,
        // center panel holding the card, empty spacer. The user swipes
        // horizontally; if they release with the card still in view the
        // snap pulls back to center, otherwise it lands on a spacer and
        // we dismiss the notification on SCROLL_END.
        lv_obj_t *snap = lv_obj_create(notif_list_container);
        lv_obj_set_size(snap, 240, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(snap, 0, 0);
        lv_obj_set_style_border_width(snap, 0, 0);
        lv_obj_set_style_pad_all(snap, 0, 0);
        lv_obj_set_style_pad_column(snap, 0, 0);
        lv_obj_set_flex_flow(snap, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_flex_cross_place(snap, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_scroll_dir(snap, LV_DIR_HOR);
        lv_obj_set_scrollbar_mode(snap, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_snap_x(snap, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_flag(snap, LV_OBJ_FLAG_SCROLL_ONE, true);
        lv_obj_set_flag(snap, LV_OBJ_FLAG_SCROLL_ELASTIC, true);

        auto make_spacer = [&]()
        {
            lv_obj_t *sp = lv_obj_create(snap);
            lv_obj_set_size(sp, 240, 1);
            lv_obj_set_style_bg_opa(sp, 0, 0);
            lv_obj_set_style_border_width(sp, 0, 0);
            lv_obj_set_style_pad_all(sp, 0, 0);
            lv_obj_set_flag(sp, LV_OBJ_FLAG_SCROLLABLE, false);
            return sp;
        };

        lv_obj_t *left_sp = make_spacer();
        (void)left_sp;

        // Center panel: 240 wide so all three snap targets are uniform;
        // the actual card stays 200 wide centered inside it.
        lv_obj_t *center = lv_obj_create(snap);
        lv_obj_set_size(center, 240, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(center, 0, 0);
        lv_obj_set_style_border_width(center, 0, 0);
        lv_obj_set_style_pad_all(center, 0, 0);
        lv_obj_set_flag(center, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_flex_flow(center, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_flex_main_place(center, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_cross_place(center, LV_FLEX_ALIGN_CENTER, 0);

        lv_obj_t *card = lv_button_create(center);
        lv_obj_set_size(card, 200, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(card, 64, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1c1c1c), 0);
        lv_obj_set_style_radius(card, 64 / 2, 0);
        // Horizontal padding stays 8; vertical padding drops to 4 so the
        // natural height for src(10pt) + title(16pt) + 1-line body(14pt)
        // plus the 2 px pad_row gaps lands exactly on the 64 px min.
        // With pad_all=8 it was 70 px and every card overflowed its min.
        lv_obj_set_style_pad_hor(card, 8, 0);
        lv_obj_set_style_pad_ver(card, 4, 0);
        lv_obj_set_style_pad_left(card, 8 * 2 + 48, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 2, 0);
        lv_obj_set_flag(card, LV_OBJ_FLAG_SCROLL_ON_FOCUS, false);

        // Icon slot: when the matching `notify-img` GB message has arrived,
        // `n.img` holds a decoded RGB565 pixel buffer (sized n.img_w × n.img_h);
        // otherwise fall back to a white circle placeholder.
        // Circle-clipped icon slot. The container handles the round crop;
        // when we have an image it goes inside, otherwise the container's
        // own white background fills the circle as a placeholder.
        lv_obj_t *icon_box = lv_obj_create(card);
        lv_obj_set_size(icon_box, 48, 48);
        lv_obj_set_style_radius(icon_box, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(icon_box, true, 0);
        lv_obj_set_style_border_width(icon_box, 0, 0);
        lv_obj_set_style_pad_all(icon_box, 0, 0);
        lv_obj_set_style_bg_color(icon_box, lv_color_hex(0x222222), 0);
        lv_obj_set_scrollbar_mode(icon_box, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_flag(icon_box, LV_OBJ_FLAG_IGNORE_LAYOUT, true);
        // y=4 so the 48 px circle stays centred in the 64 px card: the
        // 8 px gap above and below comes from pad_top(4) + this y(4) on
        // top and pad_bottom(4) + (64 - 48 - 4 - 4 - 4) on bottom.
        lv_obj_set_pos(icon_box, -48 - 8, 4);

        if (!n.img.empty() && n.img_w > 0 && n.img_h > 0)
        {
            static lv_image_dsc_t dsc[16];
            static int dsc_idx = 0;
            lv_image_dsc_t *d = &dsc[dsc_idx];
            dsc_idx = (dsc_idx + 1) % 16;
            d->header.cf = LV_COLOR_FORMAT_RGB565;
            d->header.w = n.img_w;
            d->header.h = n.img_h;
            d->header.stride = n.img_w * 2;
            d->data_size = n.img.size();
            d->data = n.img.data();

            lv_obj_t *icon_img = lv_image_create(icon_box);
            lv_image_set_src(icon_img, d);
            // Pin the scale pivot to the image's top-left so the scaled
            // output fills the container from (0,0) — the default center
            // pivot leaves it offset by half the size difference.
            lv_image_set_pivot(icon_img, 0, 0);
            lv_image_set_scale(icon_img, 256 * 48 / n.img_w);
            lv_obj_set_style_bg_opa(icon_box, 0, 0);
        }

        lv_obj_t *src_label = lv_label_create(card);
        // "Source · age" — receipt age is recorded by ble.cpp on notify
        // arrival into Notification.when_ms; format_age formats it into
        // a short suffix appended to the src name.
        char age_buf[16];
        format_age(n.when_ms, age_buf, sizeof(age_buf));
        char src_buf[64];
        snprintf(src_buf, sizeof(src_buf), "%s · %s",
                 n.src.empty() ? "Notification" : n.src.c_str(),
                 age_buf);
        lv_label_set_text(src_label, src_buf);
        lv_obj_set_style_text_font(src_label, &ProductSansRegular_10, 0);
        lv_obj_set_style_text_color(src_label, lv_color_hex(0x6699ff), 0);

        if (!n.title.empty())
        {
            lv_obj_t *title = lv_label_create(card);
            lv_label_set_text(title, n.title.c_str());
            lv_obj_set_style_text_font(title, &ProductSansBold_16_emoji, 0);
            lv_obj_set_style_text_color(title, lv_color_white(), 0);
            lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(title, 130);
            lv_obj_set_style_pad_top(title, -2, 0);
            lv_obj_set_style_pad_bottom(title, -4, 0);
        }

        if (!n.body.empty())
        {
            // Body wraps to its natural content height with a hard cap of
            // BODY_LINES — LV_LABEL_LONG_DOT was forcing every body label
            // to the full 4-line height even for a two-character message,
            // which made cards with any body randomly twice as tall as
            // cards without one. LONG_WRAP + max_height lets the card
            // shrink-to-fit short text and only grow when the text
            // actually needs the room.
            constexpr int BODY_LINES = 4;
            lv_obj_t *body = lv_label_create(card);
            lv_label_set_text(body, n.body.c_str());
            lv_obj_set_style_text_font(body, &ProductSansRegular_14_emoji, 0);
            lv_obj_set_style_text_color(body, lv_color_hex(0xcccccc), 0);
            int line_h = lv_font_get_line_height(&ProductSansRegular_14_emoji);
            int line_space = lv_obj_get_style_text_line_space(body, LV_PART_MAIN);
            lv_obj_set_width(body, 130);
            lv_obj_set_height(body, LV_SIZE_CONTENT);
            lv_obj_set_style_max_height(body,
                                        line_h * BODY_LINES + line_space * (BODY_LINES - 1), 0);
            lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        }

        // Right spacer to mirror the left side; together with the center
        // panel they form the three snap targets at x = 0 / 240 / 480.
        lv_obj_t *right_sp = make_spacer();
        (void)right_sp;

        // Start the row parked on the center panel so the card is fully
        // visible; without this it would render at scroll_x = 0 (left
        // spacer centered) and look already-dismissed.
        lv_obj_update_layout(snap);
        lv_obj_scroll_to_x(snap, 240, LV_ANIM_OFF);

        // Dismiss only when the snap lands on one of the edge panels.
        // Using halfway-point thresholds rather than `sx != 240` means
        // any sub-pixel snap residue or intermediate SCROLL_END events
        // (e.g. during elastic bounce) don't accidentally dismiss — we
        // only act when the row has actually settled at sx ≈ 0 or 480.
        // Defer the rebuild via lv_async_call so it runs *after* this
        // scroll event finishes — rebuilding here would clean the
        // container we're firing from.
        uint32_t snap_id = n.id;
        lv_obj_set_user_data(snap, (void *)(uintptr_t)snap_id);
        lv_obj_add_event_cb(
            snap,
            [](lv_event_t *e)
            {
                if (dismiss_in_progress)
                    return; // already dismissing one this cycle
                lv_obj_t *cont = lv_event_get_target_obj(e);
                int32_t sx = lv_obj_get_scroll_x(cont);
                bool at_left_edge = sx <= 120;
                bool at_right_edge = sx >= 360;
                if (!at_left_edge && !at_right_edge)
                    return;
                uint32_t target_id =
                    (uint32_t)(uintptr_t)lv_obj_get_user_data(cont);
                dismiss_in_progress = true;
                // Drop any in-flight touch tracking before the rebuild
                // reshuffles the list, so a held finger after the snap
                // anim doesn't carry onto the new top card.
                lv_indev_t *act = lv_indev_active();
                if (act)
                    lv_indev_reset(act, NULL);
                ble.dismiss_notification(target_id, /*send_to_phone=*/true);
                lv_async_call(
                    [](void *)
                    {
                        rebuild_notification_list();
                        dismiss_in_progress = false;
                    },
                    nullptr);
            },
            LV_EVENT_SCROLL_END, NULL);

        // Tap a card to reopen the full popup view for that notification.
        uint32_t id = n.id;
        lv_obj_add_event_cb(
            card,
            [](lv_event_t *e)
            {
                uint32_t target_id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
                for (const auto &nn : ble.notifications())
                {
                    if (nn.id == target_id)
                    {
                        // Force popup_apply_icon to re-run for this id
                        // even if the popup was previously up for the
                        // same notification.
                        popup_last_shown_id = target_id;
                        popup_icon_applied = false;
                        popup_show(nn, true);
                        return;
                    }
                }
            },
            LV_EVENT_CLICKED, (void *)(uintptr_t)id);
    }
}

lv_obj_t *notifications_screen_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_track_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(scr, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_main_place(scr, LV_FLEX_ALIGN_START, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_flag(scr, LV_OBJ_FLAG_SCROLL_ELASTIC, false);

    lv_obj_set_style_pad_top(scr, 16, 0);
    lv_obj_set_style_pad_bottom(scr, 40, 0);
    lv_obj_set_style_pad_row(scr, 8, 0);

    notif_list_container = scr;

    // Slow timer that does two things: (1) rebuild the list panel if the
    // notification queue changed (self-skips otherwise), and (2) fire the
    // full-screen popup when a new notification arrives. Both are cheap
    // when there's nothing new. The timer only fires while the LVGL task
    // is running (i.e. the watch is awake), so popups never trigger
    // while the display is asleep — they'll show on next wake by sitting
    // in the queue and getting picked up by the notifications panel.
    lv_timer_create(
        [](lv_timer_t *)
        {
            rebuild_notification_list();
            popup_check_new();
            popup_refresh_icon();
        },
        500, NULL);

    rebuild_notification_list();

    return scr;
}
