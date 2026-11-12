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

/* DK form: v = K st + KU isrc. st = the states: the capacitors' voltages (history / g) then L5's and the windings'
 * history currents. No large companion currents cancel, so the integer ISR keeps its precision at any lever
 * position. Only the states in circuit (sidx) and the nodes something reads (rnode) are kept, compacted. */
#define NST (NCAP + 4u)
typedef struct {
    int32_t k[NET_N][NST];                      /* row r = node rnode[r], column j = state sidx[j]; Q28 */
    int32_t ku[NET_N];                          /* MOhm, Q28: the source at node A */
    int32_t g[NE];                              /* uS, Q12 (0: not in circuit) */
    uint8_t sidx[NST], rnode[NET_N], nst, nrow;
    uint32_t tabs, half;                        /* half: the light mode, the network at 22.05 kHz */
} net_coef_t;

static void net_compute(net_coef_t *c, uint32_t tabs, uint32_t lever, uint32_t half)
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
    c->half = half;
    if (half)                                   /* a capacitor's companion conductance is 2C/T: half the rate, half the g */
        for (k = 0; k < NCAP; k++) g[NE_CAP[k]] >>= 1;

    for (i = 0; i <= n; i++)                    /* the conductance matrix, exact in integers (uS Q20) */
        for (j = 0; j <= n; j++) G[i][j] = 0;
    for (i = 0; i < n; i++) G[i][i] += (int64_t)NG_LEAK << 8;
#define STAMP(p, q, y) do { G[p][p] += (y); G[q][q] += (y); G[p][q] -= (y); G[q][p] -= (y); } while (0)
    for (k = 0; k < NE; k++)
        if (g[k]) STAMP(NE_P[k], NE_Q[k], (int64_t)g[k] << 8);
    STAMP(ND_OUT, ND_N1, half ? NET_G_L5_H : NET_G_L5);
    for (k = 0; k < 3u; k++)
        for (j = 0; j < 3u; j++) {
            int64_t y = half ? NET_GM_H[k][j] : NET_GM[k][j];
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
        uint32_t need = 1u << ND_OUT | 1u << ND_N1 | 1u << ND_IN | 1u << ND_NB | 1u << ND_N3, r, ns = 0;
        for (k = 0; k < NCAP; k++)              /* the states in circuit, and the nodes their updates read */
            if (g[NE_CAP[k]]) {
                c->sidx[ns++] = (uint8_t)k;
                need |= 1u << NE_P[NE_CAP[k]] | 1u << NE_Q[NE_CAP[k]];
            }
        for (k = 0; k < 4u; k++) c->sidx[ns++] = (uint8_t)(NCAP + k);
        c->nst = (uint8_t)ns;
        c->nrow = 0;
        for (i = 0; i < n; i++) {
            if (!(need >> i & 1u)) continue;
            r = c->nrow++;
            c->rnode[r] = (uint8_t)i;
            for (j = 0; j < ns; j++) {
                uint32_t st = c->sidx[j];
                sf_t d;
                if (st < NCAP) {
                    uint32_t e = NE_CAP[st];
                    d = sf_mul(sf_add(GI(i, NE_Q[e]), sf_neg(GI(i, NE_P[e]))), sf_norm(g[e], -12));
                } else if (st == NCAP) {
                    d = sf_add(GI(i, ND_N1), sf_neg(GI(i, ND_OUT)));
                } else {
                    d = sf_add(GI(i, NW_Q[st - NCAP - 1u]), sf_neg(GI(i, NW_P[st - NCAP - 1u])));
                }
                c->k[r][j] = sf_to_q(d, 28);
            }
            c->ku[r] = sf_to_q(GI(i, ND_A), 28);
        }
    }
#undef GI
}

/* ---- the running network (ISR) ------------------------------------------------------------------------ */
/* States are int32: at three times the loudest source (white noise) they peak near 2^22 (capacitors, volts) and
 * 2^28.4 (inductor currents); tests/net_test.c NET_MAX=1 measures it. Stores saturate all the same. Every multiply
 * below is 32 x 32 -> 64. */
typedef struct {
    int32_t st[NST];                            /* capacitor voltages (2^-16 V), then L5, La, Lb, Lc currents (2^-16 uA) */
    int32_t v[NET_N + 1];                       /* node voltages (v[ND_GND] = 0; nodes nobody reads stay 0) */
} net_state_t;

static int32_t sat32(int64_t x) { return x > 2147483647 ? 2147483647 : x < -2147483647 ? -2147483647 : (int32_t)x; }

#define RSH(x, n) (((x) + ((int64_t)1 << ((n) - 1))) >> (n))   /* shift with rounding */

static void net_adopt(net_state_t *s, const net_coef_t *c)   /* elements leaving the circuit forget their state */
{
    uint32_t k;
    for (k = 0; k < NCAP; k++)
        if (!c->g[NE_CAP[k]]) s->st[k] = 0;
}

static int32_t net_step(net_state_t *s, const net_coef_t *c, int32_t isrc)   /* source current units -> V_OUT units */
{
    int32_t *v = s->v, *st = s->st, vw[3], iw[3];
    int64_t gv[3];
    const int32_t (*gm)[3] = c->half ? NET_GM_H : NET_GM, (*am)[3] = c->half ? NET_AM_H : NET_AM;
    int32_t gl5 = c->half ? NET_G_L5_H : NET_G_L5, al5 = c->half ? NET_A_L5_H : NET_A_L5;
    uint32_t r, j, k, ns = c->nst;
    int32_t x[NST];
    for (j = 0; j < ns; j++) x[j] = st[c->sidx[j]];     /* (the states in circuit, contiguous) */
    for (r = 0; r < c->nrow; r++) {
        const int32_t *kr = c->k[r];
        int64_t acc = (int64_t)c->ku[r] * isrc;
        for (j = 0; j < ns; j++) acc += (int64_t)kr[j] * x[j];
        v[c->rnode[r]] = sat32(RSH(acc, 28));
    }
    for (j = 0; j < ns; j++) {                  /* capacitor: x' = -(2 v + x) */
        uint32_t i = c->sidx[j], e;
        if (i >= NCAP) break;
        e = NE_CAP[i];
        st[i] = sat32(-(2 * ((int64_t)v[NE_P[e]] - v[NE_Q[e]]) + st[i]));
    }
    {                                           /* L5: i = g v + h, h' = g v + a i */
        int64_t gi = RSH((int64_t)gl5 * (v[ND_OUT] - v[ND_N1]), 20);
        int32_t i = sat32(gi + st[NCAP]);
        st[NCAP] = sat32(gi + RSH((int64_t)i * al5, 30));
    }
    for (k = 0; k < 3u; k++) vw[k] = v[NW_P[k]] - v[NW_Q[k]];
    for (k = 0; k < 3u; k++) {                  /* windings: i = GM v + h, h' = GM v + AM i */
        gv[k] = RSH((int64_t)gm[k][0] * vw[0] + (int64_t)gm[k][1] * vw[1] + (int64_t)gm[k][2] * vw[2], 20);
        iw[k] = sat32(gv[k] + st[NCAP + 1u + k]);
    }
    for (k = 0; k < 3u; k++)
        st[NCAP + 1u + k] = sat32(gv[k] + RSH((int64_t)am[k][0] * iw[0] + (int64_t)am[k][1] * iw[1]
                                               + (int64_t)am[k][2] * iw[2], 30));
    return v[ND_OUT];
}
