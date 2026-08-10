#include "ui.hpp"

void press(lv_event_t *e) { haptic_play(false, 40, 0); }

lv_obj_t *create_setting(lv_obj_t *parent, const char *icon,
                         lv_event_cb_t event_cb = nullptr) {
  lv_obj_t *app = lv_button_create(parent);
  lv_obj_set_size(app, 65, 65);
  lv_obj_set_style_bg_color(app, lv_color_hex(0x222222), 0);
  // Accent bg in the CHECKED state via the shared style so toggles
  // (DnD, BT, rotate) refresh when the user picks a new theme color.
  lv_obj_add_style(app, &accent_bg_style, LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_set_style_radius(app, LV_RADIUS_CIRCLE, 0);

  lv_obj_t *label = lv_label_create(app);
  lv_obj_center(label);
  SET_SYMBOL_28(label, icon);

  if (event_cb != nullptr) {
    lv_obj_add_event_cb(app, event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(app, press, LV_EVENT_CLICKED, NULL);
  } else {
    lv_obj_set_style_opa(app, LV_OPA_50, 0);
    lv_obj_set_flag(app, LV_OBJ_FLAG_CLICKABLE, false);
  }

  return app;
}

lv_obj_t *quicksettings_create(lv_obj_t *parent) {
  lv_obj_t *scr = create_screen(parent);
  lv_obj_set_scroll_dir(scr, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_bottom(scr, 10, 0);

  // Start with a hardcoded full icon so the panel never opens with a
  // half-charged silhouette flashing in for a frame before the battery
  // task reports real numbers — and so the icon never lies low if the
  // battery state hasn't been polled yet at the moment of creation.
  // The 10 s timer below swaps in the real icon shortly after.
  lv_obj_t *battery = create_setting(scr, FA_BATTERY_FULL, [](lv_event_t *) {});
  lv_obj_set_flag(battery, LV_OBJ_FLAG_CLICKABLE, false);

  lv_obj_align(lv_obj_get_child(battery, 0), LV_ALIGN_CENTER, 0, 8);

  lv_obj_t *batpct = lv_label_create(battery);
  lv_obj_set_style_text_color(batpct, lv_color_white(), 0);
  lv_obj_align(batpct, LV_ALIGN_CENTER, 0, -14);
  lv_obj_set_style_text_font(batpct, &ProductSansRegular_14, 0);
  lv_label_set_text_fmt(batpct, "%d%%", watch.battery.percent);

  // Refresh the battery icon every 10 s. The label is the only child
  // create_setting added to the button, so child(0) is safe. Just
  // lv_label_set_text — the font was already pinned by the
  // SET_SYMBOL_28 in create_setting, so we don't need to reapply it.
  lv_timer_create(
      [](lv_timer_t *t) {
        lv_obj_t *battery = (lv_obj_t *)lv_timer_get_user_data(t);
        // lv_label_set_text(lbl, getbaticon(watch.battery.charging,
        //                                   watch.battery.percent));
        SET_SYMBOL_28(lv_obj_get_child(battery, 0),
                      getbaticon(watch.battery.charging, watch.battery.percent))
        lv_label_set_text_fmt(lv_obj_get_child(battery, 1), "%d%%",
                              watch.battery.percent);
      },
      10000, battery);

  lv_obj_t *settings = create_setting(
      scr, FA_SETTINGS, [](lv_event_t *) { lv_screen_load(settings_screen); });

  lv_obj_t *calculator = create_setting(scr, FA_CALCULATOR, [](lv_event_t *) {
    lv_screen_load(calculator_screen);
  });

  lv_obj_t *units = create_setting(
      scr, FA_RULER, [](lv_event_t *) { lv_screen_load(units_screen); });

  lv_obj_t *homeassistant = create_setting(scr, FA_SMARTHOME, [](lv_event_t *) {
    lv_screen_load(homeassistant_screen);
  });
  SET_MDI_SYMBOL_28(lv_obj_get_child(homeassistant, 0), MDI_HOME_ASSISTANT);

  lv_obj_t *donotdisturb =
      create_setting(scr, FA_DONOTDISTURB, [](lv_event_t *e) {
        watch.donotdisturb = !watch.donotdisturb;
        lv_obj_set_state(lv_event_get_target_obj(e), LV_STATE_CHECKED,
                         watch.donotdisturb);
        press(NULL);
      });
  lv_obj_remove_event_cb(donotdisturb, press);

  lv_obj_t *restart =
      create_setting(scr, FA_POWEROFF, [](lv_event_t *) { esp_restart(); });

  lv_obj_remove_event_cb(restart, press);

  // lv_obj_t *rotate = create_setting(scr, FA_ROTATE, [](lv_event_t *)
  //                                   {
  //                                   settings_apply_rotation((lv_display_rotation_t)((lv_display_get_rotation(NULL)
  //                                   + 2) % 4));
  //                                   lv_obj_scroll_to_view_recursive(watchscr,
  //                                   LV_ANIM_ON); });

  lv_obj_t *flashlight = create_setting(scr, FA_FLASHLIGHT, [](lv_event_t *) {
    static uint16_t flashlight_prev;

    static lv_obj_t *flashlight_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(flashlight_screen, lv_color_white(), 0);

    flashlight_prev = watch.display.get_brightness();

    lv_screen_load_anim(flashlight_screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 100, 0,
                        false);

    watch.display.set_backlight(100);

    lv_obj_add_event_cb(
        flashlight_screen,
        [](lv_event_t *e) {
          watch.display.set_backlight(*(uint16_t *)lv_event_get_user_data(e));
          lv_screen_load_anim(main_screen, LV_SCREEN_LOAD_ANIM_FADE_OUT, 100, 0,
                              false);
        },
        LV_EVENT_CLICKED, &flashlight_prev);
  });

  lv_obj_t *findphone = create_setting(scr, FA_PHONE_VIBRATE, [](lv_event_t *) {
    // Find-phone modal: confirmation step before sending the ring
    // command (so a stray tap on the quicksettings panel doesn't
    // make the user's phone scream), plus a Stop button while
    // ringing so they don't have to dig out the phone to silence it.
    //
    // The screen is lazy-built on first invocation and reused —
    // rebuilding every open would briefly stall the LVGL task while
    // allocating widgets. Function-local statics keep the widget
    // pointers + ringing state accessible from the inner click /
    // gesture callbacks (all of which are captureless lambdas, so
    // they reference these by name as in-scope statics).
    static lv_obj_t *find_screen = nullptr;
    static lv_obj_t *title_lbl = nullptr;
    static lv_obj_t *primary_lbl = nullptr;
    static lv_obj_t *cancel_btn = nullptr;
    static bool ringing = false;

    static auto set_state = [](bool r) {
      ringing = r;
      if (r) {
        lv_label_set_text(title_lbl, "Ringing");
        lv_label_set_text(primary_lbl, "Stop");
        lv_obj_add_flag(cancel_btn, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_label_set_text(title_lbl, "Find phone?");
        lv_label_set_text(primary_lbl, "Ring");
        lv_obj_remove_flag(cancel_btn, LV_OBJ_FLAG_HIDDEN);
      }
      // Re-apply center alignment — text-change resizes the label but
      // lv_obj_align is a one-shot at the time it was last called, so
      // a wider/narrower label would otherwise stay anchored to the
      // old center position.
      lv_obj_align(title_lbl, LV_ALIGN_CENTER, 0, -25);
    };

    static auto close_screen = []() {
      lv_screen_load_anim(main_screen, LV_SCREEN_LOAD_ANIM_FADE_OUT, 100, 0,
                          false);
    };

    if (!find_screen) {
      find_screen = lv_obj_create(NULL);
      // Explicit 240×240 + zero everything that LVGL's default screen
      // theme adds (border, radius, padding, scrollbar). Without an
      // explicit size lv_obj_align resolves against a zero-width
      // parent and pins everything to the left edge.
      lv_obj_set_size(find_screen, 240, 240);
      lv_obj_set_style_bg_color(find_screen, lv_color_black(), 0);
      lv_obj_set_style_pad_all(find_screen, 0, 0);
      lv_obj_set_style_border_width(find_screen, 0, 0);
      lv_obj_set_style_radius(find_screen, 0, 0);
      lv_obj_remove_flag(find_screen, LV_OBJ_FLAG_SCROLLABLE);

      // Manual absolute layout with LV_ALIGN_CENTER + Y offsets.
      // Tried flex (both on the screen and via a centered child
      // container) — neither produced visible centering on this
      // build; widgets stuck to the left edge. lv_obj_align with
      // LV_ALIGN_CENTER pivots the widget's own center on the
      // parent's center + offset, so as long as find_screen has a
      // real width, the horizontal position is deterministic
      // regardless of the widget's intrinsic size.

      lv_obj_t *icon_lbl = lv_label_create(find_screen);
      SET_SYMBOL_48(icon_lbl, FA_PHONE_VIBRATE);
      lv_obj_set_style_text_color(icon_lbl, lv_color_white(), 0);
      lv_obj_align(icon_lbl, LV_ALIGN_CENTER, 0, -75);

      title_lbl = lv_label_create(find_screen);
      lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);
      lv_obj_set_style_text_font(title_lbl, &ProductSansRegular_20, 0);
      lv_obj_align(title_lbl, LV_ALIGN_CENTER, 0, -25);

      lv_obj_t *primary_btn = lv_button_create(find_screen);
      lv_obj_set_size(primary_btn, 130, 50);
      lv_obj_set_style_radius(primary_btn, LV_RADIUS_CIRCLE, 0);
      lv_obj_add_style(primary_btn, &accent_bg_style, 0);
      lv_obj_align(primary_btn, LV_ALIGN_CENTER, 0, 25);
      primary_lbl = lv_label_create(primary_btn);
      lv_obj_center(primary_lbl);
      lv_obj_set_style_text_color(primary_lbl, lv_color_white(), 0);
      lv_obj_set_style_text_font(primary_lbl, &ProductSansRegular_20, 0);

      cancel_btn = lv_button_create(find_screen);
      lv_obj_set_size(cancel_btn, 130, 42);
      lv_obj_set_style_radius(cancel_btn, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x222222), 0);
      lv_obj_align(cancel_btn, LV_ALIGN_CENTER, 0, 80);
      lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
      lv_obj_center(cancel_lbl);
      lv_label_set_text(cancel_lbl, "Cancel");
      lv_obj_set_style_text_color(cancel_lbl, lv_color_white(), 0);
      lv_obj_set_style_text_font(cancel_lbl, &ProductSansRegular_20, 0);

      lv_obj_add_event_cb(
          primary_btn,
          [](lv_event_t *) {
            haptic_play(false, 50, 0);
            if (ringing) {
              ble.send_find_phone(false);
              set_state(false);
              close_screen();
            } else {
              ble.send_find_phone(true);
              set_state(true);
            }
          },
          LV_EVENT_CLICKED, NULL);

      lv_obj_add_event_cb(
          cancel_btn,
          [](lv_event_t *) {
            haptic_play(false, 30, 0);
            close_screen();
          },
          LV_EVENT_CLICKED, NULL);

      // Right-swipe back to the watch face. If the phone is currently
      // ringing, silence it on the way out — leaving it ringing after
      // the user has clearly moved on would be confusing.
      lv_obj_add_event_cb(
          find_screen,
          [](lv_event_t *) {
            if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
              if (ringing) {
                ble.send_find_phone(false);
                set_state(false);
              }
              close_screen();
            }
          },
          LV_EVENT_GESTURE, NULL);
    }

    // Always reset to the confirmation step on open — if a previous
    // session ended in "Ringing" but the phone auto-stopped, we don't
    // want the watch to show a Stop button that does nothing.
    set_state(false);
    lv_screen_load_anim(find_screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 100, 0,
                        false);
  });

  // Toggle BLE advertising. Disabling drops any active connection and
  // hides us from scanners; re-enabling resumes the same advertisement.
  lv_obj_t *airplanemode = create_setting(scr, FA_AIRPLANE, [](lv_event_t *e) {
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    on = !on;
    lv_obj_set_state(lv_event_get_target_obj(e), LV_STATE_CHECKED, on);
    ble.set_enabled(!on);
  });

#define ARC_RADIUS 77
#define KNOB_THICKNESS 65
#define ARC_THICKNESS 12
// #define INDICATOR_THICKNESS 20
#define INDICATOR_THICKNESS 65

  lv_obj_t *brightness = lv_arc_create(scr);
  lv_obj_set_size(brightness, ARC_RADIUS * 2 + KNOB_THICKNESS + 4,
                  ARC_RADIUS * 2 + KNOB_THICKNESS + 4);
  lv_obj_align(brightness, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_arc_width(brightness, ARC_THICKNESS, 0);
  lv_obj_set_style_arc_width(brightness, INDICATOR_THICKNESS,
                             LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(brightness, lv_color_hex(0x222222), 0);
  // lv_obj_set_style_arc_color(brightness, lv_color_hex(0x222222),
  // LV_PART_INDICATOR);

  lv_obj_set_style_pad_all(brightness, (KNOB_THICKNESS - ARC_THICKNESS) / 2 + 2,
                           LV_PART_MAIN);
  lv_obj_set_style_pad_all(brightness,
                           -(INDICATOR_THICKNESS - ARC_THICKNESS) / 2 + 2,
                           LV_PART_INDICATOR);

  lv_obj_set_style_opa(brightness, LV_OPA_0, LV_PART_KNOB);
  // lv_obj_set_style_pad_all(brightness, 10, LV_PART_KNOB);
  // lv_obj_set_style_pad_bottom(brightness, 0, LV_PART_KNOB);

  lv_arc_set_bg_angles(brightness, 270 - 60, 270 + 60);
  lv_arc_set_angles(brightness, 270 - 60, 270 + 60);
  lv_arc_set_range(brightness, 2, 100);
  lv_arc_set_value(brightness, 100);

  lv_obj_set_flag(
      brightness, LV_OBJ_FLAG_ADV_HITTEST,
      true); // allow touches through the arc so that buttons can be pressed

  lv_obj_t *knob = lv_obj_create(brightness);
  lv_obj_set_style_border_width(knob, 0, 0);
  lv_obj_add_style(knob, &accent_bg_style, 0);
  lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_size(knob, KNOB_THICKNESS, KNOB_THICKNESS);
  lv_obj_set_flag(knob, LV_OBJ_FLAG_CLICKABLE, false);
  lv_obj_set_scroll_dir(knob, LV_DIR_NONE);

  lv_obj_t *brightnessicon = lv_label_create(knob);
  SET_SYMBOL_28(brightnessicon, FA_BRIGHTNESS);
  lv_obj_align(brightnessicon, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_color(brightnessicon, lv_color_white(), 0);

  // lv_arc_align_obj_to_angle(brightness, knob, ARC_THICKNESS / 2 - 2);
  lv_arc_align_obj_to_angle(brightness, knob, INDICATOR_THICKNESS / 2 - 8);

  lv_obj_add_event_cb(
      brightness,
      [](lv_event_t *e) {
        haptic_play(false, 10, 0);

        lv_obj_t *arc = lv_event_get_target_obj(e);
        int32_t value = lv_arc_get_value(arc);
        watch.display.set_backlight(value);

        if (value > 40) {
          SET_SYMBOL_28(
              lv_obj_get_child((lv_obj_t *)lv_event_get_user_data(e), 0),
              FA_BRIGHTNESS);
        } else {
          SET_SYMBOL_28(
              lv_obj_get_child((lv_obj_t *)lv_event_get_user_data(e), 0),
              FA_BRIGHTNESS_LOW);
        }

        lv_arc_align_obj_to_angle(arc, (lv_obj_t *)lv_event_get_user_data(e),
                                  INDICATOR_THICKNESS / 2 - 8);
        // lv_arc_align_obj_to_angle(arc, (lv_obj_t *)lv_event_get_user_data(e),
        // ARC_THICKNESS / 2 - 2);
      },
      LV_EVENT_VALUE_CHANGED, knob);

  lv_obj_align(units, LV_ALIGN_CENTER, POLAR(77, -150) + 77 * 1); // Top Left

  lv_obj_align(airplanemode, LV_ALIGN_CENTER,
               POLAR(77, -150) + 77 * 2); // Mid Left

  lv_obj_align(battery, LV_ALIGN_CENTER,
               POLAR(77, 150) + 77 * 2); // Bottom Left

  lv_obj_align(findphone, LV_ALIGN_CENTER, POLAR(77, -90) + 77 * 1); // Top
  lv_obj_align(flashlight, LV_ALIGN_CENTER,
               POLAR(77, -90) + 77 * 2);                 // Top Center
  lv_obj_align(restart, LV_ALIGN_CENTER, 0, 0 + 77 * 2); // Bottom Center
  lv_obj_align(settings, LV_ALIGN_CENTER, POLAR(77, 90) + 77 * 2); // Bottom

  lv_obj_align(homeassistant, LV_ALIGN_CENTER,
               POLAR(77, -30) + 77 * 1); // Top Right
  lv_obj_align(calculator, LV_ALIGN_CENTER,
               POLAR(77, -30) + 77 * 2); // Mid Right
  lv_obj_align(donotdisturb, LV_ALIGN_CENTER,
               POLAR(77, 30) + 77 * 2); // Bottom Right

  lv_obj_update_layout(scr);
  lv_obj_scroll_to_y(scr, lv_obj_get_scroll_bottom(scr), LV_ANIM_OFF);

  return scr;
}