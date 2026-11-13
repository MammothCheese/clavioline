/* SPDX-License-Identifier: GPL-3.0-only */
/* The chip's formant filter (firmware/src/clav_formant.h) on its own: white noise through the biquads decoded for
 * a tablet combination, at 22.05 kHz, as int32 pairs (input, output) on stdout. tests/formant_check.py compares the
 * response with the circuit model (tools/clav_net_ref.py).   formant_test TABS_HEX N */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "clav_tables.h"
#include "clav_formants.h"
#include "clav_formant.h"

int main(int argc, char **argv)
{
    static fm_coef_t c;
    static fm_state_t s;
    uint32_t n, i, lcg = 12345u;
    if (argc < 3) return 2;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    fm_compute(&c, (uint32_t)strtoul(argv[1], 0, 16));
    n = (uint32_t)atoi(argv[2]);
    for (i = 0; i < n; i++) {
        int32_t x, y;
        lcg = lcg * 1664525u + 1013904223u;
        x = (int32_t)lcg >> 18;                 /* +-8192, Q15 */
        y = fm_run(&s, &c, x);
        fwrite(&x, 4, 1, stdout);
        fwrite(&y, 4, 1, stdout);
    }
    return 0;
}
