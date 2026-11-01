// Compare the C cell/contact/exit state against notes/cell_fixtures.json.
const fs = require("fs");
const path = require("path");

const DIR = __dirname;
const FILE = process.argv[2] ||
  path.join(DIR, "..", "notes", "cell_fixtures.json");
const fixture = JSON.parse(fs.readFileSync(FILE, "utf8"));

const INPUT_FLAGS = [
  ["left", 0x338E], ["right", 0x338F], ["jump", 0x338B],
  ["fire", 0x3390], ["down", 0x338C],
];

let failures = 0;
function fail(tag, msg) {
  console.error(`${tag}: ${msg}`);
  if (++failures > 60) process.exit(1);
}
function hexToBytes(hex) { return Uint8Array.from(Buffer.from(hex, "hex")); }
function bytesToHex(bytes) { return Buffer.from(bytes).toString("hex"); }

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
  for (const [slot, fields] of Object.entries(seed.objects || {})) {
    for (const [name, value] of Object.entries(fields))
      want.objects[Number(slot)][name] = value;
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

  function snapshot() {
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
      cursor: e.cc_obj_cursor() | 0, sound,
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
    if (seed.inputs) {
      for (const [name, flag] of INPUT_FLAGS)
        e.cc_input_set(flag, seed.inputs[name] ? 1 : 0);
    }
  }

  function compare(want, tag) {
    const got = snapshot();
    if (((got.player[0] & 0xffff) !== (want.player[0] & 0xffff)) ||
        ((got.player[1] & 0xffff) !== (want.player[1] & 0xffff))) {
      fail(tag, `player: C=${got.player} oracle=${want.player}`);
    }
    for (const name of Object.keys(globalOff)) {
      const g = got.globals[name] | 0, x = want.globals[name] | 0;
      if ((g & 0xffff) !== (x & 0xffff))
        fail(tag, `global ${name}: C=${g} oracle=${x}`);
    }
    if ((got.rng[0] & 0xffff) !== (want.rng[0] & 0xffff) ||
        (got.rng[1] & 0xffff) !== (want.rng[1] & 0xffff))
      fail(tag, `rng: C=${got.rng} oracle=${want.rng}`);
    if ((got.cursor & 0xffff) !== (want.cursor & 0xffff))
      fail(tag, `cursor: C=${got.cursor} oracle=${want.cursor}`);
    for (let s = 0; s < 60; s++) {
      for (const [name, off] of Object.entries(fieldOff)) {
        const g = got.objects[s][name] | 0, x = want.objects[s][name] | 0;
        if ((g & 0xffff) !== (x & 0xffff))
          fail(tag, `slot ${s} ${name}: C=${g} oracle=${x}`);
      }
    }
    for (let r = 0; r < 25; r++) {
      for (let c = 0; c < 40; c++) {
        for (let off = 0; off < 20; off++) {
          const g = parseInt(got.cells[r][c].substr(off * 2, 2), 16);
          const x = parseInt(want.cells[r][c].substr(off * 2, 2), 16);
          if (g !== x) { fail(tag, `cell (${c},${r}) +${off}: C=${g} oracle=${x}`); return; }
        }
      }
    }
    if (got.sound.length !== want.sound.length) {
      fail(tag, `sound count: C=${got.sound.length} oracle=${want.sound.length}`);
    } else {
      for (let i = 0; i < got.sound.length; i++)
        if ((got.sound[i] & 0xffff) !== (want.sound[i] & 0xffff))
          fail(tag, `sound[${i}]: C=${got.sound[i]} oracle=${want.sound[i]}`);
    }
  }

  for (const [level, base] of Object.entries(fixture.levels)) {
    e.cc_objects_init(Number(level));
    e.cc_sound_log_clear();
    compare(cloneBaseline(base), `level ${level} baseline`);
  }

  let frameCount = 0;
  for (const sc of fixture.scenarios) {
    e.cc_objects_init(sc.level);
    applySeedC(sc.seed);
    e.cc_sound_log_clear();
    if (sc.mode === "cell") {
      e.cc_test_cell_event(sc.x, sc.y);
      compare(sc.state, `${sc.name} cell_event`);
      continue;
    }
    const want = cloneBaseline(fixture.levels[String(sc.level)]);
    applySeed(want, sc.seed);
    for (let f = 0; f < sc.frames.length; f++) {
      const fr = sc.frames[f];
      for (const [name, flag] of INPUT_FLAGS)
        e.cc_input_set(flag, fr.input && fr.input[name] ? 1 : 0);
      e.cc_sound_log_clear();
      e.cc_objects_step_full();
      frameCount++;
      for (const [slot, fields] of Object.entries(fr.object_changes || {}))
        Object.assign(want.objects[Number(slot)], fields);
      for (const [row, col, hex] of fr.cell_changes || [])
        want.cells[row][col] = hex;
      want.player = fr.player;
      want.globals = fr.globals;
      want.rng = fr.rng;
      want.cursor = fr.cursor;
      want.sound = fr.sound;
      compare(want, `${sc.name} frame ${f + 1}`);
    }
  }
  if (failures) {
    console.error(`\n${failures} mismatch(es) over ${frameCount} frames`);
    process.exit(1);
  }
  console.log(`PASS: ${fixture.scenarios.length} cell scenarios, ${frameCount} frames match oracle`);
})().catch((err) => { console.error(err); process.exit(1); });
