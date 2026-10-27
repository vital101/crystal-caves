// Node harness: C5 gameplay integration test.
//   node test_gameplay.js [level]
// Demonstrates: init, input, step, collectible collision, score, level complete.
const fs = require("fs");
const path = require("path");

const DIR = __dirname;

(async () => {
  const level = parseInt(process.argv[2] || "0", 10);

  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")),
    {}
  );
  const e = instance.exports;

  // Set up sound callback
  const soundCbPtr = e.cc_alloc(4);
  new Int32Array(e.memory.buffer, soundCbPtr, 1)[0] = 0xDEADBEEF; // placeholder

  let soundsPlayed = [];
  // We can't easily set a JS callback from Node without wasm-rt, so we'll
  // just verify sound state via the export mechanism.
  // Instead, test through score changes.

  e.cc_objects_init(level);
  console.log(`Level ${level}:`);
  console.log(`  collectibles_total=${e.cc_collectibles_total()} left=${e.cc_collectibles_left()}`);
  console.log(`  score=${e.cc_score()} lives=${e.cc_lives()} complete=${e.cc_level_complete()}`);

  // Count active objects
  let active = 0;
  for (let s = 0; s < e.cc_obj_count(); s++) {
    if (e.cc_obj_state(s) !== 2 && e.cc_obj_kind(s) !== 0) active++;
  }
  console.log(`  active objects: ${active}`);

  // Simulate gameplay: move right for a while, then left back to start
  // to collect the crystal at (0,0)
  console.log('\n--- Moving right ---');
  for (let f = 1; f <= 10; f++) {
    e.cc_input_set(0x338F, 1);
    e.cc_objects_step();
  }
  console.log(`  frame 10: player=(${e.cc_player_x()},${e.cc_player_y()}) cam=(${e.cc_cam_x()},${e.cc_cam_y()})`);
  console.log(`  score=${e.cc_score()} left=${e.cc_collectibles_left()}`);

  console.log('\n--- Moving left back to start ---');
  e.cc_input_set(0x338F, 0);
  e.cc_input_set(0x338E, 1);
  for (let f = 1; f <= 10; f++) {
    e.cc_objects_step();
  }
  console.log(`  frame 20: player=(${e.cc_player_x()},${e.cc_player_y()}) cam=(${e.cc_cam_x()},${e.cc_cam_y()})`);
  console.log(`  score=${e.cc_score()} left=${e.cc_collectibles_left()} complete=${e.cc_level_complete()}`);

  // Try moving up/down
  console.log('\n--- Moving up ---');
  e.cc_input_set(0x338E, 0);
  e.cc_input_set(0x338D, 1);
  for (let f = 1; f <= 5; f++) {
    e.cc_objects_step();
  }
  console.log(`  frame 25: player=(${e.cc_player_x()},${e.cc_player_y()})`);
  console.log(`  score=${e.cc_score()}`);

  console.log('\n--- Final state ---');
  console.log(`  score=${e.cc_score()} lives=${e.cc_lives()} left=${e.cc_collectibles_left()}`);
  console.log(`  level_complete=${e.cc_level_complete()}`);

  // Count remaining collectibles
  let remaining = 0;
  for (let s = 0; s < e.cc_obj_count(); s++) {
    if (e.cc_obj_kind(s) === 1 && e.cc_obj_state(s) !== 0x63) remaining++;
  }
  console.log(`  remaining kind-1 objects: ${remaining}`);

})().catch((err) => {
  console.error(err);
  process.exit(1);
});
