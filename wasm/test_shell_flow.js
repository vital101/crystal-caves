// R2: exact title/credits scroll regression.
//
// The fixture is captured from the real title routine
// (0x0FC5:0x8265 / linear 0x17EB5): a 433-tick hold, a 0..200 forward
// traverse at one row per vertical retrace (~2 PIT ticks), a 433-tick
// credits hold, then 200..0.  The virtual screen is cc1.ttl over cc1.cdt.
const fs = require("fs");
const path = require("path");

const read = (p) => fs.readFileSync(path.join(__dirname, p));
const FLOW = JSON.parse(read("../notes/shell_flow_fixtures.json"));
const wasm = read("render.wasm");
const screens = read("assets/screens.bin");
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
  e.cc_set_screens(copy(screens));
  const pp = e.cc_alloc(64);
  const p32 = new Uint32Array(e.memory.buffer, pp, 16);
  pal.colors.forEach(([r, g, b], i) => { p32[i] = (r << 16) | (g << 8) | b; });
  e.cc_set_palette(pp);

  const row = () => e.cc_shell_scroll_row();
  const dir = () => e.cc_shell_scroll_dir();
  const phase = () => e.cc_shell_scroll_phase();
  const state = () => e.cc_shell_state();

  const timing = FLOW.timing;
  check(timing.hold_ticks === 433, "fixture hold is 433 ticks");
  check(timing.scroll_rows === 200, "fixture scroll is 200 rows");
  check(timing.ticks_per_row === 2, "fixture row cadence is 2 ticks");
  check(FLOW.forward_rows.length === 201 &&
        FLOW.forward_rows[0] === 0 &&
        FLOW.forward_rows[200] === 200, "fixture forward rows 0..200");
  check(FLOW.backward_rows.length === 201 &&
        FLOW.backward_rows[0] === 200 &&
        FLOW.backward_rows[200] === 0, "fixture backward rows 200..0");
  check(FLOW.sound_events.length === 0, "title routine plays no sound");

  // ---- logo hold -------------------------------------------------------
  e.cc_shell_init();
  check(state() === 0, "starts at logo");
  e.cc_shell_step_ticks(720);
  check(state() === 0, "logo holds for 720 ticks");
  e.cc_shell_step_ticks(1);
  check(state() === 15, "logo ends at tick 721 into the attract");

  // ---- title hold ------------------------------------------------------
  for (let t = 0; t < 432; t++) e.cc_shell_step_ticks(1);
  check(row() === 0 && phase() === 0 && dir() === 1,
        "title hold: row 0 for 432 ticks");
  e.cc_shell_step_ticks(1);
  check(phase() === 1 && row() === 0,
        "forward traverse starts at row 0 (tick 433)");

  // ---- forward traverse ------------------------------------------------
  let seen = [row()];
  let last = row();
  for (let t = 0; t < 402; t++) {
    e.cc_shell_step_ticks(1);
    if (row() !== last) { last = row(); seen.push(last); }
  }
  check(phase() === 2 && row() === 200, "forward ends in the credits hold");
  check(JSON.stringify(seen) === JSON.stringify(FLOW.forward_rows),
        "forward row sequence matches the oracle");

  // ---- credits hold ----------------------------------------------------
  for (let t = 0; t < 432; t++) e.cc_shell_step_ticks(1);
  check(phase() === 2 && row() === 200, "credits hold survives 432 ticks");
  e.cc_shell_step_ticks(1);
  check(phase() === 3 && row() === 200, "backward traverse starts at row 200");

  // ---- backward traverse ----------------------------------------------
  seen = [row()];
  last = row();
  for (let t = 0; t < 402; t++) {
    e.cc_shell_step_ticks(1);
    if (row() !== last) { last = row(); seen.push(last); }
  }
  check(phase() === 0 && row() === 0 && dir() === 1,
        "backward ends at the title hold");
  check(JSON.stringify(seen) === JSON.stringify(FLOW.backward_rows),
        "backward row sequence matches the oracle");

  // ---- pixel comparison at sampled rows -------------------------------
  const expectCrop = (r) => {
    const out = new Uint8Array(320 * 200 * 4);
    for (let y = 0; y < 200; y++) {
      const sy = r + y;
      const src = sy < 200 ? 1 : 2;
      const off = (src * 320 * 200 + (sy % 200) * 320);
      for (let x = 0; x < 320; x++) {
        const c = pal.colors[screens[off + x] & 15];
        const d = (y * 320 + x) * 4;
        out[d] = c[0]; out[d + 1] = c[1]; out[d + 2] = c[2]; out[d + 3] = 255;
      }
    }
    return out;
  };
  const view = () => new Uint8Array(
    e.memory.buffer, e.cc_view_framebuffer(), 320 * 200 * 4);
  const same = (a, b) => {
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
  };

  e.cc_shell_init();
  e.cc_shell_key(ENTER);                 // logo -> attract
  for (const target of [0, 1, 50, 100, 199, 200]) {
    // forward pass: restart and step to the row
    e.cc_shell_init();
    e.cc_shell_key(ENTER);
    e.cc_shell_step_ticks(433);          // leave the title hold
    let guard = 0;
    while (row() !== target && guard++ < 1000) e.cc_shell_step_ticks(2);
    e.cc_shell_render();
    check(same(view(), expectCrop(target)), `forward frame row ${target}`);
  }
  for (const target of [199, 150, 100, 1, 0]) {
    e.cc_shell_init();
    e.cc_shell_key(ENTER);
    e.cc_shell_step_ticks(433 + 402 + 433);   // through the credits hold
    let guard = 0;
    while (row() !== target && guard++ < 1000) e.cc_shell_step_ticks(2);
    e.cc_shell_render();
    check(same(view(), expectCrop(target)), `backward frame row ${target}`);
  }

  // ---- sound -----------------------------------------------------------
  e.cc_sound_init();
  e.cc_sound_log_clear();
  e.cc_shell_init();
  check(e.cc_sound_log_count() === 1 && e.cc_sound_log_at(0) === 32,
        "logo plays ID 32");
  e.cc_sound_log_clear();
  e.cc_shell_step_ticks(721);
  for (let t = 0; t < 1670; t++) e.cc_shell_step_ticks(1);
  check(e.cc_sound_log_count() === 0, "attract sequence is silent");

  // ---- key skip --------------------------------------------------------
  e.cc_shell_init();
  e.cc_shell_key(ENTER);
  e.cc_shell_step_ticks(100);
  e.cc_shell_key(ENTER);
  check(state() === 2, "key during the title hold opens the menu");
  e.cc_shell_init();
  e.cc_shell_key(ENTER);
  e.cc_shell_step_ticks(433 + 100);
  e.cc_shell_key(ENTER);
  check(state() === 2, "key during the forward scroll opens the menu");

  // ---- menu quit restarts the attract ---------------------------------
  e.cc_shell_init();
  e.cc_shell_key(ENTER);
  e.cc_shell_step_ticks(433 + 200);
  e.cc_shell_key(ENTER);
  check(state() === 2, "menu reached");
  e.cc_shell_key("q".charCodeAt(0));
  check(state() === 15 && row() === 0 && phase() === 0,
        "quit to title restarts at the title hold");

  if (failures) {
    console.error(`\n${failures} failure(s)`);
    process.exit(1);
  }
  console.log("\nPASS: exact title/credits scroll");
})();
