#include "ui.hpp"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <cmath>
#include <cstdio>
#include <cstring>

// `TAG` is referenced from the export task's ESP_LOGs. Must be static —
// NimBLE's ble_sm_alg.c exports a non-static TAG of its own that we'd
// collide with at link time (see CLAUDE.md).
static const char *TAG = "UNI";

// $1 Unistroke Recognizer (Wobbrock, Wilson & Li, UIST 2007).
// Standalone scratch screen: user draws a single character, on release the
// stroke is resampled / rotated / scaled / translated to canonical form and
// path-distance-compared against a set of templates. The best match name and
// score (0..1, higher is better) are shown above the canvas.
//
// Buffers are heap_caps_calloc'd into PSRAM on first screen open. Putting
// them in .bss costs ~20 KB of internal SRAM at link time, which the BT
// controller's boot-time malloc cannot spare (BLE_INIT failure + emi.c
// assertion). PSRAM is plentiful; the access overhead is fine for the few
// thousand floats touched per recognition.

namespace
{

struct Pt
{
    float x, y;
};

constexpr int kN = 64;
constexpr float kSquareSize = 250.0f;
const float kHalfDiag = 0.5f * sqrtf(kSquareSize * kSquareSize + kSquareSize * kSquareSize);

constexpr int kMaxStroke = 512;

// --- Raw templates (in .rodata via const) ----------------------------------
// Each template is a list of waypoints in a 0..100 box (x→right, y→down),
// traced as a single stroke. The recognizer is orientation- and order-
// sensitive: same shape drawn upside down or with reversed stroke direction
// won't match. Resampling normalizes to kN evenly-spaced points, so the
// waypoint count per template only needs to capture shape inflections.
//
// User-facing stroke guide (start → end, with notable turns):

#define TPL(name) static const Pt RAW_##name[]

// 0:  top → counter-clockwise around → back to top
TPL(d0)  = {{50,15},{25,30},{20,55},{30,80},{55,88},{78,75},{82,50},{72,25},{50,15}};
// 1:  small flag at top-left → straight down
TPL(d1)  = {{30,30},{50,15},{50,85}};
// 2:  top-left curve → top → curve right and down to middle → diagonal to
//     bottom-left → straight across to bottom-right
TPL(d2)  = {{20,30},{50,15},{80,30},{50,55},{20,85},{80,85}};
// 3:  top-left → across top → curve back to middle → curve down → curl
//     left at bottom-left
TPL(d3)  = {{20,20},{60,20},{50,50},{70,55},{75,75},{55,90},{20,85}};
// 4:  start near top → diagonal down-left to middle → across to middle-right
//     → up to top-right → straight down to bottom-right (small backtrack
//     at top-right; resampling handles it)
TPL(d4)  = {{55,15},{20,55},{80,55},{80,15},{80,85}};
// 5:  top-right → across top to top-left → straight down → loop right
//     around to bottom-right → curl to bottom-left
TPL(d5)  = {{80,15},{30,15},{25,45},{60,45},{78,60},{72,82},{40,90},{20,80}};
// 6:  top-right → curl down-left to bottom-left → up the right side →
//     loop back to middle-left (closing the bottom oval)
TPL(d6)  = {{70,15},{45,25},{28,45},{22,70},{35,88},{60,88},{78,72},{72,55},{52,48},{30,55}};
// 7:  top-left → straight across to top-right → diagonal down to bottom-mid
TPL(d7)  = {{20,15},{80,15},{40,85}};
// 8:  top → curl down-left → cross middle → curl down-right → bottom →
//     curl up-left → cross middle again → curl up-right → back to top
TPL(d8)  = {{50,15},{30,28},{32,42},{50,50},{68,58},{70,78},{50,88},{30,78},{32,58},{50,50},{68,42},{70,28},{50,15}};
// 9:  middle-right → loop counter-clockwise around top → close at right →
//     drop straight down to bottom
TPL(d9)  = {{72,45},{55,28},{35,32},{28,48},{42,58},{62,52},{72,45},{55,88}};

// A:  drawn as a chevron ^ — start bottom-left → up to top → down to
//     bottom-right (no crossbar)
TPL(A) = {{20,85},{50,15},{80,85}};
// B:  bottom-left → up to top-left → curve right to mid-left → curve right
//     to bottom-left (one stroke around two bumps; has implicit backtrack
//     at middle, $1 handles it). Train if it misrecognizes.
TPL(B) = {{20,85},{20,15},{60,18},{78,32},{60,48},{20,50},{60,52},{78,68},{60,85},{20,85}};
// C:  top-right → curve counter-clockwise around the left → end bottom-right
TPL(C) = {{78,25},{45,18},{22,38},{20,65},{42,85},{78,80}};
// D:  top-left → straight down → curve right and around back to top-left
TPL(D) = {{30,15},{30,85},{60,82},{78,55},{62,22},{30,15}};
// E:  ε-shape — top-right → curve up-and-left → down to middle-left →
//     across to middle (the middle bar) → back left → down → across bottom
TPL(E) = {{78,25},{38,18},{22,42},{52,50},{22,58},{38,82},{78,75}};
// F:  top-right → across to top-left → down to middle-left → across to
//     middle-mid → backtrack to middle-left → down to bottom-left
TPL(F) = {{80,15},{20,15},{20,50},{55,50},{20,50},{20,85}};
// G:  C-shape + a horizontal tail tucking back to the middle. Top-right →
//     curve counter-clockwise around left → end bottom-right → up to
//     middle-right → across left to middle-center
TPL(G) = {{78,25},{45,18},{22,38},{20,65},{42,85},{78,80},{78,55},{55,55}};
// H:  bottom-left → up to top-left → backtrack to middle-left → across to
//     middle-right → up to top-right → down to bottom-right
TPL(H) = {{20,85},{20,15},{20,50},{80,50},{80,15},{80,85}};
// I:  plain vertical line (top-mid → bottom-mid). Risks colliding with '1'
//     (which has a top flag); your training stroke will disambiguate.
TPL(I) = {{50,18},{50,82}};
// J:  top → straight down → curl left at the bottom
TPL(J) = {{60,15},{60,75},{50,88},{35,82},{25,68}};
// K:  top-left → down to bottom-left → backtrack to middle-left → diagonal
//     up to top-right → backtrack to middle-left → diagonal down to
//     bottom-right
TPL(K) = {{20,15},{20,85},{20,50},{80,15},{20,50},{80,85}};
// L:  top-left → straight down → straight across to bottom-right
TPL(L) = {{30,15},{30,85},{80,85}};
// M:  bottom-left → up to top-left → diagonal down to middle → diagonal up
//     to top-right → down to bottom-right
TPL(M) = {{15,85},{15,15},{50,55},{85,15},{85,85}};
// N:  bottom-left → up to top-left → diagonal down to bottom-right → up
//     to top-right
TPL(N) = {{20,85},{20,15},{80,85},{80,15}};
// O:  same shape as 0, drawn clockwise to differentiate. Top → curve
//     right and around → back to top
TPL(O) = {{50,15},{72,25},{82,50},{72,75},{50,88},{25,75},{20,50},{25,30},{50,15}};
// P:  bottom-left → straight up to top-left → curve right and back down →
//     close at middle-left
TPL(P) = {{25,88},{25,15},{62,18},{78,35},{62,52},{25,50}};
// Q:  like O (counter-clockwise) plus a short diagonal tail at bottom-right
TPL(Q) = {{50,15},{25,30},{20,55},{30,80},{55,88},{78,75},{82,50},{72,25},{50,15},{65,75},{88,92}};
// R:  P-shape + bottom-right leg. Bottom-left → up → curve right → close
//     middle-left → diagonal down to bottom-right
TPL(R) = {{25,88},{25,15},{62,18},{78,35},{62,52},{25,50},{78,88}};
// S:  top-right → curve up-and-left → curve down-right through middle →
//     curve back left to bottom-left
TPL(S) = {{75,25},{40,18},{25,35},{50,50},{72,65},{60,82},{25,78}};
// T:  top-left → across to top-right → backtrack to top-mid → straight down
TPL(T) = {{20,20},{80,20},{50,20},{50,85}};
// U:  top-left → straight down → curve right at bottom → straight up to
//     top-right
TPL(U) = {{20,15},{22,65},{40,85},{60,85},{78,65},{80,15}};
// V:  top-left → diagonal down to bottom-mid → diagonal up to top-right
TPL(V) = {{20,15},{50,85},{80,15}};
// W:  top-left → diagonal down to bottom-left → up to middle → down to
//     bottom-right → up to top-right
TPL(W) = {{15,15},{30,85},{50,40},{70,85},{85,15}};
// X:  top-left → diagonal to bottom-right → backtrack across bottom →
//     diagonal up to top-right (drawn as one path with the bottom edge
//     connecting the two diagonals)
TPL(X) = {{20,15},{80,85},{20,85},{80,15}};
// Y:  top-left → diagonal down to middle → diagonal up to top-right →
//     backtrack to middle → straight down to bottom
TPL(Y) = {{20,15},{50,55},{80,15},{50,55},{50,85}};
// Z:  top-left → across to top-right → diagonal to bottom-left → across to
//     bottom-right
TPL(Z) = {{20,15},{80,15},{20,85},{80,85}};
#undef TPL

struct RawTemplate
{
    const char *name;
    const Pt *pts;
    int n;
};

#define REG(name, label) { label, RAW_##name, (int)(sizeof(RAW_##name)/sizeof(RAW_##name[0])) }
static const RawTemplate kRawTemplates[] = {
    REG(d0,"0"), REG(d1,"1"), REG(d2,"2"), REG(d3,"3"), REG(d4,"4"),
    REG(d5,"5"), REG(d6,"6"), REG(d7,"7"), REG(d8,"8"), REG(d9,"9"),
    REG(A,"A"), REG(B,"B"), REG(C,"C"), REG(D,"D"), REG(E,"E"),
    REG(F,"F"), REG(G,"G"), REG(H,"H"), REG(I,"I"), REG(J,"J"), REG(K,"K"), REG(L,"L"),
    REG(M,"M"), REG(N,"N"), REG(O,"O"), REG(P,"P"), REG(Q,"Q"),
    REG(R,"R"), REG(S,"S"), REG(T,"T"), REG(U,"U"), REG(V,"V"),
    REG(W,"W"), REG(X,"X"), REG(Y,"Y"), REG(Z,"Z"),
};
#undef REG

constexpr int kNumTemplates = sizeof(kRawTemplates) / sizeof(kRawTemplates[0]);
// Max user-trained samples per character. Capped so the total NVS footprint
// (1 + count × 512 per char, × 36 chars) stays under the 64 KB nvs partition.
constexpr int kMaxSamples = 3;

// --- Preprocessed templates and user samples (PSRAM, lazy-allocated) -------
// g_template_pts: kNumTemplates × kN — built-in fallback templates,
//                 preprocessed from kRawTemplates at first init.
// g_user_samples: kNumTemplates × kMaxSamples × kN — user-trained samples,
//                 loaded from NVS at init, written back on save.
// g_sample_count: per-character count of valid entries in g_user_samples
//                 (0..kMaxSamples). Recognition uses user samples where
//                 count > 0, else falls back to the default in g_template_pts.
static Pt *g_template_pts = nullptr;
static Pt *g_user_samples = nullptr;
static uint8_t g_sample_count[kNumTemplates] = {};
static bool g_templates_ready = false;

static inline Pt *sample_ptr(int char_idx, int sample_idx)
{
    return &g_user_samples[(char_idx * kMaxSamples + sample_idx) * kN];
}

// --- $1 algorithm primitives on raw (Pt*, n) buffers -----------------------

static float path_length(const Pt *p, int n)
{
    float d = 0;
    for (int i = 1; i < n; i++)
    {
        float dx = p[i].x - p[i - 1].x;
        float dy = p[i].y - p[i - 1].y;
        d += sqrtf(dx * dx + dy * dy);
    }
    return d;
}

// Resamples src[0..n_in) into dst[0..n_out). Doesn't mutate src; instead the
// loop carries a `prev` cursor that advances by I along each chord per emit.
// Equivalent to Wobbrock's insert-into-input formulation but cleaner with
// fixed-size buffers — and no infinite-loop hazard when a chord lands on I.
static void resample(const Pt *src, int n_in, Pt *dst, int n_out)
{
    if (n_in < 1)
    {
        for (int k = 0; k < n_out; k++) dst[k] = {0, 0};
        return;
    }
    if (n_in < 2)
    {
        for (int k = 0; k < n_out; k++) dst[k] = src[0];
        return;
    }
    float I = path_length(src, n_in) / (n_out - 1);
    int oi = 0;
    dst[oi++] = src[0];
    Pt prev = src[0];
    float D = 0;
    int i = 1;
    while (oi < n_out && i < n_in)
    {
        Pt cur = src[i];
        float dx = cur.x - prev.x;
        float dy = cur.y - prev.y;
        float d = sqrtf(dx * dx + dy * dy);
        if ((D + d) >= I && d > 0)
        {
            float t = (I - D) / d;
            Pt q{prev.x + t * dx, prev.y + t * dy};
            dst[oi++] = q;
            prev = q;
            D = 0;
            // intentionally don't advance i — remainder of chord may emit again
        }
        else
        {
            D += d;
            prev = cur;
            i++;
        }
    }
    while (oi < n_out) dst[oi++] = src[n_in - 1];
}

static Pt centroid(const Pt *p, int n)
{
    Pt c{0, 0};
    for (int i = 0; i < n; i++) { c.x += p[i].x; c.y += p[i].y; }
    c.x /= n;
    c.y /= n;
    return c;
}

static void scale_to_square(Pt *p, int n, float size)
{
    float minx = INFINITY, miny = INFINITY, maxx = -INFINITY, maxy = -INFINITY;
    for (int i = 0; i < n; i++)
    {
        if (p[i].x < minx) minx = p[i].x;
        if (p[i].x > maxx) maxx = p[i].x;
        if (p[i].y < miny) miny = p[i].y;
        if (p[i].y > maxy) maxy = p[i].y;
    }
    float bx = maxx - minx, by = maxy - miny;
    if (bx < 1e-6f) bx = 1e-6f;
    if (by < 1e-6f) by = 1e-6f;
    for (int i = 0; i < n; i++)
    {
        p[i].x = (p[i].x - minx) * size / bx;
        p[i].y = (p[i].y - miny) * size / by;
    }
}

static void translate_to_origin(Pt *p, int n)
{
    Pt c = centroid(p, n);
    for (int i = 0; i < n; i++) { p[i].x -= c.x; p[i].y -= c.y; }
}

static float path_distance(const Pt *a, const Pt *b, int n)
{
    if (n == 0) return INFINITY;
    float d = 0;
    for (int i = 0; i < n; i++)
    {
        float dx = a[i].x - b[i].x, dy = a[i].y - b[i].y;
        d += sqrtf(dx * dx + dy * dy);
    }
    return d / n;
}

// Orientation-locked $1 — neither the indicative-angle pre-rotation step
// from the paper nor the golden-section rotation search. The watch is held
// in a fixed orientation; "bottom is bottom" by design. Direct point-by-
// point path distance gives the cleanest character discrimination.
static void preprocess(const Pt *src, int n_in, Pt *dst)
{
    resample(src, n_in, dst, kN);
    scale_to_square(dst, kN, kSquareSize);
    translate_to_origin(dst, kN);
}

// --- User-trained template persistence (NVS namespace "uni") ---------------
// Key format: "t_<name>" — 3 chars, safely under the 15-char NVS limit.
//
// On-disk blob format v2:
//   [u8 magic=0xFE][u8 count][int16 (x*100, y*100) × kN × count]
//
// Storage per char: 2 + count × kN × 4 = 2 + count × 256 bytes.
//   1 sample → 258 B, 2 → 514 B, 3 → 770 B. For 36 chars × 3 samples that's
//   ~27 KB on top of the ~5 KB of other NVS data — comfortably under the
//   64 KB nvs partition with wear-levelling headroom.
//
// Old format (no magic byte, count at offset 0, float[2]×kN payload) is
// still readable so existing saves survive the upgrade. New writes always
// use v2.
//
// Quantization: preprocessed points sit in [-125, 125] after scale_to_square
// + translate_to_origin, so ×100 fits in int16 (-12500..12500). 0.01 unit
// precision is irrelevant for the averaged-Euclidean path_distance metric.

constexpr uint8_t kBlobMagicV2 = 0xFE;
constexpr float kQuantScale = 100.0f;
constexpr float kQuantInvScale = 1.0f / 100.0f;

static void nvs_key_for(int idx, char *out, size_t out_sz)
{
    snprintf(out, out_sz, "t_%s", kRawTemplates[idx].name);
}

static void load_samples_for(int char_idx)
{
    g_sample_count[char_idx] = 0;
    nvs_handle_t h;
    if (nvs_open("uni", NVS_READONLY, &h) != ESP_OK) return;
    char key[16];
    nvs_key_for(char_idx, key, sizeof(key));
    size_t sz = 0;
    if (nvs_get_blob(h, key, nullptr, &sz) != ESP_OK || sz < 1)
    {
        nvs_close(h);
        return;
    }
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) { nvs_close(h); return; }
    if (nvs_get_blob(h, key, buf, &sz) != ESP_OK)
    {
        free(buf);
        nvs_close(h);
        return;
    }
    nvs_close(h);

    Pt *dst = sample_ptr(char_idx, 0);
    if (buf[0] == kBlobMagicV2 && sz >= 2)
    {
        uint8_t count = buf[1];
        if (count > kMaxSamples) count = kMaxSamples;
        size_t expected = 2 + (size_t)count * kN * 2 * sizeof(int16_t);
        if (sz == expected)
        {
            const int16_t *qpts = (const int16_t *)(buf + 2);
            int n = count * kN;
            for (int i = 0; i < n; i++)
            {
                dst[i].x = qpts[i * 2] * kQuantInvScale;
                dst[i].y = qpts[i * 2 + 1] * kQuantInvScale;
            }
            g_sample_count[char_idx] = count;
        }
    }
    else
    {
        // Legacy float format — still read on first boot after the upgrade.
        uint8_t count = buf[0];
        if (count > kMaxSamples) count = kMaxSamples;
        size_t expected = 1 + (size_t)count * kN * sizeof(Pt);
        if (sz == expected)
        {
            memcpy(dst, buf + 1, count * kN * sizeof(Pt));
            g_sample_count[char_idx] = count;
        }
    }
    free(buf);
}

static void save_samples_for(int char_idx)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("uni", NVS_READWRITE, &h);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "nvs_open failed for %s: %s",
                 kRawTemplates[char_idx].name, esp_err_to_name(err));
        return;
    }
    char key[16];
    nvs_key_for(char_idx, key, sizeof(key));
    uint8_t count = g_sample_count[char_idx];
    size_t blob_sz = 2 + (size_t)count * kN * 2 * sizeof(int16_t);
    uint8_t *buf = (uint8_t *)malloc(blob_sz);
    if (!buf)
    {
        ESP_LOGE(TAG, "save: malloc(%u) failed for %s",
                 (unsigned)blob_sz, kRawTemplates[char_idx].name);
        nvs_close(h);
        return;
    }
    buf[0] = kBlobMagicV2;
    buf[1] = count;
    int16_t *qpts = (int16_t *)(buf + 2);
    const Pt *src = sample_ptr(char_idx, 0);
    int n = count * kN;
    for (int i = 0; i < n; i++)
    {
        qpts[i * 2] = (int16_t)lroundf(src[i].x * kQuantScale);
        qpts[i * 2 + 1] = (int16_t)lroundf(src[i].y * kQuantScale);
    }
    err = nvs_set_blob(h, key, buf, blob_sz);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "nvs_set_blob(%s, %u B) failed: %s",
                 kRawTemplates[char_idx].name, (unsigned)blob_sz,
                 esp_err_to_name(err));
    }
    else
    {
        err = nvs_commit(h);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "nvs_commit failed for %s: %s",
                     kRawTemplates[char_idx].name, esp_err_to_name(err));
        }
    }
    nvs_close(h);
    free(buf);
}

// Append a new sample. If already at kMaxSamples, FIFO-shifts (drops oldest).
static void add_sample(int char_idx, const Pt *preprocessed)
{
    uint8_t &c = g_sample_count[char_idx];
    if (c < kMaxSamples)
    {
        memcpy(sample_ptr(char_idx, c), preprocessed, kN * sizeof(Pt));
        c++;
    }
    else
    {
        memmove(sample_ptr(char_idx, 0), sample_ptr(char_idx, 1),
                (kMaxSamples - 1) * kN * sizeof(Pt));
        memcpy(sample_ptr(char_idx, kMaxSamples - 1), preprocessed, kN * sizeof(Pt));
    }
    save_samples_for(char_idx);
}

static void clear_samples_for(int char_idx)
{
    g_sample_count[char_idx] = 0;
    nvs_handle_t h;
    if (nvs_open("uni", NVS_READWRITE, &h) != ESP_OK) return;
    char key[16];
    nvs_key_for(char_idx, key, sizeof(key));
    nvs_erase_key(h, key);
    nvs_commit(h);
    nvs_close(h);
}

// Preprocesses every built-in template into g_template_pts (used as fallback
// when a character has no user samples), and loads any saved user samples
// from NVS. Idempotent.
static void init_templates_if_needed()
{
    if (g_templates_ready || !g_template_pts) return;
    for (int i = 0; i < kNumTemplates; i++)
    {
        preprocess(kRawTemplates[i].pts, kRawTemplates[i].n, &g_template_pts[i * kN]);
        load_samples_for(i);
    }
    g_templates_ready = true;
}

struct Result
{
    const char *name;
    float score;
};

static Result recognize(const Pt *raw, int n)
{
    if (n < 2) return {"?", 0};
    Pt pts[kN];
    preprocess(raw, n, pts);
    float best = INFINITY;
    const char *best_name = "?";
    for (int i = 0; i < kNumTemplates; i++)
    {
        // If user samples exist for this character, score against the best
        // one and skip the built-in default; otherwise fall back to default.
        uint8_t sc = g_sample_count[i];
        float char_best = INFINITY;
        if (sc > 0)
        {
            for (int s = 0; s < sc; s++)
            {
                float d = path_distance(pts, sample_ptr(i, s), kN);
                if (d < char_best) char_best = d;
            }
        }
        else
        {
            char_best = path_distance(pts, &g_template_pts[i * kN], kN);
        }
        if (char_best < best)
        {
            best = char_best;
            best_name = kRawTemplates[i].name;
        }
    }
    float score = 1.0f - best / kHalfDiag;
    if (score < 0) score = 0;
    return {best_name, score};
}

// --- UI state (pointers in .bss; buffers in PSRAM) ------------------------

static Pt *g_stroke = nullptr;
static lv_point_precise_t *g_lvgl_pts = nullptr;
static int g_stroke_count = 0;

static lv_obj_t *g_line = nullptr;
static lv_obj_t *g_result_label = nullptr;
static lv_obj_t *g_score_label = nullptr;
static bool g_stroke_finished = false;


// Allocates all big buffers from PSRAM on first call. Called when either
// the recognition screen or the training screen is first built (well after
// BLE init, so we don't compete with the BT controller's internal-SRAM
// allocation).
static bool ensure_buffers()
{
    if (g_stroke) return true;
    g_template_pts = (Pt *)heap_caps_calloc(
        kNumTemplates * kN, sizeof(Pt), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_user_samples = (Pt *)heap_caps_calloc(
        kNumTemplates * kMaxSamples * kN, sizeof(Pt), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_stroke = (Pt *)heap_caps_calloc(
        kMaxStroke, sizeof(Pt), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_lvgl_pts = (lv_point_precise_t *)heap_caps_calloc(
        kMaxStroke, sizeof(lv_point_precise_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return g_template_pts && g_user_samples && g_stroke && g_lvgl_pts;
}

// Takes the line widget to update (recognition screen and training screen
// each have their own line widget but share g_stroke / g_lvgl_pts since
// only one is active at a time).
static void reset_stroke(lv_obj_t *line)
{
    g_stroke_count = 0;
    if (line)
    {
        lv_line_set_points_mutable(line, g_lvgl_pts, 0);
        lv_obj_invalidate(line);
    }
}

static void append_current_point(lv_obj_t *line)
{
    if (g_stroke_count >= kMaxStroke) return;
    lv_point_t cur;
    lv_indev_get_point(lv_indev_active(), &cur);
    g_stroke[g_stroke_count] = {(float)cur.x, (float)cur.y};
    g_lvgl_pts[g_stroke_count] = lv_point_to_precise(&cur);
    g_stroke_count++;
    if (line)
    {
        lv_line_set_points_mutable(line, g_lvgl_pts, g_stroke_count);
        lv_obj_invalidate(line);
    }
}

} // namespace

// Forward decl — the training screen is lazy-built the first time the user
// taps the T button on the recognition screen. Definition is at the end of
// this file (after unistroke_register_app).
static lv_obj_t *training_screen_create();


lv_obj_t *unistroke_create(lv_obj_t *parent)
{
    ensure_buffers();

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    // Small back button at top-center. EVENT_BUBBLE lets a stroke that
    // happens to start in this region still propagate to the screen's
    // PRESSED/PRESSING handlers — only a clean tap (press + release in
    // place) fires LV_EVENT_CLICKED and dismisses.
    lv_obj_t *back = lv_label_create(scr);
    lv_label_set_text(back, "<");
    lv_obj_set_style_text_font(back, &ProductSansBold_24, 0);
    lv_obj_set_style_text_color(back, lv_color_hex(0x888888), 0);
    lv_obj_align(back, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_flag(back, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_set_flag(back, LV_OBJ_FLAG_EVENT_BUBBLE, true);
    lv_obj_set_ext_click_area(back, 8);
    lv_obj_add_event_cb(back, [](lv_event_t *) {
        lv_screen_load_anim(main_screen, LV_SCREEN_LOAD_ANIM_FADE_OUT, 100, 0, false);
    }, LV_EVENT_CLICKED, NULL);

    g_result_label = lv_label_create(scr);
    lv_label_set_text(g_result_label, "");
    lv_obj_set_style_text_font(g_result_label, &ProductSansBold_30, 0);
    lv_obj_set_style_text_color(g_result_label, lv_color_white(), 0);
    lv_obj_add_style(g_result_label, &accent_text_style, 0);
    lv_obj_align(g_result_label, LV_ALIGN_TOP_MID, 0, 36);

    g_score_label = lv_label_create(scr);
    lv_label_set_text(g_score_label, "draw a character");
    lv_obj_set_style_text_font(g_score_label, &ProductSansRegular_14, 0);
    lv_obj_set_style_text_color(g_score_label, lv_color_hex(0x888888), 0);
    lv_obj_align(g_score_label, LV_ALIGN_TOP_MID, 0, 74);

    // Faint canvas hint — the recognizer normalizes scale, so the actual
    // drawing area doesn't matter, but a visible guide nudges the user
    // toward a comfortable central region instead of the screen edges.
    // Strokes still register anywhere on the screen.
    lv_obj_t *hint = lv_obj_create(scr);
    lv_obj_set_size(hint, 160, 160);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_radius(hint, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(hint, LV_OPA_0, 0);
    lv_obj_set_style_border_width(hint, 1, 0);
    lv_obj_set_style_border_color(hint, lv_color_hex(0x333333), 0);
    lv_obj_set_flag(hint, LV_OBJ_FLAG_CLICKABLE, false);
    lv_obj_set_flag(hint, LV_OBJ_FLAG_SCROLLABLE, false);

    g_line = lv_line_create(scr);
    lv_obj_set_size(g_line, 240, 240);
    lv_obj_align(g_line, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_line_set_points_mutable(g_line, g_lvgl_pts, 0);
    lv_obj_add_style(g_line, &accent_line_style, 0);
    lv_obj_set_style_line_width(g_line, 6, 0);
    lv_obj_set_style_line_rounded(g_line, true, 0);

    // "T" at the bottom opens the dedicated training screen (grid of all
    // characters, per-char detail view, multi-sample storage, BLE export).
    lv_obj_t *train_btn = lv_label_create(scr);
    lv_label_set_text(train_btn, "T");
    lv_obj_set_style_text_font(train_btn, &ProductSansBold_24, 0);
    lv_obj_set_style_text_color(train_btn, lv_color_hex(0x888888), 0);
    lv_obj_align(train_btn, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_flag(train_btn, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_set_flag(train_btn, LV_OBJ_FLAG_EVENT_BUBBLE, true);
    lv_obj_set_ext_click_area(train_btn, 12);
    lv_obj_add_event_cb(train_btn, [](lv_event_t *) {
        static lv_obj_t *train_scr = nullptr;
        if (!train_scr) train_scr = training_screen_create();
        lv_screen_load_anim(train_scr, LV_SCREEN_LOAD_ANIM_FADE_IN, 100, 0, false);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(scr, [](lv_event_t *e) {
        // Template preprocessing is deferred to the first press so the work
        // (and stack usage) doesn't happen during boot.
        init_templates_if_needed();
        lv_obj_t *line = (lv_obj_t *)lv_event_get_user_data(e);
        if (g_stroke_finished)
        {
            g_stroke_finished = false;
            reset_stroke(line);
        }
        append_current_point(line);
    }, LV_EVENT_PRESSED, g_line);

    lv_obj_add_event_cb(scr, [](lv_event_t *e) {
        append_current_point((lv_obj_t *)lv_event_get_user_data(e));
    }, LV_EVENT_PRESSING, g_line);

    lv_obj_add_event_cb(scr, [](lv_event_t *) {
        if (g_stroke_count < 2) return;
        Result r = recognize(g_stroke, g_stroke_count);
        lv_label_set_text(g_result_label, r.name);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", (int)(r.score * 100));
        lv_label_set_text(g_score_label, buf);
        g_stroke_finished = true;
    }, LV_EVENT_RELEASED, NULL);

    return scr;
}

void unistroke_register_app()
{
    if (!g_appsscreen) return;
    create_app(g_appsscreen, FA_KEYBOARD, "Unistroke", [](lv_event_t *) {
        static lv_obj_t *scr = nullptr;
        if (!scr) scr = unistroke_create(NULL);
        // Don't wire LV_DIR_RIGHT swipe-to-dismiss like other appsonly
        // screens — LVGL classifies every drawing stroke as a gesture
        // direction, so any rightward-moving character (5, 7, Z, C, …)
        // would dismiss instead of being recognized. The back-arrow label
        // on the screen is the only exit.
        lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_FADE_IN, 100, 0, false);
    });

    // Export runs inline on the LVGL task (see EXPORT click handler).
    // No worker task — its presence broke NimBLE receive empirically.
}

// ===== Training screen =====================================================
// Two views toggled by container visibility:
//   Grid view: 6×6 grid of all characters with sample-count coloring.
//   Detail view: drawing canvas + save/clear, opened by tapping a grid cell.
// Header (back + title + export button) is always visible. Back goes to
// detail→grid→main_screen depending on current state.

namespace {

static lv_obj_t *g_grid_view = nullptr;
static lv_obj_t *g_detail_view = nullptr;
static lv_obj_t *g_grid_cells[kNumTemplates] = {};
static lv_obj_t *g_grid_dots[kNumTemplates] = {};
static lv_obj_t *g_back_btn = nullptr;
static lv_obj_t *g_train_header_title = nullptr;
static lv_obj_t *g_detail_title = nullptr;
static lv_obj_t *g_detail_count = nullptr;
static lv_obj_t *g_detail_line = nullptr;
static lv_obj_t *g_detail_status = nullptr;
static lv_obj_t *g_export_status = nullptr;
static int g_detail_char_idx = -1;

// Two-tap confirmation for export. First tap arms; second tap within a 4 s
// window actually runs the export. Otherwise the disarm timer reverts state.
static bool g_export_armed = false;
static lv_timer_t *g_export_disarm_timer = nullptr;

// Sample-count dots displayed below each grid cell character — small
// indicator strip so the user can tell at a glance which characters have
// training data without going into the detail view.
static void refresh_grid_dot(int idx)
{
    if (!g_grid_dots[idx]) return;
    char buf[8];
    int n = g_sample_count[idx];
    if (n <= 0) { lv_label_set_text(g_grid_dots[idx], " "); return; }
    if (n > kMaxSamples) n = kMaxSamples;
    // Solid dots for each sample, max 3 since kMaxSamples=3
    static const char *DOTS[] = {"·", "··", "···"};
    snprintf(buf, sizeof(buf), "%s", DOTS[n - 1]);
    lv_label_set_text(g_grid_dots[idx], buf);
}

static void refresh_grid_cell(int idx)
{
    if (!g_grid_cells[idx]) return;
    bool trained = g_sample_count[idx] > 0;
    lv_obj_set_style_bg_color(g_grid_cells[idx],
        trained ? g_accent_color : lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(g_grid_cells[idx], LV_OPA_COVER, 0);
    refresh_grid_dot(idx);
}

static void refresh_all_grid_cells()
{
    for (int i = 0; i < kNumTemplates; i++) refresh_grid_cell(i);
}

static void show_grid_view()
{
    g_detail_char_idx = -1;
    refresh_all_grid_cells();
    if (g_back_btn) lv_label_set_text(g_back_btn, "DONE");
    lv_label_set_text(g_train_header_title, "tap a letter");
    lv_obj_set_flag(g_grid_view, LV_OBJ_FLAG_HIDDEN, false);
    lv_obj_set_flag(g_detail_view, LV_OBJ_FLAG_HIDDEN, true);
}

static void show_detail_view(int char_idx)
{
    g_detail_char_idx = char_idx;
    if (g_back_btn) lv_label_set_text(g_back_btn, "BACK");
    // Big char label *is* the title — clear the subtitle so the two labels
    // don't stack on top of each other.
    lv_label_set_text(g_train_header_title, "");
    char buf[24];
    lv_label_set_text(g_detail_title, kRawTemplates[char_idx].name);
    snprintf(buf, sizeof(buf), "%d / %d samples",
             g_sample_count[char_idx], kMaxSamples);
    lv_label_set_text(g_detail_count, buf);
    lv_label_set_text(g_detail_status, "draw to add");
    g_stroke_count = 0;
    reset_stroke(g_detail_line);
    g_stroke_finished = false;
    lv_obj_set_flag(g_grid_view, LV_OBJ_FLAG_HIDDEN, true);
    lv_obj_set_flag(g_detail_view, LV_OBJ_FLAG_HIDDEN, false);
}

// Background task that streams all training data to the connected phone via
// the Gadgetbridge file API. Format on the receiving side is one sample per
// line: "<name> <sample_idx> <x0> <y0> <x1> <y1> … <x63> <y63>". First
// message writes (overwrites) the file; subsequent messages append.
// Show a final status, wait 2.5 s so the user can read it, then revert the
// combined EXPORT button to its idle text so they can tap again. Called
// from run_export which runs on the LVGL task, so lv_refr_now is needed
// to force a panel flush before vTaskDelay blocks the task.
static void export_finish(const char *final_text)
{
    if (g_export_status) lv_label_set_text(g_export_status, final_text);
    lv_refr_now(NULL);
    vTaskDelay(pdMS_TO_TICKS(2500));
    if (g_export_status) lv_label_set_text(g_export_status, "EXPORT");
    lv_refr_now(NULL);
}

// One pass of an export. Called from export_worker each time the user taps
// the EXPORT button. Doesn't `vTaskDelete` itself — the worker outlives any
// single export and loops back to its semaphore wait.
static void run_export()
{
    ESP_LOGI(TAG, "run_export entered");
    int total = 0;
    for (int i = 0; i < kNumTemplates; i++)
        if (g_sample_count[i] > 0) total++;
    ESP_LOGI(TAG, "total trained chars: %d", total);
    if (total == 0)
    {
        ESP_LOGW(TAG, "no samples — exiting");
        export_finish("no samples");
        return;
    }

    // Conservatively sized: per sample is ~64 × ("xx.xx yy.yy ") = ~14 chars
    // × 64 = 896 chars + a 16-char prefix + 2-char \n. × 3 samples = ~2800.
    // Plain malloc (not heap_caps_malloc) so SPIRAM_MALLOC_ALWAYSINTERNAL
    // routing decides where to put it — buffers passed to NimBLE need to
    // be addressable by the BT controller, and plain malloc respects that.
    size_t cap = 4096;
    char *content = (char *)malloc(cap);
    char *json = (char *)malloc(cap + 256);
    ESP_LOGI(TAG, "alloc content=%p json=%p", content, json);
    if (!content || !json)
    {
        if (content) free(content);
        if (json) free(json);
        ESP_LOGE(TAG, "alloc failed");
        export_finish("out of memory");
        return;
    }

    bool first = true;
    int sent = 0;
    for (int i = 0; i < kNumTemplates; i++)
    {
        uint8_t sc = g_sample_count[i];
        if (sc == 0) continue;
        ESP_LOGI(TAG, "char %d (%s): %d samples", i, kRawTemplates[i].name, (int)sc);
        size_t off = 0;
        for (int s = 0; s < sc; s++)
        {
            off += snprintf(content + off, cap - off, "%s %d",
                            kRawTemplates[i].name, s);
            Pt *p = sample_ptr(i, s);
            for (int j = 0; j < kN; j++)
            {
                if (off + 32 >= cap) break;
                off += snprintf(content + off, cap - off, " %.2f %.2f",
                                p[j].x, p[j].y);
            }
            // Literal \n (two chars) — JSON-escaped newline so the resulting
            // file has one sample per line on the phone side.
            if (off + 4 < cap) off += snprintf(content + off, cap - off, "\\n");
        }
        int json_len = snprintf(json, cap + 256,
                 "{t:\"file\",n:\"unistroke.dat\",c:\"%s\",m:\"%c\"}",
                 content, first ? 'w' : 'a');
        ESP_LOGI(TAG, "  content_off=%u json_len=%d, calling send_gb",
                 (unsigned)off, json_len);
        // Each send_gb breaks the JSON into ~13 BLE notifications (MTU 247
        // → 244-byte chunks, ~3 KB JSON). NimBLE's mbuf pool drains via
        // BLE_GAP_EVENT_NOTIFY_TX which only fires after the radio actually
        // sends — at our connection interval, that's ~150 ms per chunk. If
        // we slam too many in too fast, ble_hs_mbuf_from_flat starts
        // returning NULL and send_gb fails (connected=true, just no mbuf).
        // Retry with exponential-ish backoff lets the pool drain.
        bool ok = false;
        for (int retry = 0; retry < 8; retry++)
        {
            ok = ble.send_gb(json);
            if (ok) break;
            ESP_LOGW(TAG, "  send_gb -> 0 (retry %d, connected=%d)",
                     retry, (int)ble.connected());
            vTaskDelay(pdMS_TO_TICKS(500 + retry * 250));
        }
        ESP_LOGI(TAG, "  send_gb -> %d", (int)ok);
        if (!ok)
        {
            ESP_LOGE(TAG, "send_gb failed after retries — bailing");
            free(content); free(json);
            export_finish("ble busy");
            return;
        }
        first = false;
        sent++;
        // We're running on the LVGL task (export runs inline from the
        // EXPORT click handler), so lv_label_set_text already holds the
        // implicit lock. lv_refr_now forces the panel flush before we
        // block again so the user sees live "exporting N/M" progress —
        // without it, the label updates in memory but the screen stays
        // frozen on "starting…" until run_export returns.
        char status[40];
        snprintf(status, sizeof(status), "exporting %d/%d", sent, total);
        if (g_export_status) lv_label_set_text(g_export_status, status);
        lv_refr_now(NULL);
        // Per-message delay tuned for the mbuf-pool drain rate. Each
        // send_gb queues ~13 BLE notifications; at our 100–200 ms
        // connection interval, draining all 13 takes ~1.5–2.5 s. Going
        // faster than the drain rate exhausts the mbuf pool, which then
        // starts dropping RX notifications too (= BLE receive breaks
        // silently for the rest of the session).
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
    ESP_LOGI(TAG, "export complete: %d/%d chars sent", sent, total);

    free(content); free(json);
    export_finish("exported");
}

} // namespace

static lv_obj_t *training_screen_create()
{
    ensure_buffers();
    init_templates_if_needed();

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    // --- Header (shared, always visible on top) ------------------------
    // Round-display constraint: at y=8 the visible x-band is roughly 89..151,
    // so back must sit near the centre. We put "DONE" at the top centre
    // (handles detail→grid → home navigation) since it's the largest tap
    // target and the only exit. "EXPORT" and "CLEAR" go at the bottom centre
    // — bottom y=-12 (i.e. y≈218) has a visible x-band of about 65..175,
    // plenty of room for a single label.

    g_back_btn = lv_label_create(scr);
    lv_label_set_text(g_back_btn, "DONE");
    lv_obj_set_style_text_font(g_back_btn, &ProductSansBold_16, 0);
    lv_obj_set_style_text_color(g_back_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(g_back_btn, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_flag(g_back_btn, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_set_ext_click_area(g_back_btn, 14);
    lv_obj_add_event_cb(g_back_btn, [](lv_event_t *) {
        // Detail → grid → home (recognition screen / watch face).
        if (g_detail_char_idx >= 0) show_grid_view();
        else lv_screen_load_anim(main_screen, LV_SCREEN_LOAD_ANIM_FADE_OUT, 100, 0, false);
    }, LV_EVENT_CLICKED, NULL);

    g_train_header_title = lv_label_create(scr);
    lv_label_set_text(g_train_header_title, "");
    lv_obj_set_style_text_font(g_train_header_title, &ProductSansRegular_14, 0);
    lv_obj_set_style_text_color(g_train_header_title, lv_color_hex(0x888888), 0);
    lv_obj_align(g_train_header_title, LV_ALIGN_TOP_MID, 0, 34);
    // g_export_status is now the same widget as the EXPORT button (see
    // below) — one label that acts as a tappable button when idle and as
    // a progress indicator while exporting. Keeping a separate status
    // label would overlap the bottom of the grid on the round display.

    // --- Grid view -----------------------------------------------------
    // The grid is sized to fit inside the inscribed square of the 240
    // round display (≈169 px). 6×6 cells of 24×24 with 2 px gap → 154 px.
    // Vertical centre is below the DONE/title header (y≈56..210).
    g_grid_view = lv_obj_create(scr);
    lv_obj_set_size(g_grid_view, 240, 240);
    lv_obj_align(g_grid_view, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(g_grid_view, LV_OPA_0, 0);
    lv_obj_set_style_border_width(g_grid_view, 0, 0);
    lv_obj_set_style_pad_all(g_grid_view, 0, 0);
    lv_obj_set_scroll_dir(g_grid_view, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(g_grid_view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flag(g_grid_view, LV_OBJ_FLAG_CLICKABLE, false);

    const int kCols = 6;
    const int kCellW = 24;
    const int kCellH = 24;
    const int kGap = 2;
    int grid_w = kCols * kCellW + (kCols - 1) * kGap;        // 154
    int grid_x0 = (240 - grid_w) / 2;                        // 43
    // Grid spans y=50..204 (154 px tall). Leaves a 12 px gap above the
    // bottom EXPORT label so the last row (U V W X Y Z) isn't covered.
    int grid_y0 = 50;
    for (int i = 0; i < kNumTemplates; i++)
    {
        int row = i / kCols, col = i % kCols;
        lv_obj_t *cell = lv_obj_create(g_grid_view);
        lv_obj_set_size(cell, kCellW, kCellH);
        lv_obj_set_pos(cell, grid_x0 + col * (kCellW + kGap),
                        grid_y0 + row * (kCellH + kGap));
        lv_obj_set_style_radius(cell, 6, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_flag(cell, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_flag(cell, LV_OBJ_FLAG_CLICKABLE, true);
        lv_obj_add_event_cb(cell, [](lv_event_t *e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            show_detail_view(idx);
        }, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(cell);
        lv_label_set_text(lbl, kRawTemplates[i].name);
        lv_obj_set_style_text_font(lbl, &ProductSansBold_16, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

        // No per-cell dot strip — 24 px cells can't fit char + dots
        // legibly. Background colour alone (set in refresh_grid_cell)
        // tells trained vs untrained.
        g_grid_cells[i] = cell;
        g_grid_dots[i] = nullptr;
    }
    refresh_all_grid_cells();

    // Combined EXPORT button + progress label. Idle text "EXPORT"; while
    // running the export task swaps in "starting…", "exporting N/M",
    // "exported", "ble offline", or "no samples". One widget so the user
    // always sees what their tap did (separate status labels would have
    // to sit on top of the grid on this round display).
    g_export_status = lv_label_create(g_grid_view);
    lv_label_set_text(g_export_status, "EXPORT");
    lv_obj_set_style_text_font(g_export_status, &ProductSansBold_16, 0);
    lv_obj_set_style_text_color(g_export_status, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(g_export_status, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_flag(g_export_status, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_set_ext_click_area(g_export_status, 14);
    lv_obj_add_event_cb(g_export_status, [](lv_event_t *) {
        if (!g_export_armed)
        {
            // First tap: arm. Show red "EXPORT?" and start a 4 s window.
            // If the user doesn't tap again within that window the disarm
            // timer fires and reverts the label + flag.
            g_export_armed = true;
            lv_label_set_text(g_export_status, "EXPORT?");
            lv_obj_set_style_text_color(g_export_status, lv_color_hex(0xFF5555), 0);
            if (g_export_disarm_timer) lv_timer_delete(g_export_disarm_timer);
            g_export_disarm_timer = lv_timer_create([](lv_timer_t *t) {
                g_export_armed = false;
                if (g_export_status)
                {
                    lv_label_set_text(g_export_status, "EXPORT");
                    lv_obj_set_style_text_color(g_export_status,
                        lv_color_hex(0xCCCCCC), 0);
                }
                lv_timer_delete(t);
                g_export_disarm_timer = nullptr;
            }, 4000, NULL);
            lv_timer_set_repeat_count(g_export_disarm_timer, 1);
            ESP_LOGI(TAG, "EXPORT armed; awaiting confirmation");
            return;
        }
        // Second tap inside the window: actually run.
        g_export_armed = false;
        if (g_export_disarm_timer)
        {
            lv_timer_delete(g_export_disarm_timer);
            g_export_disarm_timer = nullptr;
        }
        lv_obj_set_style_text_color(g_export_status, lv_color_hex(0xCCCCCC), 0);
        ESP_LOGI(TAG, "EXPORT confirmed, BLE connected=%d", (int)ble.connected());
        lv_label_set_text(g_export_status, "starting…");
        // Force a frame so the user sees the "starting…" feedback before
        // we block the LVGL task with the export. We hold lvgl_port_lock
        // here (we're inside an event callback) so lv_refr_now is safe.
        lv_refr_now(NULL);
        // Run the whole export inline on the LVGL task. UI freezes for
        // the duration but BLE host on core 0 keeps running. Creating a
        // separate worker task — even one sleeping on a semaphore — has
        // empirically broken NimBLE receive on this build (TX-heavy
        // burst + small mbuf pool, or task TCB/stack interfering with
        // RX buffer allocation; either way, sidestepping it).
        run_export();
    }, LV_EVENT_CLICKED, NULL);

    // --- Detail view ---------------------------------------------------
    g_detail_view = lv_obj_create(scr);
    lv_obj_set_size(g_detail_view, 240, 240);
    lv_obj_align(g_detail_view, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(g_detail_view, LV_OPA_0, 0);
    lv_obj_set_style_border_width(g_detail_view, 0, 0);
    lv_obj_set_style_pad_all(g_detail_view, 0, 0);
    lv_obj_set_scroll_dir(g_detail_view, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(g_detail_view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flag(g_detail_view, LV_OBJ_FLAG_HIDDEN, true);

    // Char display + sample count, both at TOP_MID (in the visible band).
    g_detail_title = lv_label_create(g_detail_view);
    lv_label_set_text(g_detail_title, "");
    lv_obj_set_style_text_font(g_detail_title, &ProductSansBold_30, 0);
    lv_obj_set_style_text_color(g_detail_title, lv_color_white(), 0);
    lv_obj_add_style(g_detail_title, &accent_text_style, 0);
    lv_obj_align(g_detail_title, LV_ALIGN_TOP_MID, 0, 36);

    g_detail_count = lv_label_create(g_detail_view);
    lv_label_set_text(g_detail_count, "");
    lv_obj_set_style_text_font(g_detail_count, &ProductSansRegular_14, 0);
    lv_obj_set_style_text_color(g_detail_count, lv_color_hex(0x888888), 0);
    lv_obj_align(g_detail_count, LV_ALIGN_TOP_MID, 0, 76);

    // Canvas hint, centred under the title.
    lv_obj_t *hint = lv_obj_create(g_detail_view);
    lv_obj_set_size(hint, 150, 100);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 22);
    lv_obj_set_style_radius(hint, 12, 0);
    lv_obj_set_style_bg_opa(hint, LV_OPA_0, 0);
    lv_obj_set_style_border_width(hint, 1, 0);
    lv_obj_set_style_border_color(hint, lv_color_hex(0x333333), 0);
    lv_obj_set_flag(hint, LV_OBJ_FLAG_CLICKABLE, false);
    lv_obj_set_flag(hint, LV_OBJ_FLAG_SCROLLABLE, false);

    g_detail_line = lv_line_create(g_detail_view);
    lv_obj_set_size(g_detail_line, 240, 240);
    lv_obj_align(g_detail_line, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_line_set_points_mutable(g_detail_line, g_lvgl_pts, 0);
    lv_obj_add_style(g_detail_line, &accent_line_style, 0);
    lv_obj_set_style_line_width(g_detail_line, 5, 0);
    lv_obj_set_style_line_rounded(g_detail_line, true, 0);

    g_detail_status = lv_label_create(g_detail_view);
    lv_label_set_text(g_detail_status, "");
    lv_obj_set_style_text_font(g_detail_status, &ProductSansRegular_14, 0);
    lv_obj_set_style_text_color(g_detail_status, lv_color_hex(0x888888), 0);
    lv_obj_align(g_detail_status, LV_ALIGN_BOTTOM_MID, 0, -32);

    // "CLEAR" button at the bottom centre of detail view (wipes all
    // samples for the currently-viewed character).
    lv_obj_t *clear = lv_label_create(g_detail_view);
    lv_label_set_text(clear, "CLEAR");
    lv_obj_set_style_text_font(clear, &ProductSansBold_16, 0);
    lv_obj_set_style_text_color(clear, lv_color_hex(0xFF8888), 0);
    lv_obj_align(clear, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_flag(clear, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_set_ext_click_area(clear, 14);
    lv_obj_add_event_cb(clear, [](lv_event_t *) {
        if (g_detail_char_idx < 0) return;
        clear_samples_for(g_detail_char_idx);
        char buf[24];
        snprintf(buf, sizeof(buf), "%d / %d samples", 0, kMaxSamples);
        lv_label_set_text(g_detail_count, buf);
        lv_label_set_text(g_detail_status, "cleared");
    }, LV_EVENT_CLICKED, NULL);

    // Drawing & save handlers on the detail view container.
    lv_obj_add_event_cb(g_detail_view, [](lv_event_t *e) {
        if (g_detail_char_idx < 0) return;
        if (g_stroke_finished)
        {
            g_stroke_finished = false;
            reset_stroke(g_detail_line);
        }
        append_current_point(g_detail_line);
    }, LV_EVENT_PRESSED, NULL);

    lv_obj_add_event_cb(g_detail_view, [](lv_event_t *e) {
        if (g_detail_char_idx < 0) return;
        append_current_point(g_detail_line);
    }, LV_EVENT_PRESSING, NULL);

    lv_obj_add_event_cb(g_detail_view, [](lv_event_t *e) {
        if (g_detail_char_idx < 0 || g_stroke_count < 2) return;
        Pt processed[kN];
        preprocess(g_stroke, g_stroke_count, processed);
        add_sample(g_detail_char_idx, processed);
        char buf[24];
        snprintf(buf, sizeof(buf), "%d / %d samples",
                 g_sample_count[g_detail_char_idx], kMaxSamples);
        lv_label_set_text(g_detail_count, buf);
        lv_label_set_text(g_detail_status, "saved");
        g_stroke_finished = true;
    }, LV_EVENT_RELEASED, NULL);

    // Bring the back button and subtitle to the top of the z-order so the
    // full-screen detail / grid containers (added later as children of scr)
    // don't intercept taps in the back-button region.
    lv_obj_move_foreground(g_back_btn);
    lv_obj_move_foreground(g_train_header_title);

    show_grid_view();
    return scr;
}
