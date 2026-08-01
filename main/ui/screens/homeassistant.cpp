#include "ble.hpp"
#include "ui.hpp"

#include "cJSON.h"
#include "esp_lvgl_port.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define HA_URL "ha.iegs.synology.me"
#define HA_API_KEY                                                             \
  "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."                                      \
  "eyJpc3MiOiI0N2YwMTkxYzU5YWU0YTVjYmQ2NjQ0Y2E0MmNlYzAyYyIsImlhdCI6MTc4NDQ4MD" \
  "I2MiwiZXhwIjoyMDk5ODQwMjYyfQ.PguRDlPL9GNaGTKmyyRE0UNoqZMyj4mARJUQUkhE8AA"

#define HA_ENDPOINT_LIGHT_ON "api/services/light/turn_on"
#define HA_ENDPOINT_LIGHT_OFF "api/services/light/turn_off"
#define HA_ENDPOINT_LIGHT_TOGGLE "api/services/light/toggle"

#define HA_ENDPOINT_FAN_PERCENTAGE "api/services/fan/set_percentage"
#define HA_ENDPOINT_FAN_TOGGLE "api/services/fan/toggle"
#define HA_ENDPOINT_COVER_POSITION "api/services/cover/set_cover_position"
#define HA_ENDPOINT_COVER_TOGGLE "api/services/cover/toggle"
#define HA_ENDPOINT_BUTTON_PRESS "api/services/button/press"

enum CardType {
  CARD_SLIDER,
  CARD_SLIDER_3,
  CARD_INFO,
  CARD_BUTTON,
};

enum EntityType {
  HA_INFO,
  HA_LIGHT,
  HA_BUTTON,
  HA_FAN,
  HA_COVER,
};

uint8_t cardType[] = {CARD_INFO, CARD_SLIDER, CARD_BUTTON, CARD_SLIDER_3,
                      CARD_SLIDER};

struct HAEntity {
  lv_obj_t *card;
  EntityType type;
  char *id;
  char *name;
  char *icon;
  lv_color_t color;
  char *unit; // suffix shown after an info card's value, e.g. "°F" / "W"
};

// HAEntity temp;
// HAEntity power;
// HAEntity lights;
// HAEntity fan;
// HAEntity window;
// HAEntity largeshade;
// HAEntity smallshade;
// HAEntity pc;

#define NUM_ENTITIES 8

HAEntity entities[NUM_ENTITIES];

void homeassistant_update(lv_event_t *e);
void update_cards();

HAEntity create_ha_card(lv_obj_t *parent, EntityType type, const char *id,
                        const char *name, const char *symbol,
                        lv_color_t color, const char *unit = "%") {
  lv_obj_t *card = lv_obj_create(parent);

  uint8_t cardtype = cardType[type];

// HA: 242x110
#define SF 110 / 110

  uint8_t width = 200;

  // if (type == CARD_INFO) {
  //   width = 96;
  // }

  uint8_t height = 56;

  if (cardtype == CARD_SLIDER || cardtype == CARD_SLIDER_3) {
    height = 110;
  }

  lv_obj_set_size(card, width * SF, height * SF);
  lv_obj_set_style_bg_color(card, lv_color_make(28, 28, 28), 0);
  lv_obj_set_style_border_color(card, lv_color_make(52, 52, 52), 0);
  lv_obj_set_scroll_dir(card, LV_DIR_NONE);
  lv_obj_set_style_radius(card, 12 * SF, 0);
  lv_obj_set_style_pad_all(card, -2, 0);
  lv_obj_set_style_border_width(card, 2, 0);

  lv_obj_t *iconbg = lv_obj_create(card);
  lv_obj_set_size(iconbg, 36 * SF, 36 * SF);
  lv_obj_set_style_radius(iconbg, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(iconbg, 0, 0);
  lv_obj_set_style_bg_color(iconbg, color, 0);
  lv_obj_set_style_bg_opa(iconbg, 120, 0);
  lv_obj_set_scroll_dir(iconbg, LV_DIR_NONE);
  lv_obj_set_pos(iconbg, 10 * SF, 10 * SF);

  lv_obj_t *icon = lv_label_create(iconbg);
  SET_MDI_SYMBOL_22(icon, symbol);
  lv_obj_set_style_text_color(icon, color, 0);
  lv_obj_center(icon);

  lv_obj_t *namelbl = lv_label_create(card);
  lv_obj_set_style_text_font(namelbl, &ProductSansRegular_14, 0);
  lv_obj_set_pos(namelbl, 56 * SF, 10 * SF);
  lv_label_set_text(namelbl, name);

  if (cardtype != CARD_BUTTON) {
    lv_obj_t *valuelbl = lv_label_create(card);
    lv_obj_set_style_text_font(valuelbl, &ProductSansRegular_12, 0);
    lv_obj_set_pos(valuelbl, 56 * SF, 30 * SF);
    lv_label_set_text(valuelbl, "---");
  }

  if (cardtype == CARD_SLIDER || cardtype == CARD_SLIDER_3) {
    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_size(slider, 176 * SF, 42 * SF);
    // Don't let a horizontal drag on the slider bubble up to the screen's
    // right-swipe back gesture (registered in create_app). GESTURE_BUBBLE is
    // on by default; clearing it keeps the drag local to the slider.
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_radius(slider, 12 * SF, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 12 * SF, LV_PART_INDICATOR);
    lv_obj_set_style_opa(slider, 0, LV_PART_KNOB);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_LEFT, 12 * SF, -12 * SF);
    lv_obj_set_style_bg_color(slider, color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, 120, LV_PART_INDICATOR);

    if (cardtype == CARD_SLIDER_3) {
      lv_slider_set_range(slider, 0, 3);
      lv_slider_set_value(slider, 3, 0);
    } else {
      lv_slider_set_value(slider, 100, 0);
    }

    // All three send on LV_EVENT_RELEASED (finger lifted) rather than
    // LV_EVENT_VALUE_CHANGED, so a single command goes out when the drag
    // finishes instead of one per intermediate value. The value label and
    // accent color still track live off VALUE_CHANGED (see the cb below).
    // Bonus: the response handler's synthetic VALUE_CHANGED (used to refresh
    // the label from an incoming state) no longer echoes a command back to HA.
    if (type == HA_LIGHT) {
      lv_obj_add_event_cb(
          slider,
          [](lv_event_t *e) {
            std::string message =
                std::string("{t:\"http\", url:\"https://" HA_URL
                            "/" HA_ENDPOINT_LIGHT_ON "\", method:\"post\", "
                            "headers:{\"Authorization\":\"Bearer " HA_API_KEY
                            "\", \"Content-Type\":\"application/json\"}, "
                            "body:{\"entity_id\":\"") +
                std::string((const char *)lv_event_get_user_data(e)) +
                std::string("\", \"brightness\":") +
                std::to_string(lv_slider_get_value(lv_event_get_target_obj(e)) *
                               2.55f) +
                std::string("}}");
            ble.send_gb(message.c_str());
          },
          LV_EVENT_RELEASED, (void *)id);
    } else if (type == HA_COVER) {
      lv_obj_add_event_cb(
          slider,
          [](lv_event_t *e) {
            std::string message =
                std::string("{t:\"http\", url:\"https://" HA_URL
                            "/" HA_ENDPOINT_COVER_POSITION
                            "\", method:\"post\", "
                            "headers:{\"Authorization\":\"Bearer " HA_API_KEY
                            "\", \"Content-Type\":\"application/json\"}, "
                            "body:{\"entity_id\":\"") +
                std::string((const char *)lv_event_get_user_data(e)) +
                std::string("\", \"position\":") +
                std::to_string(
                    lv_slider_get_value(lv_event_get_target_obj(e))) +
                std::string("}}");
            ble.send_gb(message.c_str());
          },
          LV_EVENT_RELEASED, (void *)id);
    } else if (type == HA_FAN) {
      lv_obj_add_event_cb(
          slider,
          [](lv_event_t *e) {
            uint32_t value = lv_slider_get_value(lv_event_get_target_obj(e));
            value = 33.3f * value;

            if (value == 99)
              value = 100;

            std::string message =
                std::string("{t:\"http\", url:\"https://" HA_URL
                            "/" HA_ENDPOINT_FAN_PERCENTAGE
                            "\", method:\"post\", "
                            "headers:{\"Authorization\":\"Bearer " HA_API_KEY
                            "\", \"Content-Type\":\"application/json\"}, "
                            "body:{\"entity_id\":\"") +
                std::string((const char *)lv_event_get_user_data(e)) +
                std::string("\", \"percentage\":") + std::to_string(value) +
                std::string("}}");
            ble.send_gb(message.c_str());
          },
          LV_EVENT_RELEASED, (void *)id);
    }

    lv_obj_add_event_cb(
        slider,
        [](lv_event_t *e) {
          lv_obj_t *slider = lv_event_get_target_obj(e);
          lv_obj_t *card = lv_obj_get_parent(slider);
          int32_t value = lv_slider_get_value(slider);

          if (lv_slider_get_max_value(slider) == 3) {
            value = 33.3f * value;

            if (value == 99)
              value = 100;
          }

          // ESP_LOGI("homeassistant", "Slider changed to %li\n", value);

          // Card child order: 0=iconbg, 1=namelbl, 2=valuelbl, 3=slider.
          // (icon is a child of iconbg, not the card.) Index 3 is the slider
          // itself — writing label text onto it corrupts the slider and snaps
          // its value to 0.
          lv_obj_t *valuelbl = lv_obj_get_child(card, 2);
          lv_label_set_text_fmt(valuelbl, "%li%%", value);

          lv_obj_t *iconbg = lv_obj_get_child(card, 0);
          lv_obj_t *icon = lv_obj_get_child(iconbg, 0);

          // The accent color is packed into the event user_data as a 32-bit
          // RGB value (see the add_event_cb below). Passing &color directly
          // would be a dangling pointer — `color` is ha_card's stack
          // parameter, gone by the time this fires.
          lv_color_t color =
              lv_color_hex((uint32_t)(uintptr_t)lv_event_get_user_data(e));

          if (value == 0) {
            color = lv_color_make(189, 189, 189);
          }

          lv_obj_set_style_bg_color(iconbg, color, 0);
          lv_obj_set_style_text_color(icon, color, 0);

          lv_obj_set_style_bg_color(slider, color, LV_PART_MAIN);
          lv_obj_set_style_bg_color(slider, color, LV_PART_INDICATOR);
        },
        LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)lv_color_to_u32(color));
  }

  HAEntity entity = HAEntity{card,         type,  (char *)id,   (char *)name,
                             (char *)icon, color, (char *)unit};

  // lv_obj_add_event_cb(
  //     card,
  //     [](lv_event_t *e) {
  //       HAEntity *entity = (HAEntity *)lv_event_get_user_data(e);
  //       char *endpoint = NULL;

  //       switch (entity->type) {
  //       case HA_BUTTON:
  //         endpoint = HA_ENDPOINT_BUTTON_PRESS;
  //         break;
  //       case HA_LIGHT:
  //         endpoint = HA_ENDPOINT_LIGHT_TOGGLE;
  //         break;
  //       case HA_FAN:
  //         endpoint = HA_ENDPOINT_FAN_TOGGLE;
  //         break;
  //       case HA_COVER:
  //         endpoint = HA_ENDPOINT_COVER_TOGGLE;
  //         break;
  //       case HA_INFO:
  //         break;
  //       }

  //       if (endpoint != NULL) {
  //         std::string message =
  //             std::string("{t:\"http\", url:\"https://" HA_URL
  //                         "/") + endpoint + "\", method:\"post\", "
  //                         "headers:{\"Authorization\":\"Bearer " HA_API_KEY
  //                         "\", \"Content-Type\":\"application/json\"}, "
  //                         "body:{\"entity_id\":\"" +
  //             std::string((const char *)lv_event_get_user_data(e)) +
  //             std::string("\"}}");
  //         ble.send_gb(message.c_str());
  //       }

  //       update_cards();
  //     },
  //     LV_EVENT_CLICKED, (void *)&entity);

  return entity;
}

lv_obj_t *homeassistant_create(lv_obj_t *parent) {
  lv_obj_t *scr = create_screen(parent);

  // return scr;

  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_flex_track_place(scr, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_set_style_flex_cross_place(scr, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

  lv_obj_set_style_pad_row(scr, 8, 0);

  lv_obj_set_style_pad_bottom(scr, 50, 0);

  // lv_obj_t *scrlabel = lv_label_create(scr);
  // lv_label_set_text(scrlabel, "Home Assistant");
  // lv_obj_set_style_text_font(scrlabel, &ProductSansRegular_20, 0);
  // lv_obj_set_style_text_color(scrlabel, lv_color_white(), 0);
  // // lv_obj_set_style_text_align(appslabel, LV_TEXT_ALIGN_CENTER, 0);

  // lv_obj_set_style_pad_top(scrlabel, 12, 0);
  // lv_obj_set_style_pad_bottom(scrlabel, 8, 0);

  lv_obj_set_style_pad_top(scr, 50, 0);

  entities[0] =
      create_ha_card(scr, HA_INFO, "sensor.garrett_temperature", "Garrett Temp",
                     MDI_THERMOMETER, lv_color_make(255, 120, 0), "°F");
  entities[1] = create_ha_card(scr, HA_INFO, "sensor.garrett_stairs_3_1min",
                               "Garrett Power", MDI_LIGHTNING,
                               lv_color_make(255, 120, 0), "W");

  entities[2] =
      create_ha_card(scr, HA_LIGHT, "light.main_lights", "Family Room Main",
                     MDI_LIGHTBULB, lv_color_make(255, 160, 0));
  entities[3] = create_ha_card(scr, HA_FAN, "fan.garrett_fan", "Garrett Fan",
                               MDI_FAN, lv_color_make(76, 200, 80));
  entities[4] =
      create_ha_card(scr, HA_COVER, "cover.garrett_window", "Garrett Window",
                     MDI_WINDOW, lv_color_make(28, 150, 255));
  entities[5] = create_ha_card(
      scr, HA_COVER, "cover.garrett_large_shades_cover", "Garrett Large Shades",
      MDI_WINDOW_SHADE, lv_color_make(33, 150, 243));
  entities[6] = create_ha_card(
      scr, HA_COVER, "cover.garrett_small_shades_cover", "Garrett Small Shades",
      MDI_WINDOW_SHADE, lv_color_make(33, 150, 243));
  entities[7] = create_ha_card(scr, HA_BUTTON, "input_button.wake_garrett_pc",
                               "Garrett Wake PC", MDI_DESKTOP_PC,
                               lv_color_make(68, 115, 158));

  lv_obj_add_event_cb(scr, homeassistant_update, LV_EVENT_SCREEN_LOADED, NULL);

  return scr;
}

void update_cards() {
  for (uint8_t i = 0; i < NUM_ENTITIES; i++) {
    // Info-only cards (HA_INFO) have no backing entity — skip them so we
    // don't GET /api/states/ with an empty id.
    if (!entities[i].id || entities[i].id[0] == '\0')
      continue;

    // The id is a runtime value, so the URL must be built at runtime:
    // `"literal" + char* + "literal"` is pointer arithmetic, not
    // concatenation, and send_gb() wants a C string. std::string handles it.
    std::string msg =
        std::string("{t:\"http\", url:\"https://" HA_URL "/api/states/") +
        entities[i].id +
        "\", method:\"get\", headers:{\"Authorization\":\"Bearer " HA_API_KEY
        "\", \"Content-Type\":\"application/json\"}}";
    ble.send_gb(msg.c_str());
  }
}

void homeassistant_update(lv_event_t *e) {
  update_cards();
  // ble.send_gb(
  //     "{t:\"http\",
  //     url:\"https://ha.iegs.synology.me/api/services/light/turn_off\",
  //     method:\"post\", headers:{\"Authorization\":\"Bearer " HA_API_KEY "\",
  //     \"Content-Type\":\"application/json\"},
  //     body:{\"entity_id\":\"light.main_lights\"}}");
}

// Map a Home Assistant entity's state + attributes onto a 0..100 level for
// the card's slider. Falls back to plain on/off (100/0) when there's no
// finer-grained attribute to read.
static int ha_level_from_state(EntityType type, const char *state,
                               cJSON *attrs) {
  bool on = state && (strcmp(state, "on") == 0 || strcmp(state, "open") == 0);
  switch (type) {
  case HA_LIGHT: {
    if (!on)
      return 0;
    cJSON *b =
        attrs ? cJSON_GetObjectItemCaseSensitive(attrs, "brightness") : nullptr;
    if (cJSON_IsNumber(b))
      return (int)(b->valuedouble * 100.0 / 255.0 + 0.5); // 0..255 -> 0..100
    return 100;
  }
  case HA_FAN: {
    if (!on)
      return 0;
    cJSON *p =
        attrs ? cJSON_GetObjectItemCaseSensitive(attrs, "percentage") : nullptr;
    if (cJSON_IsNumber(p))
      return (int)p->valuedouble;
    return 100;
  }
  case HA_COVER: {
    cJSON *pos =
        attrs ? cJSON_GetObjectItemCaseSensitive(attrs, "current_position")
              : nullptr;
    if (cJSON_IsNumber(pos))
      return (int)pos->valuedouble;
    return on ? 100 : 0;
  }
  default:
    return on ? 100 : 0;
  }
}

// Called from the BLE rx task (ble.cpp's {t:"http"} dispatch) with the inner
// JSON of a Home Assistant REST state response, e.g.
//   {"entity_id":"light.main_lights","state":"on","attributes":{...},...}
// Matches entity_id to a card and updates its slider + value label.
void homeassistant_response_recieved(const char *resp_json) {
  cJSON *root = cJSON_Parse(resp_json);
  if (!root)
    return;

  cJSON *idj = cJSON_GetObjectItemCaseSensitive(root, "entity_id");
  if (cJSON_IsString(idj) && idj->valuestring) {
    int idx = -1;
    for (uint8_t i = 0; i < NUM_ENTITIES; i++) {
      if (entities[i].id && strcmp(entities[i].id, idj->valuestring) == 0) {
        idx = i;
        break;
      }
    }

    if (idx >= 0) {
      cJSON *statej = cJSON_GetObjectItemCaseSensitive(root, "state");
      const char *state = cJSON_IsString(statej) ? statej->valuestring : "";
      cJSON *attrs = cJSON_GetObjectItemCaseSensitive(root, "attributes");
      int level = ha_level_from_state(entities[idx].type, state, attrs);

      uint8_t ct = cardType[entities[idx].type];

      // Runs on the BLE rx task, not the LVGL task — hold the port lock
      // around the widget mutation.
      if ((ct == CARD_SLIDER || ct == CARD_SLIDER_3 || ct == CARD_INFO) &&
          lvgl_port_lock(0)) {
        lv_obj_t *card = entities[idx].card;
        if (ct == CARD_INFO) {
          // Info card: show the sensor state followed by its unit. The value
          // label is child 2 (iconbg=0, namelbl=1, valuelbl=2; no slider), and
          // the unit string carries any spacing it needs. Numeric states are
          // trimmed to a single decimal (whole numbers show none); non-numeric
          // states (e.g. "unavailable") pass through verbatim.
          lv_obj_t *valuelbl = lv_obj_get_child(card, 2);
          const char *unit = entities[idx].unit ? entities[idx].unit : "";
          char buf[48];
          char *endp = nullptr;
          double v = strtod(state, &endp);
          if (endp != state && *endp == '\0') {
            if (v == (double)(long long)v)
              snprintf(buf, sizeof(buf), "%lld%s", (long long)v, unit);
            else
              snprintf(buf, sizeof(buf), "%.1f%s", v, unit);
          } else {
            snprintf(buf, sizeof(buf), "%s%s", state, unit);
          }
          lv_label_set_text(valuelbl, buf);
        } else {
          lv_obj_t *slider =
              lv_obj_get_child(card, lv_obj_get_child_count(card) - 1);
          int32_t sval = (ct == CARD_SLIDER_3) ? (level * 3 + 50) / 100 : level;
          lv_slider_set_value(slider, sval, LV_ANIM_OFF);
          // Reuse the slider's own VALUE_CHANGED handler to refresh the value
          // label and accent color from the new position.
          lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL);
        }
        lvgl_port_unlock();
      }
    }
  }

  cJSON_Delete(root);
}