// Text-window oracle regression: compares cc_text_draw_block() with the real
// renderer at 0x0FC5:0x0877 (captured from original wrappers / line tables).
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const crypto = require('crypto');

const wasm = fs.readFileSync(path.join(__dirname, 'render.wasm'));
const f1 = fs.readFileSync(path.join(__dirname, 'assets/mni_f1.bin'));
const f2 = fs.readFileSync(path.join(__dirname, 'assets/mni_f2.bin'));
const spl = fs.readFileSync(path.join(__dirname, 'assets/mni_spl.bin'));
const pal = JSON.parse(fs.readFileSync(path.join(__dirname, 'assets/palette.json')));
const fixtures = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'notes', 'text_fixtures.json')));

(async () => {
  const { instance } = await WebAssembly.instantiate(wasm, {});
  const e = instance.exports;
  const mem = () => new Uint8Array(e.memory.buffer);
  const copy = (b) => { const p = e.cc_alloc(b.length); mem().set(b, p); return p; };
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
    e.cc_global_set(0x512E, c.camera.x & 0xFFFF);
    e.cc_global_set(0x5130, c.camera.y & 0xFFFF);
    e.cc_global_set(0x2B5C, c.camera.scroll & 0xFFFF);

    const lines = e.cc_text_block_lines(c.block_index);
    if (lines !== c.lines.length) {
      console.log(`TEXT ${c.name}: block has ${lines} lines, fixture has ${c.lines.length}`);
      fail++;
      continue;
    }
    e.cc_text_draw_block(c.block_index, 0, 0);

    const raw = zlib.inflateSync(Buffer.from(c.fb_b64, 'base64'));
    if (raw.length !== c.fb_len) {
      console.log(`TEXT ${c.name}: fixture length ${raw.length}, want ${c.fb_len}`);
      fail++;
      continue;
    }
    const sha = crypto.createHash('sha256').update(raw).digest('hex');
    if (sha !== c.fb_sha256) {
      console.log(`TEXT ${c.name}: fixture sha mismatch`);
      fail++;
      continue;
    }

    const fb = e.cc_view_framebuffer();
    const u8 = new Uint8Array(e.memory.buffer, fb, 320 * 200 * 4);
    let diff = 0, first = -1;
    for (let i = 0; i < 320 * 200; i++) {
      const rgb = (u8[i * 4] << 16) | (u8[i * 4 + 1] << 8) | u8[i * 4 + 2];
      const got = colorToIndex.get(rgb);
      if (got !== raw[i]) {
        diff++;
        if (first < 0) first = i;
      }
    }
    if (diff) {
      console.log(`TEXT ${c.name}: ${diff} differing pixels, first@(${first % 320},${Math.floor(first / 320)})`);
      fail++;
    } else {
      console.log(`PASS TEXT ${c.name}`);
    }
  }

  if (fail) process.exit(1);
})();
