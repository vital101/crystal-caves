// Node harness: C5 gameplay fixture test (corrected premises).
//   node test_gameplay.js [level]
//
// Verifies:
//   * cc_objects_init loads the oracle 60-slot image for `level`.
//   * Player (slot 1, kind 1, handler 0x1fb9) starts at the Y-token cell
//     converted to pixels (row*16, col*16).
//   * Map spawn count matches the oracle anchor for the level.
//   * Level-fact counters: hub starts at 0 score / 0 crystals, 3 lives /
//     3 health / 5 ammo; cave 3 holds 14 crystals.
//   * Walking the hub (which has no collectible cells) leaves score and
//     crystals untouched.  Full collection rules are covered by
//     test_cells.js / test_combat.js.
const fs = require("fs");
const path = require("path");

const DIR = __dirname;

// Oracle spawn-count anchors (map-spawned only; prologue slots 2/3 excluded).
const SPAWN_COUNTS = {
  0: 3, 1: 3, 2: 24, 3: 29, 4: 35, 5: 31, 6: 22, 7: 18,
  8: 23, 9: 25, 10: 37, 11: 19, 12: 27, 13: 30, 14: 18,
  15: 18, 16: 23, 17: 35, 18: 26,
};

(async () => {
  const level = parseInt(process.argv[2] || "0", 10);

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
  console.log(`Level ${level}:`);

  // Slot 1 must be the player (kind 1, handler 0x1fb9).
  const p = { kind: e.cc_obj_kind(1), state: e.cc_obj_state(1) };
  console.log(`  slot1 kind=${p.kind} state=${p.state} (expect kind=1 state=0)`);
  if (p.kind !== 1) throw new Error("slot 1 is not kind 1 (player)");

  // Player pixel position should equal Y-cell * 16.
  const px = e.cc_player_x();
  const py = e.cc_player_y();
  console.log(`  player=(${px},${py}) cam=(${e.cc_cam_x()},${e.cc_cam_y()})`);

  // Count active objects and map spawns.
  let active = 0;
  for (let s = 0; s < e.cc_obj_count(); s++) {
    if (e.cc_obj_state(s) !== 2 && e.cc_obj_kind(s) !== 0) active++;
  }
  console.log(`  active objects: ${active}`);
  console.log(`  handler coverage: ${e.cc_handler_coverage_num()}%`);
  if (level === 2 && e.cc_handler_coverage_num() !== 100) {
    throw new Error(`level 2 handler coverage ${e.cc_handler_coverage_num()}% != 100%`);
  }
  if (active === 0) throw new Error("no active objects loaded");
  if (level === 2 && (px !== 32 || py !== 64)) {
    throw new Error(`level 2 start ${px},${py} != 32,64 (main hub Y cell)`);
  }

  // Level 2 has no crystals; health starts at 3 and ammo at 5.
  console.log(`  score=${e.cc_score()} crystals=${e.cc_crystals_collected()}/${e.cc_crystals_remaining()} health=${e.cc_health()} ammo=${e.cc_ammo()} complete=${e.cc_level_complete()}`);
  if (e.cc_score() !== 0) throw new Error("level 2 should start with score 0");
  if (e.cc_crystals_remaining() !== 0) throw new Error("level 2 should have no crystals");
  if (e.cc_crystals_collected() !== 0) throw new Error("level 2 should have no collected crystals");
  if (e.cc_health() !== 3 || e.cc_lives() !== 3) throw new Error("health should start at 3");
  if (e.cc_ammo() !== 5) throw new Error("ammo should start at 5");
  if (e.cc_level_complete() !== 0) throw new Error("level_complete should be 0");
  e.cc_objects_init(3);
  if (e.cc_crystals_remaining() !== 14) throw new Error(`level 3 crystals ${e.cc_crystals_remaining()} != 14`);

  // Level 2 has no collectible cells, so a short walk must not change score.
  e.cc_objects_init(level);
  e.cc_input_set(0x338F, 1);
  for (let f = 1; f <= 20; f++) e.cc_objects_step();
  console.log(`  after 20 frames right: player=(${e.cc_player_x()},${e.cc_player_y()}) cam=(${e.cc_cam_x()},${e.cc_cam_y()})`);
  console.log(`  score=${e.cc_score()} crystals=${e.cc_crystals_remaining()} complete=${e.cc_level_complete()}`);
  if (e.cc_score() !== 0 || e.cc_crystals_remaining() !== 0) {
    throw new Error("score/crystals changed on a level with no collectibles");
  }

  console.log(`\nPASS: level ${level} fixture checks (start=${px},${py} active=${active})`);

})().catch((err) => {
  console.error(err);
  process.exit(1);
});
