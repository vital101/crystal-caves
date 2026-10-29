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
    e.cc_hud_render();
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
  if (fail) process.exit(1);
})();
