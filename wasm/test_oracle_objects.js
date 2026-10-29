// Compare the C object update loop against tools/emulate_game.py fixtures.
//   node test_oracle_objects.js <fixture.json>
const fs = require("fs");
const path = require("path");

const DIR = __dirname;
const fixture = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const fields = [
  ["state", 0x00], ["p02", 0x02], ["p04", 0x04],
  ["x", 0x06], ["y", 0x08], ["x_prev", 0x0a], ["y_prev", 0x0c],
  ["tile", 0x0e], ["p10", 0x10], ["p12", 0x12], ["p14", 0x14],
  ["p16", 0x16], ["p18", 0x18], ["p1a", 0x1a], ["shape", 0x1c],
  ["p1e", 0x1e], ["p20", 0x20], ["p22", 0x22], ["p24", 0x24],
  ["kind", 0x26], ["ready", 0x28], ["handler_off", 0x29],
  ["handler_seg", 0x2b],
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
      for (const [jf, off] of fields) {
        const a = ob[jf] | 0;
        const b = e.cc_obj_get(ob.index, off) | 0;
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
