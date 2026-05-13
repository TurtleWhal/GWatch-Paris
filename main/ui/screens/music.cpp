#include "ui.hpp"

#include <string>

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
static std::string s_last_state;

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
    const MusicState &m = ble.music();

    // Track/artist/album labels — only set when changed so the scroll
    // animation on songlbl doesn't restart every tick.
    if (m.track != s_last_track)
    {
        s_last_track = m.track;
        lv_label_set_text(songlbl, m.track.empty() ? "" : m.track.c_str());
    }
    if (m.artist != s_last_artist)
    {
        s_last_artist = m.artist;
        lv_label_set_text(artistlbl, m.artist.empty() ? "" : m.artist.c_str());
    }
    if (m.album != s_last_album)
    {
        s_last_album = m.album;
        lv_label_set_text(albumlbl, m.album.empty() ? "" : m.album.c_str());
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
        lv_label_set_text(duration, buf);
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
    lv_label_set_text(position, buf);
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
}

lv_obj_t *music_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);

    artistlbl = lv_label_create(scr);
    lv_obj_set_style_text_font(artistlbl, &ProductSansBold_16, 0);
    lv_obj_align(artistlbl, LV_ALIGN_CENTER, 0, -46);
    lv_label_set_text(artistlbl, "");
    lv_obj_set_size(artistlbl, 210, 20);
    lv_label_set_long_mode(artistlbl, LV_LABEL_LONG_MODE_DOTS);

    songlbl = lv_label_create(scr);
    lv_obj_set_style_text_font(songlbl, &ProductSansBold_30, 0);
    lv_obj_align(songlbl, LV_ALIGN_CENTER, 0, -22);
    lv_obj_set_size(songlbl, 226, 38);
    lv_label_set_long_mode(songlbl, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_label_set_text(songlbl, "—");

    playbackbar = lv_slider_create(scr);
    lv_obj_set_size(playbackbar, 220, 5);
    lv_obj_align(playbackbar, LV_ALIGN_CENTER, 0, 0);
    // Gadgetbridge's BangleJS bridge has no seek protocol — make the
    // bar a status indicator only, not a touch target.
    lv_obj_set_flag(playbackbar, LV_OBJ_FLAG_CLICKABLE, false);
    lv_slider_set_range(playbackbar, 0, 1);
    lv_slider_set_value(playbackbar, 0, LV_ANIM_OFF);

    position = lv_label_create(scr);
    lv_obj_set_style_text_font(position, &ProductSansRegular_14, 0);
    lv_obj_align(position, LV_ALIGN_LEFT_MID, 10, 18);
    lv_label_set_text(position, "0:00");

    duration = lv_label_create(scr);
    lv_obj_set_style_text_font(duration, &ProductSansRegular_14, 0);
    lv_obj_align(duration, LV_ALIGN_RIGHT_MID, -10, 18);
    lv_obj_set_style_text_align(duration, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(duration, "0:00");

    albumlbl = lv_label_create(scr);
    lv_obj_set_style_text_font(albumlbl, &ProductSansRegular_14, 0);
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
        playbtn, [](lv_event_t *) { haptic_play(false, 80, 0); },
        LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(
        prevbtn, [](lv_event_t *) { haptic_play(false, 80, 0); },
        LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(
        nextbtn, [](lv_event_t *) { haptic_play(false, 80, 0); },
        LV_EVENT_PRESSED, NULL);

    // 1 Hz refresh — drives the position label/slider tick. Slider
    // moves 1 second at a time which matches the resolution Gadgetbridge
    // sends, so a faster timer wouldn't make it look smoother.
    lv_timer_create(music_update, 1000, scr);

    return scr;
}
