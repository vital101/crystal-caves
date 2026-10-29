// Compare the C object update loop against tools/emulate_game.py fixtures.
//   node test_oracle_objects.js <fixture.json>
const fs = require("fs");
const path = require("path");

const DIR = __dirname;
const fixture = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const fields = [
  ["state", "cc_obj_state"],
  ["x", "cc_obj_x"],
  ["y", "cc_obj_y"],
  ["tile", "cc_obj_tile"],
  ["p10", "cc_obj_p10"],
  ["p12", "cc_obj_p12"],
  ["p14", "cc_obj_p14"],
  ["p16", "cc_obj_p16"],
  ["p18", "cc_obj_p18"],
  ["p1a", "cc_obj_p1a"],
  ["shape", "cc_obj_shape"],
  ["p1e", "cc_obj_vx"],
  ["p20", "cc_obj_vy"],
  ["p22", "cc_obj_speed"],
  ["p24", "cc_obj_p24"],
  ["kind", "cc_obj_kind"],
  ["ready", "cc_obj_ready"],
  ["handler_off", "cc_obj_handler"],
];

(async () => {
  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")), {});
  const e = instance.exports;
  const cells = fs.readFileSync(path.join(DIR, "..", "out", "portable", "cells.bin"));
  const cp = e.cc_alloc(cells.length);
  new Uint8Array(e.memory.buffer, cp, cells.length).set(cells);
  e.cc_set_cells(cp);
  const level = fixture.level ?? fixture.frames[0].level;
  e.cc_objects_init(level);
  const p0 = fixture.frames[0].player;
  e.cc_set_player(p0.x, p0.y);

  function checkCells(cells, tag) {
    for (let r = 0; r < cells.length; r++) {
      for (let c = 0; c < cells[r].length; c++) {
        const hex = cells[r][c];
        for (let off = 0; off < 20; off++) {
          const want = parseInt(hex.substr(off * 2, 2), 16);
          const got = e.cc_cell_byte(c, r, off);
          if (got !== want) {
            throw new Error(`${tag} cell (${c},${r}) byte ${off}: C=${got} oracle=${want}`);
          }
        }
      }
    }
  }
  checkCells(fixture.initial_cells || fixture.frames[0].cells, "initial");

  let failures = 0;
  for (let f = 0; f < fixture.frames.length; f++) {
    e.cc_objects_step_objects();
    const want = fixture.frames[f];
    checkCells(want.cells, `frame ${f + 1}`);
    if (want.game) {
      if ((e.cc_lives() | 0) !== (want.game.lives | 0)) {
        console.error(`frame ${f + 1} lives: C=${e.cc_lives()} oracle=${want.game.lives}`);
        failures++;
      }
      if ((e.cc_score() >>> 0) !== (want.game.score >>> 0)) {
        console.error(`frame ${f + 1} score: C=${e.cc_score()} oracle=${want.game.score}`);
        failures++;
      }
    }
    for (const ob of want.objects) {
      if (ob.index === 1) continue;
      for (const [jf, cf] of fields) {
        const a = ob[jf] | 0;
        const b = e[cf](ob.index) | 0;
        if ((a & 0xFFFF) !== (b & 0xFFFF)) {
          console.error(`frame ${f + 1} slot ${ob.index} ${jf}: C=${b} oracle=${a}`);
          if (++failures > 30) process.exit(1);
        }
      }
    }
  }
  if (failures) {
    console.error(`\n${failures} mismatch(es)`);
    process.exit(1);
  }
  console.log(`PASS: ${fixture.frames.length} frames match oracle object state`);
})().catch((err) => { console.error(err); process.exit(1); });
