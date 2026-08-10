#include "ui.hpp"

#include <string.h>

enum class UnitType : uint8_t {
  TEMP,
  LENGTH,
  WEIGHT,
  VOLUME,
};

struct Unit {
  UnitType type;
  const char *name;
  const char *symbol;
  float conversion_factor; // Factor to convert from base unit to this unit
  float offset = 0;        // Offset for units like Fahrenheit
};

// Just add new units and update the first roller
const Unit units[] = {
    {UnitType::TEMP, "Celsius", "°C", 1.0},
    {UnitType::TEMP, "Fahrenheit", "°F", 1.8, 32},
    {UnitType::TEMP, "Kelvin", " K", 1, -273.15},

    {UnitType::LENGTH, "Millimeters", "mm", 1000.0},
    {UnitType::LENGTH, "Centimeters", "cm", 100.0},
    {UnitType::LENGTH, "Meters", "m", 1.0},
    {UnitType::LENGTH, "Kilometers", "km", 0.001},
    {UnitType::LENGTH, "Inches", "in", 39.3701},
    {UnitType::LENGTH, "Feet", "ft", 3.28084},
    {UnitType::LENGTH, "Yard", "yd", 1.09361},
    {UnitType::LENGTH, "Miles", "mi", 0.000621371},
    {UnitType::LENGTH, "Nautical Miles", "NM", 0.000539957},
    {UnitType::LENGTH, "Football Fields", " USA", 0.0109361},

    {UnitType::WEIGHT, "Grams", "g", 1000.0},
    {UnitType::WEIGHT, "Kilograms", "kg", 1.0},
    {UnitType::WEIGHT, "Ounces", "oz", 35.274},
    {UnitType::WEIGHT, "Pounds", "lb", 2.20462},

    {UnitType::VOLUME, "Liters", "L", 1.0},
    {UnitType::VOLUME, "Gallons", "gal", 0.264172},
    {UnitType::VOLUME, "Ounces", "floz", 33.814},
    {UnitType::VOLUME, "Cups", "cup", 4.16667},
};

// Panel navigation state for the units screen. Manual scrolling of the
// screen is disabled; the up/down chevrons step between the panels. This
// screen is created once at boot, so a single static block is fine.
static struct UnitsNav {
  lv_obj_t *scr;
  int count;
  int index;
  lv_obj_t *uparrow;
  lv_obj_t *downarrow;
} units_nav;

lv_obj_t *unittyperoller;
lv_obj_t *unit1roller;
lv_obj_t *unit2roller;

lv_obj_t *input;
lv_obj_t *inunit;
lv_obj_t *output;

// Animate the screen to panel `idx` and update chevron visibility.
static void units_nav_go(int idx) {
  if (idx < 0 || idx >= units_nav.count)
    return;
  units_nav.index = idx;
  // The screen's scroll_dir is LV_DIR_NONE, so lv_obj_scroll_to_view (which
  // honors scroll_dir) would be a no-op here; lv_obj_scroll_to_y bypasses
  // that gate. Each 240px panel sits at content-y = idx*240 with no padding,
  // which is also its centered position since panel height == screen height.
  lv_obj_scroll_to_y(units_nav.scr, idx * 240, LV_ANIM_ON);
  // Hide whichever chevron would point past the ends.
  lv_obj_set_flag(units_nav.uparrow, LV_OBJ_FLAG_HIDDEN, idx == 0);
  lv_obj_set_flag(units_nav.downarrow, LV_OBJ_FLAG_HIDDEN,
                  idx == units_nav.count - 1);
}

void updateRollerOptions(lv_event_t *e);
void calculate(lv_event_t *e);

// Numeric keypad for the bottom panel — digits, a decimal point, and a
// backspace key ("<"), laid out phone-style in 3 columns.
static const char *const numpad_map[] = {
    "1", "2",   "3", "4",   "5", "\n", //
    "6", "7",   "8", "9",   "0", "\n", //
    " ", "", ".", "", " ", "",
};
// static const lv_buttonmatrix_ctrl_t numpad_ctrl[] = {
//     LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
//     LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
//     LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
//     LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
//     LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
//     LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
//     LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
// };

// Feeds the numeric keypad into the value textarea (passed as user_data):
// FA_BACKSPACE deletes a char, "-" toggles the sign, blank " " spacer
// buttons are ignored, and everything else (digits, ".") is appended.
static void numpad_event_cb(lv_event_t *e) {
  lv_obj_t *matrix = lv_event_get_target_obj(e);
  lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);
  if (!ta)
    return;
  uint32_t id = lv_buttonmatrix_get_selected_button(matrix);
  const char *txt = lv_buttonmatrix_get_button_text(matrix, id);
  if (!txt)
    return;
  if (strcmp(txt, FA_BACKSPACE) == 0) {
    lv_textarea_delete_char(ta);
  } else if (strcmp(txt, "") == 0) {
    // Toggle the sign: strip a leading '-' if present, else prepend one,
    // keeping the cursor over the same digit.
    const char *cur = lv_textarea_get_text(ta);
    uint32_t pos = lv_textarea_get_cursor_pos(ta);
    if (cur[0] == '-') {
      lv_textarea_set_cursor_pos(ta, 1);
      lv_textarea_delete_char(ta);
      lv_textarea_set_cursor_pos(ta, pos > 0 ? pos - 1 : 0);
    } else {
      lv_textarea_set_cursor_pos(ta, 0);
      lv_textarea_add_char(ta, '-');
      lv_textarea_set_cursor_pos(ta, pos + 1);
    }
  } else if (strcmp(txt, " ") != 0) { // skip blank spacer buttons
    lv_textarea_add_text(ta, txt);
  }

  calculate(NULL);
}

lv_obj_t *units_create(lv_obj_t *parent) {
  lv_obj_t *scr = create_screen(parent);
  units_screen = scr;

  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(scr, 0, 0);
  // No manual scrolling — the up/down chevrons move between panels instead.
  lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

  lv_obj_t *panel1 = lv_obj_create(scr);
  lv_obj_set_size(panel1, 240, 240);
  lv_obj_set_style_border_width(panel1, 0, 0);
  lv_obj_set_style_bg_color(panel1, lv_color_hex(0x000000), 0);

  unittyperoller = lv_roller_create(panel1);
  lv_roller_set_options(unittyperoller, "Temperature\nLength\nWeight\nVolume",
                        LV_ROLLER_MODE_NORMAL);
  lv_obj_align(unittyperoller, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_size(unittyperoller, 160, 140);
  lv_obj_set_style_text_font(unittyperoller, &ProductSansBold_24, 0);
  lv_obj_set_style_radius(unittyperoller, 14, 0);

  lv_obj_t *panel2 = lv_obj_create(scr);
  lv_obj_set_size(panel2, 240, 240);
  lv_obj_set_style_border_width(panel2, 0, 0);
  lv_obj_set_style_bg_color(panel2, lv_color_hex(0x000000), 0);

  unit1roller = lv_roller_create(panel2);
  lv_roller_set_options(unit1roller, "Temperature\nLength\nWeight",
                        LV_ROLLER_MODE_NORMAL);
  lv_obj_align(unit1roller, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_size(unit1roller, 94, 110);
  lv_obj_set_style_text_font(unit1roller, &ProductSansRegular_14, 0);
  lv_obj_set_style_radius(unit1roller, 14, 0);

  lv_obj_t *to = lv_label_create(panel2);
  lv_obj_center(to);
  SET_SYMBOL_22(to, FA_CHEVRON_RIGHT);

  unit2roller = lv_roller_create(panel2);
  lv_roller_set_options(unit2roller, "Temperature\nLength\nWeight",
                        LV_ROLLER_MODE_NORMAL);
  lv_obj_align(unit2roller, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_size(unit2roller, 94, 110);
  lv_obj_set_style_text_font(unit2roller, &ProductSansRegular_14, 0);
  lv_obj_set_style_radius(unit2roller, 14, 0);

  lv_obj_t *panel3 = lv_obj_create(scr);
  lv_obj_set_size(panel3, 240, 240);
  lv_obj_set_style_border_width(panel3, 0, 0);
  lv_obj_set_style_bg_color(panel3, lv_color_hex(0x000000), 0);
  lv_obj_set_scroll_dir(panel3, LV_DIR_NONE);

  // Value entry: a one-line textarea fed by a numeric keypad below it.
  input = lv_textarea_create(panel3);
  lv_textarea_set_one_line(input, true);
  lv_textarea_set_text(input, "");
  lv_textarea_set_placeholder_text(input, "1");
  lv_obj_set_size(input, 120, 32);
  lv_obj_align(input, LV_ALIGN_TOP_MID, -20, 26);
  lv_obj_set_style_text_font(input, &ProductSansRegular_20, 0);
  lv_obj_set_style_radius(input, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_ver(input, 1, 0);

  inunit = lv_label_create(panel3);
  lv_label_set_text(inunit, "cm");
  lv_obj_set_width(inunit, 40);
  lv_obj_align(inunit, LV_ALIGN_TOP_MID, 65, 30);
  lv_obj_set_style_text_font(inunit, &ProductSansRegular_20, 0);

  output = lv_label_create(panel3);
  lv_label_set_text(output, "output mm");
  // lv_obj_set_width(output, 160);
  lv_obj_align(output, LV_ALIGN_TOP_MID, 0, 66);
  lv_obj_set_style_text_font(output, &ProductSansRegular_20, 0);

  /* Create the button matrix */
  lv_obj_t *matrix = lv_buttonmatrix_create(panel3);
  lv_buttonmatrix_set_map(matrix, numpad_map);
  // lv_buttonmatrix_set_ctrl_map(matrix, numpad_ctrl);
  lv_obj_set_size(matrix, 236, 135);
  lv_obj_align(matrix, LV_ALIGN_CENTER, 0, 46);

  lv_obj_set_style_text_font(matrix, &ProductSansRegular_24_fa, LV_PART_ITEMS);
  lv_obj_set_style_radius(matrix, LV_RADIUS_CIRCLE, LV_PART_ITEMS);
  lv_obj_set_style_pad_gap(matrix, 2, 0);
  lv_obj_set_style_bg_opa(matrix, 0, 0);
  lv_obj_set_style_border_width(matrix, 0, 0);

  lv_buttonmatrix_set_button_ctrl(matrix, 10, LV_BUTTONMATRIX_CTRL_HIDDEN);
  lv_buttonmatrix_set_button_ctrl(matrix, 11, LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_buttonmatrix_set_button_ctrl(matrix, 12, LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_buttonmatrix_set_button_ctrl(matrix, 13, LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_buttonmatrix_set_button_ctrl(matrix, 14, LV_BUTTONMATRIX_CTRL_HIDDEN);

  // Route keypad presses into the value textarea (passed as user_data).
  lv_obj_add_event_cb(matrix, numpad_event_cb, LV_EVENT_VALUE_CHANGED, input);

  // for (uint8_t i = 0; i < 19; i++)
  //   lv_buttonmatrix_set_button_ctrl(matrix, i,
  //   LV_BUTTONMATRIX_CTRL_CLICK_TRIG);

  // lv_buttonmatrix_clear_button_ctrl(matrix, 4,
  // LV_BUTTONMATRIX_CTRL_CLICK_TRIG);

  lv_obj_t *uparrow = lv_label_create(scr);
  SET_SYMBOL_22(uparrow, FA_CHEVRON_UP);
  lv_obj_align(uparrow, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_flag(uparrow, LV_OBJ_FLAG_FLOATING, true);
  lv_obj_set_flag(uparrow, LV_OBJ_FLAG_CLICKABLE, true);
  lv_obj_set_ext_click_area(uparrow, 20); // small glyph, roomy tap target
  lv_obj_add_event_cb(
      uparrow, [](lv_event_t *) { units_nav_go(units_nav.index - 1); },
      LV_EVENT_CLICKED, NULL);

  lv_obj_t *downarrow = lv_label_create(scr);
  SET_SYMBOL_22(downarrow, FA_CHEVRON_DOWN);
  lv_obj_align(downarrow, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_flag(downarrow, LV_OBJ_FLAG_FLOATING, true);
  lv_obj_set_flag(downarrow, LV_OBJ_FLAG_CLICKABLE, true);
  lv_obj_set_ext_click_area(downarrow, 20);
  lv_obj_add_event_cb(
      downarrow, [](lv_event_t *) { units_nav_go(units_nav.index + 1); },
      LV_EVENT_CLICKED, NULL);

  units_nav.scr = scr;
  units_nav.count = 3;
  units_nav.index = 0;
  units_nav.uparrow = uparrow;
  units_nav.downarrow = downarrow;
  // Start on the first panel: nothing above it, so hide the up chevron.
  lv_obj_set_flag(uparrow, LV_OBJ_FLAG_HIDDEN, true);

  lv_obj_add_event_cb(unittyperoller, updateRollerOptions,
                      LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(unit1roller, calculate, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(unit2roller, calculate, LV_EVENT_VALUE_CHANGED, NULL);

  updateRollerOptions(NULL);

  return scr;
}

void updateRollerOptions(lv_event_t *e) {
  int selected = lv_roller_get_selected(unittyperoller);

  std::string options = "";

  uint8_t select = 0;

  for (const auto &unit : units) {
    if ((int)unit.type == selected) {
      options += unit.name;
      options += "\n";

      if (unit.conversion_factor == 1.0 && unit.offset == 0) {
        select = lv_roller_get_selected(unit1roller);
      }
    }
  }

  options.pop_back(); // Remove the last newline

  lv_roller_set_options(unit1roller, options.c_str(), LV_ROLLER_MODE_NORMAL);
  lv_roller_set_options(unit2roller, options.c_str(), LV_ROLLER_MODE_NORMAL);

  lv_roller_set_selected(unit1roller, select, LV_ANIM_OFF);
  lv_roller_set_selected(unit2roller, select, LV_ANIM_OFF);

  calculate(NULL);
}

void calculate(lv_event_t *e) {
  int selected = lv_roller_get_selected(unittyperoller);
  int selectedinput = lv_roller_get_selected(unit1roller);
  int selectedoutput = lv_roller_get_selected(unit2roller);

  Unit inputunit = {UnitType::TEMP, "Error", "err", 0.0};
  Unit outputunit = {UnitType::TEMP, "Error", "err", 0.0};

  uint8_t unitindex = 0;

  for (const auto &unit : units) {
    if ((int)unit.type == selected) {
      unitindex++;
      if (unitindex - 1 == selectedinput) {
        inputunit = unit;
      }
      if (unitindex - 1 == selectedoutput) {
        outputunit = unit;
      }
    }
  }

  double inputvalue = atof(lv_textarea_get_text(input));

  if (lv_textarea_get_text(input)[0] == '\0')
    inputvalue = 1;

  double basevalue =
      (inputvalue - inputunit.offset) / inputunit.conversion_factor;
  double outputvalue =
      basevalue * outputunit.conversion_factor + outputunit.offset;

  lv_label_set_text(inunit, inputunit.symbol);
  lv_label_set_text_fmt(output, "%.3f%s", outputvalue, outputunit.symbol);
}
