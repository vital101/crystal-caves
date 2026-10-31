// Functional level-transition checks (hub door -> cave, cave exit -> hub).
const fs = require("fs");
const path = require("path");
const DIR = __dirname;
(async () => {
  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")), {});
  const e = instance.exports;
  const cells = fs.readFileSync(path.join(DIR, "..", "out", "portable", "cells.bin"));
  const cp = e.cc_alloc(cells.length);
  new Uint8Array(e.memory.buffer, cp, cells.length).set(cells);
  e.cc_set_cells(cp);
  // Real transitions replay the loader overlay; without it the cave keeps
  // the hub's door/exit state and cell image.
  const overlay = fs.readFileSync(
    path.join(DIR, "..", "out", "portable", "level_overlay.bin"));
  const op = e.cc_alloc(overlay.length);
  new Uint8Array(e.memory.buffer, op, overlay.length).set(overlay);
  e.cc_set_level_overlay(op);

  // Player hitbox (9 points) against the solid byte 0x11.
  const solidAt = (px, py) => {
    for (const dx of [3, 8, 12])
      for (const dy of [0, 8, 15]) {
        const c = Math.floor((px + dx) / 16), r = Math.floor((py + dy) / 16);
        if (e.cc_cell_byte(c, r, 0x11) & 0xFF) return true;
      }
    return false;
  };

  // Every hub door must land in a clear spawn.  The hub-door cell is a hub
  // coordinate; using it blindly in the destination cave can be solid, which
  // is why the loader falls back to the destination's Y-token start.
  e.cc_objects_init(2);
  const doorCells = [];
  for (let r = 0; r < 25; r++)
    for (let c = 0; c < 40; c++) {
      const w0 = (e.cc_cell_byte(c, r, 0x00) & 0xFF) |
                 ((e.cc_cell_byte(c, r, 0x01) & 0xFF) << 8);
      if (w0 >= 1 && w0 <= 16) doorCells[w0] = [c, r];
    }
  for (let id = 1; id <= 16; id++) {
    const dc = doorCells[id];
    if (!dc) throw new Error(`hub door ${id} cell not found`);
    e.cc_objects_init(2);
    e.cc_global_set(0x4611, dc[0] * 16 + 5);
    e.cc_global_set(0x4613, dc[1] * 16);
    for (let i = 0; i < 4; i++) e.cc_objects_step_full();
    if (e.cc_cur_level() !== id + 2)
      throw new Error(`hub door ${id} loaded level ${e.cc_cur_level()}`);
    const px = e.cc_player_x(), py = e.cc_player_y();
    if (solidAt(px, py))
      throw new Error(`hub door ${id} spawn inside a wall (${px},${py})`);
    // The loader ignores the hub-door cell in a cave and uses the Y-token
    // start, so the transition spawn must equal a fresh load of that cave.
    e.cc_objects_init(id + 2);
    if (px !== e.cc_player_x() || py !== e.cc_player_y())
      throw new Error(`hub door ${id} spawn (${px},${py}) is not the cave ` +
                      `start (${e.cc_player_x()},${e.cc_player_y()})`);
  }

  // Enter cave 1 through hub door 1 at (5,8).  The hub-door override has no
  // meaning in a cave, so the loader uses the cave's Y-token start (288,224).
  e.cc_objects_init(2);
  e.cc_global_set(0x4611, 80);
  e.cc_global_set(0x4613, 128);
  for (let i = 0; i < 4; i++) {
    e.cc_objects_step_full();
  }
  if (e.cc_cur_level() !== 3) throw new Error(`hub door loaded level ${e.cc_cur_level()}, expected 3`);
  if (e.cc_level_arg() !== 1) throw new Error(`hub door arg ${e.cc_level_arg()}, expected 1`);
  if (e.cc_player_x() !== 288 || e.cc_player_y() !== 224)
    throw new Error(`hub door spawn ${e.cc_player_x()},${e.cc_player_y()}, expected the level start 288,224`);
  if (solidAt(e.cc_player_x(), e.cc_player_y()))
    throw new Error("hub door spawn is inside solid geometry");

  const word = (c, r, off) =>
    (e.cc_cell_byte(c, r, off) & 0xFF) |
    ((e.cc_cell_byte(c, r, off + 1) & 0xFF) << 8);
  const findExit = () => {
    for (let r = 0; r < 25; r++)
      for (let c = 0; c < 40; c++) {
        const w3 = word(c, r, 0x06), w5 = word(c, r, 0x0A);
        if (w5 === 0x0C && (w3 === 0x11 || w3 === 0x0D)) return [c, r];
      }
    return null;
  };

  // Exit cave 1 back to the hub: 0xD4CC reloads without touching
  // [0x2B72]/[0x2B74], so the stale hub-door entry cell (door-1) is used and
  // the camera centres on it.
  let ex = findExit();
  if (!ex) throw new Error("cave 1 exit cell not found");
  e.cc_global_set(0x4611, ex[0] * 16 + 3);
  e.cc_global_set(0x4613, ex[1] * 16);
  e.cc_global_set(0xA0F5, 1);
  e.cc_global_set(0x513C, 1);
  e.cc_global_set(0xA0BE, 1);
  for (let i = 0; i < 19; i++) {
    e.cc_objects_step_full();
    if (e.cc_cur_level() === 2) break;
  }
  if (e.cc_cur_level() !== 2) throw new Error(`cave exit loaded level ${e.cc_cur_level()}, expected 2`);
  if (e.cc_level_arg() !== 0) throw new Error(`cave exit arg ${e.cc_level_arg()}, expected 0`);
  if (e.cc_cave_flag(1) !== 0) throw new Error("cave 1 completion flag was not cleared");
  if (solidAt(e.cc_player_x(), e.cc_player_y()))
    throw new Error("cave exit spawn is inside solid geometry");
  if (e.cc_player_x() !== 4 * 16 || e.cc_player_y() !== 8 * 16)
    throw new Error(`cave exit spawn ${e.cc_player_x()},${e.cc_player_y()}, ` +
                    "expected the hub-door entry cell 64,128");
  if (e.cc_cam_x() !== 0 || e.cc_cam_y() !== 28)
    throw new Error(`cave exit camera ${e.cc_cam_x()},${e.cc_cam_y()}, ` +
                    "expected 0,28");

  // Every cave exit returns the player beside the hub door used to enter,
  // not at the hub's default start.
  for (let id = 1; id <= 16; id++) {
    const dc = doorCells[id];
    e.cc_objects_init(2);
    e.cc_global_set(0x4611, dc[0] * 16 + 5);
    e.cc_global_set(0x4613, dc[1] * 16);
    for (let i = 0; i < 4; i++) e.cc_objects_step_full();
    if (e.cc_cur_level() !== id + 2)
      throw new Error(`door ${id} entry loaded ${e.cc_cur_level()}`);
    ex = findExit();
    if (!ex) throw new Error(`level ${id + 2} exit cell not found`);
    e.cc_global_set(0x4611, ex[0] * 16 + 3);
    e.cc_global_set(0x4613, ex[1] * 16);
    e.cc_global_set(0xA0F5, 1);
    e.cc_global_set(0x513C, 1);
    e.cc_global_set(0xA0BE, id);
    for (let i = 0; i < 19; i++) {
      e.cc_objects_step_full();
      if (e.cc_cur_level() === 2) break;
    }
    if (e.cc_cur_level() !== 2)
      throw new Error(`cave ${id} exit loaded level ${e.cc_cur_level()}`);
    const px = e.cc_player_x(), py = e.cc_player_y();
    if (py !== dc[1] * 16 || (px !== (dc[0] - 1) * 16 && px !== (dc[0] + 1) * 16))
      throw new Error(`cave ${id} hub spawn ${px},${py}, expected beside ` +
                      `door (${dc[0]},${dc[1]})`);
    const cx = Math.max(0, Math.min(0x140, px - 0xA0));
    const cy = Math.max(0, Math.min(0xC8, py - 0x64));
    if (e.cc_cam_x() !== cx || e.cc_cam_y() !== cy)
      throw new Error(`cave ${id} hub camera ${e.cc_cam_x()},` +
                      `${e.cc_cam_y()}, expected ${cx},${cy}`);
  }

  console.log("PASS: hub door and cave exit transitions load the correct levels");
})().catch((err) => { console.error(err); process.exit(1); });
