#include "ui.hpp"

#include <string>

lv_obj_t *musicscr;  // Music screen container (child of lower_layer).
                     // Exposed so wake-time logic can detect when the
                     // user was on this screen vs. notifications.
lv_obj_t *songlbl;
lv_obj_t *artistlbl;
lv_obj_t *albumlbl;

lv_obj_t *playbackbar;
lv_obj_t *duration;
lv_obj_t *position;

lv_obj_t *playbtn;
lv_obj_t *prevbtn;
lv_obj_t *nextbtn;

lv_obj_t *playicon;
lv_obj_t *albumart;      // lv_image, hidden until album art lands
lv_obj_t *albumart_box;  // 240×240 circle-clip parent for albumart

// Shadow twin labels. LVGL labels don't have a glyph-shaped shadow
// style, so we render each visible label twice: once in semi-
// transparent black offset 1 px down-right (created first, so behind),
// then the real label on top. Keeps text legible over light album art.
lv_obj_t *songlbl_sh;
lv_obj_t *artistlbl_sh;
lv_obj_t *albumlbl_sh;
lv_obj_t *duration_sh;
lv_obj_t *position_sh;

// Anchor for local interpolation of the position bar between phone
// updates. When the phone re-reports position_s (changes value), we
// rebase here; in between reports we tick locally using
// esp_timer_get_time so the bar doesn't freeze for the seconds where
// Gadgetbridge isn't sending an update.
static int32_t s_base_position_s = 0;
static int64_t s_base_time_us = 0;
static int32_t s_last_reported_position_s = -1;
static int32_t s_last_duration_s = -1;
static std::string s_last_track;
static std::string s_last_artist;
static std::string s_last_album;

// 1 Hz position-update timer. Tracked so music_destroy can stop it
// before the screen is freed — otherwise the next tick would
// dereference labels that have already been deleted by the parent's
// cascade-delete.
static lv_timer_t *s_music_update_timer = nullptr;
static std::string s_last_state;

// Track which album art is currently bound to the lv_image descriptor.
// Comparing the underlying buffer pointer is enough — the album art
// vector only ever gets fully replaced on a new image transfer.
static const uint8_t *s_last_album_art_data = nullptr;
static uint16_t s_last_album_art_w = 0;

// Mirror a label's text onto its shadow twin, so the rendered shadow
// stays in sync as the main label's content changes. No-op if either
// pointer is null.
static void set_label_pair(lv_obj_t *lbl, lv_obj_t *shadow, const char *text)
{
    if (shadow)
        lv_label_set_text(shadow, text);
    if (lbl)
        lv_label_set_text(lbl, text);
}

static void format_time(char *buf, size_t n, int32_t seconds)
{
    if (seconds < 0)
        seconds = 0;
    int m = seconds / 60;
    int s = seconds % 60;
    snprintf(buf, n, "%d:%02d", m, s);
}

void music_update(lv_timer_t *)
{
    // NOTE: do NOT promote pending album art before reading the track
    // field below. If we did, the track-change branch would run AFTER
    // the promote and clear the freshly-installed image — exactly the
    // "loaded then cleared on wake" bug. We promote AFTER the
    // track-change clear instead, so the order is: read state →
    // (track changed? clear old art) → install new art → rebind.
    const MusicState &m = ble.music();

    // Track/artist/album labels — only set when changed so the scroll
    // animation on songlbl doesn't restart every tick.
    if (m.track != s_last_track)
    {
        s_last_track = m.track;
        set_label_pair(songlbl, songlbl_sh,
                       m.track.empty() ? "" : m.track.c_str());

        // Drop the previous song's album art so we don't keep
        // showing it while waiting for the new song's image_kind=0x01
        // transfer. PSRAM is freed by the move-assign inside
        // set_album_art(). Must happen BEFORE promote_pending_album_art
        // below — otherwise we'd wipe a freshly-promoted image.
        if (!m.album_art.empty())
            ble.set_album_art(PsramByteVec{}, 0, 0);
    }

    // Now that the track-change clear has run, install any image the
    // BLE side staged. This is what makes "image received during
    // sleep" land on the screen on wake: the staged buffer survives
    // the clear above because it lives in s_pending_album_art, not in
    // music_state, until this call.
    ble.promote_pending_album_art();
    if (m.artist != s_last_artist)
    {
        s_last_artist = m.artist;
        set_label_pair(artistlbl, artistlbl_sh,
                       m.artist.empty() ? "" : m.artist.c_str());
    }
    if (m.album != s_last_album)
    {
        s_last_album = m.album;
        set_label_pair(albumlbl, albumlbl_sh,
                       m.album.empty() ? "" : m.album.c_str());
    }

    // Duration / slider range — also only on change. lv_slider's range
    // must be at least (0,1), so clamp the lower bound.
    if (m.duration_s != s_last_duration_s)
    {
        s_last_duration_s = m.duration_s;
        lv_slider_set_range(playbackbar, 0,
                            (m.duration_s > 0) ? m.duration_s : 1);
        char buf[16];
        format_time(buf, sizeof(buf), m.duration_s);
        set_label_pair(duration, duration_sh, buf);
    }

    // Phone re-reported the position — rebase the local clock so the
    // interpolated value below stays in sync with reality.
    if (m.position_s != s_last_reported_position_s)
    {
        s_last_reported_position_s = m.position_s;
        s_base_position_s = m.position_s;
        s_base_time_us = esp_timer_get_time();
    }

    // Compute current position. When playing, advance locally since the
    // last rebase; when paused, freeze at the last reported value.
    int32_t pos = s_base_position_s;
    if (m.state == "play")
        pos += (int32_t)((esp_timer_get_time() - s_base_time_us) / 1000000);
    if (m.duration_s > 0 && pos > m.duration_s)
        pos = m.duration_s;
    if (pos < 0)
        pos = 0;

    char buf[16];
    format_time(buf, sizeof(buf), pos);
    set_label_pair(position, position_sh, buf);
    lv_slider_set_value(playbackbar,
                        (m.duration_s > 0) ? pos : 0, LV_ANIM_OFF);

    // Play/pause icon — toggle on state change.
    // SET_SYMBOL_48 expands to two unbraced statements; the if/else
    // needs explicit braces or the second statement falls outside.
    if (m.state != s_last_state)
    {
        s_last_state = m.state;
        if (m.state == "play")
        {
            SET_SYMBOL_48(playicon, FA_PAUSE);
        }
        else
        {
            SET_SYMBOL_48(playicon, FA_PLAY);
        }
    }

    // Album art — show / re-bind the image descriptor when the
    // underlying buffer changes, hide via the container when cleared.
    if (albumart_box)
    {
        bool has_art = !m.album_art.empty() && m.album_art_w > 0 &&
                       m.album_art_h > 0;
        if (has_art && m.album_art.data() != s_last_album_art_data)
        {
            s_last_album_art_data = m.album_art.data();
            s_last_album_art_w = m.album_art_w;
            static lv_image_dsc_t art_dsc;
            art_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
            art_dsc.header.w = m.album_art_w;
            art_dsc.header.h = m.album_art_h;
            art_dsc.header.stride = m.album_art_w * 2;
            art_dsc.data_size = m.album_art.size();
            art_dsc.data = m.album_art.data();
            lv_image_set_src(albumart, &art_dsc);
            // Pivot at the source's centre so the scaled output stays
            // centered on the lv_image. With the lv_image centered in
            // the 240×240 container (lv_obj_center below), the
            // rendered content lands centered on the screen.
            lv_image_set_pivot(albumart,
                               m.album_art_w / 2, m.album_art_h / 2);
            lv_image_set_scale(albumart, 256 * 240 / m.album_art_w);
            lv_obj_center(albumart);
            lv_obj_set_flag(albumart_box, LV_OBJ_FLAG_HIDDEN, false);
        }
        else if (!has_art && s_last_album_art_data)
        {
            s_last_album_art_data = nullptr;
            s_last_album_art_w = 0;
            lv_obj_set_flag(albumart_box, LV_OBJ_FLAG_HIDDEN, true);
        }
    }
}

lv_obj_t *music_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);
    musicscr = scr;

    // Album-art background: a 240×240 circle-clip container with the
    // lv_image inside. Created before everything else so it sits at
    // the back; labels and buttons draw on top.
    //
    // The container does the round clip in software (panel is round
    // too, but doing it here means the clip is correct even if the
    // panel mask ever changes). The lv_image keeps its natural source
    // size — lv_image_set_src would reset any explicit size anyway —
    // and is centered inside the container via lv_obj_center plus a
    // pivot at the source's centre so scaling expands outward from
    // the middle and the rendered content stays centred on the screen.
    albumart_box = lv_obj_create(scr);
    lv_obj_set_size(albumart_box, 240, 240);
    lv_obj_align(albumart_box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(albumart_box, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(albumart_box, true, 0);
    lv_obj_set_style_border_width(albumart_box, 0, 0);
    lv_obj_set_style_pad_all(albumart_box, 0, 0);
    lv_obj_set_style_bg_opa(albumart_box, 0, 0);
    lv_obj_set_scrollbar_mode(albumart_box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flag(albumart_box, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_flag(albumart_box, LV_OBJ_FLAG_HIDDEN, true);

    albumart = lv_image_create(albumart_box);

    // Helper: every shadow label is the same shape as its real label
    // but 1 px down-right with semi-transparent black ink. Created
    // before the real label so it's behind in the screen's child list
    // (which is also z-order).
    auto shadow_for = [&](const lv_font_t *font, int x, int y,
                          lv_align_t align,
                          lv_label_long_mode_t mode,
                          int w, int h,
                          lv_text_align_t text_align = LV_TEXT_ALIGN_AUTO) {
        lv_obj_t *l = lv_label_create(scr);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, lv_color_black(), 0);
        lv_obj_set_style_text_opa(l, LV_OPA_70, 0);
        lv_obj_align(l, align, x + 1, y + 1);
        if (mode != (lv_label_long_mode_t)-1)
            lv_label_set_long_mode(l, mode);
        if (w || h)
            lv_obj_set_size(l, w, h);
        if (text_align != LV_TEXT_ALIGN_AUTO)
            lv_obj_set_style_text_align(l, text_align, 0);
        lv_label_set_text(l, "");
        return l;
    };

    artistlbl_sh = shadow_for(&ProductSansBold_16_emoji, 0, -46, LV_ALIGN_CENTER,
                              LV_LABEL_LONG_MODE_DOTS, 210, 20);
    artistlbl = lv_label_create(scr);
    lv_obj_set_style_text_font(artistlbl, &ProductSansBold_16_emoji, 0);
    lv_obj_align(artistlbl, LV_ALIGN_CENTER, 0, -46);
    lv_label_set_text(artistlbl, "");
    lv_obj_set_size(artistlbl, 210, 20);
    lv_label_set_long_mode(artistlbl, LV_LABEL_LONG_MODE_DOTS);

    songlbl_sh = shadow_for(&ProductSansBold_30_emoji, 0, -22, LV_ALIGN_CENTER,
                            LV_LABEL_LONG_MODE_SCROLL_CIRCULAR, 226, 38);
    songlbl = lv_label_create(scr);
    lv_obj_set_style_text_font(songlbl, &ProductSansBold_30_emoji, 0);
    lv_obj_align(songlbl, LV_ALIGN_CENTER, 0, -22);
    lv_obj_set_size(songlbl, 226, 38);
    lv_label_set_long_mode(songlbl, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_label_set_text(songlbl, "");

    playbackbar = lv_slider_create(scr);
    lv_obj_set_size(playbackbar, 220, 5);
    lv_obj_align(playbackbar, LV_ALIGN_CENTER, 0, 0);
    // Gadgetbridge's BangleJS bridge has no seek protocol — make the
    // bar a status indicator only, not a touch target.
    lv_obj_set_flag(playbackbar, LV_OBJ_FLAG_CLICKABLE, false);
    lv_slider_set_range(playbackbar, 0, 1);
    lv_slider_set_value(playbackbar, 0, LV_ANIM_OFF);

    position_sh = shadow_for(&ProductSansRegular_14, 10, 18, LV_ALIGN_LEFT_MID,
                             (lv_label_long_mode_t)-1, 0, 0);
    lv_label_set_text(position_sh, "0:00");
    position = lv_label_create(scr);
    lv_obj_set_style_text_font(position, &ProductSansRegular_14, 0);
    lv_obj_align(position, LV_ALIGN_LEFT_MID, 10, 18);
    lv_label_set_text(position, "0:00");

    duration_sh = shadow_for(&ProductSansRegular_14, -10, 18, LV_ALIGN_RIGHT_MID,
                             (lv_label_long_mode_t)-1, 0, 0,
                             LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text(duration_sh, "0:00");
    duration = lv_label_create(scr);
    lv_obj_set_style_text_font(duration, &ProductSansRegular_14, 0);
    lv_obj_align(duration, LV_ALIGN_RIGHT_MID, -10, 18);
    lv_obj_set_style_text_align(duration, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(duration, "0:00");

    albumlbl_sh = shadow_for(&ProductSansRegular_14_emoji, 0, 18, LV_ALIGN_CENTER,
                             LV_LABEL_LONG_MODE_DOTS, 150, 16,
                             LV_TEXT_ALIGN_CENTER);
    albumlbl = lv_label_create(scr);
    lv_obj_set_style_text_font(albumlbl, &ProductSansRegular_14_emoji, 0);
    lv_obj_align(albumlbl, LV_ALIGN_CENTER, 0, 18);
    lv_obj_set_style_text_align(albumlbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(albumlbl, 150, 16);
    lv_label_set_long_mode(albumlbl, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(albumlbl, "");

    playbtn = lv_button_create(scr);
    lv_obj_set_size(playbtn, 80, 80);
    lv_obj_align(playbtn, LV_ALIGN_CENTER, 0, 76);
    lv_obj_set_style_radius(playbtn, LV_RADIUS_CIRCLE, 0);

    playicon = lv_label_create(playbtn);
    lv_obj_center(playicon);
    SET_SYMBOL_48(playicon, FA_PLAY);

    prevbtn = lv_button_create(scr);
    lv_obj_set_size(prevbtn, 50, 50);
    lv_obj_align(prevbtn, LV_ALIGN_CENTER, -70, 56);
    lv_obj_set_style_radius(prevbtn, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *previcon = lv_label_create(prevbtn);
    lv_obj_center(previcon);
    SET_SYMBOL_32(previcon, FA_PREVIOUS);

    nextbtn = lv_button_create(scr);
    lv_obj_set_size(nextbtn, 50, 50);
    lv_obj_align(nextbtn, LV_ALIGN_CENTER, 70, 56);
    lv_obj_set_style_radius(nextbtn, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *nexticon = lv_label_create(nextbtn);
    lv_obj_center(nexticon);
    SET_SYMBOL_32(nexticon, FA_NEXT);

    // Button handlers. The optimistic icon flip on the play button is
    // there so the user sees the state change immediately even though
    // the phone's confirming musicstate may take a moment to arrive —
    // the periodic update from music_update will correct it if the
    // phone disagrees (e.g. the music app rejected the command).
    lv_obj_add_event_cb(
        playbtn,
        [](lv_event_t *)
        {
            bool playing = ble.music().state == "play";
            ble.send_music_control(playing ? "pause" : "play");
            SET_SYMBOL_48(playicon, playing ? FA_PLAY : FA_PAUSE);
        },
        LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(
        prevbtn,
        [](lv_event_t *) { ble.send_music_control("previous"); },
        LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(
        nextbtn,
        [](lv_event_t *) { ble.send_music_control("next"); },
        LV_EVENT_CLICKED, NULL);

    // Short haptic tap on every button press for tactile feedback.
    lv_obj_add_event_cb(
        playbtn, [](lv_event_t *) { haptic_play(false, 50, 0); },
        LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(
        prevbtn, [](lv_event_t *) { haptic_play(false, 50, 0); },
        LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(
        nextbtn, [](lv_event_t *) { haptic_play(false, 50, 0); },
        LV_EVENT_PRESSED, NULL);

    // 1 Hz refresh — drives the position label/slider tick. Slider
    // moves 1 second at a time which matches the resolution Gadgetbridge
    // sends, so a faster timer wouldn't make it look smoother.
    s_music_update_timer = lv_timer_create(music_update, 1000, scr);

    return scr;
}

// Tear down the music screen and the 1 Hz updater. Called by ui.cpp's
// music_visibility_tick when music has been paused long enough that
// the screen should no longer be reachable. Safe to call multiple
// times; no-ops if music_create hasn't been called (or its result has
// already been destroyed).
void music_destroy(void)
{
    if (s_music_update_timer)
    {
        lv_timer_delete(s_music_update_timer);
        s_music_update_timer = nullptr;
    }
    if (musicscr)
    {
        // Cascade-delete frees all the child labels/buttons/images.
        // Their global pointers (songlbl etc.) become stale here — that
        // is OK because music_update is gone and nothing else reads
        // them; the next music_create() reassigns them all anyway.
        lv_obj_delete(musicscr);
        musicscr = nullptr;
    }
    // Reset the local-interpolation anchor so the recreated screen
    // doesn't briefly show a stale position from the previous session.
    s_last_track.clear();
    s_last_artist.clear();
    s_last_album.clear();
    s_last_reported_position_s = -1;
    s_last_duration_s = -1;
}

void music_refresh(void)
{
    // No-op when the music screen hasn't been created (or has been
    // destroyed after the 5-minute idle window). The widget pointers
    // music_update touches are NULL in that state — calling through
    // would crash.
    if (!musicscr) return;

    // Rebase the position-interpolation anchor to NOW. While the
    // watch was asleep the LVGL task was suspended, so music_update
    // didn't tick — but esp_timer_get_time() kept advancing. Without
    // this reset, the next music_update() would compute
    //   pos = s_base_position_s + (now - pre_sleep_s_base_time)/1e6
    // and add the entire sleep duration to the displayed position
    // (showing a song "5 minutes ahead" after a 5-minute sleep).
    // Anchoring to the latest reported value + now keeps the local
    // clock honest until the phone's next musicstate update fully
    // re-syncs us.
    const MusicState &m = ble.music();
    s_base_position_s = m.position_s;
    s_base_time_us = esp_timer_get_time();

    // Run the normal update path immediately so any
    // track / artist / album / state / album-art changes that
    // arrived via BLE while asleep are reflected on the screen the
    // instant the user looks at it, instead of after the next 1 Hz
    // timer tick (up to a second of stale display otherwise). The
    // existing s_last_* comparisons inside music_update will detect
    // a track change and run the album-art clear-and-re-bind path.
    music_update(nullptr);
}
