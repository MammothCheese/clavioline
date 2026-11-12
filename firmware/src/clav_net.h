/* SPDX-License-Identifier: GPL-3.0-only */
/* The tone-forming network of Fig. 4 (Electronic Engineering, Oct. 1952) on the chip.
 *
 * The netlist is tools/clav_net_ref.py's, element for element. Every capacitor and inductor is a trapezoidal
 * (bilinear) companion model, so the network runs as   G v = J   each sample: J from the elements' histories and
 * the source, v = G^-1 J, then the histories move on. This is exact for the netlist (frequency warping near Nyquist
 * aside); tests/net_check.py compares it with the reference.
 *
 * G^-1 depends on the tablets and the knee lever (R32), so net_compute() rebuilds it in the main loop when they
 * change, with a small software floating point (no FPU, no 64-bit division). The ISR (net_step) is integer only.
 * Units: voltage 2^-16 V, current 2^-16 uA, conductance uS Q12 (inductors Q20), G^-1 MOhm. */
#pragma once
#include <stdint.h>

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
    r = (int64_t)q << 13;                       /* ~ 2^59 / m */
    for (k = 0; k < 2; k++) {
        err = (1LL << 59) - m * r;
        r += (r * (err >> 29)) >> 30;
    }
    return sf_norm(b.m < 0 ? -r : r, -59 - b.e);
}
static int sf_absgt(sf_t a, sf_t b)             /* |a| > |b| */
{
    int32_t am = a.m < 0 ? -a.m : a.m, bm = b.m < 0 ? -b.m : b.m;
    if (!am) return 0;
    if (!bm) return 1;
    return a.e != b.e ? a.e > b.e : am > bm;
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

/* ---- the elements ------------------------------------------------------------------------------------ */
#define ND_GND NET_N                            /* ground: an extra node held at 0 */
enum { E_R5, E_C5, E_CS, E_C17, E_R29, E_R30, E_C28, E_R31, E_C29, E_R32, E_RIN, E_RPL5, E_RPA, E_RPB, E_RPC,
       E_SV, E_CSH, E_R28, E_CN1, E_GN1, E_CN3, E_C27, E_SB, E_COUT, NE };
static const uint8_t NE_P[NE] = {ND_A, ND_A, ND_IN, ND_IN, ND_a, ND_NV, ND_IN, ND_IN, ND_b, ND_IN, ND_OUT, ND_OUT,
                                 ND_IN, ND_NB, ND_N3, ND_NV, ND_N1, ND_OUT, ND_N1, ND_N1, ND_N3, ND_NB, ND_NB, ND_OUT};
static const uint8_t NE_Q[NE] = {ND_GND, ND_IN, ND_OUT, ND_a, ND_NV, ND_GND, ND_GND, ND_b, ND_GND, ND_GND, ND_GND,
                                 ND_N1, ND_NB, ND_N1, ND_GND, ND_GND, ND_GND, ND_N1, ND_GND, ND_GND, ND_GND, ND_GND,
                                 ND_GND, ND_GND};
static const uint8_t NE_CAP[] = {E_C5, E_CS, E_C17, E_C28, E_C29, E_CSH, E_CN1, E_CN3, E_C27, E_COUT};
#define NCAP (sizeof NE_CAP / sizeof NE_CAP[0])
static const uint8_t NW_P[3] = {ND_IN, ND_NB, ND_GND}, NW_Q[3] = {ND_NB, ND_N1, ND_N3};   /* La, Lb, Lc */

/* tablet bits (the network uses 0..12) */
enum { T_1, T_2, T_3, T_4, T_5, T_6, T_7, T_8, T_9, T_O, T_A, T_B, T_V, T_P, T_VIB1, T_VIB2, T_VIB3, T_AMP,
       T_SUB1P, T_SUB2P, T_SUB1F, T_SUB2F, T_COUNT };
#define TB(t) (1u << (t))
#define NET_TABS 0x1FFFu

/* DK form: v = KC xc + KL hl + KU isrc. xc = the capacitors' states in volts (history / g), hl = L5's and the
 * windings' history currents. No large companion currents cancel, so the integer ISR keeps its precision at any
 * lever position. */
typedef struct {
    int32_t kc[NET_N][NCAP];                    /* dimensionless, Q28 */
    int32_t kl[NET_N][4];                       /* MOhm, Q28: L5, La, Lb, Lc */
    int32_t ku[NET_N];                          /* MOhm, Q28: the source at node A */
    int32_t g[NE];                              /* uS, Q12 (0: not in circuit) */
    uint32_t tabs;
} net_coef_t;

static void net_compute(net_coef_t *c, uint32_t tabs, uint32_t lever)
{
    static sf_t a[NET_N][2 * NET_N];
    int64_t G[NET_N + 1][NET_N + 1];
    int32_t *g = c->g;
    uint32_t i, j, k, n = NET_N;
#define S(t) ((tabs >> (t)) & 1u)
    g[E_R5] = NG_R5; g[E_C5] = NG_C5;
    g[E_CS] = NG_C30 + (int32_t)S(T_7) * NG_C24 + (int32_t)S(T_8) * NG_C25 + (int32_t)S(T_9) * NG_C26;
    g[E_C17] = NG_C17; g[E_R29] = NG_R29; g[E_R30] = NG_R30; g[E_C28] = NG_C28; g[E_R31] = NG_R31; g[E_C29] = NG_C29;
    g[E_R32] = NET_G_R32[lever > LEVER_MAX ? LEVER_MAX : lever];
    g[E_RIN] = NG_RIN; g[E_RPL5] = NG_RP_L5; g[E_RPA] = NG_RPA; g[E_RPB] = NG_RPB; g[E_RPC] = NG_RPC;
    g[E_SV] = S(T_V) ? NG_SHORT : 0;
    g[E_CSH] = (int32_t)S(T_1) * NG_C18 + (int32_t)S(T_2) * NG_C19;
    g[E_R28] = S(T_O) ? NG_R28 : 0;
    g[E_CN1] = (int32_t)S(T_3) * NG_C20 + (int32_t)S(T_4) * NG_C21;
    g[E_GN1] = (int32_t)S(T_3) * NG_R26 + (int32_t)S(T_4) * NG_R27;
    g[E_CN3] = (int32_t)S(T_5) * NG_C22 + (int32_t)S(T_6) * NG_C23;
    g[E_C27] = S(T_A) ? NG_C27 : 0;
    g[E_SB] = S(T_B) ? NG_SHORT : 0;
    g[E_COUT] = NG_COUT;
#undef S
    c->tabs = tabs & NET_TABS;

    for (i = 0; i <= n; i++)                    /* the conductance matrix, exact in integers (uS Q20) */
        for (j = 0; j <= n; j++) G[i][j] = 0;
    for (i = 0; i < n; i++) G[i][i] += (int64_t)NG_LEAK << 8;
#define STAMP(p, q, y) do { G[p][p] += (y); G[q][q] += (y); G[p][q] -= (y); G[q][p] -= (y); } while (0)
    for (k = 0; k < NE; k++)
        if (g[k]) STAMP(NE_P[k], NE_Q[k], (int64_t)g[k] << 8);
    STAMP(ND_OUT, ND_N1, NET_G_L5);
    for (k = 0; k < 3u; k++)
        for (j = 0; j < 3u; j++) {
            int64_t y = NET_GM[k][j];
            G[NW_P[k]][NW_P[j]] += y; G[NW_Q[k]][NW_Q[j]] += y;
            G[NW_P[k]][NW_Q[j]] -= y; G[NW_Q[k]][NW_P[j]] -= y;
        }
#undef STAMP

    for (i = 0; i < n; i++)                     /* [G | I], Gauss-Jordan with partial pivoting, in software float */
        for (j = 0; j < 2u * n; j++)
            a[i][j] = j < n ? sf_norm(G[i][j], -20) : sf_norm(i == j - n, 0);
    for (k = 0; k < n; k++) {
        uint32_t piv = k;
        sf_t inv;
        for (i = k + 1; i < n; i++)
            if (sf_absgt(a[i][k], a[piv][k])) piv = i;
        if (piv != k)
            for (j = 0; j < 2u * n; j++) { sf_t t = a[k][j]; a[k][j] = a[piv][j]; a[piv][j] = t; }
        inv = sf_recip(a[k][k]);
        for (j = 0; j < 2u * n; j++) a[k][j] = sf_mul(a[k][j], inv);
        for (i = 0; i < n; i++)
            if (i != k && a[i][k].m) {
                sf_t f = sf_neg(a[i][k]);
                for (j = 0; j < 2u * n; j++) a[i][j] = sf_add(a[i][j], sf_mul(f, a[k][j]));
            }
    }
    /* G was uS, so G^-1 (a[.][n..2n-1]) is MOhm. A history current h between p and q enters J as -h at p, +h at q. */
#define GI(r, col) ((col) == ND_GND ? zero : a[r][n + (col)])
    {
        sf_t zero = {0, 0};
        for (i = 0; i < n; i++) {
            for (k = 0; k < NCAP; k++) {
                uint32_t e = NE_CAP[k];
                sf_t d = sf_add(GI(i, NE_Q[e]), sf_neg(GI(i, NE_P[e])));
                c->kc[i][k] = sf_to_q(sf_mul(d, sf_norm(g[e], -12)), 28);
            }
            c->kl[i][0] = sf_to_q(sf_add(GI(i, ND_N1), sf_neg(GI(i, ND_OUT))), 28);
            for (k = 0; k < 3u; k++)
                c->kl[i][k + 1u] = sf_to_q(sf_add(GI(i, NW_Q[k]), sf_neg(GI(i, NW_P[k]))), 28);
            c->ku[i] = sf_to_q(GI(i, ND_A), 28);
        }
    }
#undef GI
}

/* ---- the running network (ISR) ------------------------------------------------------------------------ */
/* States are int32: at three times the loudest source (white noise) they peak near 2^22 (capacitors, volts) and
 * 2^28.4 (inductor currents); tests/net_test.c NET_MAX=1 measures it. Stores saturate all the same. */
typedef struct {
    int32_t xc[NCAP];                           /* capacitor states, volts (2^-16 V) */
    int32_t hl[4];                              /* L5, La, Lb, Lc history currents (2^-16 uA) */
    int32_t v[NET_N + 1];                       /* node voltages (v[ND_GND] = 0) */
} net_state_t;

static int32_t sat32(int64_t x) { return x > 2147483647 ? 2147483647 : x < -2147483647 ? -2147483647 : (int32_t)x; }

#define RSH(x, n) (((x) + ((int64_t)1 << ((n) - 1))) >> (n))   /* shift with rounding */

static void net_adopt(net_state_t *s, const net_coef_t *c)   /* elements leaving the circuit forget their state */
{
    uint32_t k;
    for (k = 0; k < NCAP; k++)
        if (!c->g[NE_CAP[k]]) s->xc[k] = 0;
}

static int32_t net_step(net_state_t *s, const net_coef_t *c, int32_t isrc)   /* source current units -> V_OUT units */
{
    int32_t *v = s->v;
    int64_t vw[3], iw[3], gv[3];
    uint32_t k, n;
    for (n = 0; n < NET_N; n++) {
        const int32_t *kc = c->kc[n], *kl = c->kl[n];
        int64_t acc = (int64_t)c->ku[n] * isrc;
        for (k = 0; k < NCAP; k++) acc += (int64_t)kc[k] * s->xc[k];
        for (k = 0; k < 4u; k++) acc += (int64_t)kl[k] * s->hl[k];
        v[n] = sat32(RSH(acc, 28));
    }
    v[ND_GND] = 0;
    for (k = 0; k < NCAP; k++) {                /* capacitor: x' = -(2 v + x) */
        uint32_t e = NE_CAP[k];
        s->xc[k] = c->g[e] ? sat32(-(2 * ((int64_t)v[NE_P[e]] - v[NE_Q[e]]) + s->xc[k])) : 0;
    }
    {                                           /* L5: i = g v + h, h' = g v + a i */
        int64_t gi = RSH((int64_t)NET_G_L5 * ((int64_t)v[ND_OUT] - v[ND_N1]), 20), i = gi + s->hl[0];
        s->hl[0] = sat32(gi + RSH(i * NET_A_L5, 30));
    }
    for (k = 0; k < 3u; k++) vw[k] = (int64_t)v[NW_P[k]] - v[NW_Q[k]];
    for (k = 0; k < 3u; k++) {                  /* windings: i = GM v + h, h' = GM v + AM i */
        gv[k] = RSH(NET_GM[k][0] * vw[0] + NET_GM[k][1] * vw[1] + NET_GM[k][2] * vw[2], 20);
        iw[k] = gv[k] + s->hl[k + 1u];
    }
    for (k = 0; k < 3u; k++)
        s->hl[k + 1u] = sat32(gv[k] + RSH(NET_AM[k][0] * iw[0] + NET_AM[k][1] * iw[1] + NET_AM[k][2] * iw[2], 30));
    return v[ND_OUT];
}
