/* SPDX-License-Identifier: GPL-3.0-only */
/* The chip's network (firmware/src/clav_net.h) on its own: the response to white noise for a tablet set and a lever step,
 * as int32 pairs (input, V_OUT) on stdout (binary). tests/net_check.py compares it with tools/clav_net_ref.py.
 *   net_test TABS_HEX LEVER N [IMPULSE] */
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "clav_tables.h"
#include "clav_net.h"

int main(int argc, char **argv)
{
    static net_coef_t c;
    static net_state_t s;
    uint32_t tabs, lever, n, i;
    int32_t imp;
    if (argc < 4) return 2;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    tabs = (uint32_t)strtoul(argv[1], 0, 16);
    lever = (uint32_t)atoi(argv[2]);
    n = (uint32_t)atoi(argv[3]);
    imp = argc > 4 ? atoi(argv[4]) : 65536000;  /* 1 mA */
    net_compute(&c, tabs, lever);
    net_adopt(&s, &c);
    if (getenv("NET_K")) {                      /* KC, KL, KU as numbers */
        uint32_t j;
        for (i = 0; i < NET_N; i++) {
            for (j = 0; j < NCAP; j++) fprintf(stderr, "%.9g ", c.kc[i][j] / 268435456.0);
            for (j = 0; j < 4; j++) fprintf(stderr, "%.9g ", c.kl[i][j] / 268435456.0);
            fprintf(stderr, "%.9g%c", c.ku[i] / 268435456.0, 10);
        }
        return 0;
    }
    if (getenv("NET_DUMP")) {                   /* the source to each node, MOhm */
        for (i = 0; i < NET_N; i++) fprintf(stderr, "%g ", c.ku[i] / 268435456.0);
        fprintf(stderr, "\n");
    }
    {
        uint32_t lcg = 12345u;                  /* white noise of +-IMPULSE (as loud as the instrument plays) */
        for (i = 0; i < n; i++) {
            int32_t x, y;
            lcg = lcg * 1664525u + 1013904223u;
            x = (int32_t)(((int64_t)(int32_t)lcg * imp) >> 31);
            y = net_step(&s, &c, x);
            if (getenv("NET_MAX")) {
                static int32_t mx[3];
                uint32_t j;
                for (j = 0; j < NCAP; j++) if ((s.xc[j] < 0 ? -s.xc[j] : s.xc[j]) > mx[0]) mx[0] = s.xc[j] < 0 ? -s.xc[j] : s.xc[j];
                for (j = 0; j < 4; j++) if ((s.hl[j] < 0 ? -s.hl[j] : s.hl[j]) > mx[1]) mx[1] = s.hl[j] < 0 ? -s.hl[j] : s.hl[j];
                for (j = 0; j < NET_N; j++) if ((s.v[j] < 0 ? -s.v[j] : s.v[j]) > mx[2]) mx[2] = s.v[j] < 0 ? -s.v[j] : s.v[j];
                if (i == n - 1) fprintf(stderr, "max xc %lld  hl %lld  v %lld%c", (long long)mx[0], (long long)mx[1], (long long)mx[2], 10);
            }
            fwrite(&x, 4, 1, stdout);
            fwrite(&y, 4, 1, stdout);
        }
    }
    return 0;
}
