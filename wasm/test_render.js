// Node harness: validate the freestanding wasm renderer against the Python one.
//   node test_render.js [level]
// Loads render.wasm + ../out/portable/{gfx.bin,levels.bin,palette.json},
// renders, reports non-black %, and writes out_wasm_levelNN.png (Node zlib).
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const DIR = __dirname;
const PORT = path.join(DIR, '..', 'out', 'portable');
const W = 640, H = 400;

function writePng(file, rgba) {
  const raw = Buffer.alloc((W * 4 + 1) * H);
  for (let y = 0; y < H; y++) {
    raw[y * (W * 4 + 1)] = 0; // filter none
    rgba.copy(raw, y * (W * 4 + 1) + 1, y * W * 4, (y + 1) * W * 4);
  }
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length, 0);
    const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td) >>> 0, 0);
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4);
  ihdr[8] = 8; ihdr[9] = 6; // 8-bit RGBA
  const png = Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
  fs.writeFileSync(file, png);
}

let T = null;
function crc32(buf) {
  if (!T) {
    T = new Int32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
      T[n] = c;
    }
  }
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = T[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

(async () => {
  const level = parseInt(process.argv[2] || '9', 10);
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(path.join(DIR, 'render.wasm')), {});
  const e = instance.exports;
  const gfx = fs.readFileSync(path.join(PORT, 'gfx.bin'));
  const levels = fs.readFileSync(path.join(PORT, 'levels.bin'));
  const pal = JSON.parse(fs.readFileSync(path.join(PORT, 'palette.json'), 'utf8'));

  const copyIn = (buf) => {
    const p = e.cc_alloc(buf.length);
    new Uint8Array(e.memory.buffer, p, buf.length).set(buf);
    return p;
  };
  const gfxP = copyIn(gfx), lvP = copyIn(levels);
  const palP = e.cc_alloc(128);
  const palU32 = new Uint32Array(e.memory.buffer, palP, 32);
  for (let i = 0; i < 32; i++) {
    const [r, g, b] = pal.colors[i];
    palU32[i] = (r << 16) | (g << 8) | b;
  }
  e.cc_set_assets(gfxP, lvP);
  e.cc_set_palette(palP);

  e.cc_render_level(level, e.cc_framebuffer());
  const fbP = e.cc_framebuffer();
  const rgba = Buffer.from(new Uint8Array(e.memory.buffer, fbP, W * H * 4));

  let nz = 0;
  for (let i = 0; i < W * H * 4; i += 4) if (rgba[i] | rgba[i + 1] | rgba[i + 2]) nz++;
  const out = path.join(DIR, `out_wasm_level${String(level).padStart(2, '0')}.png`);
  writePng(out, rgba);
  console.log(`wasm level ${level}: nonblack ${(100 * nz / (W * H)).toFixed(1)}% -> ${path.basename(out)}`);
})().catch((err) => { console.error(err); process.exit(1); });
