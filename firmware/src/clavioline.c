/* SPDX-License-Identifier: GPL-3.0-only */
/* CLAVIOLINE: a recreation of the 1950s Clavioline (Electronic Engineering, Oct. 1952; the Selmer Concert tone
 * generator drawing DRG 1289) for the M-VAVE FM-1, on the Dinghy platform (platform.c, unchanged).
 *
 * PLAY mode
 *   keys        monophonic, highest note wins (the key ladder); legato never re-attacks (one shared gate contact);
 *               after silence the oscillator restarts from rest
 *   PRESETS     the knee lever (expression); MIDI CC 11
 *   MASTER      the volume (platform.c)
 *   OCT- OCT+   the coupler LOW / MED / HIGH (S1)
 *   ALGORITHM   the keyboard window, +0 / +1 octave (the FM-1's 27 keys over the original's 36, F..E)
 *   SELECT      presets A..Z (the 1952 Table 1, the Selmer voicings quoted by Sound On Sound), then 4 user slots;
 *               every power-on starts on CLAVIOLINE TONE
 *   SAVE        store the tablets and the coupler in the user slot shown
 *   GLO         STOPS mode (vibrato, P and the sub-octaves are tablets there); FX SEL ENV LFO EDIT do nothing here
 *   SEQ         OPTIONS: pluck decay, knee lever speed, portamento (none of them on the original)
 *   KNOB 1      fine tune +-50 cents (VR6)
 * STOPS mode (GLO or HOME back; nothing is lost)
 *   white keys  F3..E5: tablets 1 2 3 4 5 6 7 8 9 O A B V P;  F5, G5: nothing
 *   black keys  F#3 G#3 A#3: vibrato III II I;  C#4: AMP;  A#4 C#5 D#5 F#5: Sub I p, Sub II p, Sub I f, Sub II f
 *   FX SEL ENV LFO EDIT   play C4 D4 E4 F4 G4 while held, to try the tablets
 *   PLAY        holds the last note as a drone while you change tablets; MIDI notes play as usual
 *
 * The sound, at 22.05 kHz: the multivibrator's wave shape from band-limited wavetables (+ the Concert's sub-octave
 * dividers) -> the control valve's attack (printed cathode network; tablet P shortens it) -> the coupling capacitor ->
 * the tone network as a formant filter (five biquads per tablet combination, fitted to the circuit model) -> the knee
 * lever. Then at 44.1 kHz: the valve amplifier (mostly 2nd harmonic) -> the loudspeaker and cabinet. Values marked
 * ESTIMATE are not documented in any source we have: docs/ENGINE.md lists them all. */
static void app_idle(void);
#define LCD_IDLE app_idle
#define APP_NAME "CLAVIOLINE"
#include "platform.c"
#include "clav_tables.h"
#include "clav_formants.h"
#include "clav_formant.h"
#if BOARD_SMK37
#error "CLAVIOLINE is FM-1 only for now"
#endif
#ifndef APP_BUILD
#define APP_BUILD "local"                       /* tools/build.py passes the commit it built */
#endif

/* ---- the sound (audio ISR) ------------------------------------------------------------------------- */
#define OSC_AMP 12000                           /* the oscillator's peak, Q15 */
#define SUB_F 12000                             /* Sub I / II "f": as loud as the main tone. ESTIMATE */
#define SUB_P 3795                              /* "p": 10 dB softer. ESTIMATE */
#define T_ON 66u                                /* key contact 1 (oscillator) to contact 2 (gate): 3 ms at 22.05 kHz. ESTIMATE */
#define T_OFF 88u                               /* on release, contact 2 opens 4 ms before contact 1. ESTIMATE */
#define FADE_STEP 744                           /* oscillator stop: 2 ms fade against a click (not historical) */
#define HP_A_Q13 8028                           /* C5's coupling, as a high-pass on the source: ~71 Hz at 22.05 kHz, Q13 */
#define IDLE_N 4410u                            /* 100 ms of silence after a note: the engine rests (no work) */
#define VIB_BIG_Q16 1211                        /* vibrato, AMP on: +-32 cents (ln2/1200 * 32 * 65536). ESTIMATE */
#define VIB_SMALL_Q16 318                       /* AMP off: 3.8x smaller (DERIVED ratio, Fig. 3 R23/R24) */
#define VIB_AM_Q15 983                          /* "a small amount of amplitude modulation": +-3 %. ESTIMATE */
#define VALVE_D_Q15 52000                       /* valve amplifier: y + 1.59 y^2 (at a third of the level): the 2nd harmonic ~-18 dB at full lever.
                                                 * ESTIMATE ("a large amount of distortion is deliberately obtained") */
/* The control valve's screen grid (Fig. 2: R4, C4, values not printed). While the valve is shut, C4 charges to the
 * full supply; when the valve opens, its screen current drains it, so the valve starts with extra gain that sags
 * to its steady value. Opening fast (tablet P) gives a sharp percussive peak; the slow normal opening only a swell.
 * Fitted to runs of P notes in Vintage Keys Studio, "The Joe Meek Telstar Keyboard: The Clavioline" (YouTube,
 * JK0m1moewgA, about 1:59) (each note peaks about 16 dB over its steady level, -4.5 /
 * -7.5 / -9 dB after 10 / 20 / 40 ms, and re-peaks on the next note): ESTIMATES, Q30 per 22.05 kHz step. */
#define SAG_A1 4869580                          /* drained in 10 ms at full conduction */
#define SAG_R 749154                            /* recharged through R4 in 65 ms */
#define SAG_K1 40                               /* its weight in the gain */
#define SAG_INVN 10348                          /* 1 / the gain at steady conduction, Q16: steady notes keep their level */
#define AMP_IN_Q15 10923                        /* into the amplifier at a third (-9.5 dB): headroom for the peaks ... */
#define AMP_OUT_Q8 640                          /* ... and 2.5x after the speaker */
static const uint32_t VIB_INC[3] = {112195064u, 137127301u, 162059537u};   /* 4.5, 5.5, 6.5 Hz per 256-frame block, 2^32 */

static volatile uint32_t a_inc;                 /* main -> ISR: phase step of the sounding note (44.1 kHz) */
static volatile uint32_t a_held;                /* main -> ISR: a key (or MIDI note) is down */
static volatile uint32_t a_tabs;                /* main -> ISR: the tablets (P, vibrato, sub-octaves) */
static volatile uint32_t a_lever;               /* main -> ISR: the knee lever's gain, Q15 */
static volatile uint32_t a_decay, a_decay_all;  /* main -> ISR: the pluck decay (Q30 per step, 0 = off), on all notes */
static volatile uint32_t a_pluck;               /* main -> ISR: counts key presses (each restarts the decay) */
static volatile uint32_t a_glide;               /* main -> ISR: portamento, Q16 per 256-frame block (0 = off) */
/* OPTIONS (not on the original): portamento, time constants 15 / 30 / 60 / 120 / 250 / 500 ms; 1 - exp(-5.8 ms / T),
 * Q16 */
static const uint16_t GLIDE_Q16[6] = {21031u, 11528u, 6044u, 3095u, 1504u, 756u};
/* OPTIONS (not on the original): the pluck decay, 40 dB down in 0.3 / 0.6 / 1.2 / 2.5 s; per 22.05 kHz step, Q30 */
static const uint32_t DECAY_Q30[4] = {1072994576u, 1073368135u, 1073554963u, 1073652127u};
static volatile uint32_t a_sounding;            /* ISR -> main: something can still be heard */
static volatile uint32_t a_load, a_load_peak;   /* ISR -> main: render time, per mille of the block (peak held) */
#define HALF_FRAMES_TICKS (256u * 1000000u / 44100u * FM1_TICKS_PER_US)   /* one 256-frame block, 24 MHz ticks */
#ifdef DINGHY_HOST
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void) { return 0; }   /* (no load on the host) */
#endif

static fm_coef_t fm_buf[2];                     /* the formant filter: the ISR runs fm_buf[fm_cur] */
static volatile uint32_t fm_cur, fm_pending;    /* main: 1 = adopt the other buffer (after a short fade) */
static fm_state_t fm_st;

static struct {
    uint32_t ph, wraps, held, pend_on, pend_off, lfo;
    int32_t run, stopping, gate, fade, dcx, dcy, xfade, xdir, g, gstep, yh[4], hp, hpx, lev;
    uint32_t quiet, idle, pluck, env;           /* env: the pluck decay, Q30 */
    uint32_t ginc;                              /* (OPTIONS) the gliding phase step; 0 = none yet */
    int32_t s1;                                 /* the screen grid's charge, Q30 (1 = full) */
    int32_t x[3];                               /* V2 cathode network, Q30: 1 = cut off, 0 = conducting */
    int32_t sx1[3], sx2[3], sy1[3], sy2[3];     /* the speaker's biquads */
} au = {.x = {1 << 30, 1 << 30, 1 << 30}, .xfade = 32767, .env = 1u << 30, .s1 = 1 << 30};

static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (Dinghy's) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

static int32_t wt_read(const int16_t *t, uint32_t ph)   /* a wavetable, linearly interpolated */
{
    uint32_t i = ph >> 22, fr = (ph >> 6) & 0xFFFFu;
    int32_t a = t[i], b = t[(i + 1u) & (WT_N - 1u)];
    return a + (((b - a) * (int32_t)fr) >> 16);
}

static uint32_t wt_table(uint32_t inc)          /* the half-octave table for a phase step (22.05 kHz) */
{
    uint32_t k = 0;
    while (k + 1u < WT_NT && inc >= WT_EDGE[k + 1u]) k++;
    return k;
}

static int32_t valve(int32_t y)                 /* the amplifier: mostly 2nd harmonic, then a soft ceiling */
{
    int32_t u;
    y = y > 46000 ? 46000 : y < -46000 ? -46000 : y;   /* (32-bit products below) */
    y += (((y * y) >> 15) * VALVE_D_Q15) >> 15;
    u = y > 32767 ? 32767 : y < -32767 ? -32767 : y;
    return u + (u >> 1) - ((((u * u) >> 15) * u) >> 16);   /* 1.5u - 0.5u^3 */
}

static int32_t speaker(int32_t x)               /* the loudspeaker and cabinet: three biquads at 44.1 kHz (ESTIMATE) */
{
    uint32_t k;
    int32_t v = x * 256;                        /* Q15 -> Q23 for headroom and precision */
    for (k = 0; k < 3u; k++) {
        const int32_t *c = SPEAKER[k];
        int64_t acc = (int64_t)c[0] * v + (int64_t)c[1] * au.sx1[k] + (int64_t)c[2] * au.sx2[k]
                    - (int64_t)c[3] * au.sy1[k] - (int64_t)c[4] * au.sy2[k];
        int64_t y = (acc + (1LL << 27)) >> 28;
        int32_t yo = y > 2147483647 ? 2147483647 : y < -2147483647 ? -2147483647 : (int32_t)y;
        au.sx2[k] = au.sx1[k]; au.sx1[k] = v;
        au.sy2[k] = au.sy1[k]; au.sy1[k] = yo;
        v = yo;
    }
    return v >> 8;
}

/* The audio interrupt. The instrument runs at 22.05 kHz, one step for every two output samples; a 4-point
 * half-band interpolator brings it to 44.1 kHz for the valve amplifier and the speaker. After 100 ms of silence it
 * rests and costs nothing. */
static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t i, k, tabs = a_tabs, held = a_held, inc = a_inc, inc2, ko, ko1, ko2, decay;
    int32_t sounding = 0, am = 32767, s1, s2, lev_t = (int32_t)a_lever;
    const fm_coef_t *fc;
    const int16_t *tp, *ts1, *ts2;
    uint32_t t0 = fm1_ticks();

    if (fm_pending && (au.idle || (au.xdir < 0 && au.xfade <= 0))) {   /* a new formant filter: at rest, or faded out */
        fm_cur ^= 1u;
        fm_pending = 0;
        au.xdir = au.idle ? 0 : 1;
        if (au.idle) au.xfade = 32767;
    } else if (fm_pending && au.xdir >= 0) {
        au.xdir = -1;                           /* (a tablet change: 2.9 ms out, then in) */
    }
    fc = &fm_buf[fm_cur];

    if (au.idle) {                              /* silent: nothing to compute until a key goes down */
        if (!held) { a_sounding = 0; a_load = 0; return; }
        au.idle = 0;
        au.ginc = 0;                            /* (after silence a note starts at its own pitch) */
    }
    if (a_glide && au.ginc)                     /* (OPTIONS) portamento: the pitch follows the note, RC-like */
        au.ginc += (uint32_t)(int32_t)((((int64_t)inc - (int64_t)au.ginc) * (int64_t)a_glide) >> 16);
    else
        au.ginc = inc;
    inc = au.ginc;

    /* vibrato (Fig. 3): the highest speed tablet down sets the rate; AMP the depth */
    k = tabs & TB(T_VIB3) ? 2u : tabs & TB(T_VIB2) ? 1u : tabs & TB(T_VIB1) ? 0u : 3u;
    if (k < 3u) {
        int32_t sv = sine(au.lfo), d = tabs & TB(T_AMP) ? VIB_BIG_Q16 : VIB_SMALL_Q16;
        au.lfo += VIB_INC[k];
        inc += (uint32_t)(((int64_t)inc * ((d * sv) >> 15)) >> 16);
        am = 32767 + ((VIB_AM_Q15 * sv) >> 15);
    }
    if (a_pluck != au.pluck) { au.pluck = a_pluck; au.env = 1u << 30; }   /* (OPTIONS) a key press: full again */
    {
        uint32_t dk = a_decay;
        decay = dk && (a_decay_all || (tabs & TB(T_P))) ? dk : 0u;
        if (!decay) au.env = 1u << 30;
    }
    s1 = (tabs & TB(T_SUB1F) ? SUB_F : 0) + (tabs & TB(T_SUB1P) ? SUB_P : 0);
    s2 = (tabs & TB(T_SUB2F) ? SUB_F : 0) + (tabs & TB(T_SUB2P) ? SUB_P : 0);

    inc2 = inc << 1;                            /* the phase step at 22.05 kHz */
    ko = wt_table(inc2);                        /* band-limited tables for this pitch, and an octave or two below */
    ko1 = ko > 1u ? ko - 2u : 0u;
    ko2 = ko > 3u ? ko - 4u : 0u;
    tp = WT_PULSE[ko]; ts1 = WT_SQUARE[ko1]; ts2 = WT_SQUARE[ko2];

    for (i = 0; i < n; i += 2u) {
        int32_t v = 0, g, y, yi, o;
        uint32_t j;

        if (held && !au.held) {                 /* key contacts: 1 starts the oscillator, 2 opens the gate */
            au.pend_off = 0;
            au.pend_on = T_ON;
            if (!au.run) { au.run = 1; au.ph = 0; au.wraps = 0; }   /* a stopped multivibrator starts from rest */
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

        if (!(i & 7u)) {                        /* V2's cathode network, stepped every 8 output samples (exactly) */
            const int32_t (*phi)[3], *gam;
            int32_t x0 = au.x[0], x1 = au.x[1], x2 = au.x[2], vk, gt;
            uint32_t idx, frac;
            k = (uint32_t)au.gate << 1 | (tabs & TB(T_P) ? 1u : 0u);   /* P: the short time constant (S2) */
            phi = GATE_PHI8[k];
            gam = GATE_GAM8[k];
            au.x[0] = (int32_t)(((int64_t)phi[0][0] * x0 + (int64_t)phi[0][1] * x1 + (int64_t)phi[0][2] * x2) >> 30) + gam[0];
            au.x[1] = (int32_t)(((int64_t)phi[1][0] * x0 + (int64_t)phi[1][1] * x1 + (int64_t)phi[1][2] * x2) >> 30) + gam[1];
            au.x[2] = (int32_t)(((int64_t)phi[2][0] * x0 + (int64_t)phi[2][1] * x1 + (int64_t)phi[2][2] * x2) >> 30) + gam[2];
            vk = au.x[2] < 0 ? 0 : au.x[2] > (1 << 30) ? (1 << 30) : au.x[2];
            idx = (uint32_t)vk >> 22;
            frac = ((uint32_t)vk >> 6) & 0xFFFFu;
            gt = GATE_GAIN[idx] + (int32_t)((((int32_t)GATE_GAIN[idx + 1u] - (int32_t)GATE_GAIN[idx]) * (int32_t)frac) >> 16);
            au.gstep = (gt - au.g) >> 2;        /* the gain glides there over the 4 steps */
        }
        g = au.g;
        au.g += au.gstep;

        if (au.run) {                           /* the multivibrator's wave, and the dividers' squares */
            v = (wt_read(tp, au.ph) * OSC_AMP) >> 15;
            if (s1) v += (wt_read(ts1, (au.wraps & 1u) << 31 | au.ph >> 1) * s1) >> 15;
            if (s2) v += (wt_read(ts2, (au.wraps & 3u) << 30 | au.ph >> 2) * s2) >> 15;
            if (au.ph > 0xFFFFFFFFu - inc2) au.wraps++;
            au.ph += inc2;
            if (au.stopping) {
                au.fade -= FADE_STEP;
                if (au.fade <= 0) { au.fade = 0; au.run = 0; au.stopping = 0; }
            }
            v = (v * au.fade) >> 15;            /* (|v| < 45000: 32-bit products) */
        }
        v = (v * am) >> 15;                     /* (vibrato's small AM is on the oscillator, V1) */
        v = (v * g) >> 15;                      /* the control valve: the attack (and the release) */
        {                                       /* its screen grid: the charge stores drain while it conducts */
            int64_t gd = (int64_t)g;            /* (Q15) */
            int32_t mq;
            au.s1 += (int32_t)((((int64_t)SAG_R * ((1 << 30) - au.s1)) >> 30) - ((((SAG_A1 * gd) >> 15) * au.s1) >> 30));
            mq = (int32_t)(((((int64_t)1 << 30) + (int64_t)SAG_K1 * au.s1) >> 18) * SAG_INVN >> 16);
            v = (int32_t)(((int64_t)v * mq) >> 12);   /* (gain in Q12: up to ~6.5 at the very start) */
        }
        {                                       /* C5, the coupling capacitor: a first-order high-pass */
            int32_t hp = ((au.hp + v - au.hpx) * HP_A_Q13) >> 13;
            au.hpx = v;
            au.hp = hp;
            v = hp;
        }
        y = fm_run(&fm_st, fc, v);              /* the tone-forming network: the tablets' formants */
        au.lev += (lev_t - au.lev) >> 7;        /* the knee lever, smoothed (~6 ms) */
        y = (int32_t)(((int64_t)y * au.lev) >> 15);
        if (decay) au.env = (uint32_t)(((uint64_t)au.env * decay) >> 30);   /* (OPTIONS) the pluck decay */
        y = (int32_t)(((int64_t)y * (int32_t)(au.env >> 15)) >> 15);
        au.yh[0] = au.yh[1]; au.yh[1] = au.yh[2]; au.yh[2] = au.yh[3]; au.yh[3] = y;

        for (j = 0; j < 2u; j++) {              /* two output samples: the step's, then the half-band midpoint */
            if (!j) yi = au.yh[1];
            else yi = (int32_t)((((int64_t)au.yh[1] + au.yh[2]) * 9 - ((int64_t)au.yh[0] + au.yh[3])) >> 4);
            if (au.xdir) {                      /* tablet change: 2.9 ms out, 2.9 ms in */
                au.xfade += au.xdir * 256;
                if (au.xfade >= 32767) { au.xfade = 32767; au.xdir = 0; }
                if (au.xfade < 0) au.xfade = 0;
            }
            yi = yi > 60000 ? 60000 : yi < -60000 ? -60000 : yi;
            yi = (yi * au.xfade) >> 15;
            yi = (yi * AMP_IN_Q15) >> 15;
            yi = speaker(valve(yi));            /* the amplifier, then the loudspeaker */
            yi = (yi * AMP_OUT_Q8) >> 8;
            yi = yi > 32767 ? 32767 : yi < -32767 ? -32767 : yi;
            au.dcy += yi * 256 - au.dcx - (au.dcy >> 9);   /* DC blocker, ~14 Hz, Q8 */
            au.dcx = yi * 256;
            o = au.dcy >> 8;
            if (o > 2 || o < -2 || au.run) au.quiet = 0;
            else if (au.quiet < IDLE_N) au.quiet++;
            if (o > 2 || o < -2) sounding = 1;
            out[2u * (i + j)] += o;
            out[2u * (i + j) + 1u] += o;
        }
    }
    a_sounding = (uint32_t)(sounding | au.run);
    if (au.quiet >= IDLE_N && !held && !au.run) {   /* rest: the filters back to zero, the gate shut */
        memset(&fm_st, 0, sizeof fm_st);
        memset(au.yh, 0, sizeof au.yh);
        memset(au.sx1, 0, sizeof au.sx1); memset(au.sx2, 0, sizeof au.sx2);
        memset(au.sy1, 0, sizeof au.sy1); memset(au.sy2, 0, sizeof au.sy2);
        au.hp = au.hpx = au.dcx = au.dcy = au.g = au.gstep = 0;
        au.x[0] = au.x[1] = au.x[2] = 1 << 30;
        au.s1 = 1 << 30;                        /* (at rest the screen capacitor is full again) */
        au.quiet = 0;
        au.idle = 1;
    }
    {                                           /* the load: this render's time against the 5.8 ms it may take */
        uint32_t pm = (fm1_ticks() - t0) / (HALF_FRAMES_TICKS / 1000u);
        a_load = pm;
        if (pm > a_load_peak) a_load_peak = pm;
    }
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
    uint8_t decay, decay_all, pad2[2];          /* OPTIONS: the pluck decay (0 = off, 1..4), on all notes */
    uint8_t pad3[4];                            /* (versions 6, 7: an arpeggiator, since removed) */
    uint8_t knee, glide, pad4[2];               /* OPTIONS: knee lever speed 0..4; portamento 0 = off, 1..6 */
} set;
#define SET_VER 9u
#define SET_V5_SIZE ((uint32_t)((uint8_t *)&set.pad3 - (uint8_t *)&set))   /* (versions 3..5 ended there) */
#define SET_V6_SIZE ((uint32_t)((uint8_t *)&set.knee - (uint8_t *)&set))   /* (version 6 there) */
#define KNEE_NORMAL 2u

static uint32_t lever = 48;                     /* the knee lever (not saved): 3/4 open at power-on */
static uint8_t cnt[128];                        /* how many keys / MIDI notes / the drone hold each note */
static int32_t top = -1, last_note = -1;

static void note_add(uint32_t n) { if (n < 128u && cnt[n] < 255u) cnt[n]++; }
static void note_del(uint32_t n) { if (n < 128u && cnt[n]) cnt[n]--; }

static void set_pitch(int32_t n)                /* the oscillator's step for a note, through the coupler */
{
    int32_t p = n + 12 * set.coupler;
    if (p < 0) p = 0;
    if (p > 131) p = 131;
    a_inc = (uint32_t)(((uint64_t)(INC_C9[p % 12] >> (10 - p / 12)) * TUNE_Q16[set.tune + 50]) >> 16);
}

static void update_voice(void)                  /* the highest held note wins, as the key ladder does */
{
    int32_t n;
    for (n = 127; n >= 0 && !cnt[n]; n--) ;
    top = n;
    if (n < 0) { a_held = 0; return; }
    last_note = n;
    set_pitch(n);
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
    /* in alphabetical order. EE: Electronic Engineering, Oct. 1952, Table 1 ("Med.-High" played MED; "O or V" as two
     * presets); SOS: the Selmer voicing tables as quoted by Sound On Sound, March 2007; ORGAN SUB: the Concert's Sub I
     * and Sub II 'f' on the raw tone, "an organ tone" (SOS) */
    {"ALTO SAX", T(2) | T(3) | V2, MED},                            /* SOS */
    {"ARABIAN FLUTE", T(1) | T(4) | T(8) | V1, HIGH},               /* SOS */
    {"BAGPIPE", T(1) | T(9) | V1, MED},                             /* SOS */
    {"BANJO", T(3) | T(4) | T(B) | T(P), MED},                      /* EE */
    {"BASS SAX", T(4) | V3, LOW},                                   /* SOS */
    {"BASS VIOLIN", T(1) | T(V) | V1, LOW},                         /* SOS */
    {"BASSOON", T(3) | T(7), LOW},                                  /* EE */
    {"CELLO", T(1) | T(V) | V2, LOW},                               /* SOS */
    {"CHURCH ORGAN A", T(4) | T(6), LOW},                           /* SOS */
    {"CHURCH ORGAN B", T(4) | T(9), LOW},                           /* SOS */
    {"CHURCH ORGAN C", T(6), LOW},                                  /* SOS */
    {"CLAVIOLINE TONE", T(3) | T(4) | T(6) | V3 | AMP, MED},        /* SOS; the preset at power-on */
    {"CORNET", T(6) | V1, MED},                                     /* EE */
    {"ELECTRIC GUITAR", T(4) | T(P) | V2, MED},                     /* SOS */
    {"ENGLISH HORN", T(2) | T(3) | T(B), MED},                      /* SOS */
    {"FIFE", T(B) | T(O), HIGH},                                    /* SOS */
    {"FLUTE", T(3) | T(4) | T(5) | V1, HIGH},                       /* EE */
    {"FRENCH HORN", T(3), LOW},                                     /* SOS */
    {"HARPSICHORD", T(3) | T(5) | T(6) | T(8) | T(P), HIGH},        /* SOS */
    {"HAWAIIAN GUITAR", T(1) | T(4) | T(6) | T(P) | V2 | AMP, MED}, /* EE */
    {"HORN", T(2) | T(3) | V3 | AMP, LOW},                          /* EE */
    {"HUNTING HORN", T(3) | V3 | AMP, LOW},                         /* SOS */
    {"MANDOLIN", T(3) | T(6) | T(8) | T(P), HIGH},                  /* EE */
    {"MUSICAL SAW", T(3) | T(B) | V2 | AMP, HIGH},                  /* EE */
    {"MUTED GYPSY VLN", T(1) | T(O) | V2 | AMP, MED},               /* SOS */
    {"OBOE", T(1) | T(4) | T(8) | V1, MED},                         /* EE */
    {"ORCH HORN", T(3) | V2, LOW},                                  /* SOS */
    {"ORGAN SUB", T(SUB1F) | T(SUB2F), MED},                        /* the Concert */
    {"PICCOLO", T(1) | T(4) | T(O) | V2, HIGH},                     /* EE */
    {"REED PIPE", T(B), HIGH},                                      /* SOS */
    {"TENOR SAX", T(4) | V3, MED},                                  /* EE */
    {"THEATRE ORGAN", T(4) | V3 | AMP, MED},                        /* SOS */
    {"TROMBONE", T(3) | V2, LOW},                                   /* EE */
    {"TRUMPET", V2, MED},                                           /* EE */
    {"VIOLA O", T(1) | T(O) | V2 | AMP, MED},                       /* EE */
    {"VIOLA V", T(1) | T(V) | V2 | AMP, MED},                       /* EE */
    {"VIOLIN O", T(1) | T(O) | V2 | AMP, HIGH},                     /* EE */
    {"VIOLIN V", T(1) | T(V) | V2 | AMP, HIGH},                     /* EE */
    {"VOX CELESTA", T(4) | T(5) | T(6) | V3 | AMP, MED},            /* SOS */
    {"ZITHER", T(1) | T(4) | T(6) | T(P) | V3 | AMP, MED},          /* SOS */
};
#undef T
#define NFACT (sizeof PRESET / sizeof PRESET[0])
#define NPRESET (NFACT + NUSER)
#define P_CLAVIOLINE 11u                        /* CLAVIOLINE TONE: every power-on starts here */
static uint32_t edited;                         /* the tablets differ from the preset shown */

static void apply_preset(uint32_t i)
{
    if (i < NFACT) { set.tabs = PRESET[i].tabs; set.coupler = PRESET[i].coupler; }
    else { set.tabs = set.user[i - NFACT].tabs; set.coupler = set.user[i - NFACT].coupler; }
    set.preset = (uint16_t)i;
    edited = 0;
}

/* ---- the screen: 1950s space age (deep-space navy, ivory tablets, a satellite, teal and orange lamps) ------ */
#define C_NAVY RGB(8, 16, 38)
#define C_PANEL RGB(18, 40, 62)
#define C_CHROME RGB(150, 172, 190)
#define C_IVORY RGB(238, 230, 206)
#define C_INK RGB(28, 24, 22)
#define C_ORANGE RGB(255, 142, 36)
#define C_TEAL RGB(56, 196, 186)
#define C_MINT RGB(160, 244, 222)
#define C_FAINT RGB(58, 80, 104)
#define C_RED RGB(214, 64, 52)
#define C_VIOLET RGB(150, 120, 210)
#define C_STAR RGB(206, 216, 255)
#define C_AMBER C_ORANGE

static void cv_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)   /* a filled circle */
{
    int32_t dy;
    for (dy = -r; dy <= r; dy++) {
        int32_t dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
        cv_rect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}

static void cv_tablet(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t band, int32_t on, const char *lab,
                      uint16_t bg)          /* an ivory tablet with a coloured band; pressed: lower, glowing orange */
{
    if (on) { y += 3; cv_rect(x - 1, y - 1, w + 2, h + 2, C_ORANGE); }
    cv_rect(x, y, w, h, on ? C_ORANGE : C_IVORY);
    cv_rect(x, y, w, 4, on ? C_IVORY : band);
    cv_pset(x, y, bg); cv_pset(x + w - 1, y, bg); cv_pset(x, y + h - 1, bg); cv_pset(x + w - 1, y + h - 1, bg);
    cv_text(x + (w - text_w(&FONT_S, lab)) / 2, y + (h - 16) / 2 + 2, &FONT_S, lab, C_INK);
}

static void draw_title_text(const char *t, uint16_t colour);
static void draw_title(uint32_t stops)
{
    draw_title_text(stops ? "STOPS" : "CLAVIOLINE", stops ? C_ORANGE : C_IVORY);
}

static void draw_title_text(const char *t, uint16_t colour)   /* stars, the title, a satellite, an orbit line */
{
    static const uint8_t STARS[][2] = {{6, 6}, {30, 30}, {52, 4}, {74, 33}, {98, 2}, {121, 35}, {145, 5}, {168, 31},
                                       {190, 3}, {212, 28}, {234, 9}, {14, 22}, {226, 34}, {110, 18}};
    int32_t i, tw = text_w(&FONT_L, t), x = (240 - tw) / 2 + 10;
    cv_begin(240, 40, C_NAVY);
    for (i = 0; i < (int32_t)(sizeof STARS / sizeof STARS[0]); i++) {
        cv_pset(STARS[i][0], STARS[i][1], C_STAR);
        if (i % 3 == 0) { cv_pset(STARS[i][0] - 1, STARS[i][1], C_FAINT); cv_pset(STARS[i][0] + 1, STARS[i][1], C_FAINT); }
    }
    {                                           /* the satellite: a sphere with a band and four antennae */
        int32_t sx = x - 17, sy = 18;
        cv_line(sx - 9, sy - 9, sx + 9, sy + 9, C_CHROME);
        cv_line(sx - 9, sy + 9, sx + 9, sy - 9, C_CHROME);
        cv_disc(sx, sy, 6, C_CHROME);
        cv_rect(sx - 6, sy - 1, 13, 2, C_TEAL);
        cv_pset(sx - 2, sy - 3, C_IVORY);
    }
    cv_text(x, 3, &FONT_L, t, colour);
    cv_line(x, 37, x + tw, 37, C_TEAL);
    cv_rect(x + tw + 3, 36, 3, 3, C_ORANGE);
    cv_blit(0, 0);
}

static void draw_rail(void)                     /* the tablets, in two rows, on a panel with chrome edges */
{
    static const char *const L1[14] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "O", "A", "B", "V", "P"};
    static const char *const L2[8] = {"III", "II", "I", "+", "Ip", "IIp", "If", "IIf"};   /* in the black-key order */
    static const uint8_t B2[8] = {T_VIB3, T_VIB2, T_VIB1, T_AMP, T_SUB1P, T_SUB2P, T_SUB1F, T_SUB2F};
    uint32_t i, tabs = set.tabs;
    cv_begin(240, 32, C_NAVY);                  /* the black-key tablets on top, the white-key ones below, as in STOPS */
    cv_rect(2, 1, 236, 31, C_PANEL);
    cv_rect(2, 0, 236, 1, C_CHROME);
    for (i = 0; i < 8u; i++)
        cv_tablet((i < 4u ? 7 : 15) + (int32_t)i * 27, 4, 24, 22, i < 4u ? C_TEAL : C_VIOLET,
                  (int32_t)(tabs >> B2[i] & 1u), L2[i], C_PANEL);
    cv_blit(0, 40);
    cv_begin(240, 40, C_NAVY);
    cv_rect(2, 0, 236, 38, C_PANEL);
    cv_rect(2, 38, 236, 1, C_CHROME);
    for (i = 0; i < 14u; i++)
        cv_tablet(8 + (int32_t)i * 16, 3, 14, 28, i == 13u ? C_ORANGE : C_RED, (int32_t)(tabs >> i & 1u), L1[i], C_PANEL);
    cv_blit(0, 72);
}

static void draw_preset(void)                   /* the preset in a capsule */
{
    char s[24];
    const char *p = set.preset < NFACT ? PRESET[set.preset].name : "USER";
    uint32_t i = 0;
    int32_t w;
    while (*p && i < 18u) s[i++] = *p++;
    if (set.preset >= NFACT) { s[i++] = ' '; s[i++] = (char)('1' + set.preset - NFACT); }
    if (edited) s[i++] = '*';
    s[i] = 0;
    w = text_w(&FONT_S, s) + 12;
    cv_begin(240, 26, C_NAVY);
    cv_disc(120 - w / 2, 13, 10, C_PANEL);
    cv_disc(120 + w / 2, 13, 10, C_PANEL);
    cv_rect(120 - w / 2, 3, w, 21, C_PANEL);
    cv_disc(120 - w / 2 - 16, 13, 2, C_TEAL);
    cv_disc(120 + w / 2 + 16, 13, 2, C_TEAL);
    cv_text(120 - (w - 12) / 2, 5, &FONT_S, s, C_IVORY);
    cv_blit(0, 112);
}

static void draw_note(void)                     /* the note, between two orbit marks */
{
    static const char *const NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char s[8] = "-";
    int32_t w;
    if (top >= 0) {
        int32_t p = top + 12 * set.coupler, i = 0;
        const char *q;
        if (p < 0) p = 0;
        for (q = NAME[p % 12]; *q; ) s[i++] = *q++;
        if (p >= 12) s[i++] = (char)('0' + p / 12 - 1);
        s[i] = 0;
    }
    w = text_w(&FONT_L, s);
    cv_begin(240, 34, C_NAVY);
    cv_line(40, 17, 120 - w / 2 - 12, 17, C_FAINT);
    cv_line(120 + w / 2 + 12, 17, 200, 17, C_FAINT);
    cv_disc(120 - w / 2 - 12, 17, 2, C_TEAL);
    cv_disc(120 + w / 2 + 12, 17, 2, C_TEAL);
    cv_text(120 - w / 2, 1, &FONT_L, s, C_MINT);
    cv_blit(0, 138);
}

static void draw_coupler(void)                  /* three lamps */
{
    static const char *const W[3] = {"LOW", "MED", "HIGH"};
    uint32_t i;
    cv_begin(240, 22, C_NAVY);
    for (i = 0; i < 3u; i++) {
        int32_t x = 22 + (int32_t)i * 74, on = (int32_t)i - 1 == set.coupler;
        cv_disc(x, 11, 6, on ? C_ORANGE : C_PANEL);
        if (on) { cv_pset(x - 2, 9, C_IVORY); cv_pset(x - 1, 8, C_IVORY); }
        cv_text(x + 11, 3, &FONT_S, W[i], on ? C_IVORY : C_FAINT);
    }
    cv_blit(0, 172);
}

static void draw_lever(void)                    /* the knee lever: a row of lamps, teal, mint, orange */
{
    int32_t i, lit = (int32_t)((lever * 20u + LEVER_MAX - 1u) / LEVER_MAX);
    cv_begin(240, 24, C_NAVY);
    cv_text(6, 4, &FONT_S, "KNEE", C_IVORY);
    for (i = 0; i < 20; i++) {
        uint16_t c = C_PANEL;
        if (i < lit) c = i < 12 ? C_TEAL : i < 16 ? C_MINT : C_ORANGE;   /* like a period level meter */
        cv_rect(52 + i * 9, 6, 7, 12, c);
    }
    cv_blit(0, 194);
}

static void draw_status(const char *msg, uint16_t colour)
{
    char s[32] = "KEYS F3-G5  TUNE +00";
    int32_t t = set.tune < 0 ? -set.tune : set.tune;
    const char *p = msg;
    if (!p) {
        s[6] = (char)('3' + set.window);
        s[9] = (char)('5' + set.window);
        s[17] = set.tune < 0 ? '-' : '+';
        s[18] = (char)('0' + t / 10);
        s[19] = (char)('0' + t % 10);
        p = s;
        colour = C_CHROME;
    }
    cv_begin(240, 22, C_NAVY);
    cv_rect(20, 0, 200, 1, C_PANEL);
    cv_text((240 - text_w(&FONT_S, p)) / 2, 4, &FONT_S, p, colour);
    cv_blit(0, 218);
}

#define NOPT 4u
static void draw_options(uint32_t sel)          /* OPTIONS: settings the original did not have */
{
    static const char *const NAME[NOPT] = {"PLUCK DECAY", "DECAY ON", "KNEE SPEED", "PORTAMENTO"};
    static const char *const KNEE[5] = {"FINE", "GENTLE", "NORMAL", "FAST", "VERY FAST"};
    static const char *const GLIDE[7] = {"OFF", "15 MS", "30 MS", "60 MS", "120 MS", "250 MS", "500 MS"};
    static const char *const DEC[5] = {"OFF", "0.3 S", "0.6 S", "1.2 S", "2.5 S"};
    uint32_t i;
    draw_title_text("OPTIONS", C_TEAL);
    for (i = 0; i < NOPT; i++) {
        const char *v = i == 0u ? DEC[set.decay] : i == 1u ? (set.decay_all ? "ALL NOTES" : "P NOTES")
                      : i == 2u ? KNEE[set.knee] : GLIDE[set.glide];
        int32_t on = i == sel;
        cv_begin(240, 36, C_NAVY);
        if (on) { cv_rect(4, 2, 232, 32, C_PANEL); cv_rect(4, 2, 3, 32, C_TEAL); }
        cv_text(16, 10, &FONT_S, NAME[i], on ? C_IVORY : C_FAINT);
        cv_disc(140, 18, 3, on ? C_ORANGE : C_PANEL);
        cv_text(150, 10, &FONT_S, v, on ? C_ORANGE : C_FAINT);
        cv_blit(0, 48 + (int32_t)i * 38);
    }
    cv_begin(240, 36, C_NAVY);
    cv_text(12, 2, &FONT_S, "SELECT CHOOSE  KNOB 1 CHANGE", C_CHROME);
    cv_text(12, 18, &FONT_S, "SEQ BACK", C_CHROME);
    cv_blit(0, 204);
}

static char *put_pct(char *p, uint32_t pm)      /* per mille -> "nn%" */
{
    uint32_t v = (pm + 5u) / 10u;
    if (v > 999u) v = 999u;
    if (v >= 100u) *p++ = (char)('0' + v / 100u);
    if (v >= 10u) *p++ = (char)('0' + v / 10u % 10u);
    *p++ = (char)('0' + v % 10u);
    *p++ = '%';
    *p = 0;
    return p;
}

/* ---- MIDI in ---------------------------------------------------------------------------------------- */
static uint32_t idle_ok, midi_dirty;
static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2) != 0) {      /* all channels */
        uint32_t ty = st & 0xF0u;
        if (ty == 0x90u && d2) { note_add(d1); a_pluck++; midi_dirty = 1; }
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
static const int8_t KEY_TAB[27] = {             /* key (0 = F3) -> tablet, -1 none */
    T_1, T_VIB3, T_2, T_VIB2, T_3, T_VIB1, T_4, T_5, T_AMP, T_6, -1, T_7, T_8, -1, T_9, -1, T_O,
    T_SUB1P, T_A, T_B, T_SUB2P, T_V, T_SUB1F, T_P, -1, T_SUB2F, -1};

static const uint8_t TEST_BTN[5] = {BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT};   /* STOPS: notes to try */
static const uint8_t TEST_NOTE[5] = {60, 62, 64, 65, 67};                            /* C4 D4 E4 F4 G4 */

static void app_main(void)
{
    uint32_t prev = 0, k, t_change = 0, dirty = 0, t_lever = 0, stops = 0, drone = 0, drone_note = 0;
    uint32_t net_tabs = ~0u, t_msg = 0, t_cpu = 0, cpu_over = 0, tnotes = 0;
    char cpu_txt[40] = "CPU";
    uint32_t drawn_note = ~0u, drawn_lever = ~0u, drawn_cpl = ~0u, drawn_st = ~0u, drawn_tabs = ~0u, drawn_pre = ~0u;
    uint32_t drawn_stops = ~0u, drawn_opt = ~0u, opts = 0, osel = 0;
    const char *msg = 0;
    uint8_t key_note[27] = {0};
    uint32_t kon = 0;                           /* keys whose note is in the stack */

    if (settings_load(&set, sizeof set) == 0 && (set.ver == 7u || set.ver == 8u)) {
        if (set.ver == 7u && set.glide) set.glide++;   /* (from 7: the glide times move up one, for the new 15 ms) */
        set.ver = SET_VER;                      /* from versions 7, 8: the knee speed back to NORMAL */
        set.knee = KNEE_NORMAL;
    } else if (settings_load(&set, sizeof set) == 0) {
    } else if ((settings_load(&set, SET_V6_SIZE) == 0 && set.ver == 6u)
               || (settings_load(&set, SET_V5_SIZE) == 0 && set.ver >= 3u && set.ver <= 5u)) {
        set.ver = SET_VER;                      /* from versions 3..6: keep the slots, tuning, window */
        set.decay = 0; set.knee = KNEE_NORMAL; set.glide = 0;
    }
    memset(set.pad3, 0, sizeof set.pad3);
    if (set.ver != SET_VER || set.coupler < -1 || set.coupler > 1 || set.window > 1u || set.tune < -50 || set.tune > 50
        || set.preset >= NPRESET || set.decay > 4u || set.decay_all > 1u || set.knee > 4u || set.glide > 6u) {
        memset(&set, 0, sizeof set);            /* nothing usable stored: afresh (pluck decay off, no portamento) */
        set.ver = SET_VER;
        set.knee = KNEE_NORMAL;
    }
    apply_preset(P_CLAVIOLINE);                 /* every power-on starts on CLAVIOLINE TONE (user slots are kept) */
    set.decay = 0;                              /* ... with the pluck decay and the portamento off */
    set.glide = 0;
    fm_compute(&fm_buf[0], set.tabs);           /* the formant filter before the sound starts */
    net_tabs = set.tabs & NET_TABS;
    a_tabs = set.tabs;
    a_lever = LEVER_GAIN[lever];

    lcd_fill(0, 0, 240, 240, C_NAVY);
    msg = "BUILD " APP_BUILD;                   /* which firmware this is, for the first seconds */
    t_msg = fm1_ms;

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
                    if (t != -1) { edited = 1; dirty = 1; t_change = fm1_ms; }
                }
                continue;
            }
            if (up >> k & 1u && kon >> k & 1u) { note_del(key_note[k]); kon &= ~(1u << k); midi_send(0x80u, key_note[k], 0); changed = 1; }
            if (down >> k & 1u) { key_note[k] = (uint8_t)(base + k); note_add(key_note[k]); kon |= 1u << k; a_pluck++; midi_send(0x90u, key_note[k], 100); changed = 1; }
        }

        b = fm1_input_edges(&rel);
        if (stops) {                            /* FX SEL ENV LFO EDIT play C4 D4 E4 F4 G4, to try the tablets */
            for (k = 0; k < 5u; k++) {
                if (rel >> TEST_BTN[k] & 1u && tnotes >> k & 1u) { note_del(TEST_NOTE[k]); tnotes &= ~(1u << k); changed = 1; }
                if (b >> TEST_BTN[k] & 1u && !(tnotes >> k & 1u)) { note_add(TEST_NOTE[k]); tnotes |= 1u << k; a_pluck++; changed = 1; }
            }
            b &= ~(1u << BTN_FX | 1u << BTN_SEL | 1u << BTN_ENV | 1u << BTN_LFO | 1u << BTN_EDIT);
        }
        if (b & (1u << BTN_SEQ) || (opts && b & (1u << BTN_HOME))) {     /* OPTIONS (not on the original) */
            opts = b & (1u << BTN_SEQ) ? !opts : 0;
            b &= ~(1u << BTN_HOME);
            lcd_fill(0, 0, 240, 240, C_NAVY);
            drawn_stops = drawn_tabs = drawn_pre = drawn_note = drawn_cpl = drawn_lever = drawn_st = drawn_opt = ~0u;
        }
        if (b & (1u << BTN_GLO) || (stops && b & (1u << BTN_HOME))) {   /* PLAY <-> STOPS */
            uint32_t was = stops;
            stops = b & (1u << BTN_GLO) ? !stops : 0;
            if (stops && !was)                  /* the keys held now stop sounding: they become tablets */
                for (k = 0; k < 27u; k++)
                    if (kon >> k & 1u) { note_del(key_note[k]); midi_send(0x80u, key_note[k], 0); }
            if (stops) kon = 0;
            if (!stops && drone) { note_del(drone_note); drone = 0; }
            if (!stops) {                       /* the test notes end with STOPS mode */
                for (k = 0; k < 5u; k++)
                    if (tnotes >> k & 1u) note_del(TEST_NOTE[k]);
                tnotes = 0;
            }
            changed = 1;
        }
        if (stops && b & (1u << BTN_PLAY)) {    /* the drone: hold the last note while editing */
            if (drone) { note_del(drone_note); drone = 0; }
            else if (last_note >= 0) { drone_note = (uint32_t)last_note; note_add(drone_note); a_pluck++; drone = 1; }
            changed = 1;
        }
        if (b & (1u << BTN_OCT_DOWN) && set.coupler > -1) { set.coupler--; edited = 1; dirty = 1; changed = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.coupler < 1) { set.coupler++; edited = 1; dirty = 1; changed = 1; t_change = fm1_ms; }
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
        if (opts && (e = fm1_enc_take(ENC_SELECT)) != 0) osel = (uint32_t)(((int32_t)osel + e % (int32_t)NOPT + (int32_t)NOPT) % (int32_t)NOPT);
        if (opts && (e = fm1_enc_take(ENC_KNOB1)) != 0) {
            int32_t d;
            if (osel == 0u) { d = (int32_t)set.decay + e; set.decay = (uint8_t)(d < 0 ? 0 : d > 4 ? 4 : d); }
            else if (osel == 1u) set.decay_all ^= 1u;
            else if (osel == 2u) { d = (int32_t)set.knee + e; set.knee = (uint8_t)(d < 0 ? 0 : d > 4 ? 4 : d); }
            else { d = (int32_t)set.glide + e; set.glide = (uint8_t)(d < 0 ? 0 : d > 6 ? 6 : d); }
            dirty = 1; t_change = fm1_ms;
        }
        if (!opts && (e = fm1_enc_take(ENC_SELECT)) != 0) {
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
        if (!opts && (e = fm1_enc_take(ENC_KNOB1)) != 0) {
            int32_t t = set.tune + e;
            set.tune = (int8_t)(t < -50 ? -50 : t > 50 ? 50 : t);
            dirty = 1; changed = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {   /* the knee lever: faster turning, bigger steps */
            uint32_t dt = fm1_ms - t_lever;
            static const uint8_t SPEED[5][3] = {   /* (OPTIONS) steps per detent: slow / medium / fast turning */
                {1, 1, 1}, {1, 1, 2}, {1, 2, 4}, {2, 4, 8}, {3, 6, 12}};
            int32_t l = (int32_t)lever + e * SPEED[set.knee][dt < 30u ? 2 : dt < 70u ? 1 : 0];
            t_lever = fm1_ms;
            lever = (uint32_t)(l < 0 ? 0 : l > (int32_t)LEVER_MAX ? (int32_t)LEVER_MAX : l);
        }

        midi_in();
        if (changed || midi_dirty) { midi_dirty = 0; update_voice(); }
        a_tabs = set.tabs;
        a_decay = set.decay ? DECAY_Q30[set.decay - 1u] : 0u;
        a_decay_all = set.decay_all;
        a_glide = set.glide ? GLIDE_Q16[set.glide - 1u] : 0u;

        a_lever = LEVER_GAIN[lever > LEVER_MAX ? LEVER_MAX : lever];
        /* a new formant filter when the tablets changed (and the ISR has taken the last one) */
        if (!fm_pending && (set.tabs & NET_TABS) != net_tabs) {
            fm_compute(&fm_buf[fm_cur ^ 1u], set.tabs);
            net_tabs = set.tabs & NET_TABS;
            fm_pending = 1;
        }

        if (stops)                              /* key LEDs: the tablets down, or the key that sounds */
            for (k = 0; k < 27u; k++)
                fm1_led_key(KEY_LED(k), KEY_TAB[k] >= 0 && (set.tabs >> KEY_TAB[k] & 1u));
        else
            for (k = 0; k < 27u; k++)
                fm1_led_key(KEY_LED(k), top >= 0 && (uint32_t)top == base + k);
        fm1_led_key(BTN_OCT_DOWN, set.coupler < 0);
        fm1_led_key(BTN_OCT_UP, set.coupler > 0);
        if (stops) {                            /* the five note buttons glow, and light while held */
            for (k = 0; k < 5u; k++) {
                if (tnotes >> k & 1u) fm1_led_key(TEST_BTN[k], 1);
                else { fm1_led_key(TEST_BTN[k], 0); fm1_led_key_glow(TEST_BTN[k], 1); }
            }
        } else {                                /* PLAY: those buttons do nothing, and stay dark */
            for (k = 0; k < 5u; k++) { fm1_led_key_glow(TEST_BTN[k], 0); fm1_led_key(TEST_BTN[k], 0); }
        }
        fm1_led_key(BTN_GLO, stops);
        fm1_led_key(BTN_SEQ, opts);
        fm1_led_key(BTN_PLAY, drone);

        if (msg && fm1_ms - t_msg > 1500u) { msg = 0; drawn_st = ~0u; }
        if (fm1_ms - t_cpu >= 1000u) {          /* the audio load, once a second: shown in STOPS, or when it runs out */
            char *q = cpu_txt;
            const char *w = "CPU ";
            uint32_t pk = a_load_peak;
            a_load_peak = 0;
            t_cpu = fm1_ms;
            cpu_over = pk > 900u;
            if (cpu_over) w = "AUDIO OVERLOAD ";
            while (*w) *q++ = *w++;
            q = put_pct(q, a_load);
            w = "  PEAK ";
            while (*w) *q++ = *w++;
            put_pct(q, pk);
            if (stops || cpu_over) drawn_st = ~0u;
        }
        idle_ok = 1;
        if (opts) {
            uint32_t st = osel | (uint32_t)set.decay << 4 | (uint32_t)set.decay_all << 8 | (uint32_t)set.knee << 9
                        | (uint32_t)set.glide << 12;
            if (st != drawn_opt) { drawn_opt = st; draw_options(osel); }
        } else {
        if (stops != drawn_stops) { drawn_stops = stops; draw_title(stops); drawn_st = ~0u; }
        if (set.tabs != drawn_tabs) { drawn_tabs = set.tabs; draw_rail(); }
        if ((set.preset | edited << 15) != drawn_pre) { drawn_pre = set.preset | edited << 15; draw_preset(); }
        if (((uint32_t)(top + 1) | (uint32_t)(set.coupler + 1) << 8) != drawn_note) {
            drawn_note = (uint32_t)(top + 1) | (uint32_t)(set.coupler + 1) << 8;
            draw_note();
        }
        if ((uint32_t)(set.coupler + 1) != drawn_cpl) { drawn_cpl = (uint32_t)(set.coupler + 1); draw_coupler(); }
        if (lever != drawn_lever) { drawn_lever = lever; draw_lever(); }
        if (msg && drawn_st != 0xFFFFFFFEu) { drawn_st = 0xFFFFFFFEu; draw_status(msg, C_AMBER); }
        else if (!msg && (stops || cpu_over)) {
            if (drawn_st != 0xFFFFFFFDu) { drawn_st = 0xFFFFFFFDu; draw_status(cpu_txt, cpu_over ? C_AMBER : C_CHROME); }
        } else if (!msg && ((uint32_t)set.window | (uint32_t)(set.tune + 50) << 1) != drawn_st) {
            drawn_st = (uint32_t)set.window | (uint32_t)(set.tune + 50) << 1;
            draw_status(0, C_CHROME);
        }
        }
        idle_ok = 0;

        if (dirty && !a_sounding && fm1_ms - t_change > 2000u) {   /* save when silent (an erase stalls the sound) */
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
