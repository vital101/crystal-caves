// Episode 2 compiled-data regression: render_ep2.wasm must reproduce the
// original loader's object image, player/camera start state, cursor, cells and
// key globals for every level.
//
// Episode 2 data is bring-your-own and git-ignored; this harness SKIPS (exit
// 0) unless the ep2 oracle dumps and the generated build data exist:
//
//   python3 ../tools/emulate_startup.py --episode ep2
//   python3 ../tools/emulate_load.py --episode ep2 --out ../notes/ep2/oracle_objects.json
//   python3 ../tools/build_oracle_catalog.py --episode ep2
//   python3 ../tools/export_assets.py --episode ep2
//   make -C wasm ep2
'use strict';

const fs = require('fs');
const path = require('path');

const DIR = __dirname;
const ROOT = path.join(DIR, '..');
const WASM = path.join(DIR, 'render_ep2.wasm');
const CATALOG = path.join(ROOT, 'notes', 'ep2', 'oracle_catalog.json');
const STARTUP = path.join(ROOT, 'notes', 'ep2', 'startup.json');
const ORACLE = path.join(ROOT, 'notes', 'ep2', 'oracle_objects.json');
const CELLS = path.join(ROOT, 'out', 'portable_ep2', 'cells.bin');

for (const p of [WASM, CATALOG, STARTUP, ORACLE, CELLS]) {
  if (!fs.existsSync(p)) {
    console.log(`SKIP test-ep2-data: missing ${path.relative(ROOT, p)} (BYO data)`);
    process.exit(0);
  }
}

const catalog = JSON.parse(fs.readFileSync(CATALOG, 'utf8'));
const startup = JSON.parse(fs.readFileSync(STARTUP, 'utf8'));
const oracle = JSON.parse(fs.readFileSync(ORACLE, 'utf8'));

const LEVELS = 19;
const FIELDS = [
  ['state', 0x00], ['p02', 0x02], ['p04', 0x04],
  ['x', 0x06], ['y', 0x08], ['x_prev', 0x0a], ['y_prev', 0x0c],
  ['tile', 0x0e], ['p10', 0x10], ['p12', 0x12], ['p14', 0x14],
  ['p16', 0x16], ['p18', 0x18], ['p1a', 0x1a], ['shape', 0x1c],
  ['p1e', 0x1e], ['p20', 0x20], ['p22', 0x22], ['p24', 0x24],
  ['kind', 0x26], ['ready', 0x28], ['handler_off', 0x29],
  ['handler_seg', 0x2b],
];
const GLOBALS = [0x02b8, 0x5122, 0xa0be, 0xa0cc, 0x4611, 0x4613, 0x512e, 0x5130];

(async () => {
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(WASM), {});
  const e = instance.exports;
  const cellsBytes = fs.readFileSync(CELLS);
  const cp = e.cc_alloc(cellsBytes.length);
  new Uint8Array(e.memory.buffer, cp, cellsBytes.length).set(cellsBytes);
  e.cc_set_cells(cp);

  let failures = 0;
  const bad = (msg) => {
    console.error(msg);
    if (++failures > 40) { console.error('too many failures'); process.exit(1); }
  };
  const check = (msg, got, want) => {
    if (((got | 0) & 0xffff) !== ((want | 0) & 0xffff)) {
      bad(`${msg}: C=${got | 0} oracle=${want | 0}`);
    }
  };

  for (let lv = 0; lv < LEVELS; lv++) {
    const L = catalog.levels[String(lv)];
    const G = startup.levels[String(lv)].globals;
    e.cc_objects_init(lv);

    // Player, camera, cursor and the loader-visible globals.
    check(`L${lv} player_x`, e.cc_player_x(), G['4611']);
    check(`L${lv} player_y`, e.cc_player_y(), G['4613']);
    check(`L${lv} cam_x`, e.cc_cam_x(), G['512e']);
    check(`L${lv} cam_y`, e.cc_cam_y(), G['5130']);
    check(`L${lv} cursor`, e.cc_obj_cursor(), L.cursor);
    for (const off of GLOBALS) {
      const key = off.toString(16);
      if (G[key] !== undefined) check(`L${lv} global ${key}`, e.cc_global_get(off), G[key]);
    }

    // Object image (slot 1 is the player, compared via the accessors above).
    for (const ob of L.objects) {
      if (ob.index === 1) continue;
      for (const [jf, off] of FIELDS) {
        check(`L${lv} slot ${ob.index} ${jf}`, e.cc_obj_get(ob.index, off), ob[jf]);
      }
    }

    // Full 20-byte cells.
    const grid = oracle[String(lv)].cells;
    for (let r = 0; r < 25; r++) {
      for (let c = 0; c < 40; c++) {
        const hex = grid[r][c];
        for (let off = 0; off < 20; off++) {
          const want = parseInt(hex.substr(off * 2, 2), 16);
          const got = e.cc_cell_byte(c, r, off);
          if ((got & 0xff) !== want) {
            bad(`L${lv} cell (${c},${r}) byte ${off}: C=${got} oracle=${want}`);
          }
        }
      }
    }
    console.log(`ok  level ${lv}`);
  }

  // The only cell-event rule that differs between episodes: a crystal is
  // w6=0x0D with w7 in the episode's range.  Pick one up through the real
  // cell-event handler and confirm the counters and cell clear.
  for (const lv of [3, 14]) {
    const grid = oracle[String(lv)].cells;
    let crystal = null;
    for (let r = 0; r < 25 && !crystal; r++) {
      for (let c = 0; c < 40 && !crystal; c++) {
        const w6 = parseInt(grid[r][c].substr(24, 2), 16)
                 | (parseInt(grid[r][c].substr(26, 2), 16) << 8);
        const w7 = parseInt(grid[r][c].substr(28, 2), 16)
                 | (parseInt(grid[r][c].substr(30, 2), 16) << 8);
        if (w6 === 0x0d && w7 >= 0x0d && w7 <= 0x10) crystal = { r, c };
      }
    }
    if (!crystal) {
      bad(`L${lv}: no episode-2 crystal cell found in the oracle dump`);
      continue;
    }
    e.cc_objects_init(lv);
    const before = e.cc_crystals_remaining() | 0;
    const collected = e.cc_crystals_collected() | 0;
    const score = e.cc_score() >>> 0;
    e.cc_test_cell_event(crystal.c * 16 + 8, crystal.r * 16 + 8);
    if ((e.cc_crystals_remaining() | 0) !== before - 1) {
      bad(`L${lv} crystal pickup: remaining ${e.cc_crystals_remaining()} != ${before - 1}`);
    }
    if ((e.cc_crystals_collected() | 0) !== collected + 1) {
      bad(`L${lv} crystal pickup: collected ${e.cc_crystals_collected()} != ${collected + 1}`);
    }
    if ((e.cc_score() >>> 0) === score) {
      bad(`L${lv} crystal pickup: score unchanged`);
    }
    const w7 = e.cc_cell_byte(crystal.c, crystal.r, 0x0e)
             | (e.cc_cell_byte(crystal.c, crystal.r, 0x0f) << 8);
    if (w7 !== 0) bad(`L${lv} crystal pickup: cell w7=${w7}, expected 0`);
    console.log(`ok  level ${lv} crystal pickup (ep2 rule 0x0D..0x10)`);
  }

  if (failures) {
    console.error(`\n${failures} episode-2 data mismatch(es)`);
    process.exit(1);
  }
  console.log('\nALL 19 EPISODE 2 LEVELS: objects, start state and cells match the oracle');
})().catch((err) => { console.error(err); process.exit(1); });
