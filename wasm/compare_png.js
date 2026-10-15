// Pixel-exact compare of two PNGs (8-bit RGB/RGBA). Usage: node compare_png.js a.png b.png
const fs = require('fs');
const zlib = require('zlib');

function decodePng(file) {
  const b = fs.readFileSync(file);
  let o = 8, w = 0, h = 0, ct = 0, bd = 0, idat = [];
  while (o < b.length) {
    const len = b.readUInt32BE(o); const typ = b.toString('ascii', o + 4, o + 8);
    const data = b.slice(o + 8, o + 8 + len);
    if (typ === 'IHDR') { w = data.readUInt32BE(0); h = data.readUInt32BE(4); bd = data[8]; ct = data[9]; }
    else if (typ === 'IDAT') idat.push(data);
    else if (typ === 'IEND') break;
    o += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const ch = ct === 6 ? 4 : ct === 2 ? 3 : 1;
  const stride = w * ch;
  const out = Buffer.alloc(h * stride);
  let p = 0;
  for (let y = 0; y < h; y++) {
    const f = raw[p++];
    const line = raw.slice(p, p + stride); p += stride;
    for (let x = 0; x < stride; x++) {
      const a = x >= ch ? out[y * stride + x - ch] : 0;
      const bb = y > 0 ? out[(y - 1) * stride + x] : 0;
      const c = (x >= ch && y > 0) ? out[(y - 1) * stride + x - ch] : 0;
      let v = line[x];
      switch (f) {
        case 1: v = (v + a) & 255; break;
        case 2: v = (v + bb) & 255; break;
        case 3: v = (v + ((a + bb) >> 1)) & 255; break;
        case 4: { const pa = Math.abs(bb - c), pb = Math.abs(a - c), pc = Math.abs(a + bb - 2 * c);
                  const pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? bb : c); v = (v + pr) & 255; break; }
      }
      out[y * stride + x] = v;
    }
  }
  return { w, h, ch, data: out };
}

const A = decodePng(process.argv[2]);
const B = decodePng(process.argv[3]);
if (A.w !== B.w || A.h !== B.h) { console.log(`size differs ${A.w}x${A.h} vs ${B.w}x${B.h}`); process.exit(1); }
let nz = 0, first = -1;
for (let i = 0; i < A.w * A.h; i++) {
  const ar = A.data[i * A.ch], ag = A.data[i * A.ch + 1], ab = A.data[i * A.ch + 2];
  const br = B.data[i * B.ch], bg = B.data[i * B.ch + 1], bb = B.data[i * B.ch + 2];
  if (ar !== br || ag !== bg || ab !== bb) { nz++; if (first < 0) first = i; }
}
console.log(`${process.argv[2]} vs ${process.argv[3]}: ${A.w}x${A.h} differing_pixels=${nz}${nz ? ' first@' + first : ''}`);
