// wasm/test_render_digest.js — characterization digests for the shell/text/
// render surface (Refactor Phase 2 safety net).
//
// Same principle as test_refactor_digest.js: deterministic SHA-256 digests of
// observable outputs.  This harness deliberately samples the seams owned by
// render/, text/ and shell/render.c more densely than the Phase 0 file:
// static + cell renderings per level and camera, raw view primitives, text
// drawing, PCX screens and the title/credits scroll.  A mechanical split must
// not move any digest; drift here is a real regression.
//
// Goldens: notes/refactor_render_digests.json (separate from the Phase 0
// file, which this harness never writes).
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const DIR = __dirname;
const ROOT = path.join(DIR, '..');
const FIXTURE = path.join(ROOT, 'notes', 'refactor_render_digests.json');
const UPDATE = process.argv.includes('--update');

const W = 320, H = 200, TEX_W = 640, TEX_H = 400;
const read = (p) => fs.readFileSync(path.join(DIR, p));
const sha256 = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');

const wasmBytes = read('render.wasm');
const assets = {
  gfx: read('assets/gfx.bin'),
  levels: read('assets/levels.bin'),
  cells: read('assets/cells.bin'),
  overlay: read('assets/level_overlay.bin'),
  screens: read('assets/screens.bin'),
  f1: read('assets/mni_f1.bin'),
  f2: read('assets/mni_f2.bin'),
  spl: read('assets/mni_spl.bin'),
  palette: JSON.parse(read('assets/palette.json').toString('utf8')),
};

function boot() {
  const instance = new WebAssembly.Instance(new WebAssembly.Module(wasmBytes));
  const e = instance.exports;
  const mem = () => new Uint8Array(e.memory.buffer);
  const copy = (b) => {
    const p = e.cc_alloc(b.length);
    if (!p) throw new Error(`asset allocation failed (${b.length} bytes)`);
    mem().set(b, p);
    return p;
  };

  const f1P = copy(assets.f1), f2P = copy(assets.f2);
  e.cc_set_assets(copy(assets.gfx), copy(assets.levels));
  e.cc_set_cells(copy(assets.cells));
  e.cc_set_level_overlay(copy(assets.overlay));
  e.cc_set_screens(copy(assets.screens));
  e.cc_text_set_fonts(f1P, f2P, copy(assets.spl));

  const palP = e.cc_alloc(64);
  const p32 = new Uint32Array(e.memory.buffer, palP, 16);
  assets.palette.colors.forEach(([r, g, b], i) => { p32[i] = (r << 16) | (g << 8) | b; });
  e.cc_set_palette(palP);

  e.cc_sound_init();
  e.cc_sound_set_enabled(0);
  e.cc_sound_log_clear();

  const copyString = (s) => {
    const p = e.cc_alloc(s.length + 1);
    const m = mem();
    for (let i = 0; i < s.length; i++) m[p + i] = s.charCodeAt(i);
    m[p + s.length] = 0;
    return p;
  };
  const viewDigest = () => sha256(new Uint8Array(e.memory.buffer, e.cc_view_framebuffer(), W * H * 4));
  const textureDigest = () => sha256(new Uint8Array(e.memory.buffer, e.cc_framebuffer(), TEX_W * TEX_H * 4));
  const shellDigest = () => sha256(Buffer.from(JSON.stringify({
    state: e.cc_shell_state(),
    overlay: e.cc_shell_overlay(),
    hud: e.cc_shell_hud_active(),
    scrollRow: e.cc_shell_scroll_row(),
    scrollDir: e.cc_shell_scroll_dir(),
    scrollPhase: e.cc_shell_scroll_phase(),
    message: e.cc_shell_message_block(),
    pageCount: e.cc_shell_page_count(),
    pageIndex: e.cc_shell_page_index(),
  }), 'utf8'));

  return { e, f1P, copyString, viewDigest, textureDigest, shellDigest };
}

// ---------------------------------------------------------------- scenarios

function staticRenders() {
  const h = boot();
  const out = {};
  for (let level = 0; level < 19; level++) {
    h.e.cc_render_level(level, h.e.cc_framebuffer());
    out[`level${String(level).padStart(2, '0')}`] = h.textureDigest();
  }
  return out;
}

function views() {
  const h = boot();
  const out = {};
  for (let level = 0; level < 19; level++) {
    for (const [cx, cy] of [[0, 0], [320, 200]]) {
      h.e.cc_render_view(level, cx, cy);
      out[`level${String(level).padStart(2, '0')}@${cx},${cy}`] = h.viewDigest();
    }
  }
  return out;
}

function cellViews() {
  const h = boot();
  const out = {};
  for (let level = 2; level <= 18; level++) {
    h.e.cc_objects_init(level);
    for (const [cx, cy] of [[0, 0], [160, 100], [320, 200]]) {
      h.e.cc_render_view_cells(level, cx, cy);
      out[`level${String(level).padStart(2, '0')}@${cx},${cy}`] = h.viewDigest();
    }
  }
  return out;
}

function viewOps() {
  const h = boot();
  const out = {};
  const e = h.e;
  e.cc_view_clear(1);
  out.clear1 = h.viewDigest();
  e.cc_view_fill_rect(10, 20, 30, 40, 5);
  e.cc_view_fill_rect(-8, -8, 24, 24, 12);          // clipped at the origin
  e.cc_view_fill_rect(310, 195, 32, 32, 9);         // clipped at the far edge
  out.fills = h.viewDigest();
  for (let i = 0; i < 64; i++) e.cc_view_set_pixel(319 - (i & 7), 199 - (i >> 3), i & 15);
  out.pixels = h.viewDigest();
  for (let gy = 0; gy < 4; gy++)                    // F2 glyphs 0..3 via bank
    for (let gx = 0; gx < 4; gx++)
      e.cc_view_draw_glyph(h.f1P + 3 + (0x10 + gy * 4 + gx) * 40, 8 + gx * 12, 80 + gy * 12);
  out.glyphs = h.viewDigest();
  return out;
}

function textOps() {
  const h = boot();
  const e = h.e;
  const out = {};
  e.cc_view_clear(0);
  e.cc_text_draw_string(8, 8, h.copyString('CRYSTAL CAVES 0123456789'));
  out.string = h.viewDigest();
  e.cc_text_draw_frame(4, 30, 80, 40);
  out.frame = h.viewDigest();
  // Two NUL-terminated lines in one buffer for cc_text_draw_lines.
  const linesBuf = h.copyString('line one\nline two\0');
  const ptrs = e.cc_alloc(8);
  new Uint32Array(e.memory.buffer, ptrs, 2).set([linesBuf, linesBuf + 9]);
  e.cc_text_draw_lines(ptrs, 2);
  out.lines = h.viewDigest();
  for (const id of [0, 1, 2, 4, 8, 16, 24, 32]) {
    const n = e.cc_text_block_lines(id);
    out[`block_lines_${id}`] = n > 0 ? `${n}:${(e.cc_view_clear(0), e.cc_text_draw_block(id, 0, 8), h.viewDigest())}` : '0';
  }
  return out;
}

function screens() {
  const h = boot();
  const out = {};
  for (let idx = 0; idx < 4; idx++) {
    h.e.cc_draw_screen(idx);
    out[`screen${idx}`] = h.viewDigest();
  }
  return out;
}

function titleScroll() {
  const h = boot();
  const e = h.e;
  const out = {};
  e.cc_shell_init();
  const cp = (name) => {
    e.cc_shell_render();
    out[name] = { view: h.viewDigest(), shell: h.shellDigest() };
  };
  cp('logo');
  e.cc_shell_key(13);                 // logo -> credits scroll
  for (let i = 1; i <= 8; i++) {
    e.cc_shell_step_ticks(120);
    cp(`credits_tick${i * 120}`);
  }
  return out;
}

// ---------------------------------------------------------------- fixture

function compare(expected, actual, pathParts = [], diffs = []) {
  if (diffs.length >= 20) return diffs;
  if (Array.isArray(expected) || Array.isArray(actual)) {
    if (!Array.isArray(expected) || !Array.isArray(actual)) {
      diffs.push(`${pathParts.join('.')}: type mismatch`);
      return diffs;
    }
    if (expected.length !== actual.length) {
      diffs.push(`${pathParts.join('.')}: length ${actual.length}, expected ${expected.length}`);
      return diffs;
    }
    for (let i = 0; i < expected.length; i++)
      compare(expected[i], actual[i], [...pathParts, String(i)], diffs);
    return diffs;
  }
  if (expected && typeof expected === 'object') {
    if (!actual || typeof actual !== 'object') {
      diffs.push(`${pathParts.join('.')}: type mismatch`);
      return diffs;
    }
    const keys = new Set([...Object.keys(expected), ...Object.keys(actual)]);
    for (const key of [...keys].sort())
      compare(expected[key], actual[key], [...pathParts, key], diffs);
    return diffs;
  }
  if (expected !== actual)
    diffs.push(`${pathParts.join('.')}: ${actual}, expected ${expected}`);
  return diffs;
}

function countSnapshots(value) {
  if (Array.isArray(value)) return value.reduce((n, item) => n + countSnapshots(item), 0);
  if (!value || typeof value !== 'object') return typeof value === 'string' && value.length > 16 ? 1 : 0;
  let n = typeof value.view === 'string' ? 1 : 0;
  for (const item of Object.values(value)) n += countSnapshots(item);
  return n;
}

const result = {
  version: 1,
  note: 'SHA-256 digests of render/text/scroll seams; regenerate with node test_render_digest.js --update',
  static_render: staticRenders(),
  view: views(),
  view_cells: cellViews(),
  view_ops: viewOps(),
  text_ops: textOps(),
  screens: screens(),
  title_scroll: titleScroll(),
};

const snapshots = countSnapshots(result);
if (UPDATE) {
  fs.writeFileSync(FIXTURE, JSON.stringify(result, null, 2) + '\n');
  console.log(`WROTE ${path.relative(ROOT, FIXTURE)} (${snapshots} snapshots)`);
  process.exit(0);
}
if (!fs.existsSync(FIXTURE)) {
  console.error(`FAIL missing ${path.relative(ROOT, FIXTURE)}; run \`node test_render_digest.js --update\` once`);
  process.exit(1);
}
const expected = JSON.parse(fs.readFileSync(FIXTURE, 'utf8'));
const diffs = compare(expected, result);
if (diffs.length) {
  for (const d of diffs) console.error(`FAIL ${d}`);
  console.error(`\n${diffs.length} digest mismatch(es); a refactor must not change these hashes`);
  process.exit(1);
}
console.log(`PASS: ${snapshots} render-digest snapshots match ${path.relative(ROOT, FIXTURE)}`);
