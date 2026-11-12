/* SPDX-License-Identifier: GPL-3.0-only */
/* CLAVIOLINE: a recreation of the 1950s Clavioline (Electronic Engineering, Oct. 1952) for the M-VAVE FM-1,
 * on the Dinghy platform (platform.c: updates, USB rescue, MIDI, keys, screen, flash; unchanged).
 *
 * Stage 2: the playable core.
 *   keys        monophonic, highest note wins (the key ladder of Fig. 2); legato never re-attacks (all keys
 *               share one gate contact); after silence the oscillator restarts from rest
 *   PRESETS     the knee lever (expression; R32): turn fast for big moves. MIDI CC 11 moves it too
 *   MASTER      the volume (platform.c)
 *   OCT- OCT+   the coupler: LOW / MED / HIGH (S1, one octave each way)
 *   ALGORITHM   the keyboard window: the FM-1's 27 keys over the original's 36 (F..E), +0 or +1 octave
 *   ENV         tablet P: the short attack time constant (S2)
 *   KNOB 1      fine tune +-50 cents (VR6, the performer's tuning control)
 *   MIDI in     notes join the same highest-note stack (the coupler applies)
 * To come: the tone-forming network and its 13 tablets (Stage 3), vibrato, the valve amplifier, STOPS mode,
 * presets (Stage 4).
 *
 * Values marked ESTIMATE are not documented in any source we have; see docs/CLAVIOLINE.md. */
static void app_idle(void);
#define LCD_IDLE app_idle
#define APP_NAME "CLAVIOLINE"
#include "platform.c"
#include "clav_tables.h"
#if BOARD_SMK37
#error "CLAVIOLINE is FM-1 only for now"
#endif

/* ---- the sound (audio ISR) ------------------------------------------------------------------------- */
#define OSC_AMP 12000                           /* trapezoid peak, Q15 (headroom for the network to come) */
#define EDGE_K_Q16 57803u                       /* trapezoid edge: t_e * FS / 2, Q16; t_e = 40 us. ESTIMATE */
#define T_ON 132u                               /* key contact 1 (oscillator) to contact 2 (gate): 3 ms. ESTIMATE */
#define T_OFF 176u                              /* on release, contact 2 opens 4 ms before contact 1. ESTIMATE */
#define FADE_STEP 372                           /* oscillator stop: 2 ms fade against a click (not historical) */
#define DC_R 32697                              /* DC blocker, ~15 Hz (the coupling capacitors), Q15 */

static volatile uint32_t a_inc;                 /* main -> ISR: phase step of the sounding note */
static volatile uint32_t a_held;                /* main -> ISR: a key (or MIDI note) is down */
static volatile uint32_t a_p;                   /* main -> ISR: tablet P */
static volatile uint32_t a_lever;               /* main -> ISR: knee lever gain, Q15 */
static volatile uint32_t a_sounding;            /* ISR -> main: something can still be heard */

static struct {
    uint32_t ph, held, pend_on, pend_off;
    int32_t run, stopping, gate, fade, carry, levq, dcx, dcy;
    int32_t x[3];                               /* V2 cathode network, Q30: 1 = cut off, 0 = conducting */
} au = {.x = {1 << 30, 1 << 30, 1 << 30}};

static int32_t cube6(uint32_t u)                /* u^3 / 6, u in Q16 (0..65536) -> Q16 */
{
    return (int32_t)((((((uint64_t)u * u) >> 16) * u) >> 16) / 6u);
}

/* The trapezoid at the control valve's output. One period = 2^32 of phase: a rising edge centred on phase 0,
 * a falling edge on half a period (duty 50 %, ESTIMATE: "similar to a square wave", Sound On Sound), each 2h
 * wide in phase. m = OSC_AMP / h, scaled by 2^24. */
static int32_t trap_naive(uint32_t p, int32_t h, uint32_t m)
{
    int32_t s = (int32_t)p, q = (int32_t)(p - 0x80000000u);
    if (s >= -h && s < h) return (int32_t)(((int64_t)s * m) >> 24);
    if (q >= -h && q < h) return -(int32_t)(((int64_t)q * m) >> 24);
    return p < 0x80000000u ? OSC_AMP : -OSC_AMP;
}

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t i, k, inc = a_inc, held = a_held, h, m, invi, c[4];
    int32_t S, ds[4], lev_t = (int32_t)a_lever;
    const int32_t (*phi)[3];
    const int32_t *gam;
    int32_t sounding = 0;

    /* the oscillator's edges are fixed in time (ESTIMATE: an RC-limited valve edge), so they take a larger
     * part of a high note's period */
    h = (uint32_t)(((uint64_t)inc * EDGE_K_Q16) >> 16);
    if (h < 4096u) h = 4096u;
    if (h > 0x3FFFFFFFu) h = 0x3FFFFFFFu;
    m = ((uint32_t)OSC_AMP << 16) / (h >> 8);
    S = (int32_t)(((uint64_t)inc * m) >> 24);  /* edge slope, per sample */
    invi = 0xFFFFFFFFu / ((inc >> 8) | 1u);
    c[0] = 0u - h; c[1] = h; c[2] = 0x80000000u - h; c[3] = 0x80000000u + h;
    ds[0] = S; ds[1] = -S; ds[2] = -S; ds[3] = S;   /* the slope change at each corner */

    for (i = 0; i < n; i++) {
        int32_t v = 0, g, vk;
        uint32_t idx, frac;

        /* the key contacts: 1 starts the oscillator, 2 (a moment later) opens the gate; on release the
         * other way round */
        if (held && !au.held) {
            au.pend_off = 0;
            au.pend_on = T_ON;
            if (!au.run) { au.run = 1; au.ph = 0; au.carry = 0; }   /* a stopped multivibrator starts from rest */
            au.stopping = 0;
            au.fade = 32767;
        } else if (!held && au.held) {
            au.pend_on = 0;
            au.gate = 0;
            au.pend_off = T_OFF;
        }
        au.held = held;
        if (au.pend_on && !--au.pend_on) au.gate = 1;
        if (au.pend_off && !--au.pend_off) au.stopping = 1;

        /* V2's cathode network (Fig. 2, exact discretisation of the printed values) */
        k = (uint32_t)au.gate << 1 | (a_p ? 1u : 0u);
        phi = GATE_PHI[k];
        gam = GATE_GAM[k];
        {
            int32_t x0 = au.x[0], x1 = au.x[1], x2 = au.x[2];
            au.x[0] = (int32_t)(((int64_t)phi[0][0] * x0 + (int64_t)phi[0][1] * x1 + (int64_t)phi[0][2] * x2) >> 30) + gam[0];
            au.x[1] = (int32_t)(((int64_t)phi[1][0] * x0 + (int64_t)phi[1][1] * x1 + (int64_t)phi[1][2] * x2) >> 30) + gam[1];
            au.x[2] = (int32_t)(((int64_t)phi[2][0] * x0 + (int64_t)phi[2][1] * x1 + (int64_t)phi[2][2] * x2) >> 30) + gam[2];
        }
        vk = au.x[2] < 0 ? 0 : au.x[2] > (1 << 30) ? (1 << 30) : au.x[2];
        idx = (uint32_t)vk >> 22;
        frac = ((uint32_t)vk >> 6) & 0xFFFFu;
        g = GATE_GAIN[idx] + (int32_t)((((int32_t)GATE_GAIN[idx + 1u] - (int32_t)GATE_GAIN[idx]) * (int32_t)frac) >> 16);

        if (au.run) {
            v = trap_naive(au.ph, (int32_t)h, m) + au.carry;
            au.carry = 0;
            for (k = 0; k < 4u; k++) {           /* polyBLAMP at each corner inside (ph, ph + inc] */
                uint32_t dlt = c[k] - au.ph;
                if (dlt - 1u < inc) {
                    uint32_t d = (uint32_t)(((uint64_t)(dlt >> 8) * invi) >> 16);
                    if (d > 65536u) d = 65536u;
                    v += (int32_t)(((int64_t)ds[k] * cube6(65536u - d)) >> 16);
                    au.carry += (int32_t)(((int64_t)ds[k] * cube6(d)) >> 16);
                }
            }
            au.ph += inc;
            if (au.stopping) {
                au.fade -= FADE_STEP;
                if (au.fade <= 0) { au.fade = 0; au.run = 0; au.stopping = 0; au.carry = 0; }
            }
            v = (v * au.fade) >> 15;
        }
        v = (v * g) >> 15;                      /* the control valve */
        au.levq += (lev_t * 256 - au.levq) >> 9;   /* the lever, smoothed (~12 ms) */
        v = (int32_t)(((int64_t)v * au.levq) >> 23);

        au.dcy = v * 256 - au.dcx + (int32_t)(((int64_t)au.dcy * DC_R) >> 15);   /* Q8 */
        au.dcx = v * 256;
        v = au.dcy >> 8;
        if (v > 2 || v < -2) sounding = 1;
        out[2u * i] += v;
        out[2u * i + 1u] += v;
    }
    a_sounding = (uint32_t)(sounding | au.run);
}

/* ---- notes (main loop) ------------------------------------------------------------------------------ */
static const uint32_t INC_C9[12] = {            /* MIDI 120..131 (C9..B9); lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
#define KEY0 53u                                /* FM-1 key 0 = F3 = the original's lowest F (absolute pitch: ESTIMATE) */

static struct { int8_t coupler; uint8_t window, p; int8_t tune; uint8_t ver; } set = {0, 0, 0, 0, 1};
static uint32_t lever = 48;                     /* not saved (it moves all the time): 3/4 open at power-on */
static uint8_t cnt[128];                        /* how many keys / MIDI notes hold each note */
static int32_t top = -1;                        /* the note sounding (keyboard note, before the coupler) */

static void note_add(uint32_t n) { if (n < 128u && cnt[n] < 255u) cnt[n]++; }
static void note_del(uint32_t n) { if (n < 128u && cnt[n]) cnt[n]--; }

static void update_voice(void)                  /* the highest held note wins, as the key ladder does */
{
    int32_t n, p;
    for (n = 127; n >= 0 && !cnt[n]; n--) ;
    top = n;
    if (n < 0) { a_held = 0; return; }
    p = n + 12 * set.coupler;
    if (p < 0) p = 0;
    if (p > 131) p = 131;
    a_inc = (uint32_t)(((uint64_t)(INC_C9[p % 12] >> (10 - p / 12)) * TUNE_Q16[set.tune + 50]) >> 16);
    a_held = 1;                                 /* (pitch first, then the gate) */
}

/* ---- the screen --------------------------------------------------------------------------------------- */
#define C_CREAM RGB(232, 222, 190)
#define C_INK RGB(40, 30, 20)
#define C_AMBER RGB(255, 166, 40)
#define C_WOOD RGB(52, 32, 20)
#define C_GREY RGB(96, 96, 96)
#define C_VIB RGB(170, 182, 196)

static void draw_rail(void)                     /* the 18 tablets of the front rail; a pressed one sits lower, lit */
{
    static const char *const LAB[18] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "O", "A", "B", "V",
                                        "P", "1", "2", "3", "+"};
    uint32_t i;
    cv_begin(240, 42, C_WOOD);
    for (i = 0; i < 18u; i++) {
        int32_t x = 3 + (int32_t)i * 13, on = i == 13u && set.p;   /* (tone and vibrato tablets: Stages 3, 4) */
        uint16_t face = on ? C_AMBER : i >= 14u ? C_VIB : C_CREAM;
        int32_t y = on ? 8 : 3;
        cv_rect(x, y, 12, 31, face);
        cv_text(x + (12 - text_w(&FONT_S, LAB[i])) / 2, y + 8, &FONT_S, LAB[i], C_INK);
    }
    cv_blit(0, 56);
}

static void draw_note(void)
{
    static const char *const NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char s[8] = "-";
    if (top >= 0) {
        int32_t p = top + 12 * set.coupler, i = 0;
        const char *q;
        if (p < 0) p = 0;
        for (q = NAME[p % 12]; *q; ) s[i++] = *q++;
        if (p >= 12) s[i++] = (char)('0' + p / 12 - 1);
        s[i] = 0;
    }
    draw_text_box(0, 108, 240, &FONT_L, s, C_WHITE, 1);
}

static void draw_coupler(void)
{
    static const char *const W[3] = {"LOW", "MED", "HIGH"};
    uint32_t i;
    cv_begin(240, 20, C_BLACK);
    for (i = 0; i < 3u; i++)
        cv_text(28 + (int32_t)i * 70, 2, &FONT_S, W[i], (int32_t)i - 1 == set.coupler ? C_AMBER : C_GREY);
    cv_blit(0, 150);
}

static void draw_lever(void)
{
    int32_t w = (int32_t)(lever * 160u / LEVER_MAX);
    cv_begin(240, 24, C_BLACK);
    cv_text(8, 4, &FONT_S, "KNEE", C_CREAM);
    cv_rect(64, 4, 164, 16, C_GREY);
    cv_rect(66, 6, 160, 12, C_BLACK);
    if (w) cv_rect(66, 6, w, 12, C_AMBER);
    cv_blit(0, 180);
}

static void draw_status(void)
{
    char s[32] = "KEYS F3-G5  TUNE +00";
    int32_t t = set.tune < 0 ? -set.tune : set.tune;
    s[6] = (char)('3' + set.window);
    s[9] = (char)('5' + set.window);
    s[17] = set.tune < 0 ? '-' : '+';
    s[18] = (char)('0' + t / 10);
    s[19] = (char)('0' + t % 10);
    draw_text_box(0, 214, 240, &FONT_S, s, C_GREY, 1);
}

/* ---- MIDI in ---------------------------------------------------------------------------------------- */
static uint32_t idle_ok, midi_dirty;
static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2) != 0) {      /* all channels */
        uint32_t ty = st & 0xF0u;
        if (ty == 0x90u && d2) { note_add(d1); midi_dirty = 1; }
        else if (ty == 0x80u || ty == 0x90u) { note_del(d1); midi_dirty = 1; }
        else if (ty == 0xB0u && d1 == 11u) {    /* CC 11 expression: the knee lever */
            lever = ((uint32_t)d2 * LEVER_MAX + 63u) / 127u;
            a_lever = LEVER_GAIN[lever];
            midi_dirty = 1;
        }
    }
}
static void app_idle(void)
{
    if (idle_ok) midi_in();
}

static void app_main(void)
{
    uint32_t prev = 0, k, t_change = 0, dirty = 0, t_lever = 0;
    uint32_t drawn_note = ~0u, drawn_lever = ~0u, drawn_cpl = ~0u, drawn_st = ~0u, drawn_p = ~0u;
    uint8_t key_note[27] = {0};                 /* the note each held key plays */

    if (settings_load(&set, sizeof set) || set.ver != 1u || set.coupler < -1 || set.coupler > 1 || set.window > 1u
        || set.p > 1u || set.tune < -50 || set.tune > 50) {
        set.coupler = 0; set.window = 0; set.p = 0; set.tune = 0; set.ver = 1;
    }
    a_p = set.p;
    a_lever = LEVER_GAIN[lever];

    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 10, 240, &FONT_L, "CLAVIOLINE", C_CREAM, 1);

    for (;;) {
        uint32_t keys, down, up, rel, b, base = KEY0 + 12u * set.window, changed = 0;
        int32_t e;
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {
            if (up >> k & 1u) { note_del(key_note[k]); midi_send(0x80u, key_note[k], 0); changed = 1; }
            if (down >> k & 1u) { key_note[k] = (uint8_t)(base + k); note_add(key_note[k]); midi_send(0x90u, key_note[k], 100); changed = 1; }
        }

        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_OCT_DOWN) && set.coupler > -1) { set.coupler--; dirty = 1; changed = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.coupler < 1) { set.coupler++; dirty = 1; changed = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_ENV)) { set.p ^= 1u; a_p = set.p; dirty = 1; t_change = fm1_ms; }
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {
            int32_t w = (int32_t)set.window + (e > 0 ? 1 : -1);
            set.window = (uint8_t)(w < 0 ? 0 : w > 1 ? 1 : w);
            dirty = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) {
            int32_t t = set.tune + e;
            set.tune = (int8_t)(t < -50 ? -50 : t > 50 ? 50 : t);
            dirty = 1; changed = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {   /* the knee lever: faster turning, bigger steps */
            uint32_t dt = fm1_ms - t_lever;
            int32_t l = (int32_t)lever + e * (dt < 30u ? 4 : dt < 70u ? 2 : 1);
            t_lever = fm1_ms;
            lever = (uint32_t)(l < 0 ? 0 : l > (int32_t)LEVER_MAX ? (int32_t)LEVER_MAX : l);
            a_lever = LEVER_GAIN[lever];
        }

        midi_in();
        if (changed || midi_dirty) { midi_dirty = 0; update_voice(); }

        for (k = 0; k < 27u; k++)               /* the key that sounds */
            fm1_led_key(KEY_LED(k), top >= 0 && (uint32_t)top == base + k);
        fm1_led_key(BTN_OCT_DOWN, set.coupler < 0);
        fm1_led_key(BTN_OCT_UP, set.coupler > 0);
        fm1_led_key(BTN_ENV, set.p);

        idle_ok = 1;
        if ((uint32_t)set.p != drawn_p) { drawn_p = set.p; draw_rail(); }
        if ((uint32_t)(top + 1) << 8 != drawn_note) { drawn_note = (uint32_t)(top + 1) << 8; draw_note(); }
        if ((uint32_t)(set.coupler + 1) != drawn_cpl) { drawn_cpl = (uint32_t)(set.coupler + 1); draw_coupler(); draw_note(); }
        if (lever != drawn_lever) { drawn_lever = lever; draw_lever(); }
        if (((uint32_t)set.window | (uint32_t)(set.tune + 50) << 1) != drawn_st) {
            drawn_st = (uint32_t)set.window | (uint32_t)(set.tune + 50) << 1;
            draw_status();
        }
        idle_ok = 0;

        if (dirty && !a_sounding && fm1_ms - t_change > 2000u) {   /* save when silent (an erase stalls the sound) */
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
