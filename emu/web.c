/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1 in a web browser: the app (firmware/src/clavioline.c) runs unchanged, compiled to WebAssembly with
 * Emscripten (emu/build.sh). platform.c includes this file instead of the hardware when DINGHY_WEB is set (with
 * DINGHY_HOST, as tests/host.c). The page (docs/play/) presses the keys, buttons and knobs through the web_* calls,
 * draws the screen from fb, and plays the sound from the ring below; saves go to the browser's storage.
 *
 * The app's main loop never returns: each pass calls platform_poll (one millisecond). Here platform_poll renders the
 * sound that millisecond is worth into the ring, and when the ring is far enough ahead of what the page has played it
 * sleeps (emscripten_sleep, with ASYNCIFY: the browser runs meanwhile). So the sound sets the pace, as on the FM-1. */
#include <stdint.h>
#include <string.h>
#include <emscripten.h>

#ifndef APP_NAME
#define APP_NAME "CLAVIOLINE"
#endif
#ifndef APP_VER
#define APP_VER "web"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

static volatile uint32_t fm1_ms;
#define HOST_NENC 7
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges, host_rel;           /* press edges, release edges since the app last asked */
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = host_edges;
    host_edges = 0;
    *released = host_rel;
    host_rel = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { return 0; }   /* (the page sets fm1_in.notes) */
static int32_t host_enc[HOST_NENC];             /* detents owed to the app */
static int32_t fm1_enc_take(uint32_t e) { int32_t v = host_enc[e % HOST_NENC]; host_enc[e % HOST_NENC] = 0; return v; }
static uint64_t host_leds, host_glow;           /* bit id: LED id lit (0..13 buttons, 14..40 keys); the dim layer */
static void fm1_irq_off(void) {}                 /* (the sound is rendered between the main loop's passes) */
static void fm1_irq_on(void) {}
static void fm1_led_key(uint32_t id, int on) { if (on) host_leds |= (uint64_t)1 << id; else host_leds &= ~((uint64_t)1 << id); }
static void fm1_led_key_glow(uint32_t id, int on) { if (on) host_glow |= (uint64_t)1 << id; else host_glow &= ~((uint64_t)1 << id); }

/* the screen: 240 x 240 RGB565 (the page reads it) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);   /* (gfx.c sends the bytes swapped) */
            }
}
#ifndef CV_MAX
#define CV_MAX (240u * 64u)
#endif
#include "gfx.c"

/* storage: in one block the page keeps in the browser (localStorage), store.h's sizes; a[0] used, a[1..3] the length */
#include "store.h"
static struct {
    uint8_t set[SECTOR_MAX + 4], blob[NBLOB][SECTOR_MAX + 4], slot[NSLOT][SLOT_MAX + 4];
} store;
EM_JS(void, js_store_saved, (void), { if (Module.onStoreSaved) Module.onStoreSaved(); });
static int host_load(uint8_t *a, uint32_t max, void *p, uint32_t n)
{
    if (!a[0] || n > max || (uint32_t)(a[1] | a[2] << 8 | a[3] << 16) != n) return -1;
    memcpy(p, a + 4, n);
    return 0;
}
static int host_save(uint8_t *a, uint32_t max, const void *p, uint32_t n)
{
    if (n > max) return -1;
    a[0] = 1; a[1] = (uint8_t)n; a[2] = (uint8_t)(n >> 8); a[3] = (uint8_t)(n >> 16);
    memcpy(a + 4, p, n);
    js_store_saved();
    return 0;
}
static int settings_load(void *p, uint32_t n) { return host_load(store.set, SECTOR_MAX, p, n); }
static int settings_save(const void *p, uint32_t n) { return host_save(store.set, SECTOR_MAX, p, n); }
static int blob_load(uint32_t i, void *p, uint32_t n) { return i < NBLOB ? host_load(store.blob[i], SECTOR_MAX, p, n) : -1; }
static int blob_save(uint32_t i, const void *p, uint32_t n) { return i < NBLOB ? host_save(store.blob[i], SECTOR_MAX, p, n) : -1; }
static int slot_load(uint32_t i, void *p, uint32_t n) { return i < NSLOT ? host_load(store.slot[i], SLOT_MAX, p, n) : -1; }
static int slot_save(uint32_t i, const void *p, uint32_t n) { return i < NSLOT ? host_save(store.slot[i], SLOT_MAX, p, n) : -1; }

/* MIDI: in from the page (Web MIDI, as the TRS jack), out to nowhere */
static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    mq_r++;
    return 2;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

/* the sound: a ring of stereo 16-bit frames; the app writes (ring_w), the page reads (ring_r, moved by web_ring_read) */
#define RING 16384u
#define AHEAD 1024u                             /* rendered ahead of the page, at most (~23 ms) */
static int16_t ring[RING * 2u];
static volatile uint32_t ring_w, ring_r;

static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t owed;
    static int32_t blk[2u * HALF_FRAMES];
    fm1_ms++;
    owed += FS;                                 /* the frames this millisecond is worth, in blocks as the ISR renders */
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i;
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        for (i = 0; i < HALF_FRAMES; i++) {
            int32_t l = blk[2u * i], r = blk[2u * i + 1u];
            uint32_t at = (ring_w + i) % RING;
            ring[2u * at] = (int16_t)(l > 32767 ? 32767 : l < -32767 ? -32767 : l);
            ring[2u * at + 1u] = (int16_t)(r > 32767 ? 32767 : r < -32767 ? -32767 : r);
        }
        ring_w += HALF_FRAMES;
    }
    while (ring_w - ring_r > AHEAD) emscripten_sleep(2);   /* far enough ahead: let the browser play it */
}

/* ---- the page's calls */
EMSCRIPTEN_KEEPALIVE void web_btn(uint32_t id, uint32_t down)   /* a button down (1) or up (0); ids as platform.c's BTN_ */
{
    if (id > 31u) return;
    if (down) { host_edges |= 1u << id; fm1_in.buttons |= 1u << id; }
    else { host_rel |= 1u << id; fm1_in.buttons &= ~(1u << id); }
}
EMSCRIPTEN_KEEPALIVE void web_key(uint32_t k, uint32_t down)   /* a key, 0 (F3) .. 26 (G5) */
{
    if (k > 26u) return;
    if (down) fm1_in.notes |= 1u << k; else fm1_in.notes &= ~(1u << k);
}
EMSCRIPTEN_KEEPALIVE void web_enc(uint32_t e, int32_t n) { host_enc[e % HOST_NENC] += n; }   /* an encoder, n detents */
EMSCRIPTEN_KEEPALIVE void web_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (mq_w - mq_r >= 64u) return;
    mq[mq_w % 64u][0] = (uint8_t)st; mq[mq_w % 64u][1] = (uint8_t)d1; mq[mq_w % 64u][2] = (uint8_t)d2;
    mq_w++;
}
EMSCRIPTEN_KEEPALIVE uint16_t *web_fb(void) { return fb; }
EMSCRIPTEN_KEEPALIVE uint32_t web_ms(void) { return fm1_ms; }   /* (the firmware's clock, for the page's status) */
EMSCRIPTEN_KEEPALIVE uint32_t web_leds_lo(void) { return (uint32_t)host_leds; }
EMSCRIPTEN_KEEPALIVE uint32_t web_leds_hi(void) { return (uint32_t)(host_leds >> 32); }
EMSCRIPTEN_KEEPALIVE int16_t *web_ring(void) { return ring; }
EMSCRIPTEN_KEEPALIVE uint32_t web_ring_size(void) { return RING; }
EMSCRIPTEN_KEEPALIVE uint32_t web_ring_w(void) { return ring_w; }
EMSCRIPTEN_KEEPALIVE uint32_t web_ring_r(void) { return ring_r; }
EMSCRIPTEN_KEEPALIVE void web_ring_read(uint32_t n) { ring_r += n; }
EMSCRIPTEN_KEEPALIVE uint8_t *web_store(void) { return (uint8_t *)&store; }
EMSCRIPTEN_KEEPALIVE uint32_t web_store_size(void) { return sizeof store; }

int main(void)
{
    app_main();
    return 0;
}
