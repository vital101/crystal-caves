/* Crystal Caves Phase 6 - deterministic PC-speaker trace renderer.
 *
 * The runtime asset (sound.json) stores, for every 140.06 Hz driver tick, the
 * PIT channel-2 divisor and the speaker gate state.  This module turns that
 * trace into signed 16-bit samples using only integer arithmetic so the
 * output is identical in Node and every browser.
 *
 * Must match tools/sound_banks.py:synth_reference() exactly.
 */
(function (root) {
  'use strict';

  const PIT_HZ = 1193181;
  const TICK_DIVISOR = 0x2147;
  const AMPLITUDE = 12000;

  /** @returns {number} samples produced by a trace at sampleRate. */
  function sampleCount(trace, sampleRate) {
    return Math.floor(trace.length * sampleRate * TICK_DIVISOR / PIT_HZ);
  }

  /** Render a tick trace to signed 16-bit samples (exact integer reference). */
  function renderInt16(trace, sampleRate, amplitude) {
    amplitude = amplitude === undefined ? AMPLITUDE : amplitude;
    const total = sampleCount(trace, sampleRate);
    const out = new Int16Array(total);
    const two32 = 4294967296;
    let phase = 0;

    for (let k = 0; k < trace.length; k++) {
      const start = Math.floor(k * sampleRate * TICK_DIVISOR / PIT_HZ);
      let end = Math.floor((k + 1) * sampleRate * TICK_DIVISOR / PIT_HZ);
      if (end > total) end = total;
      const divisor = trace[k][0] | 0;
      const speaker = trace[k][1] ? 1 : 0;
      const inc = divisor
        ? Math.floor(PIT_HZ * two32 / (divisor * sampleRate))
        : 0;
      for (let i = start; i < end; i++) {
        if (divisor) phase = (phase + inc) >>> 0;
        if (speaker) out[i] = phase < 0x80000000 ? amplitude : -amplitude;
      }
    }
    return out;
  }

  /** Render a tick trace to WebAudio-compatible Float32 samples. */
  function renderFloat(trace, sampleRate, amplitude) {
    const src = renderInt16(trace, sampleRate, amplitude);
    const out = new Float32Array(src.length);
    for (let i = 0; i < src.length; i++) out[i] = src[i] / 32768;
    return out;
  }

  const api = {
    PIT_HZ: PIT_HZ,
    TICK_DIVISOR: TICK_DIVISOR,
    AMPLITUDE: AMPLITUDE,
    sampleCount: sampleCount,
    renderInt16: renderInt16,
    renderFloat: renderFloat,
  };

  root.CCSynth = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
