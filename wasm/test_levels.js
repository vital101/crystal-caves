// Functional level-transition checks (hub door -> cave, cave exit -> hub).
const fs = require("fs");
const path = require("path");
const DIR = __dirname;
(async () => {
  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")), {});
  const e = instance.exports;
  const cells = fs.readFileSync(path.join(DIR, "..", "out", "portable", "cells.bin"));
  const cp = e.cc_alloc(cells.length);
  new Uint8Array(e.memory.buffer, cp, cells.length).set(cells);
  e.cc_set_cells(cp);

  // Enter cave 1 through hub door 1 at (5,8).
  e.cc_objects_init(2);
  e.cc_global_set(0x4611, 80);
  e.cc_global_set(0x4613, 128);
  for (let i = 0; i < 4; i++) e.cc_objects_step_full();
  if (e.cc_cur_level() !== 3) throw new Error(`hub door loaded level ${e.cc_cur_level()}, expected 3`);
  if (e.cc_level_arg() !== 1) throw new Error(`hub door arg ${e.cc_level_arg()}, expected 1`);
  if (e.cc_player_x() !== 64 || e.cc_player_y() !== 128)
    throw new Error(`hub door spawn ${e.cc_player_x()},${e.cc_player_y()}, expected 64,128`);

  // Exit level 3: unlock the exit, stand on it, complete the animation.
  e.cc_objects_init(3);
  e.cc_global_set(0x4611, 37 * 16 + 3);
  e.cc_global_set(0x4613, 17 * 16);
  e.cc_global_set(0xA0F5, 1);
  e.cc_global_set(0x513C, 1);
  e.cc_global_set(0xA0BE, 1);
  for (let i = 0; i < 19; i++) e.cc_objects_step_full();
  if (e.cc_cur_level() !== 2) throw new Error(`cave exit loaded level ${e.cc_cur_level()}, expected 2`);
  if (e.cc_level_arg() !== 0) throw new Error(`cave exit arg ${e.cc_level_arg()}, expected 0`);
  if (e.cc_cave_flag(1) !== 0) throw new Error("cave 1 completion flag was not cleared");

  console.log("PASS: hub door and cave exit transitions load the correct levels");
})().catch((err) => { console.error(err); process.exit(1); });
