/* Shared Node helpers for the wasm regression harnesses.
 *
 * Every test is still runnable on its own (`node test_x.js`); this module only
 * removes the repeated "read render.wasm + all portable assets, instantiate,
 * allocate copies, build a scratch state buffer" boilerplate.  Browser-only
 * tests use the assertion and JSON helpers without touching wasm memory.
 */
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const DIR = __dirname;

const ASSET_FILES = {
  gfx: 'assets/gfx.bin',
  levels: 'assets/levels.bin',
  cells: 'assets/cells.bin',
  overlay: 'assets/level_overlay.bin',
  screens: 'assets/screens.bin',
  f1: 'assets/mni_f1.bin',
  f2: 'assets/mni_f2.bin',
  spl: 'assets/mni_spl.bin',
};

/** Read a path relative to wasm/. */
function read(rel) {
  return fs.readFileSync(path.join(DIR, rel));
}

/** Read and parse a JSON path relative to wasm/. */
function readJson(rel) {
  return JSON.parse(read(rel).toString('utf8'));
}

/** Load every portable asset the full game needs (cached for the process). */
let cachedAssets = null;
function loadAssets() {
  if (cachedAssets) return cachedAssets;
  const assets = {};
  for (const [key, file] of Object.entries(ASSET_FILES))
    assets[key] = read(file);
  assets.palette = readJson('assets/palette.json');
  cachedAssets = assets;
  return assets;
}

/** Instantiate render.wasm and return its exports. */
async function loadWasm(file) {
  const { instance } = await WebAssembly.instantiate(read(file || 'render.wasm'), {});
  return instance.exports;
}

/**
 * Bind allocation helpers to one wasm instance.
 * @param {object} e wasm exports
 * @returns {{mem: function(): Uint8Array, copy: function(Uint8Array): number,
 *           copyString: function(string): number}}
 */
function bindMemory(e) {
  const mem = () => new Uint8Array(e.memory.buffer);
  const copy = (bytes) => {
    const p = e.cc_alloc(bytes.length);
    if (!p) throw new Error(`wasm allocation failed (${bytes.length} bytes)`);
    mem().set(bytes, p);
    return p;
  };
  const copyString = (s) => {
    const p = e.cc_alloc(s.length + 1);
    const m = mem();
    for (let i = 0; i < s.length; i++) m[p + i] = s.charCodeAt(i);
    m[p + s.length] = 0;
    return p;
  };
  return { mem, copy, copyString };
}

/**
 * Copy the full portable asset set into a running wasm instance.
 * @param {object} e wasm exports
 * @param {function(Uint8Array): number} copy byte-copy helper
 * @param {object} [assetSet] optional loadAssets() result
 */
function copyAssets(e, copy, assetSet) {
  const a = assetSet || loadAssets();
  e.cc_set_assets(copy(a.gfx), copy(a.levels));
  e.cc_set_cells(copy(a.cells));
  e.cc_set_level_overlay(copy(a.overlay));
  e.cc_set_screens(copy(a.screens));
  e.cc_text_set_fonts(copy(a.f1), copy(a.f2), copy(a.spl));

  const palP = e.cc_alloc(64);
  const p32 = new Uint32Array(e.memory.buffer, palP, 16);
  a.palette.colors.forEach(([r, g, b], i) => {
    p32[i] = (r << 16) | (g << 8) | b;
  });
  e.cc_set_palette(palP);
}

/**
 * Instantiate the game with every asset wired in.
 * @param {{wasm?: string, assets?: object, sound?: boolean}} [options]
 *   `sound: false` disables the driver before the assets are copied.
 * @returns {Promise<{e: object, mem: function(): Uint8Array,
 *                    copy: function(Uint8Array): number,
 *                    copyString: function(string): number}>}
 */
async function loadGame(options) {
  const opts = options || {};
  const e = await loadWasm(opts.wasm);
  /* Optional ordering hook: some fixtures must silence the sound driver
   * before assets are copied. */
  if (opts.sound === false) e.cc_sound_set_enabled(0);
  const bound = bindMemory(e);
  copyAssets(e, bound.copy, opts.assets);
  return Object.assign({ e }, bound);
}

/** Raw cc_state_save() bytes (header + payload). */
function stateBytes(e, stateP, stateSize, mem) {
  const n = e.cc_state_save(stateP, stateSize);
  return mem().slice(stateP, stateP + n);
}

/** SHA-256 of a state save image. */
function stateHash(e, stateP, stateSize, mem) {
  return sha256(stateBytes(e, stateP, stateSize, mem));
}

/** In-memory Storage implementation with a write-failure switch. */
function makeStorage() {
  const m = new Map();
  return {
    failWrites: false,
    getItem(k) { return m.has(k) ? m.get(k) : null; },
    setItem(k, v) {
      if (this.failWrites) throw new Error('quota');
      m.set(k, String(v));
    },
    removeItem(k) { m.delete(k); },
    clear() { m.clear(); },
    raw() { return m; }
  };
}

/**
 * Common PASS/FAIL reporting for the standalone Node harnesses.
 * @returns {{assert: function(boolean, string): void,
 *            check: function(boolean, string): void,
 *            failures: number,
 *            finish: function(string): void}}
 */
function createAsserts() {
  let failures = 0;
  function assert(cond, msg) {
    if (!cond) { console.error('FAIL:', msg); failures++; }
    else console.log('PASS', msg);
  }
  return {
    assert,
    check: assert,
    get failures() { return failures; },
    finish(message) {
      if (failures) {
        console.error(`\n${failures} check(s) failed`);
        process.exit(1);
      }
      if (message) console.log(`\nPASS: ${message}`);
    }
  };
}

/** Bytewise comparison of two Uint8Array/Array values. */
function bytesEqual(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

/** Read a NUL-terminated string from wasm memory. */
function cstr(e, p, max) {
  const limit = max === undefined ? 64 : max;
  const m = new Uint8Array(e.memory.buffer);
  let s = '';
  for (let i = 0; p && i < limit && m[p + i]; i++) s += String.fromCharCode(m[p + i]);
  return s;
}

/** ASCII helper shared by the shell tests. */
function key(ch) {
  return typeof ch === 'number' ? ch : ch.charCodeAt(0);
}

function hexToBytes(hex) {
  return Uint8Array.from(Buffer.from(hex, 'hex'));
}

function bytesToHex(bytes) {
  return Buffer.from(bytes).toString('hex');
}

/** SHA-256 hex digest of bytes (Buffer/Uint8Array/string). */
function sha256(bytes) {
  return crypto.createHash('sha256').update(bytes).digest('hex');
}

module.exports = {
  DIR,
  read,
  readJson,
  loadAssets,
  loadWasm,
  bindMemory,
  copyAssets,
  loadGame,
  stateBytes,
  stateHash,
  makeStorage,
  createAsserts,
  bytesEqual,
  cstr,
  key,
  hexToBytes,
  bytesToHex,
  sha256,
};
