/* SPDX-License-Identifier: GPL-3.0-only */
/* CLAVIOLINE: a recreation of the 1950s Clavioline (Electronic Engineering, Oct. 1952; the Selmer Concert tone
 * generator drawing DRG 1289) for the M-VAVE FM-1, on the Dinghy platform (platform.c, unchanged).
 *
 * PLAY mode
 *   keys        monophonic, highest note wins (the key ladder); legato never re-attacks (one shared gate contact);
 *               after silence the oscillator restarts from rest
 *   PRESETS     the knee lever (R32, inside the tone network: it shapes the tone as well as the level); MIDI CC 11
 *   MASTER      the volume (platform.c)
 *   OCT- OCT+   the coupler LOW / MED / HIGH (S1)
 *   ALGORITHM   the keyboard window, +0 / +1 octave (the FM-1's 27 keys over the original's 36, F..E)
 *   SELECT      presets: the 1952 Table 1, the Selmer voicings quoted by Sound On Sound, 4 user slots
 *   SAVE        store the tablets and the coupler in the user slot shown
 *   LFO         vibrato speed: off, I (4.5 Hz), II (5.5), III (6.5)      FX   vibrato amplitude (the AMP tablet)
 *   ENV         tablet P (the short attack)                              EDIT STOPS mode
 *   KNOB 1      fine tune +-50 cents (VR6)
 * STOPS mode (EDIT or HOME back; nothing is lost)
 *   white keys  F3..E5: tablets 1 2 3 4 5 6 7 8 9 O A B V P;  F5: all tone tablets off
 *   black keys  F#3 G#3: Sub I p, Sub II p;  C#4 D#4: Sub I f, Sub II f (Concert);  F#4 G#4 A#4: vibrato I II III;
 *               C#5: AMP
 *   PLAY        holds the last note as a drone while you change tablets; MIDI notes play as usual
 *
 * The sound: trapezoid oscillator (+ Concert sub-octave dividers) -> V2 gate (printed cathode network) -> the
 * Fig. 4 tone network, exact (clav_net.h) -> the valve amplifier's 2nd-harmonic distortion. Values marked ESTIMATE
 * are not documented in any source we have: docs/ENGINE.md lists them all. */
static void app_idle(void);
#define LCD_IDLE app_idle
#define APP_NAME "CLAVIOLINE"
#include "platform.c"
#include "clav_tables.h"
#include "clav_net.h"
#if BOARD_SMK37
#error "CLAVIOLINE is FM-1 only for now"
#endif

/* ---- the sound (audio ISR) ------------------------------------------------------------------------- */
#define OSC_AMP 12000                           /* trapezoid peak, Q15 */
#define EDGE_K_Q16 144507u                      /* trapezoid edge: t_e * FS / 2, Q16; t_e = 100 us. ESTIMATE ("trapezoidal": visible slopes) */
#define SUB_F 12000                             /* Sub I / II "f": as loud as the main tone. ESTIMATE */
#define SUB_P 3795                              /* "p": 10 dB softer. ESTIMATE */
#define T_ON 132u                               /* key contact 1 (oscillator) to contact 2 (gate): 3 ms. ESTIMATE */
#define T_OFF 176u                              /* on release, contact 2 opens 4 ms before contact 1. ESTIMATE */
#define FADE_STEP 372                           /* oscillator stop: 2 ms fade against a click (not historical) */
#define DC_R 32697                              /* DC blocker, ~15 Hz, Q15 */
#define VIB_BIG_Q16 1211                        /* vibrato, AMP on: +-32 cents (ln2/1200 * 32 * 65536). ESTIMATE */
#define VIB_SMALL_Q16 318                       /* AMP off: 3.8x smaller (DERIVED ratio, Fig. 3 R23/R24) */
#define VIB_AM_Q15 983                          /* "a small amount of amplitude modulation": +-3 %. ESTIMATE */
#define VALVE_D_Q15 11469                       /* valve 2nd harmonic: y + 0.35 y^2. ESTIMATE */
static const uint32_t VIB_INC[3] = {112195064u, 137127301u, 162059537u};   /* 4.5, 5.5, 6.5 Hz per 256-frame block, 2^32 */

static volatile uint32_t a_inc;                 /* main -> ISR: phase step of the sounding note */
static volatile uint32_t a_held;                /* main -> ISR: a key (or MIDI note) is down */
static volatile uint32_t a_tabs;                /* main -> ISR: the tablets (P, vibrato, sub-octaves) */
static volatile uint32_t a_sounding;            /* ISR -> main: something can still be heard */

static net_coef_t net_buf[2];                   /* the network: the ISR runs net_buf[net_cur] */
static volatile uint32_t net_cur, net_pending;  /* main: 1 = adopt the other buffer, 3 = with a fade (new tablets) */
static net_state_t net_st;

static struct {
    uint32_t ph, held, pend_on, pend_off, lfo, sub;
    int32_t run, stopping, gate, fade, carry, dcx, dcy, xfade, xdir;
    int32_t x[3];                               /* V2 cathode network, Q30: 1 = cut off, 0 = conducting */
} au = {.x = {1 << 30, 1 << 30, 1 << 30}, .xfade = 32767};

static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (Dinghy's) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
static int32_t cube6(uint32_t u)                /* u^3 / 6, u in Q16 (0..65536) -> Q16 */
{
    return (int32_t)((uint32_t)(((((uint64_t)u * u) >> 16) * u) >> 16) / 6u);   /* (a 32-bit division: no __udivdi3) */
}
static int32_t sq2(uint32_t u)                  /* u^2 / 2, Q16 */
{
    return (int32_t)((((uint64_t)u * u) >> 16) >> 1);
}

/* The trapezoid at the control valve: a rising edge centred on phase 0, a falling one on half a period (duty 50 %:
 * "similar to a square wave", Sound On Sound), each 2h wide in phase. m = OSC_AMP / h, scaled by 2^24. */
static int32_t trap_naive(uint32_t p, int32_t h, uint32_t m)
{
    int32_t s = (int32_t)p, q = (int32_t)(p - 0x80000000u);
    if (s >= -h && s < h) return (int32_t)(((int64_t)s * m) >> 24);
    if (q >= -h && q < h) return -(int32_t)(((int64_t)q * m) >> 24);
    return p < 0x80000000u ? OSC_AMP : -OSC_AMP;
}

static int32_t valve(int32_t y)                 /* the amplifier: mostly 2nd harmonic, then a soft ceiling */
{
    int32_t u;
    y += (int32_t)(((((int64_t)y * y) >> 15) * VALVE_D_Q15) >> 15);
    u = y > 32767 ? 32767 : y < -32767 ? -32767 : y;
    return u + (u >> 1) - (int32_t)(((((int64_t)u * u) >> 15) * u) >> 16);   /* 1.5u - 0.5u^3 */
}

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t i, k, tabs = a_tabs, held = a_held, h, m, invi, c[4], inc = a_inc;
    int32_t S, ds[4], sounding = 0, am = 32767, s1, s2;
    const net_coef_t *nc;
    const int32_t (*phi)[3];
    const int32_t *gam;

    /* the network: adopt a new one now, or after fading out (a tablet change: real tablets click) */
    if (net_pending && (net_pending != 3u || (au.xdir < 0 && au.xfade <= 0))) {
        net_cur ^= 1u;
        net_adopt(&net_st, &net_buf[net_cur]);
        if (net_pending == 3u) au.xdir = 1;
        net_pending = 0;
    } else if (net_pending == 3u && au.xdir >= 0 && au.xfade > 0) {
        au.xdir = -1;
    }
    nc = &net_buf[net_cur];

    /* vibrato (Fig. 3): the highest speed tablet down sets the rate; AMP the depth */
    k = tabs & TB(T_VIB3) ? 2u : tabs & TB(T_VIB2) ? 1u : tabs & TB(T_VIB1) ? 0u : 3u;
    if (k < 3u) {
        int32_t sv = sine(au.lfo), d = tabs & TB(T_AMP) ? VIB_BIG_Q16 : VIB_SMALL_Q16;
        au.lfo += VIB_INC[k];
        inc += (uint32_t)(((int64_t)inc * ((d * sv) >> 15)) >> 16);
        am = 32767 + ((VIB_AM_Q15 * sv) >> 15);
    }
    s1 = (tabs & TB(T_SUB1F) ? SUB_F : 0) + (tabs & TB(T_SUB1P) ? SUB_P : 0);
    s2 = (tabs & TB(T_SUB2F) ? SUB_F : 0) + (tabs & TB(T_SUB2P) ? SUB_P : 0);

    h = (uint32_t)(((uint64_t)inc * EDGE_K_Q16) >> 16);   /* edges fixed in time (ESTIMATE) */
    if (h < 4096u) h = 4096u;
    if (h > 0x3FFFFFFFu) h = 0x3FFFFFFFu;
    m = ((uint32_t)OSC_AMP << 16) / (h >> 8);
    S = (int32_t)(((uint64_t)inc * m) >> 24);
    invi = 0xFFFFFFFFu / ((inc >> 8) | 1u);
    c[0] = 0u - h; c[1] = h; c[2] = 0x80000000u - h; c[3] = 0x80000000u + h;
    ds[0] = S; ds[1] = -S; ds[2] = -S; ds[3] = S;

    for (i = 0; i < n; i++) {
        int32_t v = 0, g, vk, y;
        uint32_t idx, frac;

        if (held && !au.held) {                 /* key contacts: 1 starts the oscillator, 2 opens the gate */
            au.pend_off = 0;
            au.pend_on = T_ON;
            if (!au.run) { au.run = 1; au.ph = 0; au.carry = 0; }
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

        k = (uint32_t)au.gate << 1 | (tabs & TB(T_P) ? 1u : 0u);   /* V2's cathode network */
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
            int32_t subs, carry_next = 0;
            v = trap_naive(au.ph, (int32_t)h, m) + au.carry;
            for (k = 0; k < 4u; k++) {           /* polyBLAMP at each corner inside (ph, ph + inc] */
                uint32_t dlt = c[k] - au.ph;
                if (dlt - 1u < inc) {
                    uint32_t d = (uint32_t)(((uint64_t)(dlt >> 8) * invi) >> 16);
                    if (d > 65536u) d = 65536u;
                    v += (int32_t)(((int64_t)ds[k] * cube6(65536u - d)) >> 16);
                    carry_next += (int32_t)(((int64_t)ds[k] * cube6(d)) >> 16);
                }
            }
            subs = (au.sub & 1u ? s1 : -s1) + (au.sub & 2u ? s2 : -s2);
            v += subs;
            if (au.ph > 0xFFFFFFFFu - inc) {     /* a new period: the dividers flip (Concert V5, V6), polyBLEP */
                uint32_t d = (uint32_t)(((uint64_t)((0u - au.ph) >> 8) * invi) >> 16), was = au.sub;
                int32_t step;
                if (d > 65536u) d = 65536u;
                au.sub ^= 1u;
                if ((was & 1u) && !(au.sub & 1u)) au.sub ^= 2u;
                step = ((au.sub & 1u ? s1 : -s1) + (au.sub & 2u ? s2 : -s2)) - subs;
                v += (int32_t)(((int64_t)step * sq2(65536u - d)) >> 16);
                carry_next -= (int32_t)(((int64_t)step * sq2(d)) >> 16);
            }
            au.carry = carry_next;
            au.ph += inc;
            if (au.stopping) {
                au.fade -= FADE_STEP;
                if (au.fade <= 0) { au.fade = 0; au.run = 0; au.stopping = 0; au.carry = 0; }
            }
            v = (int32_t)(((int64_t)v * au.fade) >> 15);
        }
        v = (int32_t)(((int64_t)v * g) >> 15);   /* the control valve */
        v = (int32_t)(((int64_t)v * am) >> 15);
        y = net_step(&net_st, nc, v * NET_ISRC_MUL);   /* the tone-forming network, knee lever inside */
        y = (int32_t)(((int64_t)y * NET_OUT_MUL) >> 16);
        if (au.xdir) {                          /* tablet change: 2.9 ms out, 2.9 ms in */
            au.xfade += au.xdir * 256;
            if (au.xfade >= 32767) { au.xfade = 32767; au.xdir = 0; }
            if (au.xfade < 0) au.xfade = 0;
        }
        y = (int32_t)(((int64_t)y * au.xfade) >> 15);
        y = valve(y);
        au.dcy = y * 256 - au.dcx + (int32_t)(((int64_t)au.dcy * DC_R) >> 15);   /* Q8 */
        au.dcx = y * 256;
        y = au.dcy >> 8;
        if (y > 2 || y < -2) sounding = 1;
        out[2u * i] += y;
        out[2u * i + 1u] += y;
    }
    a_sounding = (uint32_t)(sounding | au.run);
}

/* ---- notes (main loop) ------------------------------------------------------------------------------ */
static const uint32_t INC_C9[12] = {            /* MIDI 120..131 (C9..B9); lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
#define KEY0 53u                                /* FM-1 key 0 = F3 = the original's lowest F (absolute pitch: ESTIMATE) */
#define NUSER 4u

typedef struct { uint32_t tabs; int8_t coupler; } setup_t;
static struct {
    int8_t coupler; uint8_t window; int8_t tune; uint8_t ver;
    uint32_t tabs;
    uint16_t preset; uint16_t pad;
    setup_t user[NUSER];
} set;
#define SET_VER 2u

static uint32_t lever = 48;                     /* the knee lever (not saved): 3/4 open at power-on */
static uint8_t cnt[128];                        /* how many keys / MIDI notes / the drone hold each note */
static int32_t top = -1, last_note = -1;

static void note_add(uint32_t n) { if (n < 128u && cnt[n] < 255u) cnt[n]++; }
static void note_del(uint32_t n) { if (n < 128u && cnt[n]) cnt[n]--; }

static void update_voice(void)                  /* the highest held note wins, as the key ladder does */
{
    int32_t n, p;
    for (n = 127; n >= 0 && !cnt[n]; n--) ;
    top = n;
    if (n < 0) { a_held = 0; return; }
    last_note = n;
    p = n + 12 * set.coupler;
    if (p < 0) p = 0;
    if (p > 131) p = 131;
    a_inc = (uint32_t)(((uint64_t)(INC_C9[p % 12] >> (10 - p / 12)) * TUNE_Q16[set.tune + 50]) >> 16);
    a_held = 1;                                 /* (pitch first, then the gate) */
}

/* ---- presets ------------------------------------------------------------------------------------------ */
#define V1 TB(T_VIB1)
#define V2 TB(T_VIB2)
#define V3 TB(T_VIB3)
#define AMP TB(T_AMP)
#define T(x) TB(T_##x)
enum { LOW = -1, MED = 0, HIGH = 1 };
static const struct { const char *name; uint32_t tabs; int8_t coupler; } PRESET[] = {
    /* Electronic Engineering, Oct. 1952, Table 1 ("Med.-High" played MED; "O or V" as two presets) */
    {"VIOLIN O", T(1) | T(O) | V2 | AMP, HIGH}, {"VIOLIN V", T(1) | T(V) | V2 | AMP, HIGH},
    {"VIOLA O", T(1) | T(O) | V2 | AMP, MED}, {"VIOLA V", T(1) | T(V) | V2 | AMP, MED},
    {"TENOR SAX", T(4) | V3, MED}, {"TRUMPET", V2, MED}, {"TROMBONE", T(3) | V2, LOW},
    {"HORN", T(2) | T(3) | V3 | AMP, LOW}, {"BASSOON", T(3) | T(7), LOW}, {"CORNET", T(6) | V1, MED},
    {"OBOE", T(1) | T(4) | T(8) | V1, MED}, {"FLUTE", T(3) | T(4) | T(5) | V1, HIGH},
    {"PICCOLO", T(1) | T(4) | T(O) | V2, HIGH}, {"HAWAIIAN GUITAR", T(1) | T(4) | T(6) | T(P) | V2 | AMP, MED},
    {"BANJO", T(3) | T(4) | T(B) | T(P), MED}, {"MANDOLIN", T(3) | T(6) | T(8) | T(P), HIGH},
    {"MUSICAL SAW", T(3) | T(B) | V2 | AMP, HIGH},
    /* Selmer voicing tables as quoted by Sound On Sound, March 2007 (where not already above) */
    {"ALTO SAX", T(2) | T(3) | V2, MED}, {"ARABIAN FLUTE", T(1) | T(4) | T(8) | V1, HIGH},
    {"BAGPIPE", T(1) | T(9) | V1, MED}, {"BASS SAX", T(4) | V3, LOW}, {"BASS VIOLIN", T(1) | T(V) | V1, LOW},
    {"CELLO", T(1) | T(V) | V2, LOW}, {"CHURCH ORGAN A", T(4) | T(6), LOW}, {"CHURCH ORGAN B", T(4) | T(9), LOW},
    {"CHURCH ORGAN C", T(6), LOW}, {"ELECTRIC GUITAR", T(4) | T(P) | V2, MED}, {"ENGLISH HORN", T(2) | T(3) | T(B), MED},
    {"HARPSICHORD", T(3) | T(5) | T(6) | T(8) | T(P), HIGH}, {"FIFE", T(B) | T(O), HIGH}, {"FRENCH HORN", T(3), LOW},
    {"HUNTING HORN", T(3) | V3 | AMP, LOW}, {"MUTED GYPSY VLN", T(1) | T(O) | V2 | AMP, MED}, {"ORCH HORN", T(3) | V2, LOW},
    {"REED PIPE", T(B), HIGH}, {"THEATRE ORGAN", T(4) | V3 | AMP, MED},
    {"CLAVIOLINE TONE", T(3) | T(4) | T(6) | V3 | AMP, MED}, {"VOX CELESTA", T(4) | T(5) | T(6) | V3 | AMP, MED},
    {"ZITHER", T(1) | T(4) | T(6) | T(P) | V3 | AMP, MED},
    /* the Concert's Sub I / Sub II 'f' on the raw tone: "an organ tone" (Sound On Sound) */
    {"ORGAN SUB", T(SUB1F) | T(SUB2F), MED},
};
#undef T
#define NFACT (sizeof PRESET / sizeof PRESET[0])
#define NPRESET (NFACT + NUSER)
static uint32_t edited;                         /* the tablets differ from the preset shown */

static void apply_preset(uint32_t i)
{
    if (i < NFACT) { set.tabs = PRESET[i].tabs; set.coupler = PRESET[i].coupler; }
    else { set.tabs = set.user[i - NFACT].tabs; set.coupler = set.user[i - NFACT].coupler; }
    set.preset = (uint16_t)i;
    edited = 0;
}

/* ---- the screen --------------------------------------------------------------------------------------- */
#define C_CREAM RGB(232, 222, 190)
#define C_INK RGB(40, 30, 20)
#define C_AMBER RGB(255, 166, 40)
#define C_WOOD RGB(52, 32, 20)
#define C_GREY RGB(96, 96, 96)
#define C_VIB RGB(170, 182, 196)
#define C_SUB RGB(196, 176, 150)

static void draw_title(uint32_t stops)
{
    draw_text_box(0, 4, 240, &FONT_L, stops ? "STOPS" : "CLAVIOLINE", stops ? C_AMBER : C_CREAM, 1);
}

static void draw_rail(void)                     /* the front rail: a pressed tablet sits lower, lit */
{
    static const char *const L1[14] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "O", "A", "B", "V", "P"};
    static const char *const L2[8] = {"I", "II", "III", "+", "Ip", "IIp", "If", "IIf"};
    static const uint8_t B2[8] = {T_VIB1, T_VIB2, T_VIB3, T_AMP, T_SUB1P, T_SUB2P, T_SUB1F, T_SUB2F};
    uint32_t i, tabs = set.tabs;
    cv_begin(240, 40, C_WOOD);
    for (i = 0; i < 14u; i++) {
        int32_t x = 8 + (int32_t)i * 16, on = (int32_t)(tabs >> i & 1u), y = on ? 7 : 2;
        cv_rect(x, y, 15, 31, on ? C_AMBER : C_CREAM);
        cv_text(x + (15 - text_w(&FONT_S, L1[i])) / 2, y + 8, &FONT_S, L1[i], C_INK);
    }
    cv_blit(0, 40);
    cv_begin(240, 32, C_WOOD);
    for (i = 0; i < 8u; i++) {
        int32_t x = (i < 4u ? 8 : 16) + (int32_t)i * 27, on = (int32_t)(tabs >> B2[i] & 1u), y = on ? 6 : 2;
        cv_rect(x, y, 26, 24, on ? C_AMBER : i < 4u ? C_VIB : C_SUB);
        cv_text(x + (26 - text_w(&FONT_S, L2[i])) / 2, y + 4, &FONT_S, L2[i], C_INK);
    }
    cv_blit(0, 80);
}

static void draw_preset(void)
{
    char s[24];
    const char *p = set.preset < NFACT ? PRESET[set.preset].name : "USER";
    uint32_t i = 0;
    while (*p && i < 18u) s[i++] = *p++;
    if (set.preset >= NFACT) { s[i++] = ' '; s[i++] = (char)('1' + set.preset - NFACT); }
    if (edited) s[i++] = '*';
    s[i] = 0;
    draw_text_box(0, 116, 240, &FONT_S, s, C_CREAM, 1);
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
    draw_text_box(0, 136, 240, &FONT_L, s, C_WHITE, 1);
}

static void draw_coupler(void)
{
    static const char *const W[3] = {"LOW", "MED", "HIGH"};
    uint32_t i;
    cv_begin(240, 20, C_BLACK);
    for (i = 0; i < 3u; i++)
        cv_text(28 + (int32_t)i * 70, 2, &FONT_S, W[i], (int32_t)i - 1 == set.coupler ? C_AMBER : C_GREY);
    cv_blit(0, 172);
}

static void draw_lever(void)
{
    int32_t w = (int32_t)(lever * 160u / LEVER_MAX);
    cv_begin(240, 22, C_BLACK);
    cv_text(8, 3, &FONT_S, "KNEE", C_CREAM);
    cv_rect(64, 3, 164, 16, C_GREY);
    cv_rect(66, 5, 160, 12, C_BLACK);
    if (w) cv_rect(66, 5, w, 12, C_AMBER);
    cv_blit(0, 194);
}

static void draw_status(const char *msg)
{
    char s[32] = "KEYS F3-G5  TUNE +00";
    int32_t t = set.tune < 0 ? -set.tune : set.tune;
    if (msg) { draw_text_box(0, 220, 240, &FONT_S, msg, C_AMBER, 1); return; }
    s[6] = (char)('3' + set.window);
    s[9] = (char)('5' + set.window);
    s[17] = set.tune < 0 ? '-' : '+';
    s[18] = (char)('0' + t / 10);
    s[19] = (char)('0' + t % 10);
    draw_text_box(0, 220, 240, &FONT_S, s, C_GREY, 1);
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
            midi_dirty = 1;
        }
    }
}
static void app_idle(void)
{
    if (idle_ok) midi_in();
}

/* ---- STOPS mode: the keys as tablets ----------------------------------------------------------------- */
static const int8_t KEY_TAB[27] = {             /* key (0 = F3) -> tablet, -1 none, -2 all tone tablets off */
    T_1, T_SUB1P, T_2, T_SUB2P, T_3, -1, T_4, T_5, T_SUB1F, T_6, T_SUB2F, T_7, T_8, T_VIB1, T_9, T_VIB2, T_O,
    T_VIB3, T_A, T_B, T_AMP, T_V, -1, T_P, -2, -1, -1};

static void app_main(void)
{
    uint32_t prev = 0, k, t_change = 0, dirty = 0, t_lever = 0, stops = 0, drone = 0, drone_note = 0;
    uint32_t net_tabs = ~0u, net_lever = ~0u, t_msg = 0;
    uint32_t drawn_note = ~0u, drawn_lever = ~0u, drawn_cpl = ~0u, drawn_st = ~0u, drawn_tabs = ~0u, drawn_pre = ~0u;
    uint32_t drawn_stops = ~0u;
    const char *msg = 0;
    uint8_t key_note[27] = {0};
    uint32_t kon = 0;                           /* keys whose note is in the stack */

    if (settings_load(&set, sizeof set) || set.ver != SET_VER || set.coupler < -1 || set.coupler > 1 || set.window > 1u
        || set.tune < -50 || set.tune > 50 || set.preset >= NPRESET) {
        memset(&set, 0, sizeof set);
        set.ver = SET_VER;
        for (k = 0; k < NUSER; k++) set.user[k].tabs = 0;
        apply_preset(5);                        /* TRUMPET: the raw tone with vibrato II */
    }
    net_compute(&net_buf[0], set.tabs, lever);  /* the network before the sound starts */
    net_adopt(&net_st, &net_buf[0]);
    net_tabs = set.tabs & NET_TABS;
    net_lever = lever;
    a_tabs = set.tabs;

    lcd_fill(0, 0, 240, 240, C_BLACK);

    for (;;) {
        uint32_t keys, down, up, rel, b, base = KEY0 + 12u * set.window, changed = 0;
        int32_t e;
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {
            if (stops) {
                if (down >> k & 1u) {
                    int32_t t = KEY_TAB[k];
                    if (t >= 0) set.tabs ^= TB(t);
                    else if (t == -2) set.tabs &= ~NET_TABS & ~TB(T_P);
                    if (t != -1) { edited = 1; dirty = 1; t_change = fm1_ms; }
                }
                continue;
            }
            if (up >> k & 1u && kon >> k & 1u) { note_del(key_note[k]); kon &= ~(1u << k); midi_send(0x80u, key_note[k], 0); changed = 1; }
            if (down >> k & 1u) { key_note[k] = (uint8_t)(base + k); note_add(key_note[k]); kon |= 1u << k; midi_send(0x90u, key_note[k], 100); changed = 1; }
        }

        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_EDIT) || (stops && b & (1u << BTN_HOME))) {   /* PLAY <-> STOPS */
            uint32_t was = stops;
            stops = b & (1u << BTN_EDIT) ? !stops : 0;
            if (stops && !was)                  /* the keys held now stop sounding: they become tablets */
                for (k = 0; k < 27u; k++)
                    if (kon >> k & 1u) { note_del(key_note[k]); midi_send(0x80u, key_note[k], 0); }
            if (stops) kon = 0;
            if (!stops && drone) { note_del(drone_note); drone = 0; }
            changed = 1;
        }
        if (stops && b & (1u << BTN_PLAY)) {    /* the drone: hold the last note while editing */
            if (drone) { note_del(drone_note); drone = 0; }
            else if (last_note >= 0) { drone_note = (uint32_t)last_note; note_add(drone_note); drone = 1; }
            changed = 1;
        }
        if (b & (1u << BTN_OCT_DOWN) && set.coupler > -1) { set.coupler--; edited = 1; dirty = 1; changed = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.coupler < 1) { set.coupler++; edited = 1; dirty = 1; changed = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_ENV)) { set.tabs ^= TB(T_P); edited = 1; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_FX)) { set.tabs ^= TB(T_AMP); edited = 1; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_LFO)) {              /* off -> I -> II -> III -> off */
            uint32_t v = set.tabs & TB(T_VIB3) ? 3u : set.tabs & TB(T_VIB2) ? 2u : set.tabs & TB(T_VIB1) ? 1u : 0u;
            set.tabs &= ~(TB(T_VIB1) | TB(T_VIB2) | TB(T_VIB3));
            v = (v + 1u) & 3u;
            if (v) set.tabs |= TB(T_VIB1 + v - 1u);
            edited = 1; dirty = 1; t_change = fm1_ms;
        }
        if (b & (1u << BTN_SAVE)) {
            if (set.preset >= NFACT) {
                set.user[set.preset - NFACT].tabs = set.tabs;
                set.user[set.preset - NFACT].coupler = set.coupler;
                edited = 0; dirty = 1; t_change = fm1_ms - 2000u;   /* saved as soon as it is silent */
                msg = "SAVED"; t_msg = fm1_ms;
            } else {
                msg = "SELECT A USER SLOT FIRST"; t_msg = fm1_ms;
            }
        }
        if ((e = fm1_enc_take(ENC_SELECT)) != 0) {
            int32_t p = (int32_t)set.preset + e;
            while (p < 0) p += (int32_t)NPRESET;
            apply_preset((uint32_t)p % NPRESET);
            dirty = 1; changed = 1; t_change = fm1_ms;
        }
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
        }

        midi_in();
        if (changed || midi_dirty) { midi_dirty = 0; update_voice(); }
        a_tabs = set.tabs;

        /* a new network when the tablets or the lever moved (and the ISR has taken the last one) */
        if (!net_pending && ((set.tabs & NET_TABS) != net_tabs || lever != net_lever)) {
            uint32_t topo = (set.tabs & NET_TABS) != net_tabs;
            net_compute(&net_buf[net_cur ^ 1u], set.tabs, lever);
            net_tabs = set.tabs & NET_TABS;
            net_lever = lever;
            net_pending = topo ? 3u : 1u;
        }

        if (stops)                              /* key LEDs: the tablets down, or the key that sounds */
            for (k = 0; k < 27u; k++)
                fm1_led_key(KEY_LED(k), KEY_TAB[k] >= 0 && (set.tabs >> KEY_TAB[k] & 1u));
        else
            for (k = 0; k < 27u; k++)
                fm1_led_key(KEY_LED(k), top >= 0 && (uint32_t)top == base + k);
        fm1_led_key(BTN_OCT_DOWN, set.coupler < 0);
        fm1_led_key(BTN_OCT_UP, set.coupler > 0);
        fm1_led_key(BTN_ENV, set.tabs >> T_P & 1u);
        fm1_led_key(BTN_FX, set.tabs >> T_AMP & 1u);
        fm1_led_key(BTN_LFO, (set.tabs & (TB(T_VIB1) | TB(T_VIB2) | TB(T_VIB3))) != 0);
        fm1_led_key(BTN_EDIT, stops);
        fm1_led_key(BTN_PLAY, drone);

        if (msg && fm1_ms - t_msg > 1500u) { msg = 0; drawn_st = ~0u; }
        idle_ok = 1;
        if (stops != drawn_stops) { drawn_stops = stops; draw_title(stops); }
        if (set.tabs != drawn_tabs) { drawn_tabs = set.tabs; draw_rail(); }
        if ((set.preset | edited << 15) != drawn_pre) { drawn_pre = set.preset | edited << 15; draw_preset(); }
        if (((uint32_t)(top + 1) | (uint32_t)(set.coupler + 1) << 8) != drawn_note) {
            drawn_note = (uint32_t)(top + 1) | (uint32_t)(set.coupler + 1) << 8;
            draw_note();
        }
        if ((uint32_t)(set.coupler + 1) != drawn_cpl) { drawn_cpl = (uint32_t)(set.coupler + 1); draw_coupler(); }
        if (lever != drawn_lever) { drawn_lever = lever; draw_lever(); }
        if (msg && drawn_st != 0xFFFFFFFEu) { drawn_st = 0xFFFFFFFEu; draw_status(msg); }
        else if (!msg && ((uint32_t)set.window | (uint32_t)(set.tune + 50) << 1) != drawn_st) {
            drawn_st = (uint32_t)set.window | (uint32_t)(set.tune + 50) << 1;
            draw_status(0);
        }
        idle_ok = 0;

        if (dirty && !a_sounding && fm1_ms - t_change > 2000u) {   /* save when silent (an erase stalls the sound) */
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
