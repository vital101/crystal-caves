// Viewport renderer regression: the 320x200 view must equal the corresponding
// crop of the full 640x400 level buffer for several levels/camera positions.
const fs = require("fs");
const path = require("path");

const DIR = __dirname;
const PORT = path.join(DIR, "..", "out", "portable");
const W = 320, H = 200, FW = 640;

(async () => {
  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")), {});
  const e = instance.exports;
  const gfx = fs.readFileSync(path.join(PORT, "gfx.bin"));
  const levels = fs.readFileSync(path.join(PORT, "levels.bin"));
  const pal = JSON.parse(fs.readFileSync(path.join(PORT, "palette.json"), "utf8"));
  const copyIn = (buf) => {
    const p = e.cc_alloc(buf.length);
    new Uint8Array(e.memory.buffer, p, buf.length).set(buf);
    return p;
  };
  e.cc_set_assets(copyIn(gfx), copyIn(levels));
  const palP = e.cc_alloc(64);
  const palU32 = new Uint32Array(e.memory.buffer, palP, 16);
  pal.colors.forEach(([r, g, b], i) => { palU32[i] = (r << 16) | (g << 8) | b; });
  e.cc_set_palette(palP);

  let n = 0;
  for (const level of [0, 2, 3, 9, 18]) {
    for (const [cx, cy] of [[0, 0], [100, 50], [320, 200]]) {
      e.cc_render_level(level, e.cc_framebuffer());
      const full = new Uint8Array(e.memory.buffer, e.cc_framebuffer(), FW * 400 * 4).slice();
      e.cc_render_view(level, cx, cy);
      const view = new Uint8Array(e.memory.buffer, e.cc_view_framebuffer(), W * H * 4);
      const x0 = Math.max(0, Math.min(FW - W, cx));
      const y0 = Math.max(0, Math.min(400 - H, cy));
      for (let y = 0; y < H; y++) {
        for (let x = 0; x < W; x++) {
          const a = ((y + y0) * FW + (x + x0)) * 4;
          const b = (y * W + x) * 4;
          if (full[a] !== view[b] || full[a + 1] !== view[b + 1] ||
              full[a + 2] !== view[b + 2]) {
            throw new Error(`level ${level} cam (${cx},${cy}) differs at ${x},${y}`);
          }
        }
      }
      n++;
    }
  }
  console.log(`PASS: ${n} viewport crops match the full level buffer`);
})().catch((err) => { console.error(err); process.exit(1); });
