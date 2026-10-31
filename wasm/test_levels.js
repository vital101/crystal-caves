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

  // Enter cave 1 through hub door 1 at (5,8).  The hub door's cell (4,8) is
  // solid in cave 1, so the loader falls back to the cave's Y-token start
  // (288,224) instead of leaving the player inside a wall.
  e.cc_objects_init(2);
  e.cc_global_set(0x4611, 80);
  e.cc_global_set(0x4613, 128);
  for (let i = 0; i < 4; i++) e.cc_objects_step_full();
  if (e.cc_cur_level() !== 3) throw new Error(`hub door loaded level ${e.cc_cur_level()}, expected 3`);
  if (e.cc_level_arg() !== 1) throw new Error(`hub door arg ${e.cc_level_arg()}, expected 1`);
  if (e.cc_player_x() !== 288 || e.cc_player_y() !== 224)
    throw new Error(`hub door spawn ${e.cc_player_x()},${e.cc_player_y()}, expected the level start 288,224`);
  if (solidAt(e.cc_player_x(), e.cc_player_y()))
    throw new Error("hub door spawn is inside solid geometry");

  // Exit level 3: unlock the exit, stand on it, complete the animation.
  e.cc_objects_init(3);
  e.cc_global_set(0x4611, 37 * 16 + 3);
  e.cc_global_set(0x4613, 17 * 16);
  e.cc_global_set(0xA0F5, 1);
  e.cc_global_set(0x513C, 1);
  e.cc_global_set(0xA0BE, 1);
  for (let i = 0; i < 19; i++) e.cc_objects_step_full();
  if (e.cc_cur_level() !== 2) throw new Error(`cave exit loaded level ${e.cc_cur_level()}, expected 2`);
  if (e.cc_level_arg() !== 0) throw new Error(`cave exit arg ${e.cc_level_arg()}, expected 0`);
  if (e.cc_cave_flag(1) !== 0) throw new Error("cave 1 completion flag was not cleared");
  if (solidAt(e.cc_player_x(), e.cc_player_y()))
    throw new Error("cave exit spawn is inside solid geometry");
  // 0x5098/0x509A hold the player's cave cell; 0xD694 copies it to
  // 0x2B72/0x2B74, so the hub entry is the same cell with a centred camera.
  if (e.cc_player_x() !== 37 * 16 || e.cc_player_y() !== 17 * 16)
    throw new Error(`cave exit spawn ${e.cc_player_x()},${e.cc_player_y()}, ` +
                    "expected 592,272");
  if (e.cc_cam_x() !== 320 || e.cc_cam_y() !== 172)
    throw new Error(`cave exit camera ${e.cc_cam_x()},${e.cc_cam_y()}, ` +
                    "expected 320,172");

  // Every cave exit re-enters the hub at the same cell (0x5098/0x509A) with
  // the camera centred on it.
  const word = (c, r, off) =>
    (e.cc_cell_byte(c, r, off) & 0xFF) |
    ((e.cc_cell_byte(c, r, off + 1) & 0xFF) << 8);
  for (let lv = 3; lv <= 18; lv++) {
    e.cc_objects_init(lv);
    let ex = null;
    for (let r = 0; r < 25 && !ex; r++)
      for (let c = 0; c < 40; c++) {
        const w3 = word(c, r, 0x06), w5 = word(c, r, 0x0A);
        if (w5 === 0x0C && (w3 === 0x11 || w3 === 0x0D)) { ex = [c, r]; break; }
      }
    if (!ex) throw new Error(`level ${lv} exit cell not found`);
    e.cc_global_set(0x4611, ex[0] * 16 + 3);
    e.cc_global_set(0x4613, ex[1] * 16);
    e.cc_global_set(0xA0F5, 1);
    e.cc_global_set(0x513C, 1);
    e.cc_global_set(0xA0BE, lv - 2);
    // Stop on the frame the hub loads: later frames apply gravity.
    for (let i = 0; i < 19; i++) {
      e.cc_objects_step_full();
      if (e.cc_cur_level() === 2) break;
    }
    if (e.cc_cur_level() !== 2)
      throw new Error(`level ${lv} exit loaded level ${e.cc_cur_level()}`);
    if (e.cc_player_x() !== ex[0] * 16 || e.cc_player_y() !== ex[1] * 16)
      throw new Error(`level ${lv} hub spawn ${e.cc_player_x()},` +
                      `${e.cc_player_y()}, expected ${ex[0] * 16},${ex[1] * 16}`);
    const cx = Math.max(0, Math.min(0x140, ex[0] * 16 - 0xA0));
    const cy = Math.max(0, Math.min(0xC8, ex[1] * 16 - 0x64));
    if (e.cc_cam_x() !== cx || e.cc_cam_y() !== cy)
      throw new Error(`level ${lv} hub camera ${e.cc_cam_x()},` +
                      `${e.cc_cam_y()}, expected ${cx},${cy}`);
  }

  console.log("PASS: hub door and cave exit transitions load the correct levels");
})().catch((err) => { console.error(err); process.exit(1); });
