#include "ui.hpp"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *btn_map[] = {
    "+", "-", "×", "÷", "<", "\n",
    "1", "2", "3", "4", "5", "\n",
    "6", "7", "8", "9", "0", "\n",
    " ", "C", "=", ".", " ", ""};

static lv_obj_t *output;
// Calculator haptic-mode state:
//   false → vibrate only on a completed click (PRESS_LOST + VALUE_CHANGED)
//   true  → vibrate on PRESS the moment the finger lands
// The screen boots in click-mode so swiping past during horizontal
// scroll doesn't pulse the motor with every brush against a button.
// First successful click in the session flips it to press-mode so
// subsequent taps feel responsive. Reset back to click-mode when the
// calculator scrolls out of view (handled by the off-screen timer
// below).
static bool press_mode = false;

/* Function prototypes */
static void btn_event_cb(lv_event_t *e);
static double evaluate_expression(const char *expr);

/* Create the calculator screen */
lv_obj_t *calculator_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    /* Create the button matrix */
    lv_obj_t *matrix = lv_buttonmatrix_create(scr);
    lv_buttonmatrix_set_map(matrix, btn_map);
    lv_obj_set_size(matrix, 236, 180);
    lv_obj_align(matrix, LV_ALIGN_CENTER, 0, 30);

    lv_obj_set_style_text_font(matrix, &ProductSansRegular_24, LV_PART_ITEMS);
    lv_obj_set_style_radius(matrix, LV_RADIUS_CIRCLE, LV_PART_ITEMS);
    lv_obj_set_style_pad_gap(matrix, 2, 0);
    lv_obj_set_style_bg_opa(matrix, 0, 0);
    lv_obj_set_style_border_width(matrix, 0, 0);

    lv_buttonmatrix_set_button_ctrl(matrix, 0, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_buttonmatrix_set_button_ctrl(matrix, 1, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_buttonmatrix_set_button_ctrl(matrix, 2, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_buttonmatrix_set_button_ctrl(matrix, 3, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_buttonmatrix_set_button_ctrl(matrix, 4, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_buttonmatrix_set_button_ctrl(matrix, 15, LV_BUTTONMATRIX_CTRL_HIDDEN);
    lv_buttonmatrix_set_button_ctrl(matrix, 19, LV_BUTTONMATRIX_CTRL_HIDDEN);

    for (uint8_t i = 0; i < 19; i++)
        lv_buttonmatrix_set_button_ctrl(matrix, i, LV_BUTTONMATRIX_CTRL_CLICK_TRIG);

    lv_buttonmatrix_clear_button_ctrl(matrix, 4, LV_BUTTONMATRIX_CTRL_CLICK_TRIG);

    lv_obj_add_event_cb(matrix, btn_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Press-time vibration: ONLY fires after the user has completed
    // at least one click this session (press_mode == true). Until
    // then, swipes through the calc screen during horizontal scroll
    // don't pulse the motor.
    lv_obj_add_event_cb(matrix, [](lv_event_t *)
                        {
                            if (press_mode) haptic_play(false, 50, 0);
                        }, LV_EVENT_PRESSED, NULL);

    // Off-screen watchdog: when the calculator scrolls out of view in
    // hor_layer, drop back to click-only mode so the next entry to
    // this screen behaves the same as a fresh boot — no immediate
    // motor pulse on the inadvertent press that happens during the
    // approach-swipe.
    lv_timer_create(
        [](lv_timer_t *t)
        {
            lv_obj_t *scr = (lv_obj_t *)lv_timer_get_user_data(t);
            lv_obj_t *parent = lv_obj_get_parent(scr);
            if (!parent) return;
            if (lv_obj_get_scroll_x(parent) != lv_obj_get_x(scr))
                press_mode = false;
        },
        200, scr);

    /* Create output text area */
    output = lv_textarea_create(scr);
    lv_obj_set_size(output, 170, 32);
    lv_obj_align(output, LV_ALIGN_CENTER, 0, -72);
    lv_obj_set_style_radius(output, LV_RADIUS_CIRCLE, 0);
    lv_textarea_set_one_line(output, true);
    lv_obj_set_style_text_font(output, &GoogleSansCode_28, 0);
    lv_textarea_set_text(output, "");
    lv_obj_set_flag(output, LV_OBJ_FLAG_SCROLL_CHAIN, false);
    lv_obj_set_style_clip_corner(output, true, 0);

    return scr;
}

/* --- Event callback for button matrix --- */
static void btn_event_cb(lv_event_t *e)
{
    lv_obj_t *btnm = lv_event_get_target_obj(e);
    const char *txt = lv_buttonmatrix_get_button_text(btnm, lv_buttonmatrix_get_selected_button(btnm));

    if (txt == NULL)
        return;

    // First completed click flips on press-mode for the rest of the
    // session. Also deliver the click's vibration here, since the
    // PRESSED handler suppressed it (press_mode was still false when
    // the finger landed).
    if (!press_mode)
    {
        press_mode = true;
        haptic_play(false, 50, 0);
    }

    if (strcmp(txt, "C") == 0)
    {
        lv_textarea_set_text(output, "");
    }
    else if (strcmp(txt, "<") == 0)
    {
        lv_textarea_delete_char(output);
    }
    else if (strcmp(txt, "=") == 0)
    {
        const char *expr = lv_textarea_get_text(output);
        double result = evaluate_expression(expr);

        char buf[64];
        if (isnan(result))
            snprintf(buf, sizeof(buf), "Err");
        else
            snprintf(buf, sizeof(buf), "%.6g", result);

        lv_textarea_set_text(output, buf);
    }
    else if (strcmp(txt, " ") != 0)
    {
        lv_textarea_add_text(output, txt);
    }
}

/* --- UTF-8 safe expression evaluator (supports + - × ÷) --- */
static double evaluate_expression(const char *expr)
{
    char buffer[128];
    size_t bi = 0;

    // Convert UTF-8 × and ÷ to ASCII * and /
    for (size_t i = 0; expr[i] && bi + 1 < sizeof(buffer);)
    {
        if ((unsigned char)expr[i] == 0xC3 && (unsigned char)expr[i + 1] == 0x97)
        {
            buffer[bi++] = '*';
            i += 2;
        }
        else if ((unsigned char)expr[i] == 0xC3 && (unsigned char)expr[i + 1] == 0xB7)
        {
            buffer[bi++] = '/';
            i += 2;
        }
        else
        {
            buffer[bi++] = expr[i++];
        }
    }
    buffer[bi] = '\0';

    // Very simple parser for two operands and one operator
    double num1 = 0, num2 = 0;
    char op = 0;
    sscanf(buffer, "%lf %c %lf", &num1, &op, &num2);

    switch (op)
    {
    case '+':
        return num1 + num2;
    case '-':
        return num1 - num2;
    case '*':
        return num1 * num2;
    case '/':
        return (num2 != 0.0) ? num1 / num2 : NAN;
    default:
        return num1; // if no operator, return as-is
    }
}
