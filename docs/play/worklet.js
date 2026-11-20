// SPDX-License-Identifier: GPL-3.0-only
// CLAVIOLINE play page: the sound player. The page sends the firmware's sound in chunks (left and right, -1..1); this
// plays them in order and says every 1024 frames how many it has played, so the page knows when to send more.
class ClavPlayer extends AudioWorkletProcessor {
  constructor() {
    super();
    this.size = 32768;
    this.l = new Float32Array(this.size);
    this.r = new Float32Array(this.size);
    this.w = 0; this.rd = 0; this.played = 0;
    this.port.onmessage = (e) => {
      const { l, r } = e.data;
      for (let i = 0; i < l.length; i++) {
        if (this.w - this.rd >= this.size) break;          // (full: the rest is dropped)
        const at = this.w % this.size;
        this.l[at] = l[i]; this.r[at] = r[i]; this.w++;
      }
    };
  }
  process(inputs, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0];
    for (let i = 0; i < L.length; i++) {
      if (this.rd < this.w) {
        const at = this.rd % this.size;
        L[i] = this.l[at]; R[i] = this.r[at]; this.rd++;
      } else { L[i] = 0; R[i] = 0; }                      // (nothing yet: silence)
    }
    this.played += L.length;
    if (this.played >= 1024) { this.port.postMessage(this.played); this.played = 0; }
    return true;
  }
}
registerProcessor('clav-player', ClavPlayer);
