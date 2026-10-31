// R3: animated input cursor at the '^' marker cell.
//
// The original window routine locates the '^' marker and, after the initial
// key-flush, repaints that cell every 21 PIT ticks with F2 glyphs 45..49
// (0x0FC5:0x0A18/0x0A37).  Glyph 8 of F1 is the window interior used to
// clear the cell first.
const fs = require("fs");
const path = require("path");

const read = (p) => fs.readFileSync(path.join(__dirname, p));
const wasm = read("render.wasm");
const gfx = read("assets/gfx.bin");
const levels = read("assets/levels.bin");
const cells = read("assets/cells.bin");
const overlay = read("assets/level_overlay.bin");
const screens = read("assets/screens.bin");
const f1 = read("assets/mni_f1.bin");
const f2 = read("assets/mni_f2.bin");
const spl = read("assets/mni_spl.bin");
const pal = JSON.parse(read("assets/palette.json"));
const ENTER = 13;

let failures = 0;
function check(cond, msg) {
  if (!cond) {
    console.error("FAIL:", msg);
    if (++failures > 20) process.exit(1);
  } else console.log("PASS", msg);
}

(async () => {
  const { instance } = await WebAssembly.instantiate(wasm, {});
  const e = instance.exports;
  const mem = () => new Uint8Array(e.memory.buffer);
  const copy = (b) => { const p = e.cc_alloc(b.length); mem().set(b, p); return p; };
  e.cc_set_assets(copy(gfx), copy(levels));
  e.cc_set_cells(copy(cells));
  const ovp = e.cc_alloc(overlay.length); mem().set(overlay, ovp);
  e.cc_set_level_overlay(ovp);
  e.cc_set_screens(copy(screens));
  e.cc_text_set_fonts(copy(f1), copy(f2), copy(spl));
  const pp = e.cc_alloc(64);
  const p32 = new Uint32Array(e.memory.buffer, pp, 16);
  pal.colors.forEach(([r, g, b], i) => { p32[i] = (r << 16) | (g << 8) | b; });
  e.cc_set_palette(pp);

  // F2 cursor glyph bank: the animation uses glyphs 45..49 exactly as stored
  // in the original CC1-F2.MNI.  Glyph 49 is a byte-for-byte duplicate of 46
  // in the shipped art, so the five-frame cycle has four distinct images.
  const originalF2 = fs.readFileSync(
    path.join(__dirname, "..", "extracted", "CC1-F2.MNI")).slice(3, 3 + 2000);
  const glyph = (bank, i) => bank.slice(i * 40, (i + 1) * 40);
  check(f2.length === 2000, "F2 bank is 50 x 40-byte glyphs");
  check(Buffer.compare(f2, originalF2) === 0, "F2 export matches CC1-F2.MNI");
  check(Buffer.compare(glyph(f2, 46), glyph(f2, 49)) === 0,
        "F2 glyph 49 repeats glyph 46 (original art)");
  check(Buffer.compare(glyph(f2, 45), glyph(f2, 46)) !== 0,
        "F2 glyphs 45 and 46 are distinct");

  // Enter the intro story so page 0 ends with "Press Any Key ^".
  e.cc_shell_init();
  e.cc_shell_key(ENTER);            // logo -> attract
  e.cc_shell_key(ENTER);            // attract -> menu
  e.cc_shell_key("n".charCodeAt(0)); // new game -> intro
  check(e.cc_shell_state() === 3, "intro story entered");
  check(e.cc_shell_page_line_count() > 0, "page has lines");

  const lines = [];
  for (let i = 0; i < e.cc_shell_page_line_count(); i++) {
    const p = e.cc_shell_page_line(i);
    let s = "";
    for (let j = 0; p + j < e.memory.buffer.byteLength && mem()[p + j]; j++)
      s += String.fromCharCode(mem()[p + j]);
    lines.push(s);
  }
  let markerLine = -1, markerCol = -1;
  lines.forEach((s, i) => {
    const c = s.indexOf("^");
    if (c >= 0 && markerLine < 0) { markerLine = i; markerCol = c; }
  });
  check(markerLine >= 0, "page contains the cursor marker");

  const count = lines.length;
  let maxlen = 0;
  for (let i = 0; i < count; i++) {
    const n = lines[i].length - 1;
    if (i === 0) maxlen = n;
    if (n > maxlen) maxlen = n;
  }
  const h = count + 2;
  const wx = Math.floor((320 - maxlen * 8) / 2 / 8) - 3;
  const wy = Math.floor((200 - h * 8) / 2 / 8) - 2;
  const cx = wx + 2 + markerCol;
  const cy = wy + 2 + markerLine;
  check(cx >= 0 && cy >= 0 && cx < 40 && cy < 25, "cursor cell inside the view");

  const cellHash = () => {
    e.cc_shell_render();
    const fb = new Uint8Array(e.memory.buffer, e.cc_view_framebuffer(),
                              320 * 200 * 4);
    let hsh = 2166136261;
    for (let y = 0; y < 8; y++)
      for (let x = 0; x < 8; x++) {
        const o = ((cy * 8 + y) * 320 + (cx * 8 + x)) * 4;
        hsh ^= fb[o]; hsh = Math.imul(hsh, 16777619);
        hsh ^= fb[o + 1]; hsh = Math.imul(hsh, 16777619);
        hsh ^= fb[o + 2]; hsh = Math.imul(hsh, 16777619);
      }
    return hsh >>> 0;
  };

  const off = cellHash();            // frame 0: marker skipped, no caret
  e.cc_shell_step_ticks(20);
  const stillOff = cellHash();
  check(stillOff === off, "cursor stays hidden for the first 20 ticks");
  e.cc_shell_step_ticks(1);
  const f1h = cellHash();
  check(f1h !== off, "first cursor frame appears at tick 21");

  const frames = [f1h];
  for (let i = 0; i < 4; i++) {
    e.cc_shell_step_ticks(21);
    frames.push(cellHash());
  }
  // The F2 bank repeats glyph 46 at index 49, so the five-frame cycle has
  // four distinct images (an original-art quirk, not a decode error).
  check(new Set(frames).size === 4 && frames[4] === frames[1],
        "five-frame cursor cycle (49 repeats 46)");
  e.cc_shell_step_ticks(21);
  check(cellHash() === f1h, "cursor animation wraps after five frames");

  // The cell must be cleared back to the window interior, not black.
  const fb = new Uint8Array(e.memory.buffer, e.cc_view_framebuffer(),
                            320 * 200 * 4);
  const o = ((cy * 8) * 320 + cx * 8) * 4;
  check(fb[o] !== 0 || fb[o + 1] !== 0 || fb[o + 2] !== 0,
        "cursor cell sits on the window background");

  if (failures) {
    console.error(`\n${failures} failure(s)`);
    process.exit(1);
  }
  console.log("\nPASS: animated input cursor");
})();
