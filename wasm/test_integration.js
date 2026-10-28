// Node harness: C5 WASM integration - render level + objects on top.
//   node test_integration.js [level]
// Renders the level map, then draws every active object sprite at its
// camera-relative position. Reports drawn objects numerically.
const fs = require("fs");
const path = require("path");

const DIR = __dirname;
const W = 640, H = 400;

(async () => {
  const level = parseInt(process.argv[2] || "0", 10);

  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")),
    {}
  );
  const e = instance.exports;

  // Load assets
  const gfx = fs.readFileSync(path.join(DIR, '..', 'out', 'portable', 'gfx.bin'));
  const levels = fs.readFileSync(path.join(DIR, '..', 'out', 'portable', 'levels.bin'));
  const pal = JSON.parse(fs.readFileSync(path.join(DIR, '..', 'out', 'portable', 'palette.json'), 'utf8'));

  const copyIn = (buf) => {
    const p = e.cc_alloc(buf.length);
    new Uint8Array(e.memory.buffer, p, buf.length).set(buf);
    return p;
  };
  const gfxP = copyIn(gfx), lvP = copyIn(levels);
  const palP = e.cc_alloc(64);
  const palU32 = new Uint32Array(e.memory.buffer, palP, 16);
  for (let i = 0; i < 16 && i < pal.colors.length; i++) {
    const [r, g, b] = pal.colors[i];
    palU32[i] = (r << 16) | (g << 8) | b;
  }
  e.cc_set_assets(gfxP, lvP);
  e.cc_set_palette(palP);

  // Init objects
  e.cc_objects_init(level);
  const camX = e.cc_cam_x();
  const camY = e.cc_cam_y();
  console.log(`level ${level}: cam=(${camX},${camY})`);

  // Render level map
  e.cc_render_level(level, e.cc_framebuffer());

  // Draw all active objects
  let drawn = 0;
  for (let s = 0; s < e.cc_obj_count(); s++) {
    const st = e.cc_obj_state(s);
    const kind = e.cc_obj_kind(s);
    if (st === 2 || kind === 0) continue; // inactive/empty

    const ox = e.cc_obj_x(s);
    const oy = e.cc_obj_y(s);
    const block = e.cc_obj_tile(s);
    const shape = e.cc_obj_shape(s);

    // Screen position relative to camera
    const sx = ox - camX;
    const sy = oy - camY;

    e.cc_draw_object(block, shape, sx, sy);
    drawn++;
    console.log(`  slot ${String(s).padStart(2)} kind=${String(kind).padStart(3)} block=${block} shape=${shape} world=(${ox},${oy}) screen=(${sx},${sy}) state=${st}`);
  }

  console.log(`\nDrew ${drawn} objects on level ${level}`);

  // Count non-transparent pixels in framebuffer
  const fb = new Uint8Array(e.memory.buffer, e.cc_framebuffer(), W * H * 4);
  let nz = 0;
  for (let i = 0; i < W * H * 4; i += 4) if (fb[i] | fb[i + 1] | fb[i + 2]) nz++;
  console.log(`Framebuffer: ${(100 * nz / (W * H)).toFixed(1)}% non-black`);

})().catch((err) => {
  console.error(err);
  process.exit(1);
});
