/*
 * ui.c — landscape touch UI for Tab5-SDR.
 *
 * Rendered into a 640x360 landscape RGB565 buffer (LBUF); the P4 PPA scales x2
 * and rotates 90° onto the 720x1280 panel (half-res keeps the PPA cheap).
 * Touch maps back: lx = (1280 - panel_y)/2,  ly = panel_x/2.
 *
 * Controls: drag spectrum/waterfall to scan fast; [-1][-.1][+.1][+1] tune;
 * WFM/NFM mode toggle; WX/FDNY/EMS bookmark cyclers; volume +/- ; squelch +/-.
 */
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/ppa.h"

#include "bsp_tab5.h"
#include "sdr.h"
#include "dsp.h"
#include "audio.h"
#include "bookmarks.h"

static const char *TAG = "ui";

#define LW 640
#define LH 360
#define PANEL_W 720
#define PANEL_H 1280
#define SCALE 2

#define SPEC_Y0 42
#define SPEC_H  66
#define WF_Y0   (SPEC_Y0 + SPEC_H + 4) /* 112 */
#define WF_H    98                     /* 112..210 */
#define CTRL_Y0 214

#define DB_MIN 44.0f
#define DB_MAX 88.0f

#define FREQ_MIN 24000000u
#define FREQ_MAX 1766000000u

/* Drag tuning: full canvas width ≈ 100 MHz (fast band scanning). */
#define DRAG_HZ_PER_PX (100000000 / LW)

#define SQ_OFFSET 64.0f
#define SQ_STEP   5
#define VOL_STEP  5

#define RGB565(r, g, b) \
    ((uint16_t)(((((r) >> 3) & 0x1F) << 11) | ((((g) >> 2) & 0x3F) << 5) | (((b) >> 3) & 0x1F)))
#define COL_BG     RGB565(8, 12, 28)
#define COL_PANEL  RGB565(18, 22, 40)
#define COL_TEXT   RGB565(210, 220, 240)
#define COL_ACCENT RGB565(0, 220, 120)
#define COL_LINE   RGB565(255, 210, 0)
#define COL_BTN    RGB565(40, 60, 110)
#define COL_BTN_HI RGB565(80, 110, 180)
#define COL_TRACK  RGB565(50, 56, 78)

static uint16_t *LBUF;
static ppa_client_handle_t s_ppa;
static int s_fb_index;

static uint32_t s_center;
static int s_volume = 70;
static int s_squelch = -60;
static int s_nfm = 0;
static const char *s_label = "WFM broadcast";
static int s_wx = -1, s_fdny = -1, s_ems = -1; /* per-category cursor */

/* Tuning step (Hz) applied by the [-]/[+] buttons; STEP button cycles it. */
static const uint32_t STEPS[] = {1000, 5000, 6250, 12500, 25000, 100000, 1000000};
#define N_STEPS ((int)(sizeof(STEPS) / sizeof(STEPS[0])))
static int s_step_i = 4; /* default 25 kHz */

static void step_label(char *out, size_t n)
{
    uint32_t s = STEPS[s_step_i];
    if (s >= 1000000) snprintf(out, n, "%uM", (unsigned)(s / 1000000));
    else if (s % 1000 == 0) snprintf(out, n, "%uk", (unsigned)(s / 1000));
    else snprintf(out, n, "%u.%02uk", (unsigned)(s / 1000), (unsigned)((s % 1000) / 10));
}

/* ---- primitives ---------------------------------------------------------- */

static void rect(int x, int y, int w, int h, uint16_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LW) w = LW - x;
    if (y + h > LH) h = LH - y;
    for (int j = 0; j < h; j++) {
        uint16_t *row = LBUF + (size_t)(y + j) * LW + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

static const uint8_t *glyph(char c)
{
    static const uint8_t G[][7] = {
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
        {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        {0x0E,0x11,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x11,0x0E},
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
        {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
        {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
        {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
        {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* + */
        {0x1F,0x11,0x11,0x11,0x11,0x11,0x1F}, /* D (also generic box) */
        {0x1F,0x10,0x1C,0x10,0x10,0x10,0x10}, /* F */
        {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* W */
        {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
        {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* G (NOAA/misc) */
        {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
        {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
        {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, /* k */
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    };
    switch (c) {
    case '0' ... '9': return G[c - '0'];
    case 'M': return G[10];  case 'H': return G[11];  case 'z': return G[12];
    case 'V': return G[13];  case 'O': return G[14];  case 'L': return G[15];
    case 'S': return G[16];  case 'Q': return G[17];  case '-': return G[18];
    case '.': return G[19];  case '+': return G[20];  case 'D': return G[21];
    case 'F': return G[22];  case 'W': return G[23];  case 'I': return G[24];
    case 'G': return G[25];  case 'N': return G[26];  case 'T': return G[27];
    case 'E': return G[28];  case 'k': return G[29];  default:  return G[30];
    }
}

static void text(int x, int y, int sc, uint16_t c, const char *s)
{
    for (; *s; s++) {
        const uint8_t *g = glyph(*s);
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g[row] & (1 << (4 - col))) rect(x + col * sc, y + row * sc, sc, sc, c);
        x += 6 * sc;
    }
}
static int text_w(const char *s, int sc) { return (int)strlen(s) * 6 * sc; }

static void seg7_digit(int x, int y, int dw, int dh, int t, int d, uint16_t c)
{
    static const uint8_t M[10] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};
    int hh = (dh - 3 * t) / 2, m = M[d];
    if (m & 1)  rect(x + t, y, dw - 2 * t, t, c);
    if (m & 2)  rect(x + dw - t, y + t, t, hh, c);
    if (m & 4)  rect(x + dw - t, y + 2 * t + hh, t, hh, c);
    if (m & 8)  rect(x + t, y + 2 * t + 2 * hh, dw - 2 * t, t, c);
    if (m & 16) rect(x, y + 2 * t + hh, t, hh, c);
    if (m & 32) rect(x, y + t, t, hh, c);
    if (m & 64) rect(x + t, y + t + hh, dw - 2 * t, t, c);
}

/* ---- header: freq + mode/label + S-meter --------------------------------- */

static void draw_header(void)
{
    rect(0, 0, LW, SPEC_Y0 - 1, COL_BG);

    /* frequency, centred, 3-decimal MHz */
    char buf[16];
    snprintf(buf, sizeof buf, "%.3f", s_center / 1e6);
    const int dw = 17, dh = 28, t = 3, gap = 4, dotw = 8;
    int width = 0;
    for (const char *p = buf; *p; p++) width += (*p == '.') ? dotw : (dw + gap);
    int x = (LW - width) / 2, y = 8;
    for (const char *p = buf; *p; p++) {
        if (*p == '.') { rect(x + 2, y + dh - 5, 5, 5, COL_LINE); x += dotw; }
        else { seg7_digit(x, y, dw, dh, t, *p - '0', COL_LINE); x += dw + gap; }
    }

    /* mode + bookmark label, top-left */
    text(6, 4, 2, s_nfm ? RGB565(255, 160, 0) : COL_ACCENT, s_nfm ? "NFM" : "WFM");
    text(6, 24, 1, COL_TEXT, s_label);

    /* S-meter top-right, -60..+60 relative scale */
    int mx = 470, my = 6, mw = 160, mh = 11;
    rect(mx, my, mw, mh, COL_TRACK);
    float tt = (dsp_signal_db() - SQ_OFFSET + 60.0f) / 120.0f;
    if (tt < 0) tt = 0;
    if (tt > 1) tt = 1;
    rect(mx, my, (int)(mw * tt), mh, COL_ACCENT);
    if (s_squelch > -60) {
        float ts = (s_squelch + 60.0f) / 120.0f;
        rect(mx + (int)(mw * ts), my - 3, 2, mh + 6, RGB565(240, 80, 80));
    }
    text(mx, my + mh + 2, 1, COL_TEXT, "SIG");
}

/* ---- spectrum + waterfall ------------------------------------------------ */

static uint16_t heat(float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int r = (int)(255 * (t - 0.5f) * 2.0f), b = (int)(255 * (0.5f - t) * 2.0f);
    int g = (int)(255 * (1.0f - fabsf(t - 0.5f) * 2.0f));
    if (r < 0) r = 0;
    if (b < 0) b = 0;
    if (g < 0) g = 0;
    return RGB565(r, g, b);
}

static void draw_spectrum(const float *spec, int bins)
{
    memmove(LBUF + (size_t)(WF_Y0 + 1) * LW, LBUF + (size_t)WF_Y0 * LW,
            (size_t)(WF_H - 1) * LW * sizeof(uint16_t));
    int cx = LW / 2;
    for (int x = 0; x < LW; x++) {
        int bin = (x * bins) / LW;
        float tt = (spec[bin] - DB_MIN) / (DB_MAX - DB_MIN);
        LBUF[(size_t)WF_Y0 * LW + x] = heat(tt);
        int h = (int)(SPEC_H * (tt < 0 ? 0 : tt > 1 ? 1 : tt));
        int ytop = SPEC_Y0 + SPEC_H - h;
        for (int y = SPEC_Y0; y < SPEC_Y0 + SPEC_H; y++)
            LBUF[(size_t)y * LW + x] = (y >= ytop) ? COL_ACCENT : COL_BG;
    }
    for (int y = SPEC_Y0; y < WF_Y0 + WF_H; y++) LBUF[(size_t)y * LW + cx] = COL_LINE;
}

/* ---- control band -------------------------------------------------------- */

typedef struct { int x, y, w, h; } box_t;
static const box_t B_TDN  = {  6, 220, 56, 44};  /* tune down one step */
static const box_t B_STEP = { 66, 220, 96, 44};  /* cycle step size    */
static const box_t B_TUP  = {166, 220, 56, 44};  /* tune up one step   */
static const box_t B_MODE = {256, 220, 92, 44};  /* WFM/NFM */
static const box_t B_WX   = {  6, 272, 92, 44};
static const box_t B_FDNY = {102, 272, 120, 44};
static const box_t B_EMS  = {226, 272, 120, 44};
static const box_t B_VDN  = {404, 226, 46, 44};
static const box_t B_VUP  = {540, 226, 46, 44};
static const box_t B_SDN  = {404, 300, 46, 44};
static const box_t B_SUP  = {540, 300, 46, 44};

static void draw_box(const box_t *b, const char *label, int sc, uint16_t fill)
{
    rect(b->x, b->y, b->w, b->h, fill);
    rect(b->x, b->y, b->w, 2, COL_BTN_HI);
    text(b->x + (b->w - text_w(label, sc)) / 2, b->y + b->h / 2 - 4 * sc, sc, COL_TEXT, label);
}

static void draw_controls(void)
{
    rect(0, CTRL_Y0, LW, LH - CTRL_Y0, COL_PANEL);
    char sl[12];
    step_label(sl, sizeof sl);
    draw_box(&B_TDN, "-", 3, COL_BTN);
    draw_box(&B_STEP, sl, 2, RGB565(70, 70, 110));
    draw_box(&B_TUP, "+", 3, COL_BTN);
    draw_box(&B_MODE, s_nfm ? "NFM" : "WFM", 2, s_nfm ? RGB565(120, 70, 0) : RGB565(0, 80, 50));
    draw_box(&B_WX, "WX", 2, COL_BTN);
    draw_box(&B_FDNY, "FDNY", 2, COL_BTN);
    draw_box(&B_EMS, "EMS", 2, COL_BTN);

    char v[8];
    text(404, 212, 2, COL_TEXT, "VOL");
    draw_box(&B_VDN, "-", 3, COL_BTN);
    draw_box(&B_VUP, "+", 3, COL_BTN);
    snprintf(v, sizeof v, "%d", s_volume);
    text(470, 238, 3, COL_LINE, v);

    text(404, 286, 2, COL_TEXT, "SQL");
    draw_box(&B_SDN, "-", 3, COL_BTN);
    draw_box(&B_SUP, "+", 3, COL_BTN);
    snprintf(v, sizeof v, "%d", s_squelch);
    text(466, 312, 3, COL_LINE, v);
}

/* ---- actions ------------------------------------------------------------- */

static void retune(int64_t hz)
{
    if (hz < FREQ_MIN) hz = FREQ_MIN;
    if (hz > FREQ_MAX) hz = FREQ_MAX;
    s_center = (uint32_t)hz;
    sdr_set_center_freq(s_center);
}

static void set_mode(int nfm)
{
    s_nfm = nfm ? 1 : 0;
    dsp_set_mode(s_nfm);
}

static void apply_squelch(void)
{
    dsp_set_squelch_db(s_squelch <= -60 ? -200.0f : (float)s_squelch + SQ_OFFSET);
}

static void select_bookmark(const bookmark_t *b)
{
    retune(b->hz);
    set_mode(b->nfm);
    s_label = b->name;
}

static bool hit(const box_t *b, int x, int y)
{
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

static bool handle_touch(void)
{
    static bool prev = false, dragging = false;
    static int drag_x0;
    static uint32_t drag_f0;
    static int grace = 0;    /* ignore touches briefly after boot */
    static int run = 0;      /* consecutive raw-pressed samples (debounce) */
    bool changed = false;

    esp_lcd_touch_point_data_t p;
    int n = bsp_tab5_touch_read(&p, 1);

    /* ~1.2 s startup grace: the touch controller reports stray points right after
     * init that would otherwise "tap" a control. */
    if (grace < 150) { grace++; prev = false; run = 0; return false; }

    /* Debounce: a press must persist for 2 polls (~16 ms) before it counts,
     * rejecting single-sample glitches. */
    run = (n > 0) ? run + 1 : 0;
    bool pressed = run >= 2;
    int lx = 0, ly = 0;
    if (pressed) {
        lx = (PANEL_H - p.y) / SCALE;
        ly = p.x / SCALE;
        if (lx < 0) lx = 0;
        if (lx >= LW) lx = LW - 1;
        if (ly < 0) ly = 0;
        if (ly >= LH) ly = LH - 1;
    }
    bool edge = pressed && !prev;

    if (pressed && ly < CTRL_Y0) {
        /* drag the spectrum/waterfall to scan */
        if (edge) { dragging = true; drag_x0 = lx; drag_f0 = s_center; }
        else if (dragging) {
            int64_t df = -(int64_t)(lx - drag_x0) * DRAG_HZ_PER_PX;
            retune((int64_t)drag_f0 + df);
            s_label = "manual";
            changed = true;
        }
    } else if (edge) {
        dragging = false;
        if (hit(&B_TDN, lx, ly)) { retune((int64_t)s_center - STEPS[s_step_i]); changed = true; }
        else if (hit(&B_TUP, lx, ly)) { retune((int64_t)s_center + STEPS[s_step_i]); changed = true; }
        else if (hit(&B_STEP, lx, ly)) { s_step_i = (s_step_i + 1) % N_STEPS; changed = true; }
        else if (hit(&B_MODE, lx, ly)) {
            set_mode(!s_nfm);
            s_label = s_nfm ? "manual NFM" : "WFM broadcast";
            changed = true;
        } else if (hit(&B_WX, lx, ly)) {
            s_wx = (s_wx + 1) % BM_WX_N; select_bookmark(&BM_WX[s_wx]); changed = true;
        } else if (hit(&B_FDNY, lx, ly)) {
            s_fdny = (s_fdny + 1) % BM_FDNY_N; select_bookmark(&BM_FDNY[s_fdny]); changed = true;
        } else if (hit(&B_EMS, lx, ly)) {
            s_ems = (s_ems + 1) % BM_EMS_N; select_bookmark(&BM_EMS[s_ems]); changed = true;
        } else if (hit(&B_VDN, lx, ly)) {
            s_volume -= VOL_STEP; if (s_volume < 0) s_volume = 0;
            audio_set_volume(s_volume); changed = true;
        } else if (hit(&B_VUP, lx, ly)) {
            s_volume += VOL_STEP; if (s_volume > 100) s_volume = 100;
            audio_set_volume(s_volume); changed = true;
        } else if (hit(&B_SDN, lx, ly)) {
            s_squelch -= SQ_STEP; if (s_squelch < -60) s_squelch = -60;
            apply_squelch(); changed = true;
        } else if (hit(&B_SUP, lx, ly)) {
            s_squelch += SQ_STEP; if (s_squelch > 60) s_squelch = 60;
            apply_squelch(); changed = true;
        }
    }

    if (!pressed) dragging = false;
    prev = pressed;
    return changed;
}

/* ---- present ------------------------------------------------------------- */

static void present(void)
{
    ppa_do_scale_rotate_mirror(s_ppa, &(ppa_srm_oper_config_t){
        .in = { .buffer = LBUF, .pic_w = LW, .pic_h = LH, .block_w = LW, .block_h = LH,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .out = { .buffer = bsp_tab5_display_get_frame_buffer(s_fb_index),
                 .buffer_size = PANEL_W * PANEL_H * 2, .pic_w = PANEL_W, .pic_h = PANEL_H,
                 .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_90, .scale_x = SCALE, .scale_y = SCALE,
    });
    bsp_tab5_display_flush(s_fb_index);
    s_fb_index ^= 1;
}

static void ui_task(void *arg)
{
    (void)arg;
    rect(0, 0, LW, LH, COL_BG);
    rect(0, WF_Y0, LW, WF_H, RGB565(0, 0, 0));
    draw_controls();
    draw_header();
    present();

    const float *last = NULL;
    int bins = dsp_spectrum_bins();
    int meter_div = 0;
    while (1) {
        bool dirty = handle_touch();
        const float *spec = dsp_spectrum();
        bool newspec = spec && spec != last;
        if (newspec) {
            last = spec;
            draw_spectrum(spec, bins);
            if (++meter_div >= 4) { meter_div = 0; draw_header(); }
        }
        if (dirty) { draw_controls(); draw_header(); }
        if (newspec || dirty) present();
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

void ui_start(void)
{
    LBUF = heap_caps_malloc((size_t)LW * LH * 2, MALLOC_CAP_SPIRAM);
    if (!LBUF) { ESP_LOGE(TAG, "no PSRAM for landscape buffer"); return; }
    ESP_ERROR_CHECK(ppa_register_client(&(ppa_client_config_t){ .oper_type = PPA_OPERATION_SRM }, &s_ppa));

    sdr_stats_t st;
    sdr_get_stats(&st);
    s_center = st.center_freq;
    audio_set_volume(s_volume);
    apply_squelch();

    ESP_LOGI(TAG, "landscape UI %dx%d (PPA x%d)", LW, LH, SCALE);
    xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 6, NULL, 0);
}
