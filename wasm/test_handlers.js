// Compare the C handler/object state against notes/handler_fixtures.json.
//   node test_handlers.js [fixture.json]
const fs = require("fs");
const path = require("path");

const DIR = __dirname;
const FILE = process.argv[2] ||
  path.join(DIR, "..", "notes", "handler_fixtures.json");
const fixture = JSON.parse(fs.readFileSync(FILE, "utf8"));
const only = process.argv[3]
  ? new Set(process.argv[3].split(",").map((h) => parseInt(h, 16)))
  : null;

const INPUT_FLAGS = [
  ["left", 0x338E], ["right", 0x338F], ["jump", 0x338B],
  ["fire", 0x3390], ["down", 0x338C],
];

let failures = 0;

function fail(tag, msg) {
  console.error(`${tag}: ${msg}`);
  if (++failures > 40) process.exit(1);
}

function hexToBytes(hex) {
  return Uint8Array.from(Buffer.from(hex, "hex"));
}
function bytesToHex(bytes) {
  return Buffer.from(bytes).toString("hex");
}

function cloneBaseline(base) {
  return {
    player: base.player.slice(),
    globals: Object.assign({}, base.globals),
    objects: base.objects.map((o) => Object.assign({}, o)),
    cells: base.cells.map((row) => row.slice()),
    rng: base.rng.slice(),
    cursor: base.cursor,
    sound: [],
  };
}

function applySeed(want, seed) {
  if (seed.player) {
    if (seed.player.x !== undefined) {
      want.player[0] = seed.player.x;
      want.objects[1].x = seed.player.x;
    }
    if (seed.player.y !== undefined) {
      want.player[1] = seed.player.y;
      want.objects[1].y = seed.player.y;
    }
  }
  for (const [name, value] of Object.entries(seed.globals || {})) {
    want.globals[name] = value;
    const alias = {
      "4611": [1, "x"], "4613": [1, "y"], "4619": [1, "tile"],
      "461b": [1, "p10"], "461d": [1, "p12"], "461f": [1, "p14"],
      "4627": [1, "shape"], "4629": [1, "p1e"],
      "4652": [2, "p1a"], "4660": [2, "ready"],
    }[name];
    if (alias) want.objects[alias[0]][alias[1]] = value;
  }
  const objToGlobal = {
    "1": {"x": "4611", "y": "4613", "tile": "4619", "p10": "461b",
          "p12": "461d", "p14": "461f", "shape": "4627", "p1e": "4629"},
    "2": {"p1a": "4652", "ready": "4660"},
  };
  for (const [slot, fields] of Object.entries(seed.objects || {})) {
    Object.assign(want.objects[Number(slot)], fields);
    for (const [name, value] of Object.entries(fields)) {
      const g = (objToGlobal[slot] || {})[name];
      if (g) want.globals[g] = value;
    }
  }
  for (const [col, row, off, value] of seed.cell_bytes || []) {
    const bytes = hexToBytes(want.cells[row][col]);
    bytes[off] = value & 0xff;
    want.cells[row][col] = bytesToHex(bytes);
  }
  for (const [col, row, hex] of seed.cells || []) {
    want.cells[row][col] = hex;
  }
  if (seed.rng) {
    want.rng = seed.rng.slice();
    want.globals["2a6"] = seed.rng[0];
    want.globals["2a8"] = seed.rng[1];
  }
  if (seed.cursor !== undefined) want.cursor = seed.cursor;
}

(async () => {
  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")), {});
  const e = instance.exports;
  const cells = fs.readFileSync(path.join(DIR, "..", "out", "portable", "cells.bin"));
  const cp = e.cc_alloc(cells.length);
  new Uint8Array(e.memory.buffer, cp, cells.length).set(cells);
  e.cc_set_cells(cp);

  const fieldOff = Object.fromEntries(
    fixture.object_fields.map(([name, off]) => [name, off]));
  const globalOff = Object.fromEntries(fixture.globals);

  function cSnapshot() {
    const objects = [];
    for (let s = 0; s < 60; s++) {
      const rec = { index: s };
      for (const [name, off] of Object.entries(fieldOff)) {
        rec[name] = e.cc_obj_get(s, off) | 0;
      }
      objects.push(rec);
    }
    const globals = {};
    for (const name of Object.keys(globalOff)) {
      globals[name] = e.cc_global_get(globalOff[name]) | 0;
    }
    const grid = [];
    for (let r = 0; r < 25; r++) {
      const row = [];
      for (let c = 0; c < 40; c++) {
        let hex = "";
        for (let off = 0; off < 20; off++) {
          hex += (e.cc_cell_byte(c, r, off) & 0xff).toString(16).padStart(2, "0");
        }
        row.push(hex);
      }
      grid.push(row);
    }
    const sound = [];
    const n = e.cc_sound_log_count();
    for (let i = 0; i < n; i++) sound.push(e.cc_sound_log_at(i));
    return {
      player: [e.cc_player_x() | 0, e.cc_player_y() | 0],
      globals, objects, cells: grid,
      rng: [e.cc_rng_lo() | 0, e.cc_rng_hi() | 0],
      cursor: e.cc_obj_cursor() | 0,
      sound,
    };
  }

  function applySeedC(seed) {
    if (seed.player) {
      if (seed.player.x !== undefined) e.cc_global_set(0x4611, seed.player.x);
      if (seed.player.y !== undefined) e.cc_global_set(0x4613, seed.player.y);
    }
    for (const [name, value] of Object.entries(seed.globals || {})) {
      if (globalOff[name] === undefined) throw new Error(`unknown global ${name}`);
      e.cc_global_set(globalOff[name], value);
    }
    for (const [slot, fields] of Object.entries(seed.objects || {})) {
      for (const [name, value] of Object.entries(fields)) {
        if (fieldOff[name] === undefined) throw new Error(`unknown object field ${name}`);
        e.cc_obj_set(Number(slot), fieldOff[name], value);
      }
    }
    for (const [col, row, off, value] of seed.cell_bytes || []) {
      e.cc_cell_set(col, row, off, value);
    }
    for (const [col, row, hex] of seed.cells || []) {
      const bytes = hexToBytes(hex);
      for (let off = 0; off < 20; off++) e.cc_cell_set(col, row, off, bytes[off]);
    }
    if (seed.rng) e.cc_rng_seed(seed.rng[0], seed.rng[1]);
    if (seed.cursor !== undefined) e.cc_obj_set_cursor(seed.cursor);
  }

  function compareSnap(want, tag) {
    if (((e.cc_player_x() | 0) & 0xffff) !== (want.player[0] & 0xffff) ||
        ((e.cc_player_y() | 0) & 0xffff) !== (want.player[1] & 0xffff)) {
      fail(tag, `player: C=${e.cc_player_x()},${e.cc_player_y()} ` +
        `oracle=${want.player}`);
    }
    for (const [name] of Object.entries(globalOff)) {
      const got = e.cc_global_get(globalOff[name]) | 0;
      const exp = want.globals[name] | 0;
      if ((got & 0xffff) !== (exp & 0xffff)) {
        fail(tag, `global ${name}: C=${got} oracle=${exp}`);
      }
    }
    if ((e.cc_rng_lo() & 0xffff) !== (want.rng[0] & 0xffff) ||
        (e.cc_rng_hi() & 0xffff) !== (want.rng[1] & 0xffff)) {
      fail(tag, `rng: C=${e.cc_rng_lo()},${e.cc_rng_hi()} oracle=${want.rng}`);
    }
    if ((e.cc_obj_cursor() & 0xffff) !== (want.cursor & 0xffff)) {
      fail(tag, `cursor: C=${e.cc_obj_cursor()} oracle=${want.cursor}`);
    }
    for (let s = 0; s < 60; s++) {
      for (const [name, off] of Object.entries(fieldOff)) {
        const got = e.cc_obj_get(s, off) | 0;
        const exp = want.objects[s][name] | 0;
        if ((got & 0xffff) !== (exp & 0xffff)) {
          fail(tag, `slot ${s} ${name}: C=${got} oracle=${exp}`);
        }
      }
    }
    for (let r = 0; r < 25; r++) {
      for (let c = 0; c < 40; c++) {
        for (let off = 0; off < 20; off++) {
          const got = e.cc_cell_byte(c, r, off) & 0xff;
          const exp = parseInt(want.cells[r][c].substr(off * 2, 2), 16);
          if (got !== exp) {
            fail(tag, `cell (${c},${r}) byte ${off}: C=${got} oracle=${exp}`);
            return;
          }
        }
      }
    }
    const n = e.cc_sound_log_count();
    if (n !== want.sound.length) {
      fail(tag, `sound count: C=${n} oracle=${want.sound.length}`);
    } else {
      for (let i = 0; i < n; i++) {
        if ((e.cc_sound_log_at(i) & 0xffff) !== (want.sound[i] & 0xffff)) {
          fail(tag, `sound[${i}]: C=${e.cc_sound_log_at(i)} oracle=${want.sound[i]}`);
        }
      }
    }
  }

  for (const [level, base] of Object.entries(fixture.levels)) {
    e.cc_objects_init(Number(level));
    e.cc_sound_log_clear();
    compareSnap(cloneBaseline(base), `level ${level} baseline`);
  }

  let frames = 0;
  for (const sc of fixture.scenarios) {
    if (only && !only.has(sc.handler)) continue;
    e.cc_objects_init(sc.level);
    applySeedC(sc.seed);
    e.cc_sound_log_clear();
    const want = cloneBaseline(fixture.levels[String(sc.level)]);
    applySeed(want, sc.seed);
    compareSnap(want, `${sc.name} seeded`);

    if (sc.mode === "handler") {
      for (let i = 0; i < sc.calls.length; i++) {
        const call = sc.calls[i];
        e.cc_sound_log_clear();
        e.cc_test_call_handler(sc.handler, sc.slot, call.x, call.y);
        for (const [slot, fields] of Object.entries(call.object_changes || {})) {
          Object.assign(want.objects[Number(slot)], fields);
        }
        for (const [row, col, hex] of call.cell_changes || []) {
          want.cells[row][col] = hex;
        }
        want.player = call.player;
        want.globals = call.globals;
        want.rng = call.rng;
        want.cursor = call.cursor;
        want.sound = call.sound;
        compareSnap(want, `${sc.name} call ${i + 1}`);
      }
      continue;
    }

    for (let f = 0; f < sc.frames.length; f++) {
      const fr = sc.frames[f];
      for (const [name, flag] of INPUT_FLAGS) {
        e.cc_input_set(flag, fr.input && fr.input[name] ? 1 : 0);
      }
      e.cc_sound_log_clear();
      if (sc.mode === "full") e.cc_objects_step_full();
      else e.cc_objects_step_objects();
      frames++;
      for (const [slot, fields] of Object.entries(fr.object_changes || {})) {
        Object.assign(want.objects[Number(slot)], fields);
      }
      for (const [row, col, hex] of fr.cell_changes || []) {
        want.cells[row][col] = hex;
      }
      want.player = fr.player;
      want.globals = fr.globals;
      want.rng = fr.rng;
      want.cursor = fr.cursor;
      want.sound = fr.sound;
      compareSnap(want, `${sc.name} frame ${f + 1}`);
    }
  }
  if (failures) {
    console.error(`\n${failures} mismatch(es) over ${frames} frames`);
    process.exit(1);
  }
  if (e.cc_unknown_handler_hits() !== 0) {
    console.error(`unknown handler dispatches: ${e.cc_unknown_handler_hits()}`);
    process.exit(1);
  }
  console.log(`PASS: ${fixture.scenarios.length} handler scenarios, ` +
              `${frames} frames match oracle`);
})().catch((err) => { console.error(err); process.exit(1); });
