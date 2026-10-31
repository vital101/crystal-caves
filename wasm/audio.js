/* Crystal Caves Phase 6 - WebAudio single-channel manager.
 *
 * The C driver decides whether a sound is accepted (enable flag, priority,
 * preemption).  This class just renders the selected trace with CCSynth and
 * plays one buffer at a time, matching the original single PC-speaker
 * channel.
 */
(function (root) {
  'use strict';

  class CCAudio {
    constructor() {
      this.ctx = null;
      this.master = null;
      this.data = null;
      this.byId = {};
      this.buffers = {};
      this.source = null;
      this.sourceId = 0;
      this.enabled = true;
      this.onended = null;
      this.volume = 0.7;
    }

    load(data) {
      this.data = data;
      this.byId = {};
      this.buffers = {};
      for (const s of (data.sounds || [])) this.byId[s.id] = s;
    }

    get ready() { return !!this.ctx; }

    async unlock() {
      if (typeof window === "undefined") return false;
      const AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) return false;
      if (!this.ctx) {
        this.ctx = new AC();
        this.master = this.ctx.createGain();
        this.master.gain.value = this.volume;
        this.master.connect(this.ctx.destination);
      }
      if (this.ctx.state === "suspended") {
        try { await this.ctx.resume(); } catch (_) {}
      }
      return this.ctx.state === "running";
    }

    setEnabled(on) {
      this.enabled = on ? 1 : 0;
      /* Exact original [0x513E] semantics: do not cut the current sound. */
    }

    setVolume(v) {
      this.volume = v;
      if (this.master) this.master.gain.value = v;
    }

    _buffer(id) {
      if (!this.ctx) return null;
      if (this.buffers[id]) return this.buffers[id];
      const s = this.byId[id];
      if (!s || !root.CCSynth) return null;
      const pcm = root.CCSynth.renderFloat(s.trace, this.ctx.sampleRate);
      const buf = this.ctx.createBuffer(1, pcm.length, this.ctx.sampleRate);
      buf.copyToChannel(pcm, 0);
      this.buffers[id] = buf;
      return buf;
    }

    _stopSource() {
      if (!this.source) return;
      const src = this.source;
      this.source = null;
      src.onended = null;
      try { src.stop(); } catch (_) {}
      try { src.disconnect(); } catch (_) {}
    }

    play(id) {
      if (!this.enabled || !this.ctx) return 0;
      const buf = this._buffer(id);
      if (!buf) return 0;
      this._stopSource();
      const src = this.ctx.createBufferSource();
      src.buffer = buf;
      src.connect(this.master);
      const startedId = id;
      this.source = src;
      this.sourceId = id;
      src.onended = () => {
        if (this.source !== src) return;
        this.source = null;
        this.sourceId = 0;
        if (this.onended) this.onended(startedId);
      };
      try { src.start(); } catch (_) {
        if (this.source === src) { this.source = null; this.sourceId = 0; }
        return 0;
      }
      return 1;
    }

    stop() {
      this._stopSource();
      this.sourceId = 0;
    }
  }

  root.CCAudio = CCAudio;
  if (typeof module !== "undefined" && module.exports) module.exports = CCAudio;
})(typeof globalThis !== "undefined" ? globalThis : this);
