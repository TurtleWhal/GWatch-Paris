#include "ble.hpp"
#include "settings.hpp"
#include "ui.hpp"

#include "cJSON.h"
#include "esp_lvgl_port.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

// Loaded once from homeassistant.url / homeassistant.apiKey in
// config.json at screen-create time. Referenced inline by every
// send_gb helper below (which used to bake HA_URL / HA_API_KEY into
// the string literal via preprocessor concat). Kept as std::string so
// c_str() stays stable for the life of the process — these never get
// mutated after init.
static std::string s_ha_url;
static std::string s_ha_api_key;

struct HAEntity {
  lv_obj_t *card = nullptr;
  EntityType type = HA_INFO;
  // Owned strings — cJSON tree is deleted after init, so we copy out.
  // std::string keeps c_str() stable as long as the string isn't
  // mutated, and the entities vector is reserve()d to prevent
  // reallocation, so passing c_str() into LVGL callbacks is safe.
  std::string id;
  std::string name;
  std::string icon;
  std::string unit;
  lv_color_t color = {};
};

// Dynamic entity list. Populated from homeassistant.entities[] in
// config.json. Reserve()d up-front to the count read from the JSON so
// subsequent emplace_backs don't move existing HAEntities — the
// LVGL event callbacks stash `entity.id.c_str()` as user_data and
// would dangle on a realloc.
static std::vector<HAEntity> entities;

void homeassistant_update(lv_event_t *e);
void update_cards();

// Type-name string from config.json ("info" / "light" / "fan" /
// "cover" / "button") to the internal EntityType enum. Unknown /
// missing types fall through to HA_INFO so at worst you get a read-
// only card that never mutates HA state.
static EntityType type_from_string(const char *s) {
  if (!s)                        return HA_INFO;
  if (strcmp(s, "light")  == 0)  return HA_LIGHT;
  if (strcmp(s, "fan")    == 0)  return HA_FAN;
  if (strcmp(s, "cover")  == 0)  return HA_COVER;
  if (strcmp(s, "button") == 0)  return HA_BUTTON;
  return HA_INFO;
}

// The accent colour for a card is derived from its type rather than
// stored per-entity in config.json — keeps the config schema minimal
// and enforces visual consistency across similar entities. Numbers
// carry over from the original hardcoded call sites so this migration
// is a no-op visually.
static lv_color_t color_for_type(EntityType t) {
  switch (t) {
    case HA_INFO:   return lv_color_make(255, 120, 0);   // orange
    case HA_LIGHT:  return lv_color_make(255, 160, 0);   // amber
    case HA_FAN:    return lv_color_make( 76, 200, 80);  // green
    case HA_COVER:  return lv_color_make( 33, 150, 243); // blue
    case HA_BUTTON: return lv_color_make( 68, 115, 158); // slate
  }
  return lv_color_white();
}

// Read entities + url + apiKey out of config.json's "homeassistant"
// section into the static vectors above. Called once from
// homeassistant_create. Uses its own file open + cJSON parse rather
// than reaching into Settings' locked tree — same pattern schedule.cpp
// uses, and the extra small I/O at boot is fine.
static void ha_load_config() {
  s_ha_url.clear();
  s_ha_api_key.clear();
  entities.clear();

  FILE *f = fopen(GWATCH_CONFIG_PATH, "r");
  if (!f) return;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > 128 * 1024) { fclose(f); return; }
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) { fclose(f); return; }
  size_t got = fread(buf, 1, (size_t)sz, f);
  buf[got] = '\0';
  fclose(f);

  cJSON *root = cJSON_Parse(buf);
  free(buf);
  if (!root) return;

  cJSON *ha = cJSON_GetObjectItemCaseSensitive(root, "homeassistant");
  if (!cJSON_IsObject(ha)) { cJSON_Delete(root); return; }

  cJSON *url    = cJSON_GetObjectItemCaseSensitive(ha, "url");
  cJSON *apikey = cJSON_GetObjectItemCaseSensitive(ha, "apiKey");
  if (cJSON_IsString(url)    && url->valuestring)    s_ha_url     = url->valuestring;
  if (cJSON_IsString(apikey) && apikey->valuestring) s_ha_api_key = apikey->valuestring;

  cJSON *arr = cJSON_GetObjectItemCaseSensitive(ha, "entities");
  if (cJSON_IsArray(arr)) {
    // Reserve so subsequent emplace_backs don't reallocate — see
    // comment on `entities` above for why that matters.
    entities.reserve((size_t)cJSON_GetArraySize(arr));
    cJSON *ent;
    cJSON_ArrayForEach(ent, arr) {
      if (!cJSON_IsObject(ent)) continue;
      HAEntity e;
      cJSON *n  = cJSON_GetObjectItemCaseSensitive(ent, "name");
      cJSON *id = cJSON_GetObjectItemCaseSensitive(ent, "entity_id");
      cJSON *t  = cJSON_GetObjectItemCaseSensitive(ent, "type");
      cJSON *ic = cJSON_GetObjectItemCaseSensitive(ent, "icon");
      cJSON *u  = cJSON_GetObjectItemCaseSensitive(ent, "unit");
      if (cJSON_IsString(n)  && n->valuestring)  e.name = n->valuestring;
      if (cJSON_IsString(id) && id->valuestring) e.id   = id->valuestring;
      if (cJSON_IsString(ic) && ic->valuestring) e.icon = ic->valuestring;
      if (cJSON_IsString(u)  && u->valuestring)  e.unit = u->valuestring;
      e.type  = type_from_string(cJSON_IsString(t) ? t->valuestring : nullptr);
      e.color = color_for_type(e.type);
      entities.push_back(std::move(e));
    }
  }
  cJSON_Delete(root);
}

// Build the card widget tree for one entity. Same layout the previous
// hardcoded caller produced — only the source of the params changed.
static void build_card(lv_obj_t *parent, HAEntity &entity) {
  lv_obj_t *card = lv_obj_create(parent);
  entity.card = card;

  uint8_t cardtype = cardType[entity.type];
  lv_color_t color = entity.color;

// HA: 242x110
#define SF 110 / 110

  uint8_t width = 200;
  uint8_t height = 56;
  if (cardtype == CARD_SLIDER || cardtype == CARD_SLIDER_3) height = 110;

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
  SET_MDI_SYMBOL_22(icon, entity.icon.c_str());
  lv_obj_set_style_text_color(icon, color, 0);
  lv_obj_center(icon);

  lv_obj_t *namelbl = lv_label_create(card);
  lv_obj_set_style_text_font(namelbl, &ProductSansRegular_14, 0);
  lv_obj_set_pos(namelbl, 56 * SF, 10 * SF);
  lv_label_set_text(namelbl, entity.name.c_str());

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

    // Pass the entity's id via user_data. entities.reserve() before the
    // load loop guarantees c_str() stays stable for the process lifetime.
    const char *id_ptr = entity.id.c_str();

    // All three send on LV_EVENT_RELEASED (finger lifted) rather than
    // LV_EVENT_VALUE_CHANGED, so a single command goes out when the drag
    // finishes instead of one per intermediate value.
    if (entity.type == HA_LIGHT) {
      lv_obj_add_event_cb(
          slider,
          [](lv_event_t *e) {
            std::string message =
                std::string("{t:\"http\", url:\"https://") + s_ha_url +
                std::string("/" HA_ENDPOINT_LIGHT_ON "\", method:\"post\", "
                            "headers:{\"Authorization\":\"Bearer ") + s_ha_api_key +
                std::string("\", \"Content-Type\":\"application/json\"}, "
                            "body:{\"entity_id\":\"") +
                std::string((const char *)lv_event_get_user_data(e)) +
                std::string("\", \"brightness\":") +
                std::to_string(lv_slider_get_value(lv_event_get_target_obj(e)) *
                               2.55f) +
                std::string("}}");
            ble.send_gb(message.c_str());
          },
          LV_EVENT_RELEASED, (void *)id_ptr);
    } else if (entity.type == HA_COVER) {
      lv_obj_add_event_cb(
          slider,
          [](lv_event_t *e) {
            std::string message =
                std::string("{t:\"http\", url:\"https://") + s_ha_url +
                std::string("/" HA_ENDPOINT_COVER_POSITION
                            "\", method:\"post\", "
                            "headers:{\"Authorization\":\"Bearer ") + s_ha_api_key +
                std::string("\", \"Content-Type\":\"application/json\"}, "
                            "body:{\"entity_id\":\"") +
                std::string((const char *)lv_event_get_user_data(e)) +
                std::string("\", \"position\":") +
                std::to_string(
                    lv_slider_get_value(lv_event_get_target_obj(e))) +
                std::string("}}");
            ble.send_gb(message.c_str());
          },
          LV_EVENT_RELEASED, (void *)id_ptr);
    } else if (entity.type == HA_FAN) {
      lv_obj_add_event_cb(
          slider,
          [](lv_event_t *e) {
            uint32_t value = lv_slider_get_value(lv_event_get_target_obj(e));
            value = 33.3f * value;
            if (value == 99) value = 100;

            std::string message =
                std::string("{t:\"http\", url:\"https://") + s_ha_url +
                std::string("/" HA_ENDPOINT_FAN_PERCENTAGE
                            "\", method:\"post\", "
                            "headers:{\"Authorization\":\"Bearer ") + s_ha_api_key +
                std::string("\", \"Content-Type\":\"application/json\"}, "
                            "body:{\"entity_id\":\"") +
                std::string((const char *)lv_event_get_user_data(e)) +
                std::string("\", \"percentage\":") + std::to_string(value) +
                std::string("}}");
            ble.send_gb(message.c_str());
          },
          LV_EVENT_RELEASED, (void *)id_ptr);
    }

    lv_obj_add_event_cb(
        slider,
        [](lv_event_t *e) {
          lv_obj_t *slider = lv_event_get_target_obj(e);
          lv_obj_t *card = lv_obj_get_parent(slider);
          int32_t value = lv_slider_get_value(slider);

          if (lv_slider_get_max_value(slider) == 3) {
            value = 33.3f * value;
            if (value == 99) value = 100;
          }

          // Card child order: 0=iconbg, 1=namelbl, 2=valuelbl, 3=slider.
          lv_obj_t *valuelbl = lv_obj_get_child(card, 2);
          lv_label_set_text_fmt(valuelbl, "%li%%", value);

          lv_obj_t *iconbg = lv_obj_get_child(card, 0);
          lv_obj_t *icon = lv_obj_get_child(iconbg, 0);

          // The accent color is packed into the event user_data as a 32-bit
          // RGB value (see the add_event_cb below).
          lv_color_t color =
              lv_color_hex((uint32_t)(uintptr_t)lv_event_get_user_data(e));

          if (value == 0) color = lv_color_make(189, 189, 189);

          lv_obj_set_style_bg_color(iconbg, color, 0);
          lv_obj_set_style_text_color(icon, color, 0);
          lv_obj_set_style_bg_color(slider, color, LV_PART_MAIN);
          lv_obj_set_style_bg_color(slider, color, LV_PART_INDICATOR);
        },
        LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)lv_color_to_u32(color));
  }
}

lv_obj_t *homeassistant_create(lv_obj_t *parent) {
  lv_obj_t *scr = create_screen(parent);

  ha_load_config();

  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_flex_track_place(scr, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_set_style_flex_cross_place(scr, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

  lv_obj_set_style_pad_row(scr, 8, 0);
  lv_obj_set_style_pad_bottom(scr, 50, 0);
  lv_obj_set_style_pad_top(scr, 50, 0);

  for (HAEntity &entity : entities) {
    build_card(scr, entity);
  }

  lv_obj_add_event_cb(scr, homeassistant_update, LV_EVENT_SCREEN_LOADED, NULL);

  return scr;
}

void update_cards() {
  for (HAEntity &entity : entities) {
    if (entity.id.empty()) continue;

    std::string msg =
        std::string("{t:\"http\", url:\"https://") + s_ha_url +
        std::string("/api/states/") + entity.id +
        std::string("\", method:\"get\", headers:{\"Authorization\":\"Bearer ") + s_ha_api_key +
        std::string("\", \"Content-Type\":\"application/json\"}}");
    ble.send_gb(msg.c_str());
  }
}

void homeassistant_update(lv_event_t *e) {
  update_cards();
}

// Map a Home Assistant entity's state + attributes onto a 0..100 level for
// the card's slider. Falls back to plain on/off (100/0) when there's no
// finer-grained attribute to read.
static int ha_level_from_state(EntityType type, const char *state,
                               cJSON *attrs) {
  bool on = state && (strcmp(state, "on") == 0 || strcmp(state, "open") == 0);
  switch (type) {
  case HA_LIGHT: {
    if (!on) return 0;
    cJSON *b =
        attrs ? cJSON_GetObjectItemCaseSensitive(attrs, "brightness") : nullptr;
    if (cJSON_IsNumber(b))
      return (int)(b->valuedouble * 100.0 / 255.0 + 0.5);
    return 100;
  }
  case HA_FAN: {
    if (!on) return 0;
    cJSON *p =
        attrs ? cJSON_GetObjectItemCaseSensitive(attrs, "percentage") : nullptr;
    if (cJSON_IsNumber(p)) return (int)p->valuedouble;
    return 100;
  }
  case HA_COVER: {
    cJSON *pos =
        attrs ? cJSON_GetObjectItemCaseSensitive(attrs, "current_position")
              : nullptr;
    if (cJSON_IsNumber(pos)) return (int)pos->valuedouble;
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
  if (!root) return;

  cJSON *idj = cJSON_GetObjectItemCaseSensitive(root, "entity_id");
  if (cJSON_IsString(idj) && idj->valuestring) {
    HAEntity *hit = nullptr;
    for (HAEntity &e : entities) {
      if (e.id == idj->valuestring) { hit = &e; break; }
    }

    if (hit) {
      cJSON *statej = cJSON_GetObjectItemCaseSensitive(root, "state");
      const char *state = cJSON_IsString(statej) ? statej->valuestring : "";
      cJSON *attrs = cJSON_GetObjectItemCaseSensitive(root, "attributes");
      int level = ha_level_from_state(hit->type, state, attrs);

      uint8_t ct = cardType[hit->type];

      // Runs on the BLE rx task, not the LVGL task — hold the port lock
      // around the widget mutation.
      if ((ct == CARD_SLIDER || ct == CARD_SLIDER_3 || ct == CARD_INFO) &&
          lvgl_port_lock(0)) {
        lv_obj_t *card = hit->card;
        if (ct == CARD_INFO) {
          lv_obj_t *valuelbl = lv_obj_get_child(card, 2);
          const char *unit = hit->unit.c_str();
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
          lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL);
        }
        lvgl_port_unlock();
      }
    }
  }

  cJSON_Delete(root);
}
