// 0x20406 first interaction block: place the player on a level-3 kind-44
// crystal/bonus object and verify score/lives/ready effects.
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

  e.cc_objects_init(3);
  e.cc_set_player(144, 32);
  /* Keep the object inside the camera window: the update loop only runs
   * handlers for visible objects (0xF38). */
  e.cc_phys_set(2, 0);   /* CC_PHYS_CAM_X */
  e.cc_phys_set(3, 0);   /* CC_PHYS_CAM_Y */
  e.cc_objects_step_objects();

  const ready = e.cc_obj_ready(5);
  const score = e.cc_score() >>> 0;
  const lives = e.cc_lives();
  console.log(`slot5 ready=${ready} score=${score} lives=${lives}`);
  if (ready !== 1) throw new Error("kind-44 object was not marked collected");
  if (score !== 1000) throw new Error(`score ${score} != 1000`);
  if (lives !== 3) throw new Error(`lives ${lives} != 3 (damage + pickup)`);
  console.log("PASS: 0x20406 pickup interaction");
})().catch((err) => { console.error(err); process.exit(1); });
