#include "ui.hpp"
#include <sys/time.h>
#include <string.h>

// "Word clock" face — the QlockTwo idea adapted to a round display. Every
// letter is always present (dim); the words that spell out the current time
// light up. The classic QlockTwo is a square 11x10 grid, whose corners would
// fall outside our 240px circle. Instead the grid is laid out as TAPERED rows:
// short at top/bottom, wide through the middle, so the whole block is inscribed
// in the circle and no letter is ever clipped.
//
// Letter centres sit on a fixed pitch (CELL_W horizontally, CELL_H vertically),
// each row centred. The row widths below were chosen so the widest row still
// fits inside the circle at its vertical offset — don't grow a row past the
// length annotated next to it without re-checking against the circle radius.
//
// Time is shown in 5-minute buckets (floor), the way a word clock does:
//   :00 H O'CLOCK   :05 FIVE PAST H    :30 HALF PAST H    :35 25 TO H+1  ...

// Each row spans the full circle width at its height, so the block fills the
// face edge-to-edge. Cells that aren't part of a word hold arbitrary
// ("random-looking") filler letters baked right into these strings, so the
// gaps between words read as a dense letter field like the original. The
// column index of every real word is fixed by these strings — the WORD table
// below indexes into them, so editing a row means re-checking those offsets.
// Each row's length is the most cells that fit inside the circle at that row's
// vertical offset; don't lengthen a row without re-checking against the radius.
static const char *const GRID[] = {
    "ITKISBA",        // row0  (7)  IT IS  A
    "TWENTYHALF",     // row1  (10) TWENTY(min) HALF
    "QUARTERFIVEX",   // row2  (12) QUARTER FIVE(min)
    "TENXPASTRTOQM",  // row3  (13) TEN(min) PAST TO
    "ONETWOTHREEJPM", // row4  (14) ONE TWO THREE  PM
    "FOURFIVEELEVEN", // row5  (14) FOUR FIVE(hr) ELEVEN
    "SIXSEVENNINEW",  // row6  (13) SIX SEVEN NINE
    "EIGHTBTENXAM",   // row7  (12) EIGHT TEN(hr)  AM
    "TWELVEKXQM",     // row8  (10) TWELVE
    "OCLOCKD",        // row9  (7)  O'CLOCK
};
static constexpr int N_ROWS = sizeof(GRID) / sizeof(GRID[0]);
static constexpr int MAX_COLS = 14; // length of the widest row

// Geometry. CELL_H * N_ROWS must clear the diameter; the per-row widths above
// were sized against CELL_W and the circle radius.
static constexpr int CELL_W = 17;
static constexpr int CELL_H = 22;

static const lv_color_t COLOR_DIM = LV_COLOR_MAKE(0x33, 0x33, 0x33);
static const lv_color_t COLOR_LIT = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF);

static lv_obj_t *cells[N_ROWS][MAX_COLS];
static uint8_t last_bucket = 255;
static uint8_t last_hour12 = 255;

// A word = a contiguous run of letters in one row.
struct Word {
    uint8_t row, col, len;
};

// Fixed words (always-on or minute/connector words).
// Words that can light at the same time never sit adjacent: each pair has at
// least one filler cell (or the grid edge) between them. That's why A, AM and
// PM live in their own filler-bounded slots away from QUARTER / TWELVE / the
// hours rather than hugging them.
static constexpr Word W_IT = {0, 0, 2};
static constexpr Word W_IS = {0, 3, 2};
static constexpr Word W_A = {0, 6, 1};
static constexpr Word W_TWENTY = {1, 0, 6};
static constexpr Word W_HALF = {1, 6, 4};
static constexpr Word W_QUARTER = {2, 0, 7};
static constexpr Word W_FIVE_MIN = {2, 7, 4};
static constexpr Word W_TEN_MIN = {3, 0, 3};
static constexpr Word W_PAST = {3, 4, 4};
static constexpr Word W_TO = {3, 9, 2};
static constexpr Word W_PM = {4, 12, 2};
static constexpr Word W_AM = {7, 10, 2};
static constexpr Word W_OCLOCK = {9, 0, 6};

// Hour words, indexed 1..12.
static constexpr Word HOURS[13] = {
    {0, 0, 0},      // 0 unused
    {4, 0, 3},      // 1  ONE
    {4, 3, 3},      // 2  TWO
    {4, 6, 5},      // 3  THREE
    {5, 0, 4},      // 4  FOUR
    {5, 4, 4},      // 5  FIVE
    {6, 0, 3},      // 6  SIX
    {6, 3, 5},      // 7  SEVEN
    {7, 0, 5},      // 8  EIGHT
    {6, 8, 4},      // 9  NINE
    {7, 6, 3},      // 10 TEN
    {5, 8, 6},      // 11 ELEVEN
    {8, 0, 6},      // 12 TWELVE
};

static void light(const Word &w)
{
    for (int i = 0; i < w.len; i++)
        lv_obj_set_style_text_color(cells[w.row][w.col + i], COLOR_LIT, 0);
}

lv_obj_t *wordwatch_create(lv_obj_t *parent)
{
    lv_obj_t *scr = create_screen(parent);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    for (int row = 0; row < N_ROWS; row++)
    {
        int n = (int)strlen(GRID[row]);
        int dy = (int)((row - (N_ROWS - 1) / 2.0f) * CELL_H);

        for (int col = 0; col < MAX_COLS; col++)
        {
            if (col >= n)
            {
                cells[row][col] = nullptr;
                continue;
            }

            int dx = (int)((col - (n - 1) / 2.0f) * CELL_W);

            lv_obj_t *lbl = lv_label_create(scr);
            lv_obj_set_style_text_font(lbl, &ProductSansBold_16, 0);
            lv_obj_set_style_text_color(lbl, COLOR_DIM, 0);

            char c[2] = {GRID[row][col], 0};
            lv_label_set_text(lbl, c);

            lv_obj_align(lbl, LV_ALIGN_CENTER, dx, dy);
            cells[row][col] = lbl;
        }
    }

    // Force a full draw now: reset the change-guards so the immediate update
    // below lights the current words. Without this, switching to this face
    // leaves every letter dim until the 5-minute bucket happens to change.
    last_bucket = 255;
    last_hour12 = 255;
    wordwatch_update();

    return scr;
}

void wordwatch_update()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm t;
    localtime_r(&tv.tv_sec, &t);

    uint8_t bucket = t.tm_min / 5; // 0..11, floor to 5 minutes

    // Hour to display: current hour for "past" buckets (0..6), next hour for
    // "to" buckets (7..11). Convert to 12-hour (12 instead of 0).
    int h24 = (bucket <= 6) ? t.tm_hour : t.tm_hour + 1;
    uint8_t h12 = h24 % 12;
    if (h12 == 0)
        h12 = 12;

    if (bucket == last_bucket && h12 == last_hour12)
        return;
    last_bucket = bucket;
    last_hour12 = h12;

    // Reset everything to dim.
    for (int row = 0; row < N_ROWS; row++)
        for (int col = 0; col < MAX_COLS; col++)
            if (cells[row][col])
                lv_obj_set_style_text_color(cells[row][col], COLOR_DIM, 0);

    light(W_IT);
    light(W_IS);

    // Minute words.
    switch (bucket)
    {
    case 0:                       break;                 // o'clock
    case 1:  light(W_FIVE_MIN);   break;                 // five past
    case 2:  light(W_TEN_MIN);    break;                 // ten past
    case 3:  light(W_A); light(W_QUARTER); break;        // a quarter past
    case 4:  light(W_TWENTY);     break;                 // twenty past
    case 5:  light(W_TWENTY); light(W_FIVE_MIN); break;  // twenty-five past
    case 6:  light(W_HALF);       break;                 // half past
    case 7:  light(W_TWENTY); light(W_FIVE_MIN); break;  // twenty-five to
    case 8:  light(W_TWENTY);     break;                 // twenty to
    case 9:  light(W_A); light(W_QUARTER); break;        // a quarter to
    case 10: light(W_TEN_MIN);    break;                 // ten to
    case 11: light(W_FIVE_MIN);   break;                 // five to
    }

    // Connector.
    if (bucket == 0)
        light(W_OCLOCK);
    else if (bucket <= 6)
        light(W_PAST);
    else
        light(W_TO);

    // Hour.
    light(HOURS[h12]);

    // AM/PM are present in the grid but intentionally left unlit for now.
    (void)W_AM;
    (void)W_PM;
}
