// Exact loader-transition regression.  Loads the committed overlay asset and
// compares the full object/cell/global state after a transition.
const fs = require("fs");
const path = require("path");

const DIR = __dirname;
const FIX = process.argv[2] ||
  path.join(DIR, "..", "notes", "loader_fixtures.json");
const fixture = JSON.parse(fs.readFileSync(FIX, "utf8"));

let failures = 0;
function fail(tag, msg) {
  console.error(`${tag}: ${msg}`);
  if (++failures > 40) process.exit(1);
}

(async () => {
  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")), {});
  const e = instance.exports;
  const cells = fs.readFileSync(path.join(DIR, "..", "out", "portable", "cells.bin"));
  const cp = e.cc_alloc(cells.length);
  new Uint8Array(e.memory.buffer, cp, cells.length).set(cells);
  e.cc_set_cells(cp);
  const ovPath = process.env.CC_OVERLAY ||
    path.join(DIR, "..", "out", "portable", "level_overlay.bin");
  if (fs.existsSync(ovPath)) {
    const ov = fs.readFileSync(ovPath);
    const op = e.cc_alloc(ov.length);
    new Uint8Array(e.memory.buffer, op, ov.length).set(ov);
    e.cc_set_level_overlay(op);
  } else {
    console.warn("WARN: level_overlay.bin missing; transition overrides disabled");
  }

  const fieldOff = Object.fromEntries(
    fixture.object_fields.map(([name, off]) => [name, off]));
  const globalOff = Object.fromEntries(fixture.globals);

  function poisonSource() {
    e.cc_test_fill_source(0xA5, 0xA5);
    e.cc_global_set(0x50A5, 0x1234);
    e.cc_global_set(0x50A7, 0x9ABC);
    e.cc_global_set(0x509C, 7);
    e.cc_global_set(0x509E, 2);
    e.cc_global_set(0x50A2, 9);
    e.cc_global_set(0x50A0, 0x7D00);
    e.cc_global_set(0x50BE, 7);
    e.cc_global_set(0x50C4, 9);
    e.cc_global_set(0x50C7, 0x1234);
    e.cc_global_set(0x50C9, 0x9ABC);
    e.cc_rng_seed(0x1111, 0x2222);
    e.cc_obj_set_cursor(42);
    for (let i = 1; i <= 16; i++) e.cc_global_set(0x50A8 + i, i & 1);
  }

  function compare(sc) {
    const tag = sc.name;
    for (let s = 0; s < 60; s++) {
      for (const [name, off] of Object.entries(fieldOff)) {
        const got = e.cc_obj_get(s, off) | 0;
        const exp = sc.objects[s][name] | 0;
        if ((got & 0xffff) !== (exp & 0xffff))
          fail(tag, `slot ${s} ${name}: C=${got} oracle=${exp}`);
      }
    }
    for (let r = 0; r < 25; r++) {
      for (let c = 0; c < 40; c++) {
        const hex = sc.cells[r][c];
        for (let off = 0; off < 20; off++) {
          const got = e.cc_cell_byte(c, r, off) & 0xff;
          const exp = parseInt(hex.substr(off * 2, 2), 16);
          if (got !== exp) {
            fail(tag, `cell (${c},${r}) byte ${off}: C=${got} oracle=${exp}`);
            return;
          }
        }
      }
    }
    for (const [name, off] of Object.entries(globalOff)) {
      const got = e.cc_global_get(off) | 0;
      const exp = sc.globals[name] | 0;
      if ((got & 0xffff) !== (exp & 0xffff))
        fail(tag, `global ${name}: C=${got} oracle=${exp}`);
    }
    if ((e.cc_obj_cursor() & 0xffff) !== (sc.cursor & 0xffff))
      fail(tag, `cursor: C=${e.cc_obj_cursor()} oracle=${sc.cursor}`);
    if ((e.cc_rng_lo() & 0xffff) !== (sc.rng[0] & 0xffff) ||
        (e.cc_rng_hi() & 0xffff) !== (sc.rng[1] & 0xffff))
      fail(tag, `rng: C=${e.cc_rng_lo()},${e.cc_rng_hi()} oracle=${sc.rng}`);
  }

  for (const sc of fixture.scenarios) {
    e.cc_objects_init(sc.source);
    poisonSource();
    e.cc_level_exit(sc.arg);
    compare(sc);
  }
  if (failures) {
    console.error(`\n${failures} mismatch(es)`);
    process.exit(1);
  }
  console.log(`PASS: ${fixture.scenarios.length} loader transitions match oracle`);
})().catch((err) => { console.error(err); process.exit(1); });
