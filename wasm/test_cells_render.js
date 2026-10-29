// Mutable-cell renderer: fresh cells match the baked renderer, and clearing a
// crystal layer changes only that cell's pixels.
const fs = require("fs");
const path = require("path");
const DIR = __dirname;
const PORT = path.join(DIR, "..", "out", "portable");
(async () => {
  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")), {});
  const e = instance.exports;
  const copyIn = (b) => { const p = e.cc_alloc(b.length); new Uint8Array(e.memory.buffer, p, b.length).set(b); return p; };
  e.cc_set_assets(copyIn(fs.readFileSync(path.join(PORT, "gfx.bin"))),
                  copyIn(fs.readFileSync(path.join(PORT, "levels.bin"))));
  const pal = JSON.parse(fs.readFileSync(path.join(PORT, "palette.json")));
  const palP = e.cc_alloc(64);
  const pu = new Uint32Array(e.memory.buffer, palP, 16);
  pal.colors.forEach(([r, g, b], i) => { pu[i] = (r << 16) | (g << 8) | b; });
  e.cc_set_palette(palP);
  const cells = fs.readFileSync(path.join(PORT, "cells.bin"));
  e.cc_set_cells(copyIn(cells));

  e.cc_objects_init(3);
  e.cc_render_level(3, e.cc_framebuffer());
  const baked = new Uint8Array(e.memory.buffer, e.cc_framebuffer(), 640 * 400 * 4).slice();
  e.cc_render_cells(3, e.cc_framebuffer());
  const dyn = new Uint8Array(e.memory.buffer, e.cc_framebuffer(), 640 * 400 * 4).slice();
  for (let i = 0; i < baked.length; i++)
    if (baked[i] !== dyn[i]) throw new Error(`fresh cell render differs at byte ${i}`);

  e.cc_cell_set(9, 5, 0x0E, 0);
  e.cc_cell_set(9, 5, 0x0F, 0);
  e.cc_render_cells(3, e.cc_framebuffer());
  const cleared = new Uint8Array(e.memory.buffer, e.cc_framebuffer(), 640 * 400 * 4).slice();
  let changed = 0;
  for (let y = 0; y < 400; y++) {
    for (let x = 0; x < 640; x++) {
      const i = (y * 640 + x) * 4;
      const delta = dyn[i] !== cleared[i] || dyn[i + 1] !== cleared[i + 1] ||
                    dyn[i + 2] !== cleared[i + 2];
      if (delta) {
        changed++;
        if (x < 9 * 16 || x >= 10 * 16 || y < 5 * 16 || y >= 6 * 16)
          throw new Error(`crystal clear changed pixel outside its cell at ${x},${y}`);
      }
    }
  }
  if (changed === 0) throw new Error("clearing the crystal layer changed no pixels");
  console.log(`PASS: mutable cell renderer matches baked; crystal clear changed ${changed} pixels`);
})().catch((err) => { console.error(err); process.exit(1); });
