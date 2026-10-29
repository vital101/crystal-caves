// Node harness: step the object simulation with input/camera/collision (C4).
//   node test_objects.js [level] [frames]
// Simulates left/right input and reports player position, camera, and object state.
const fs = require("fs");
const path = require("path");

const DIR = __dirname;

(async () => {
  const level = parseInt(process.argv[2] || "0", 10);
  const frames = parseInt(process.argv[3] || "30", 10);

  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")),
    {}
  );
  const e = instance.exports;
  const cells = fs.readFileSync(path.join(DIR, "..", "out", "portable", "cells.bin"));
  const cp = e.cc_alloc(cells.length);
  new Uint8Array(e.memory.buffer, cp, cells.length).set(cells);
  e.cc_set_cells(cp);

  e.cc_objects_init(level);
  const startX = e.cc_player_x();
  const startY = e.cc_player_y();

  // Show initial state
  console.log(`level ${level}: cursor=${e.cc_obj_cursor()}`);
  console.log(`  player=(${startX},${startY}) cam=(${e.cc_cam_x()},${e.cc_cam_y()})`);

  // The oracle catalog records the level's Y-cell start; level 0 is (5,5).
  if (level === 0 && (startX !== 80 || startY !== 80)) {
    throw new Error(`level 0 start ${startX},${startY} != 80,80`);
  }

  // Count active objects
  let active = 0;
  for (let s = 0; s < e.cc_obj_count(); s++) {
    if (e.cc_obj_state(s) !== 2 && e.cc_obj_kind(s) !== 0) active++;
  }
  console.log(`  active objects: ${active}`);
  if (active === 0) throw new Error("no active objects loaded");

  // Simulate input: press right for first half, then release
  for (let f = 1; f <= frames; f++) {
    if (f <= frames / 2) {
      e.cc_input_set(0x338F, 1);   // right pressed
      e.cc_input_set(0x338E, 0);
    } else {
      e.cc_input_set(0x338F, 0);   // right released
      e.cc_input_set(0x338E, 0);
    }

    e.cc_objects_step();

    if (f <= 5 || f % 10 === 0 || f === frames) {
      const px = e.cc_player_x();
      const py = e.cc_player_y();
      const cx = e.cc_cam_x();
      const cy = e.cc_cam_y();
      const dir = e.cc_move_dir();
      const collide = e.cc_collide_player(1, 16);
      console.log(`frame ${f}: player=(${px},${py}) cam=(${cx},${cy}) dir=${dir} collide=${collide}`);

      // Report moving objects
      for (let s = 0; s < e.cc_obj_count(); s++) {
        const st = e.cc_obj_state(s);
        const kind = e.cc_obj_kind(s);
        if (st !== 2 && kind !== 0) {
          const ox = e.cc_obj_x(s);
          const oy = e.cc_obj_y(s);
          console.log(`   slot ${String(s).padStart(2)} kind=${String(kind).padStart(3)} @(${ox},${oy}) state=${st} ready=${e.cc_obj_ready(s)}`);
        }
      }
    }
  }

  // Final collision check. Slot 1 is the player and must never be returned.
  const c1 = e.cc_collide_player(1, 16);
  const c2 = e.cc_collide_player(2, 16);
  console.log(`\nFinal: collide(mode=1,r=16)=${c1}`);
  console.log(`       collide(mode=2,r=16)=${c2}`);
  if (c1 === 1 || c2 === 1) throw new Error("collision returned the player slot");

  if (level === 0 && e.cc_player_x() <= startX) {
    throw new Error(`right input did not move player: ${startX} -> ${e.cc_player_x()}`);
  }
  console.log("PASS: object init/input/collision checks");

})().catch((err) => {
  console.error(err);
  process.exit(1);
});
