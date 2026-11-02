// Golden-master characterization test for the refactoring plan.
//
//   node test_refactor_digest.js            # compare with notes/refactor_digests.json
//   node test_refactor_digest.js --update   # rewrite the fixture
//
// The fixture stores only SHA-256 hashes, so it stays small while covering
// complete deterministic state: the raw cc_state_t save image, the 320x200 RGBA
// view framebuffer and the gameplay sound-attempt log.  A refactor must not
// change these hashes; an intentional behavior change updates the fixture in
// its own commit with a clear message.
//
// Scenario groups run in fresh wasm instances, so each group is isolated from
// the others.  Sound is initialized but disabled, and every snapshot drains the
// sound log so host audio can never affect the result.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const DIR = __dirname;
const ROOT = path.join(DIR, '..');
const FIXTURE = path.join(ROOT, 'notes', 'refactor_digests.json');
const UPDATE = process.argv.includes('--update');

const W = 320;
const H = 200;
const TEX_W = 640;
const TEX_H = 400;
const STATE_FRAMES = 480;      // 8 phases x 60 frames
const SNAPSHOT_EVERY = 10;

const read = (p) => fs.readFileSync(path.join(DIR, p));
const sha256 = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');

// Deterministic input schedule: idle, walk, jump, fire, reverse.
const INPUT_FLAGS = [
  ['left', 0x338E], ['right', 0x338F], ['jump', 0x338B], ['fire', 0x3390],
];
const PHASES = [
  [60, {}],
  [60, { right: 1 }],
  [60, { right: 1, jump: 1 }],
  [60, { right: 1, fire: 1 }],
  [60, { left: 1 }],
  [60, { left: 1, jump: 1 }],
  [60, { left: 1, fire: 1 }],
  [60, {}],
];

function inputsAt(frame) {
  // frame is 1-based; wrap so the save round-trip can reuse the schedule.
  let t = (frame - 1) % STATE_FRAMES;
  let input = {};
  for (const [count, flags] of PHASES) {
    if (t < count) { input = flags; break; }
    t -= count;
  }
  return input;
}

function levelName(level) { return `level${String(level).padStart(2, '0')}`; }

let wasmModule;
const wasmBytes = read('render.wasm');
const assets = {
  gfx: read('assets/gfx.bin'),
  levels: read('assets/levels.bin'),
  cells: read('assets/cells.bin'),
  overlay: read('assets/level_overlay.bin'),
  screens: read('assets/screens.bin'),
  f1: read('assets/mni_f1.bin'),
  f2: read('assets/mni_f2.bin'),
  spl: read('assets/mni_spl.bin'),
  palette: JSON.parse(read('assets/palette.json').toString('utf8')),
};

// Instantiate a fresh game and wire the portable assets.  Each scenario group
// calls boot() so state cannot leak between groups.
function boot() {
  const instance = new WebAssembly.Instance(wasmModule);
  const e = instance.exports;
  const mem = () => new Uint8Array(e.memory.buffer);
  const copy = (b) => {
    const p = e.cc_alloc(b.length);
    if (!p) throw new Error(`asset allocation failed (${b.length} bytes)`);
    mem().set(b, p);
    return p;
  };

  e.cc_set_assets(copy(assets.gfx), copy(assets.levels));
  e.cc_set_cells(copy(assets.cells));
  e.cc_set_level_overlay(copy(assets.overlay));
  e.cc_set_screens(copy(assets.screens));
  e.cc_text_set_fonts(copy(assets.f1), copy(assets.f2), copy(assets.spl));

  const palP = e.cc_alloc(64);
  const p32 = new Uint32Array(e.memory.buffer, palP, 16);
  assets.palette.colors.forEach(([r, g, b], i) => {
    p32[i] = (r << 16) | (g << 8) | b;
  });
  e.cc_set_palette(palP);

  e.cc_sound_init();
  e.cc_sound_set_enabled(0);
  e.cc_sound_log_clear();

  const stateSize = e.cc_state_size();
  const stateP = e.cc_alloc(stateSize);
  const loadP = e.cc_alloc(stateSize);

  function copyString(s) {
    const p = e.cc_alloc(s.length + 1);
    const m = mem();
    for (let i = 0; i < s.length; i++) m[p + i] = s.charCodeAt(i);
    m[p + s.length] = 0;
    return p;
  }

  function stateBytes() {
    const n = e.cc_state_save(stateP, stateSize);
    if (!(n > 0)) throw new Error(`cc_state_save failed (${e.cc_state_last_error()})`);
    return new Uint8Array(e.memory.buffer, stateP, n).slice();
  }
  function stateDigest() { return sha256(stateBytes()); }
  function saveState() { return stateBytes(); }
  function loadState(bytes) {
    new Uint8Array(e.memory.buffer, loadP, bytes.length).set(bytes);
    return e.cc_state_load(loadP, bytes.length);
  }
  function viewDigest() {
    return sha256(new Uint8Array(e.memory.buffer, e.cc_view_framebuffer(), W * H * 4));
  }
  function textureDigest() {
    return sha256(new Uint8Array(e.memory.buffer, e.cc_framebuffer(), TEX_W * TEX_H * 4));
  }
  function soundDigest() {
    // Drain the attempt log; it is host state, deliberately outside the save
    // payload, so hash it separately instead of letting it accumulate.
    const ids = [];
    const n = e.cc_sound_log_count();
    for (let i = 0; i < n; i++) ids.push(e.cc_sound_log_at(i) & 0xFFFF);
    const total = e.cc_sound_log_total();
    e.cc_sound_log_clear();
    const bytes = Buffer.alloc(8 + ids.length * 2);
    bytes.writeUInt32LE(n, 0);
    bytes.writeUInt32LE(total >>> 0, 4);
    ids.forEach((id, i) => bytes.writeUInt16LE(id, 8 + i * 2));
    return sha256(bytes);
  }
  function readCString(ptr, max = 256) {
    const m = mem();
    let s = '';
    for (let i = 0; i < max; i++) {
      const c = m[ptr + i];
      if (!c) break;
      s += String.fromCharCode(c);
    }
    return s;
  }
  function shellDigest() {
    const pageCount = e.cc_shell_page_count();
    const pageLineCount = e.cc_shell_page_line_count();
    const lines = [];
    for (let i = 0; i < pageLineCount; i++)
      lines.push(readCString(e.cc_shell_page_line(i)));
    const hiscores = [];
    for (let i = 0; i < e.cc_shell_hiscore_count(); i++) {
      hiscores.push([
        readCString(e.cc_shell_hiscore_name(i)),
        e.cc_shell_hiscore_score(i) >>> 0,
      ]);
    }
    const summary = {
      state: e.cc_shell_state(),
      pageCount,
      pageIndex: e.cc_shell_page_index(),
      pageLines: pageLineCount,
      message: e.cc_shell_message_block(),
      requestPending: e.cc_shell_request_pending(),
      requestKind: e.cc_shell_request_kind(),
      requestSlot: e.cc_shell_request_slot(),
      level: e.cc_cur_level(),
      score: e.cc_score() >>> 0,
      hiscores,
      lines,
    };
    return sha256(Buffer.from(JSON.stringify(summary), 'utf8'));
  }

  function renderGame() {
    const level = e.cc_cur_level();
    const camX = e.cc_cam_x();
    const camY = e.cc_cam_y();
    e.cc_render_view_cells(level, camX, camY);
    for (let s = 0; s < e.cc_obj_count(); s++) {
      if (e.cc_obj_state(s) === 2 || e.cc_obj_kind(s) === 0) continue;
      const blk = e.cc_obj_tile(s);
      const sh = e.cc_obj_draw_shape(s);
      if (!blk || !sh) continue;
      e.cc_draw_object_view(blk, sh, e.cc_obj_x(s) - camX, e.cc_obj_y(s) - camY);
    }
    if (e.cc_shell_hud_active()) e.cc_hud_render_screen();
    if (e.cc_shell_overlay()) e.cc_shell_render();
  }
  function renderShell() { e.cc_shell_render(); }
  function render() {
    if (e.cc_shell_state() === 4 || e.cc_shell_overlay()) renderGame();
    else renderShell();
  }

  // Enter the in-game shell state through the real title/menu/intro path so
  // HUD rendering matches the browser frame.  Callers may then load a level.
  function enterGameShell() {
    e.cc_shell_init();
    e.cc_shell_key(13);                    // logo -> title/credits
    e.cc_shell_key(13);                    // title -> main menu
    e.cc_shell_key('n'.charCodeAt(0));     // new game -> intro story
    for (let i = 0; i < 8 && e.cc_shell_state() === 3; i++)
      e.cc_shell_key(13);
    if (e.cc_shell_state() !== 4)
      throw new Error(`enterGameShell: shell state ${e.cc_shell_state()}`);
  }

  return {
    e, stateSize, stateP,
    stateDigest, saveState, loadState, viewDigest, textureDigest,
    soundDigest, shellDigest, render, renderGame, renderShell, copyString,
    readCString,
    gameFrame: renderGame,
    enterGameShell,
  };
}

function applyInputs(e, frame) {
  const input = inputsAt(frame);
  for (const [name, flag] of INPUT_FLAGS) e.cc_input_set(flag, input[name] ? 1 : 0);
}

function snapshot(h, frame) {
  return {
    frame,
    state: h.stateDigest(),
    view: h.viewDigest(),
    sound: h.soundDigest(),
  };
}

function fail(msg) { throw new Error(msg); }

// ---------------------------------------------------------------- scenarios

function runStaticRender() {
  const h = boot();
  const out = {};
  for (let level = 0; level < 19; level++) {
    h.e.cc_objects_init(level);
    h.e.cc_render_cells(level, h.e.cc_framebuffer());
    out[levelName(level)] = h.textureDigest();
  }
  return out;
}

function runGameplay() {
  const h = boot();
  h.enterGameShell();
  const out = {};
  for (let level = 2; level <= 18; level++) {
    h.e.cc_objects_init(level);
    h.e.cc_sound_log_clear();
    h.e.cc_hud_tick();
    h.gameFrame();
    const frames = [snapshot(h, 0)];
    for (let f = 1; f <= STATE_FRAMES; f++) {
      applyInputs(h.e, f);
      h.e.cc_objects_step_full();
      h.e.cc_player_draw_sound();
      h.e.cc_hud_tick();
      if (f % SNAPSHOT_EVERY === 0) {
        h.gameFrame();
        frames.push(snapshot(h, f));
      }
    }
    if (h.e.cc_unknown_handler_hits() !== 0)
      fail(`${levelName(level)}: ${h.e.cc_unknown_handler_hits()} unknown handlers`);
    if (h.e.cc_sound_unknown_hits() !== 0)
      fail(`${levelName(level)}: ${h.e.cc_sound_unknown_hits()} unknown sounds`);
    out[levelName(level)] = frames;
  }
  return out;
}

function findDoorCells(e) {
  e.cc_objects_init(2);
  const doors = [];
  for (let r = 0; r < 25; r++) {
    for (let c = 0; c < 40; c++) {
      const w0 = (e.cc_cell_byte(c, r, 0x00) & 0xFF) |
                 ((e.cc_cell_byte(c, r, 0x01) & 0xFF) << 8);
      if (w0 >= 1 && w0 <= 16) doors[w0] = [c, r];
    }
  }
  for (let id = 1; id <= 16; id++)
    if (!doors[id]) fail(`hub door ${id} cell not found`);
  return doors;
}

function findExitCell(e) {
  for (let r = 0; r < 25; r++) {
    for (let c = 0; c < 40; c++) {
      const w3 = (e.cc_cell_byte(c, r, 0x06) & 0xFF) |
                 ((e.cc_cell_byte(c, r, 0x07) & 0xFF) << 8);
      const w5 = (e.cc_cell_byte(c, r, 0x0A) & 0xFF) |
                 ((e.cc_cell_byte(c, r, 0x0B) & 0xFF) << 8);
      if (w5 === 0x0C && (w3 === 0x11 || w3 === 0x0D)) return [c, r];
    }
  }
  return null;
}

function runTransitions() {
  const h = boot();
  h.enterGameShell();
  const e = h.e;
  const doors = findDoorCells(e);
  const out = {};

  for (let id = 1; id <= 16; id++) {
    const [dc, dr] = doors[id];
    e.cc_objects_init(2);
    h.e.cc_sound_log_clear();
    e.cc_global_set(0x4611, dc * 16 + 5);
    e.cc_global_set(0x4613, dr * 16);
    let frames = 0;
    for (; frames < 8; frames++) {
      e.cc_objects_step_full();
      if (e.cc_cur_level() !== 2) break;
    }
    if (e.cc_cur_level() !== id + 2)
      fail(`hub door ${id} loaded level ${e.cc_cur_level()}`);
    h.gameFrame();
    out[`hub_to_cave_${String(id).padStart(2, '0')}`] = snapshot(h, frames + 1);

    const exit = findExitCell(e);
    if (!exit) fail(`cave ${id} exit cell not found`);
    const [ec, er] = exit;
    e.cc_global_set(0x4611, ec * 16 + 3);
    e.cc_global_set(0x4613, er * 16);
    e.cc_global_set(0xA0F5, 1);   // exit unlocked
    e.cc_global_set(0x513C, 1);   // interaction enabled
    e.cc_global_set(0xA0BE, id);
    frames = 0;
    for (; frames < 24; frames++) {
      e.cc_objects_step_full();
      if (e.cc_cur_level() === 2) break;
    }
    if (e.cc_cur_level() !== 2)
      fail(`cave ${id} exit loaded level ${e.cc_cur_level()}`);
    h.gameFrame();
    out[`cave_${String(id).padStart(2, '0')}_to_hub`] = snapshot(h, frames + 1);
  }
  return out;
}

function runWarp() {
  const h = boot();
  h.enterGameShell();
  const out = {};
  for (let level = 0; level < 19; level++) {
    h.e.cc_objects_init(2);
    h.e.cc_warp_level(level);
    if (h.e.cc_cur_level() !== level)
      fail(`cc_warp_level(${level}) loaded ${h.e.cc_cur_level()}`);
    h.gameFrame();
    out[levelName(level)] = snapshot(h, 1);
  }
  return out;
}

function runShellFlow() {
  const h = boot();
  const e = h.e;
  const out = {};
  const cp = (name) => {
    h.render();
    out[name] = {
      state: h.stateDigest(),
      view: h.viewDigest(),
      shell: h.shellDigest(),
      sound: h.soundDigest(),
    };
  };
  const key = (k) => e.cc_shell_key(typeof k === 'number' ? k : k.charCodeAt(0));
  const expectState = (want, name) => {
    if (e.cc_shell_state() !== want)
      fail(`${name}: shell state ${e.cc_shell_state()}, expected ${want}`);
  };

  e.cc_shell_init();
  expectState(0, 'logo');
  cp('logo');

  key(13);
  expectState(15, 'credits');
  cp('credits');

  e.cc_shell_step_ticks(433 + 400);
  cp('credits_scrolled');

  key(13);
  expectState(2, 'menu');
  cp('menu');

  key('n');
  expectState(3, 'intro_story');
  cp('intro_story');

  for (let i = 0; i < 8 && e.cc_shell_state() === 3; i++) {
    key(13);
    cp(`intro_page_${i + 1}`);
  }
  expectState(4, 'intro_done');
  if (e.cc_cur_level() !== 2) fail(`intro loaded level ${e.cc_cur_level()}`);
  cp('intro_done');

  key('p');
  expectState(5, 'pause');
  cp('pause');
  key('x');
  expectState(4, 'pause_resumed');
  cp('pause_resumed');

  key(27);
  expectState(6, 'help');
  cp('help');

  key('k');
  expectState(16, 'key_redefine');
  cp('key_redefine');
  for (const scan of [0x1E, 0x20, 0x39, 0x1C]) e.cc_shell_key_scan(scan);
  expectState(17, 'key_confirm');
  cp('key_confirm');
  e.cc_shell_key_scan(0x15);              // Y commits the bindings
  expectState(6, 'key_committed');
  cp('key_committed');

  key('w');
  expectState(10, 'warp_prompt');
  cp('warp_prompt');
  key('1');
  key('6');
  key(13);
  expectState(4, 'warped');
  if (e.cc_cur_level() !== 18) fail(`warp 16 loaded level ${e.cc_cur_level()}`);
  cp('warped_level18');

  e.cc_load_arg(0);
  if (e.cc_cur_level() !== 2) fail(`load_arg(0) loaded level ${e.cc_cur_level()}`);
  cp('back_to_hub');

  // Save/restore request handshake (the JS localStorage layer is not part of
  // this test; only the wasm request/result state machine is).
  e.cc_global_set(0x50A5, 4321);
  key(27);
  key('s');
  expectState(8, 'save_prompt');
  cp('save_prompt');
  key('0');
  if (e.cc_shell_request_pending() !== 1 ||
      e.cc_shell_request_kind() !== 1 ||
      e.cc_shell_request_slot() !== 0)
    fail('save request was not raised');
  cp('save_request');
  const saved = h.saveState();
  e.cc_shell_request_done(1);
  expectState(14, 'save_done');
  cp('save_done');
  key(13);                                // "saved" message ignores Enter
  key(27);
  expectState(4, 'after_save_message');
  cp('after_save_message');

  e.cc_shell_hiscore_add(h.copyString('TEST'), 1234567);
  if (h.readCString(e.cc_shell_hiscore_name(0)) !== 'TEST' ||
      (e.cc_shell_hiscore_score(0) >>> 0) !== 1234567)
    fail('high-score insert did not land at row 0');
  cp('hiscore_added');

  e.cc_global_set(0x50A5, 99);
  key(27);
  key('r');
  expectState(9, 'restore_prompt');
  cp('restore_prompt');
  key('0');
  if (e.cc_shell_request_pending() !== 1 ||
      e.cc_shell_request_kind() !== 2)
    fail('restore request was not raised');
  cp('restore_request');
  if (!h.loadState(saved)) fail(`state load failed (${e.cc_state_last_error()})`);
  e.cc_shell_request_done(1);
  expectState(4, 'restored');
  cp('restored');
  if ((e.cc_global_get(0x50A5) & 0xFFFF) !== 4321)
    fail('restore did not recover the saved score');

  // Finale: all caves complete -> story pages -> ending -> high scores.
  // Score is a 32-bit lo/hi pair; 0x20000 is above the post-'TEST' last row.
  e.cc_global_set(0x50A5, 0);
  e.cc_global_set(0x50A7, 2);
  e.cc_global_set(0xA0FB, 1);
  e.cc_objects_step_full();
  e.cc_shell_step();
  expectState(3, 'finale_story');
  cp('finale_story');
  for (let i = 0; i < 20 && e.cc_shell_state() === 3; i++) {
    key(13);
    cp(`finale_page_${i + 1}`);
  }
  expectState(13, 'ending');
  cp('ending');
  key(13);
  if (e.cc_shell_state() !== 12)
    fail(`ending key reached state ${e.cc_shell_state()}, expected name entry`);
  for (const ch of 'DIGEST') key(ch);
  cp('name_entry');
  key(13);
  expectState(11, 'final_hiscore');
  let digestRow = -1;
  for (let i = 0; i < e.cc_shell_hiscore_count(); i++)
    if (h.readCString(e.cc_shell_hiscore_name(i)) === 'DIGEST') digestRow = i;
  if (digestRow < 0 ||
      (e.cc_shell_hiscore_score(digestRow) >>> 0) !== 131072)
    fail('name entry did not insert DIGEST with score 131072');
  if (h.readCString(e.cc_shell_hiscore_name(0)) !== 'TEST')
    fail('high-score order changed (TEST is not row 0)');
  cp('final_hiscore');

  return out;
}

function runSaveRoundTrip() {
  const h = boot();
  h.enterGameShell();
  const e = h.e;
  e.cc_objects_init(2);
  h.e.cc_sound_log_clear();

  // Warm up with the regular schedule.
  for (let f = 1; f <= 120; f++) {
    applyInputs(e, f);
    e.cc_objects_step_full();
    e.cc_player_draw_sound();
    e.cc_hud_tick();
  }
  h.gameFrame();
  const savedState = h.stateDigest();
  const savedView = h.viewDigest();
  const savedBytes = h.saveState();

  // Mutate the live state and record every snapshot.
  const mutation = [];
  for (let f = 121; f <= 180; f++) {
    applyInputs(e, f);
    e.cc_objects_step_full();
    e.cc_player_draw_sound();
    e.cc_hud_tick();
    if ((f - 120) % SNAPSHOT_EVERY === 0) {
      h.gameFrame();
      mutation.push(snapshot(h, f - 120));
    }
  }
  const mutatedState = h.stateDigest();
  const mutatedView = h.viewDigest();
  if (mutatedState === savedState)
    fail('save round-trip: the mutation did not change state');

  // Restore, then replay the identical input schedule.
  if (!h.loadState(savedBytes)) fail(`state load failed (${e.cc_state_last_error()})`);
  h.gameFrame();
  const restoredState = h.stateDigest();
  const restoredView = h.viewDigest();
  if (restoredState !== savedState) fail('save round-trip: restored state hash differs');
  if (restoredView !== savedView) fail('save round-trip: restored view hash differs');

  const replay = [];
  for (let f = 121; f <= 180; f++) {
    applyInputs(e, f);
    e.cc_objects_step_full();
    e.cc_player_draw_sound();
    e.cc_hud_tick();
    if ((f - 120) % SNAPSHOT_EVERY === 0) {
      h.gameFrame();
      replay.push(snapshot(h, f - 120));
    }
  }
  for (let i = 0; i < mutation.length; i++) {
    if (mutation[i].state !== replay[i].state ||
        mutation[i].view !== replay[i].view ||
        mutation[i].sound !== replay[i].sound)
      fail(`save round-trip: replay snapshot ${i} differs`);
  }

  return {
    saved: { state: savedState, view: savedView },
    mutated: { state: mutatedState, view: mutatedView },
    restored: { state: restoredState, view: restoredView },
    replay,
  };
}

// ---------------------------------------------------------------- fixture

function compare(expected, actual, pathParts = [], diffs = []) {
  if (diffs.length >= 20) return diffs;
  if (Array.isArray(expected) || Array.isArray(actual)) {
    if (!Array.isArray(expected) || !Array.isArray(actual)) {
      diffs.push(`${pathParts.join('.')}: type mismatch`);
      return diffs;
    }
    if (expected.length !== actual.length) {
      diffs.push(`${pathParts.join('.')}: length ${actual.length}, ` +
                 `expected ${expected.length}`);
      return diffs;
    }
    for (let i = 0; i < expected.length; i++)
      compare(expected[i], actual[i], [...pathParts, String(i)], diffs);
    return diffs;
  }
  if (expected && typeof expected === 'object') {
    if (!actual || typeof actual !== 'object') {
      diffs.push(`${pathParts.join('.')}: type mismatch`);
      return diffs;
    }
    const keys = new Set([...Object.keys(expected), ...Object.keys(actual)]);
    for (const key of [...keys].sort())
      compare(expected[key], actual[key], [...pathParts, key], diffs);
    return diffs;
  }
  if (expected !== actual)
    diffs.push(`${pathParts.join('.')}: ${actual}, expected ${expected}`);
  return diffs;
}

function countSnapshots(value) {
  if (Array.isArray(value))
    return value.reduce((n, item) => n + countSnapshots(item), 0);
  if (!value || typeof value !== 'object') return 0;
  let n = typeof value.state === 'string' ? 1 : 0;
  for (const item of Object.values(value)) {
    if (item && typeof item === 'object') n += countSnapshots(item);
  }
  return n;
}

(async () => {
  wasmModule = new WebAssembly.Module(wasmBytes);

  const result = {
    version: 1,
    note: 'SHA-256 digests; regenerate with node test_refactor_digest.js --update',
    static_render: runStaticRender(),
    gameplay: runGameplay(),
    transitions: runTransitions(),
    warp: runWarp(),
    shell: runShellFlow(),
    save_round_trip: runSaveRoundTrip(),
  };

  const snapshots = countSnapshots(result);
  if (UPDATE) {
    fs.writeFileSync(FIXTURE, JSON.stringify(result, null, 2) + '\n');
    console.log(`WROTE ${path.relative(ROOT, FIXTURE)} ` +
                `(${JSON.stringify(result).length} bytes, ${snapshots} snapshots)`);
    return;
  }

  if (!fs.existsSync(FIXTURE)) {
    console.error(`FAIL missing ${path.relative(ROOT, FIXTURE)}; ` +
                  'run `node test_refactor_digest.js --update` once');
    process.exit(1);
  }
  const expected = JSON.parse(fs.readFileSync(FIXTURE, 'utf8'));
  const diffs = compare(expected, result);
  if (diffs.length) {
    for (const d of diffs) console.error(`FAIL ${d}`);
    console.error(`\n${diffs.length} digest mismatch(es); a refactor must not ` +
                  'change these hashes');
    process.exit(1);
  }
  console.log(`PASS: ${snapshots} deterministic digest snapshots match ` +
              `${path.relative(ROOT, FIXTURE)}`);
})().catch((err) => { console.error(err); process.exit(1); });
