// Compare exact player physics against tools/physics_fixtures.py.
//   node test_physics.js [fixture.json]
const fs = require("fs");
const path = require("path");

const DIR = __dirname;
const FILE = process.argv[2] || path.join(DIR, "..", "notes", "physics_fixtures.json");
const fixture = JSON.parse(fs.readFileSync(FILE, "utf8"));

// Must stay in sync with enum cc_phys_field.

// Address -> physics field name (startup.json -> cc_phys_get indices).
const START_KEYS = {
  "4611": "x", "4613": "y", "512e": "cam_x", "5130": "cam_y",
  "4629": "move_dir", "2ba": "walk_speed", "2ca": "impulse",
  "a0b4": "walk_phase", "5134": "jump_phase", "306": "jump_flag",
  "a0fd": "airborne", "30a": "anim_suppress", "4627": "shape",
  "461f": "shape_a", "461d": "shape_b", "4619": "player_block",
  "461b": "anim_timer", "5118": "carry_x", "5126": "scroll_x",
  "5128": "carry_y", "513d": "ride_h", "5142": "ride_v",
  "a0f1": "blocked_l", "a0f2": "blocked_r", "5132": "gravity_scale",
  "2c8": "c8", "2c6": "c6", "509e": "lives", "509c": "ammo",
  "a0ea": "invuln", "4660": "hit_lock", "5124": "exit_state",
};

function checkStartup(e) {
  const startupPath = path.join(DIR, "..", "notes", "startup.json");
  const startup = JSON.parse(fs.readFileSync(startupPath, "utf8"));
  let bad = 0;
  for (let lv = 0; lv < 19; lv++) {
    e.cc_objects_init(lv);
    const g = startup.levels[String(lv)].globals;
    for (const [addr, name] of Object.entries(START_KEYS)) {
      const want = g[addr] | 0;
      const got = e.cc_phys_get(FIELD_INDEX[name]) | 0;
      if ((got & 0xFFFF) !== (want & 0xFFFF)) {
        console.error(`startup L${lv} ${name}: C=${got} oracle=${want}`);
        if (++bad > 30) return bad;
      }
    }
  }
  if (!bad) console.log("PASS: 19 level-start states match oracle");
  return bad;
}

const FIELD_INDEX = {
  x: 0, y: 1, cam_x: 2, cam_y: 3, move_dir: 4, walk_speed: 5,
  impulse: 6, walk_phase: 7, jump_phase: 8, jump_flag: 9, airborne: 10,
  anim_suppress: 11, shape: 12, shape_a: 13, shape_b: 14,
  player_block: 15, anim_timer: 16, carry_x: 17, scroll_x: 18,
  carry_y: 19, ride_h: 20, ride_v: 21, blocked_l: 22, blocked_r: 23,
  gravity_scale: 24, c8: 25, c6: 26, lives: 27, ammo: 28, invuln: 29,
  hit_lock: 30, exit_state: 31,
};

const INPUTS = [
  ["left", 0x338E], ["right", 0x338F],
  ["jump", 0x338B], ["fire", 0x3390],
];

(async () => {
  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")), {});
  const e = instance.exports;
  const cells = fs.readFileSync(path.join(DIR, "..", "out", "portable", "cells.bin"));
  const cp = e.cc_alloc(cells.length);
  new Uint8Array(e.memory.buffer, cp, cells.length).set(cells);
  e.cc_set_cells(cp);

  let failures = checkStartup(e);
  let frames = 0;
  for (const sc of fixture.scenarios) {
    e.cc_objects_init(sc.level);
    for (const [k, v] of Object.entries(sc.seed || {})) {
      const idx = FIELD_INDEX[k];
      if (idx === undefined) throw new Error(`unknown seed field ${k}`);
      e.cc_phys_set(idx, v);
    }
    for (let f = 0; f < sc.frames.length; f++) {
      const fr = sc.frames[f];
      const wantState = fr.state;
      // Set all keyboard flags each frame so ISR side effects match the oracle.
      for (const [name, flag] of INPUTS) {
        e.cc_input_set(flag, fr.input && fr.input[name] ? 1 : 0);
      }
      if (sc.mode === "platform") e.cc_objects_step();
      else e.cc_objects_step_physics();
      frames++;

      for (const name of fixture.fields) {
        const idx = FIELD_INDEX[name];
        if (idx === undefined) throw new Error(`unknown fixture field ${name}`);
        const got = e.cc_phys_get(idx) | 0;
        const want = wantState[name] | 0;
        if ((got & 0xFFFF) !== (want & 0xFFFF)) {
          console.error(`${sc.name} frame ${f + 1} ${name}: C=${got} oracle=${want}`);
          if (++failures > 30) process.exit(1);
        }
      }

      if (fr.objects) {
        for (let k = 0; k < fr.objects.length; k++) {
          const slot = sc.object_slots[k];
          const ob = fr.objects[k];
          const checks = [
            ["x", e.cc_obj_x(slot)], ["y", e.cc_obj_y(slot)],
            ["p1a", e.cc_obj_p1a(slot)], ["p18", e.cc_obj_p18(slot)],
            ["p1e", e.cc_obj_vx(slot)], ["p20", e.cc_obj_vy(slot)],
            ["p22", e.cc_obj_speed(slot)], ["shape", e.cc_obj_shape(slot)],
            ["state", e.cc_obj_state(slot)], ["ready", e.cc_obj_ready(slot)],
          ];
          for (const [name, got] of checks) {
            const want = ob[name] | 0;
            if (((got | 0) & 0xFFFF) !== (want & 0xFFFF)) {
              console.error(`${sc.name} frame ${f + 1} slot ${slot} ${name}: C=${got} oracle=${want}`);
              if (++failures > 30) process.exit(1);
            }
          }
        }
      }
    }
  }
  if (failures) {
    console.error(`\n${failures} mismatch(es) over ${frames} frames`);
    process.exit(1);
  }
  console.log(`PASS: ${fixture.scenarios.length} physics scenarios, ${frames} frames match oracle`);
})().catch((err) => { console.error(err); process.exit(1); });
