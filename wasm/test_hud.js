// HUD oracle regression: compares cc_hud_render() with the real HUD routine.
const fs = require('fs');
const path = require('path');

const wasm = fs.readFileSync(path.join(__dirname, 'render.wasm'));
const gfx = fs.readFileSync(path.join(__dirname, 'assets/gfx.bin'));
const levels = fs.readFileSync(path.join(__dirname, 'assets/levels.bin'));
const cells = fs.readFileSync(path.join(__dirname, 'assets/cells.bin'));
const f1 = fs.readFileSync(path.join(__dirname, 'assets/mni_f1.bin'));
const f2 = fs.readFileSync(path.join(__dirname, 'assets/mni_f2.bin'));
const spl = fs.readFileSync(path.join(__dirname, 'assets/mni_spl.bin'));
const pal = JSON.parse(fs.readFileSync(path.join(__dirname, 'assets/palette.json')));
const fixtures = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'notes', 'hud_fixtures.json')));

(async () => {
  const { instance } = await WebAssembly.instantiate(wasm, {});
  const e = instance.exports;
  const mem = () => new Uint8Array(e.memory.buffer);
  const copy = (b) => { const p = e.cc_alloc(b.length); mem().set(b, p); return p; };
  e.cc_set_assets(copy(gfx), copy(levels));
  e.cc_set_cells(copy(cells));
  e.cc_text_set_fonts(copy(f1), copy(f2), copy(spl));
  const pp = e.cc_alloc(64);
  const p32 = new Uint32Array(e.memory.buffer, pp, 16);
  pal.colors.forEach(([r, g, b], i) => { p32[i] = (r << 16) | (g << 8) | b; });
  e.cc_set_palette(pp);

  const colorToIndex = new Map();
  pal.colors.forEach(([r, g, b], i) => colorToIndex.set((r << 16) | (g << 8) | b, i));

  let fail = 0;
  for (const c of fixtures.cases) {
    e.cc_view_clear(0);
    for (const [off, val] of Object.entries(c.globals)) e.cc_global_set(Number(off), val);
    e.cc_hud_tick();
    const ticked = { a0c4: e.cc_global_get(0xA0C4) & 0xFFFF,
                     a0c6: e.cc_global_get(0xA0C6) & 0xFFFF };
    e.cc_hud_render();
    const afterRender = { a0c4: e.cc_global_get(0xA0C4) & 0xFFFF,
                          a0c6: e.cc_global_get(0xA0C6) & 0xFFFF };
    if (afterRender.a0c4 !== ticked.a0c4 || afterRender.a0c6 !== ticked.a0c6) {
      console.log(`HUD ${c.name}: render mutated timers ${ticked.a0c4}/${ticked.a0c6} -> ${afterRender.a0c4}/${afterRender.a0c6}`);
      fail++;
    }
    const fb = e.cc_view_framebuffer();
    const u8 = new Uint8Array(e.memory.buffer, fb, 320 * 200 * 4);
    let diff = 0, first = -1;
    for (let y = 0; y < 200; y++) {
      let x = 0;
      for (const [n, ci] of c.rows[y]) {
        for (let k = 0; k < n; k++, x++) {
          const i = (y * 320 + x) * 4;
          const rgb = (u8[i] << 16) | (u8[i + 1] << 8) | u8[i + 2];
          const got = colorToIndex.get(rgb);
          if (got !== ci) { diff++; if (first < 0) first = y * 320 + x; }
        }
      }
    }
    const a0c4 = e.cc_global_get(0xA0C4) & 0xFFFF;
    const a0c6 = e.cc_global_get(0xA0C6) & 0xFFFF;
    if (a0c4 !== c.after.a0c4 || a0c6 !== c.after.a0c6) {
      console.log(`HUD ${c.name}: timer mismatch got ${a0c4}/${a0c6} want ${c.after.a0c4}/${c.after.a0c6}`);
      fail++;
    }
    if (diff) {
      console.log(`HUD ${c.name}: ${diff} differing pixels, first@${first}`);
      fail++;
    } else {
      console.log(`PASS HUD ${c.name}`);
    }
  }

  // Cadence: one tick per game step, independent of how many times the HUD is
  // rendered between steps (the original HUD runs once per game frame).
  const resetPower = (pt, sub) => {
    e.cc_global_set(0xA0FB, 0);
    e.cc_global_set(0xA0C4, pt);
    e.cc_global_set(0xA0C6, sub);
  };
  const power = () => [e.cc_global_get(0xA0C4) & 0xFFFF,
                       e.cc_global_get(0xA0C6) & 0xFFFF];
  resetPower(12, 0);
  e.cc_hud_render();
  e.cc_hud_render();
  let p = power();
  if (p[0] !== 12 || p[1] !== 0) {
    console.log(`HUD cadence: render-only advanced ${p[0]}/${p[1]}, want 12/0`);
    fail++;
  }
  for (let step = 0; step < 18; step++) {
    e.cc_hud_tick();
    e.cc_hud_render();
    e.cc_hud_render();
  }
  p = power();
  if (p[0] !== 11 || p[1] !== 0) {
    console.log(`HUD cadence: 18 ticks gave ${p[0]}/${p[1]}, want 11/0`);
    fail++;
  } else {
    console.log('PASS HUD cadence (18 ticks, extra renders ignored)');
  }

  // Composite frame: the viewport crop already applies the full camera scroll,
  // so the screen HUD must stay at a fixed x for every [0x512E] sub-pixel.
  const hudHash = (fn) => {
    e.cc_view_clear(0);
    fn();
    const fb = e.cc_view_framebuffer();
    const u8 = new Uint8Array(e.memory.buffer, fb, 320 * 200 * 4);
    let h = 2166136261 >>> 0;
    for (let i = 0; i < 320 * 200; i++) {
      h ^= u8[i * 4]; h = Math.imul(h, 16777619);
      h ^= u8[i * 4 + 1]; h = Math.imul(h, 16777619);
      h ^= u8[i * 4 + 2]; h = Math.imul(h, 16777619);
    }
    return h >>> 0;
  };
  e.cc_global_set(0x509E, 3);
  e.cc_global_set(0x509C, 27);
  e.cc_global_set(0x50A2, 12);
  e.cc_global_set(0x50A5, 2407);
  e.cc_global_set(0x50A7, 0);
  e.cc_global_set(0xA0C4, 0xFFFF);
  e.cc_global_set(0xA0C6, 0);
  e.cc_global_set(0xA0FB, 0);
  let fixed = null, moves = 0;
  for (let k = 0; k < 8; k++) {
    e.cc_global_set(0x512E, k);
    const h = hudHash(() => e.cc_hud_render_screen());
    if (fixed === null) fixed = h;
    else if (h !== fixed) moves++;
  }
  if (moves) {
    console.log(`HUD screen render moved with the sub-pixel (${moves} frames)`);
    fail++;
  } else {
    console.log('PASS HUD screen render stays fixed during scroll');
  }
  let oracleMoves = 0;
  let oracleFirst = null;
  for (let k = 0; k < 8; k++) {
    e.cc_global_set(0x512E, k);
    const h = hudHash(() => e.cc_hud_render());
    if (oracleFirst === null) oracleFirst = h;
    else if (h !== oracleFirst) oracleMoves++;
  }
  if (!oracleMoves) {
    console.log('HUD oracle render should still apply [0x512E]&7');
    fail++;
  } else {
    console.log('PASS HUD oracle render keeps the original pan offset');
  }

  if (fail) process.exit(1);
})();
