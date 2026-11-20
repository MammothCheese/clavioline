// SPDX-License-Identifier: GPL-3.0-only
// CLAVIOLINE play page: builds the panel, starts the firmware (clavioline.js / clavioline.wasm, from emu/build.sh), draws
// its screen and LEDs, plays its sound through worklet.js, and passes on the mouse, touch, keyboard and MIDI.
(() => {
  'use strict';
  const BASE = document.currentScript.src.replace(/[^/]*$/, '');   // (this script's folder: worklet.js is beside it)
  // the FM-1's ids, as firmware/src/platform.c has them
  const BTN = { OCTD: 0, OCTU: 1, FX: 2, SEL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7, HOME: 8, SAVE: 9, ARP: 10, SEQ: 11, PLAY: 12, REC: 13 };
  const ENC = { SELECT: 0, ALGORITHM: 1, K1: 2, K2: 3, K3: 4, K4: 5, PRESETS: 6 };
  const KEY_LED0 = 14;                                       // key k's LED is id 14 + k
  const STORE_KEY = 'clavioline-play-store-v1';

  let M = null, fbPtr = 0, started = false;
  const $ = (id) => document.getElementById(id);
  const btnEl = {}, keyEl = [], encAngle = {};

  // ---- the panel: laid out as the FM-1's front is, in its own units (892 x 540), scaled to fit the page
  const dev = $('dev');
  let scrPhys = 240;                                         // (the screen's size in device pixels: fit sets it)
  const W0 = 972, H0 = 596;                                  // (a little wider than the FM-1: room for a bigger screen)
  const SCR_X = 388, SCR_Y = 186, SCR = 300;                 // the screen's place on the case: its centre, the room for it
  function fit() {
    const s = $('fm1').clientWidth / W0;
    dev.style.transform = 'scale(' + s + ')';
    $('fm1').style.height = (H0 * s) + 'px';
    // the screen is drawn outside the scaled case, in the device's own pixels: its canvas is exactly as many pixels
    // as it covers on the display, and it starts on a whole device pixel, so the browser never resamples it. A whole
    // number of device pixels per FM-1 pixel when that is near the room there (perfectly sharp squares); else the
    // room filled (drawSharp below keeps the pixels even). Its frame follows its size.
    const dpr = devicePixelRatio || 1, room = SCR * s * dpr;
    const n = Math.max(1, Math.floor(room / 240 + 0.15));    // (a touch over the room is fine: the frame grows)
    const phys = n * 240 >= 0.85 * room ? n * 240 : Math.max(240, Math.round(room));
    const css = phys / dpr, cvs = $('screen'), box = $('fm1').getBoundingClientRect();
    const snap = (v, o) => Math.round((o + v) * dpr) / dpr - o;   // (on a whole device pixel of the page)
    cvs.width = cvs.height = phys;
    cvs.style.width = cvs.style.height = css + 'px';
    const L = snap(SCR_X * s - css / 2, box.left + scrollX), T = snap(SCR_Y * s - css / 2, box.top + scrollY);
    cvs.style.left = L + 'px'; cvs.style.top = T + 'px';
    scrPhys = phys;
    const e = 4;                                             // the frame: an even black edge round the screen, e px wide,
    const bw = parseFloat(getComputedStyle(dev).borderLeftWidth) || 0;   // (the case's outline: its insides start after it)
    bez.style.left = (L - e) / s - bw + 'px'; bez.style.top = (T - e) / s - bw + 'px';   // from where the screen really is
    bez.style.width = bez.style.height = (css + 2 * e) / s + 'px';
  }
  function place(el, x, y, w, h) {                           // (x, y: the centre)
    el.style.left = (x - w / 2) + 'px'; el.style.top = (y - h / 2) + 'px'; el.style.width = w + 'px'; el.style.height = h + 'px';
    dev.appendChild(el); return el;
  }
  function knob(x, y, label, onTurn) {                       // a knob with its name above; drag up/down or scroll
    const lab = document.createElement('div'); lab.className = 'lab'; lab.textContent = label; place(lab, x, y - 38, 120, 14);
    const k = place(document.createElement('div'), x, y, 40, 40); k.className = 'knob';
    let ang = 0, y0 = null, acc = 0, wacc = 0;
    const turn = (n) => { if (!n) return; ang += n * 18; k.style.setProperty('--a', ang + 'deg'); onTurn(n); };
    k.addEventListener('pointerdown', (e) => { y0 = e.clientY; acc = 0; k.setPointerCapture(e.pointerId); e.preventDefault(); });
    k.addEventListener('pointermove', (e) => {
      if (y0 === null) return;
      acc += y0 - e.clientY; y0 = e.clientY;
      while (acc >= 10) { turn(1); acc -= 10; }
      while (acc <= -10) { turn(-1); acc += 10; }
    });
    const end = () => { y0 = null; };
    k.addEventListener('pointerup', end); k.addEventListener('pointercancel', end);
    k.addEventListener('wheel', (e) => {
      e.preventDefault();
      if (e.deltaMode === 1) { turn(e.deltaY < 0 ? 1 : -1); return; }   // (a mouse wheel in lines: a click each)
      wacc += e.deltaY;
      while (wacc <= -40) { turn(1); wacc += 40; }
      while (wacc >= 40) { turn(-1); wacc -= 40; }
    }, { passive: false });
    k.title = label + ': drag up or down, or scroll';
    return turn;
  }
  const encTurn = (id) => (n) => { if (M) M._web_enc(id, n); };
  function button(id, x, y, label, w, h, cls) {
    const b = place(document.createElement('button'), x, y, w, h); b.className = 'btn ' + (cls || '');
    b.innerHTML = '<i></i>' + label;
    btnEl[id] = b;
    let latched = false;
    b.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      if (e.shiftKey) {                                      // shift-click: held until clicked again
        latched = !latched; b.classList.toggle('latch', latched); press(id, latched);
        return;
      }
      if (latched) { latched = false; b.classList.remove('latch'); press(id, false); return; }
      b.setPointerCapture(e.pointerId); press(id, true);
    });
    const up = () => { if (!latched && b.classList.contains('down')) press(id, false); };
    b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up);
  }
  function press(id, down) {
    btnEl[id].classList.toggle('down', down);
    if (M) M._web_btn(id, down ? 1 : 0);
  }
  function plate(x, y, w, h, r) { const p = place(document.createElement('div'), x + w / 2, y + h / 2, w, h); p.className = 'plate'; p.style.borderRadius = r + 'px'; return p; }

  // top left: MASTER (the page's volume) and SELECT, PRESETS and ALGORITHM; under them OCT- OCT+ on their own plate
  let master = 0.7;
  const masterTurn = knob(72, 82, 'MASTER', (n) => {
    master = Math.min(1, Math.max(0, master + n * 0.05));
    if (gain) gain.gain.value = master * master;
  });
  masterTurn(0);
  const turners = {};
  turners.SELECT = knob(168, 82, 'SELECT', encTurn(ENC.SELECT));
  turners.PRESETS = knob(72, 175, 'PRESETS', encTurn(ENC.PRESETS));
  turners.ALGORITHM = knob(168, 175, 'ALGORITHM', encTurn(ENC.ALGORITHM));
  plate(52, 229, 136, 44, 12);
  button(BTN.OCTD, 91, 251, 'OCT-', 52, 30);
  button(BTN.OCTU, 152, 251, 'OCT+', 52, 30);
  // the screen, in its bezel
  const bez = place(document.createElement('div'), SCR_X, SCR_Y, SCR, SCR); bez.className = 'bezel';
  // top right: KNOB1-4, and the twelve buttons on their plate
  ['K1', 'K2', 'K3', 'K4'].forEach((k, i) => knob(614 + i * 90, 82, 'KNOB' + (i + 1), encTurn(ENC[k])));
  plate(576, 150, 352, 120, 16);
  [['FX', 'FX'], ['SEL', 'SEL'], ['ENV', 'ENV'], ['LFO', 'LFO'], ['EDIT', 'EDIT'], ['GLO', 'GLO']]
    .forEach(([k, l], i) => button(BTN[k], 613 + i * 56, 183, l, 44, 40));
  [['HOME', 'HOME'], ['SAVE', 'SAVE'], ['ARP', 'ARP'], ['SEQ', 'SEQ'], ['PLAY', '<u>PLAY</u>STOP'], ['REC', 'REC']]
    .forEach(([k, l], i) => button(BTN[k], 613 + i * 56, 238, l, 44, 40, k === 'PLAY' ? 'play two' : k === 'REC' ? 'rec' : ''));

  // the keys: 0 = F3 .. 26 = G5; 16 white along the bottom, 11 black above them, all on the mint keybed
  const BLACK = (k) => [1, 3, 6, 8, 10].includes((k + 5) % 12);   // (C# D# F# G# A#; key 0 is F, 5 above C)
  const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const PCKEY = {};                                          // computer key -> FM-1 key
  // the computer's keys: the 16 white keys are Q W E (the left three), Z to / (the middle ten), R T Y (the right three);
  // the 11 black keys are A to ', in order
  const WROW = ['q', 'w', 'e', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 'r', 't', 'y'];
  const AROW = ['a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', "'"];
  const WHITE_KEYS = [], BLACK_KEYS = {};
  { let wi = 0, bi = 0;
    for (let k = 0; k < 27; k++) {
      if (BLACK(k)) BLACK_KEYS[k] = AROW[bi++];
      else { WHITE_KEYS[wi] = WROW[wi]; wi++; }
    } }
  const keybed = plate(38, 364, 896, 200, 22); keybed.id = 'keys'; keybed.classList.add('keybed');
  (function layoutKeys() {
    let wi = 0;
    for (let k = 0; k < 27; k++) {
      const black = BLACK(k), el = document.createElement('div');
      let label;
      if (black) { place(el, 70 + (wi - 1) * 55.47 + 27.7, 418, 42, 82); label = BLACK_KEYS[k]; }
      else { place(el, 70 + wi * 55.47, 509, 46, 88); label = WHITE_KEYS[wi]; wi++; }
      el.className = 'key ' + (black ? 'b' : 'w');
      const n = (k + 5) % 12, oct = 3 + Math.floor((k + 5) / 12);
      el.title = NAMES[n] + oct + (label ? '  (key ' + label.toUpperCase() + ')' : '');
      el.innerHTML = '<span>' + (label || '').toUpperCase() + '</span>';
      el.dataset.k = k;
      if (label) PCKEY[label] = k;
      keyEl[k] = el;
    }
  })();
  fit();
  addEventListener('resize', () => { releaseAll(); fit(); });

  const held = new Set();
  function key(k, down) {
    if (k == null || k < 0 || k > 26) return;
    if (down === held.has(k)) return;
    if (down) held.add(k); else held.delete(k);
    keyEl[k].classList.toggle('down', down);
    if (M) M._web_key(k, down ? 1 : 0);
  }
  // mouse and touch on the keys: each pointer plays the key under it, sliding plays the next
  const ptrKey = new Map();
  const keyAt = (x, y) => { const el = document.elementFromPoint(x, y); return el && el.closest('.key') ? +el.closest('.key').dataset.k : null; };
  dev.addEventListener('pointerdown', (e) => {
    const k = keyAt(e.clientX, e.clientY); if (k === null) return;
    e.preventDefault(); dev.setPointerCapture(e.pointerId);
    ptrKey.set(e.pointerId, k); key(k, true);
  });
  dev.addEventListener('pointermove', (e) => {
    if (!ptrKey.has(e.pointerId)) return;
    const k = keyAt(e.clientX, e.clientY), was = ptrKey.get(e.pointerId);
    if (k !== was) { key(was, false); key(k, true); ptrKey.set(e.pointerId, k); }
  });
  const ptrUp = (e) => { if (ptrKey.has(e.pointerId)) { key(ptrKey.get(e.pointerId), false); ptrKey.delete(e.pointerId); } };
  dev.addEventListener('pointerup', ptrUp); dev.addEventListener('pointercancel', ptrUp);

  // the computer's keyboard
  const PCBTN = { '7': BTN.FX, '8': BTN.SEL, '9': BTN.ENV, '0': BTN.LFO, '-': BTN.EDIT, '=': BTN.GLO, '+': BTN.GLO,
                  'u': BTN.HOME, 'i': BTN.SAVE, 'o': BTN.ARP, 'p': BTN.SEQ, '[': BTN.PLAY, ']': BTN.REC,
                  '1': BTN.OCTD, '2': BTN.OCTU, ' ': BTN.PLAY, 'Enter': BTN.REC, 'Escape': BTN.HOME };
  const PCENC = { ArrowLeft: ['SELECT', -1], ArrowRight: ['SELECT', 1], ArrowDown: ['ALGORITHM', -1], ArrowUp: ['ALGORITHM', 1],
                  PageDown: ['PRESETS', -1], PageUp: ['PRESETS', 1] };
  addEventListener('keydown', (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey || !started) return;
    if (e.target instanceof HTMLInputElement) return;
    const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    if (k in PCENC) { e.preventDefault(); turners[PCENC[k][0]](PCENC[k][1]); return; }
    if (k in PCBTN) { e.preventDefault(); if (!e.repeat) press(PCBTN[k], true); return; }
    if (k in PCKEY) { e.preventDefault(); if (!e.repeat) key(PCKEY[k], true); }
  });
  addEventListener('keyup', (e) => {
    const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    if (k in PCBTN) press(PCBTN[k], false);
    if (k in PCKEY) key(PCKEY[k], false);
  });
  function releaseAll() {
    [...held].forEach((k) => key(k, false));
    Object.values(PCBTN).forEach((id) => { if (btnEl[id].classList.contains('down') && !btnEl[id].classList.contains('latch')) press(id, false); });
  }
  addEventListener('blur', releaseAll);

  // ---- the screen and the LEDs, every frame
  // the firmware's 240 x 240 goes to a small canvas, then onto the screen's canvas at its device-pixel size: at a whole
  // number of pixels each, plainly (sharp squares); otherwise via a copy at the next whole number up, scaled down with
  // smoothing, so every FM-1 pixel comes out the same size, with soft edges no wider than one device pixel
  const cv = $('screen'), cx = cv.getContext('2d');
  const small = document.createElement('canvas'); small.width = small.height = 240;
  const sx = small.getContext('2d'), img = sx.createImageData(240, 240), px = new Uint32Array(img.data.buffer);
  const big = document.createElement('canvas'), bx = big.getContext('2d');
  function drawSharp() {
    const p = scrPhys || 240, k = p / 240;
    if (Math.abs(k - Math.round(k)) < 1e-6) { cx.imageSmoothingEnabled = false; cx.drawImage(small, 0, 0, p, p); return; }
    const up = Math.ceil(k) * 240;
    if (big.width !== up) { big.width = big.height = up; }
    bx.imageSmoothingEnabled = false; bx.drawImage(small, 0, 0, up, up);
    cx.imageSmoothingEnabled = true; cx.imageSmoothingQuality = 'high'; cx.drawImage(big, 0, 0, p, p);
  }
  const LUT = new Uint32Array(65536);                        // RGB565 -> canvas RGBA (little-endian ABGR)
  for (let c = 0; c < 65536; c++) {
    const r = (c >> 11) * 255 / 31 | 0, g = (c >> 5 & 63) * 255 / 63 | 0, b = (c & 31) * 255 / 31 | 0;
    LUT[c] = 0xFF000000 | b << 16 | g << 8 | r;
  }
  function frame() {
    if (M) {
      const fb = new Uint16Array(M.HEAPU16.buffer, fbPtr, 240 * 240);
      for (let i = 0; i < 57600; i++) px[i] = LUT[fb[i]];
      sx.putImageData(img, 0, 0); drawSharp();
      const lo = M._web_leds_lo() >>> 0, hi = M._web_leds_hi() >>> 0;
      const lit = (id) => id < 32 ? (lo >>> id) & 1 : (hi >>> (id - 32)) & 1;
      for (const id in btnEl) btnEl[id].classList.toggle('lit', !!lit(+id));
      for (let k = 0; k < 27; k++) keyEl[k].classList.toggle('lit', !!lit(KEY_LED0 + k));
    }
    requestAnimationFrame(frame);
  }

  // ---- the sound: from the firmware's ring to the player, keeping ~2048 frames queued there
  let ctx = null, node = null, gain = null, queued = 0;
  function pump() {
    if (!M || !node) return;
    const size = M._web_ring_size(), ring = new Int16Array(M.HEAP16.buffer, M._web_ring(), size * 2);
    let avail = (M._web_ring_w() - M._web_ring_r()) >>> 0;
    while (queued < 2048 && avail > 0) {
      const n = Math.min(avail, 512), r0 = M._web_ring_r() >>> 0;
      const l = new Float32Array(n), r = new Float32Array(n);
      for (let i = 0; i < n; i++) { const at = (r0 + i) % size; l[i] = ring[2 * at] / 32768; r[i] = ring[2 * at + 1] / 32768; }
      node.port.postMessage({ l, r }, [l.buffer, r.buffer]);
      M._web_ring_read(n); queued += n; avail -= n;
    }
  }

  // ---- saves: kept in this browser
  let saveTimer = 0;
  function storeSave() {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => {
      try {
        const a = new Uint8Array(M.HEAPU8.buffer, M._web_store(), M._web_store_size());
        let s = ''; for (let i = 0; i < a.length; i += 32768) s += String.fromCharCode.apply(null, a.subarray(i, i + 32768));
        localStorage.setItem(STORE_KEY, btoa(s));
      } catch (e) { console.warn('CLAVIOLINE: could not keep the save in this browser', e); }
    }, 400);
  }
  function storeLoad() {
    try {
      const s = localStorage.getItem(STORE_KEY); if (!s) return;
      const b = atob(s), n = M._web_store_size(); if (b.length !== n) return;   // (another build's sizes: start fresh)
      const a = new Uint8Array(M.HEAPU8.buffer, M._web_store(), n);
      for (let i = 0; i < n; i++) a[i] = b.charCodeAt(i);
    } catch (e) { console.warn('CLAVIOLINE: could not read the save kept in this browser', e); }
  }
  $('wipe').addEventListener('click', () => {
    if (!confirm('Forget the user slots and settings CLAVIOLINE kept in this browser? The page reloads.')) return;
    localStorage.removeItem(STORE_KEY); location.reload();
  });

  // ---- MIDI keyboards (Chrome, Edge): their notes go in as the TRS jack's would
  $('midi').addEventListener('click', async () => {
    if (!navigator.requestMIDIAccess) { $('midist').textContent = 'This browser has no Web MIDI (try Chrome or Edge).'; return; }
    try {
      const acc = await navigator.requestMIDIAccess();
      const hook = () => {
        let n = 0;
        acc.inputs.forEach((inp) => { n++; inp.onmidimessage = (m) => { const [st, d1 = 0, d2 = 0] = m.data; if (M && st < 0xF0) M._web_midi(st, d1, d2); }; });
        $('midist').textContent = n ? n + ' MIDI input' + (n > 1 ? 's' : '') + ' connected' : 'No MIDI keyboard found: plug one in.';
      };
      hook(); acc.onstatechange = hook;
    } catch (e) { $('midist').textContent = 'MIDI was not allowed.'; }
  });


  // (for a look from the browser's console: clavEmu.ms, .queued, .state)
  window.clavEmu = { get started() { return started; },
    selectPreset(i) {                                        // (a preset card: SELECT turned from the one showing to i)
      if (!M) return false;
      const d = i - M._web_preset();
      if (d) { turners.SELECT(d); }
      return true;
    },
    get ms() { return M ? M._web_ms() : 0; }, get queued() { return queued; }, get state() { return ctx ? ctx.state : 'off'; },
    get ahead() { return M ? (M._web_ring_w() - M._web_ring_r()) >>> 0 : 0; },
    get peak() { if (!M) return 0; const r = new Int16Array(M.HEAP16.buffer, M._web_ring(), M._web_ring_size() * 2); let p = 0; for (const v of r) p = Math.max(p, Math.abs(v)); return p; } };

  // ---- power on: the sound needs a click first, so the firmware starts here
  $('on').addEventListener('click', async () => {
    $('on').disabled = true; $('on').textContent = 'STARTING...';
    try {
      ctx = new AudioContext({ sampleRate: 44100, latencyHint: 'interactive' });
      await ctx.audioWorklet.addModule(BASE + 'worklet.js');
      node = new AudioWorkletNode(ctx, 'clav-player', { outputChannelCount: [2] });
      gain = ctx.createGain(); gain.gain.value = master * master;
      node.connect(gain).connect(ctx.destination);
      node.port.onmessage = (e) => { queued = Math.max(0, queued - e.data); pump(); };
      M = await Clavioline({ onStoreSaved: storeSave, print: () => {}, printErr: (t) => console.warn(t) });
      fbPtr = M._web_fb();
      storeLoad();
      setInterval(pump, 5);
      requestAnimationFrame(frame);
      started = true;
      $('power').remove();
      M.callMain([]);                                        // (runs until the firmware first waits for the sound)
      await ctx.resume();
    } catch (e) {
      console.error(e);
      $('on').textContent = 'COULD NOT START'; $('power').querySelector('p').textContent = String(e.message || e);
    }
  });
})();
