/* SPDX-License-Identifier: GPL-3.0-only */
/* The tone-forming network (Fig. 4) as a formant filter: five biquads per tablet combination.
 *
 * tools/fit_formants.py fits the circuit model's response for each of the 8192 combinations of tablets 1..9, O, A,
 * B, V with five second-order sections and stores them (clav_formants.h) as zero and pole frequencies and Qs.
 * When a tablet changes, fm_compute() turns them into biquads (bilinear transform at 22.05 kHz) in a small software
 * floating point (the chip has no FPU, no 64-bit division); the audio interrupt then runs fm_run(), integer only.
 * Each section is scaled to unit gain at its peak so the fixed point keeps its headroom; the overall gain follows. */
#pragma once
#include <stdint.h>

/* tablet bits (the formant filter uses 0..12) */
enum { T_1, T_2, T_3, T_4, T_5, T_6, T_7, T_8, T_9, T_O, T_A, T_B, T_V, T_P, T_VIB1, T_VIB2, T_VIB3, T_AMP,
       T_SUB1P, T_SUB2P, T_SUB1F, T_SUB2F, T_COUNT };
#define TB(t) (1u << (t))
#define NET_TABS 0x1FFFu

/* ---- a small software float: m * 2^e, |m| in [2^29, 2^30) (or 0) ------------------------------------ */
typedef struct { int32_t m, e; } sf_t;

static sf_t sf_norm(int64_t x, int32_t e)
{
    sf_t r = {0, 0};
    int64_t a = x < 0 ? -x : x;
    if (!x) return r;
    while (a >= (1LL << 30)) { a >>= 1; e++; }
    while (a < (1LL << 29)) { a <<= 1; e--; }
    r.m = (int32_t)(x < 0 ? -a : a);
    r.e = e;
    return r;
}
static sf_t sf_mul(sf_t a, sf_t b) { return sf_norm((int64_t)a.m * b.m, a.e + b.e); }
static sf_t sf_add(sf_t a, sf_t b)
{
    int32_t d;
    if (!a.m) return b;
    if (!b.m) return a;
    if (a.e < b.e) { sf_t t = a; a = b; b = t; }
    d = a.e - b.e;
    if (d > 50) return a;
    return sf_norm(((int64_t)a.m << 20) + (((int64_t)b.m << 20) >> d), a.e - 20);
}
static sf_t sf_neg(sf_t a) { a.m = -a.m; return a; }
static sf_t sf_recip(sf_t b)                    /* 1 / b, b != 0: a 16-bit guess, two Newton steps */
{
    int64_t m = b.m < 0 ? -(int64_t)b.m : b.m, r, err;
    uint32_t q = 0xFFFFFFFFu / (uint32_t)(m >> 14);
    int k;
    r = (int64_t)q << 13;
    for (k = 0; k < 2; k++) {
        err = (1LL << 59) - m * r;
        r += (r * (err >> 29)) >> 30;
    }
    return sf_norm(b.m < 0 ? -r : r, -59 - b.e);
}
static sf_t sf_div(sf_t a, sf_t b) { return sf_mul(a, sf_recip(b)); }
static int sf_gt(sf_t a, sf_t b)                /* a > b, both >= 0 */
{
    if (!a.m) return 0;
    if (!b.m) return 1;
    return a.e != b.e ? a.e > b.e : a.m > b.m;
}
static sf_t sf_sqrt(sf_t a)                     /* a >= 0 */
{
    uint64_t x, r, b;
    int32_t e = a.e;
    if (a.m <= 0) { sf_t z = {0, 0}; return z; }
    x = (uint64_t)a.m;
    if (e & 1) { x <<= 1; e--; }                /* even exponent */
    x <<= 30;                                   /* sqrt(x * 2^30) = sqrt(x) * 2^15 */
    r = 0;
    for (b = 1ULL << 62; b > x; b >>= 2) ;
    while (b) {                                 /* integer square root, bit by bit */
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return sf_norm((int64_t)r, e / 2 - 15);
}
static int32_t sf_to_q(sf_t a, int32_t q)       /* a * 2^q as int32, saturated */
{
    int32_t sh = a.e + q;
    if (!a.m) return 0;
    if (sh >= 2) return a.m < 0 ? -2147483647 : 2147483647;
    if (sh == 1) {
        int64_t x = (int64_t)a.m * 2;
        return x > 2147483647 ? 2147483647 : x < -2147483647 ? -2147483647 : (int32_t)x;
    }
    if (sh <= -31) return 0;
    if (sh == 0) return a.m;
    return (int32_t)(((int64_t)a.m + (1LL << (-sh - 1))) >> -sh);
}
static sf_t sf_int(int32_t v) { return sf_norm(v, 0); }
static sf_t sf_exp2(int32_t num, int32_t den_log2)   /* 2^(num / 2^den_log2), den_log2 <= 8 */
{
    int32_t q = num * (1 << (8 - den_log2));    /* in 1/256 octave */
    int32_t ip = q >> 8, fr = q & 255;          /* (an arithmetic shift: floor) */
    return sf_norm(EXP2_Q30[fr], ip - 30);
}
static sf_t sf_exp2_w(uint32_t L)               /* a stored frequency: 2^(L / 1600), interpolated */
{
    uint32_t ip = L / 1600u, r = (L % 1600u) * 256u, i = r / 1600u, sub = r % 1600u;
    int32_t v = EXP2_Q30[i] + (((EXP2_Q30[i + 1u] - EXP2_Q30[i]) >> 4) * (int32_t)sub) / 100;   /* (32-bit division) */
    return sf_norm(v, (int32_t)ip - 30);
}

/* ---- decoding one combination into biquads --------------------------------------------------------------- */
#define FM_NSEC FORMANT_NSEC
typedef struct {
    int32_t b[FM_NSEC][3], a[FM_NSEC][2];       /* Q28 */
    int32_t gq, gsh;                            /* overall gain: out = (cascade * gq) >> gsh */
    uint32_t tabs;
} fm_coef_t;

static sf_t fm_mag2(sf_t b0, sf_t b1, sf_t b2, sf_t a1, sf_t a2, sf_t c, sf_t s)   /* |H(e^jw)|^2, c = cos w, s = sin w */
{
    sf_t two = sf_int(2), c2 = sf_add(sf_mul(two, sf_mul(c, c)), sf_neg(sf_int(1))), s2 = sf_mul(two, sf_mul(s, c));
    sf_t nr = sf_add(b0, sf_add(sf_mul(b1, c), sf_mul(b2, c2))), ni = sf_add(sf_mul(b1, s), sf_mul(b2, s2));
    sf_t dr = sf_add(sf_int(1), sf_add(sf_mul(a1, c), sf_mul(a2, c2))), di = sf_add(sf_mul(a1, s), sf_mul(a2, s2));
    return sf_div(sf_add(sf_mul(nr, nr), sf_mul(ni, ni)), sf_add(sf_mul(dr, dr), sf_mul(di, di)));
}

static void fm_cs(sf_t w, sf_t K, sf_t *c, sf_t *s)   /* the digital angle of analog w: tan(theta/2) = w / K */
{
    sf_t w2 = sf_mul(w, w), K2 = sf_mul(K, K), den = sf_recip(sf_add(K2, w2));
    *c = sf_mul(sf_add(K2, sf_neg(w2)), den);
    *s = sf_mul(sf_mul(sf_int(2), sf_mul(K, w)), den);
}

static void fm_compute(fm_coef_t *f, uint32_t tabs)
{
    const uint8_t *row = FORMANT[tabs & NET_TABS];
    sf_t K = sf_int(44100), K2 = sf_mul(K, K), two = sf_int(2), g;
    uint32_t k;
    g = sf_exp2((int32_t)(row[0] | row[1] << 8) - 32768, 8);   /* (log2(g) + 128) * 256 */
    for (k = 0; k < FM_NSEC; k++) {
        const uint8_t *p = row + 2 + 6 * k;
        sf_t wz = sf_exp2_w((uint32_t)(p[0] | p[1] << 8)), qz = sf_exp2((int32_t)p[2] - 128, 3);   /* Q: (log2 + 16) * 8 */
        sf_t wp = sf_exp2_w((uint32_t)(p[3] | p[4] << 8)), qp = sf_exp2((int32_t)p[5] - 128, 3);
        sf_t nz, np_, B0, B1, B2, A0, A1, A2, b0, b1, b2, a1, a2, ia0, m, mm, c, s;
        nz = sf_mul(K, sf_div(wz, qz));
        np_ = sf_mul(K, sf_div(wp, qp));
        B0 = sf_add(sf_add(K2, nz), sf_mul(wz, wz));
        B1 = sf_mul(two, sf_add(sf_mul(wz, wz), sf_neg(K2)));
        B2 = sf_add(sf_add(K2, sf_neg(nz)), sf_mul(wz, wz));
        A0 = sf_add(sf_add(K2, np_), sf_mul(wp, wp));
        A1 = sf_mul(two, sf_add(sf_mul(wp, wp), sf_neg(K2)));
        A2 = sf_add(sf_add(K2, sf_neg(np_)), sf_mul(wp, wp));
        ia0 = sf_recip(A0);
        b0 = sf_mul(B0, ia0); b1 = sf_mul(B1, ia0); b2 = sf_mul(B2, ia0);
        a1 = sf_mul(A1, ia0); a2 = sf_mul(A2, ia0);
        /* the section's largest gain, near enough: at DC, at Nyquist, at its pole and zero frequencies */
        m = fm_mag2(b0, b1, b2, a1, a2, sf_int(1), sf_int(0));
        mm = fm_mag2(b0, b1, b2, a1, a2, sf_int(-1), sf_int(0)); if (sf_gt(mm, m)) m = mm;
        fm_cs(wp, K, &c, &s); mm = fm_mag2(b0, b1, b2, a1, a2, c, s); if (sf_gt(mm, m)) m = mm;
        fm_cs(wz, K, &c, &s); mm = fm_mag2(b0, b1, b2, a1, a2, c, s); if (sf_gt(mm, m)) m = mm;
        m = sf_sqrt(m);
        if (m.m) {
            sf_t im = sf_recip(m);
            b0 = sf_mul(b0, im); b1 = sf_mul(b1, im); b2 = sf_mul(b2, im);
            g = sf_mul(g, m);
        }
        f->b[k][0] = sf_to_q(b0, 28); f->b[k][1] = sf_to_q(b1, 28); f->b[k][2] = sf_to_q(b2, 28);
        f->a[k][0] = sf_to_q(a1, 28); f->a[k][1] = sf_to_q(a2, 28);
    }
    /* overall gain g = gq * 2^-gsh, gq in [2^29, 2^30); the cascade runs on the input * 64 */
    f->gq = g.m;
    f->gsh = -g.e + 6;
    if (f->gsh < 0) { f->gq = 2147483647; f->gsh = 0; }
    if (f->gsh > 62) { f->gq = 0; f->gsh = 0; }
    f->tabs = tabs & NET_TABS;
}

/* ---- running it (ISR) ------------------------------------------------------------------------------------ */
typedef struct { int32_t x1[FM_NSEC], x2[FM_NSEC], y1[FM_NSEC], y2[FM_NSEC]; } fm_state_t;

static int32_t fm_run(fm_state_t *st, const fm_coef_t *f, int32_t x)   /* Q15 in, Q15 out */
{
    uint32_t k;
    int32_t v = x * 64;
    for (k = 0; k < FM_NSEC; k++) {
        int64_t acc = (int64_t)f->b[k][0] * v + (int64_t)f->b[k][1] * st->x1[k] + (int64_t)f->b[k][2] * st->x2[k]
                    - (int64_t)f->a[k][0] * st->y1[k] - (int64_t)f->a[k][1] * st->y2[k];
        int64_t y = (acc + (1LL << 27)) >> 28;
        int32_t yo = y > 2147483647 ? 2147483647 : y < -2147483647 ? -2147483647 : (int32_t)y;
        st->x2[k] = st->x1[k]; st->x1[k] = v;
        st->y2[k] = st->y1[k]; st->y1[k] = yo;
        v = yo;
    }
    {
        int64_t y = ((int64_t)v * f->gq) >> f->gsh;
        return y > 2147483647 ? 2147483647 : y < -2147483647 ? -2147483647 : (int32_t)y;
    }
}
