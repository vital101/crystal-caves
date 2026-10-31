// Phase 7 persistence regression: save format, round-trips, corruption,
// high scores and config reload/migration.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const CCPersist = require('./persist.js');

const read = (p) => fs.readFileSync(path.join(__dirname, p));
const wasm = read('render.wasm');
const gfx = read('assets/gfx.bin');
const levels = read('assets/levels.bin');
const cells = read('assets/cells.bin');
const overlay = read('assets/level_overlay.bin');
const screens = read('assets/screens.bin');
const f1 = read('assets/mni_f1.bin');
const f2 = read('assets/mni_f2.bin');
const spl = read('assets/mni_spl.bin');
const pal = JSON.parse(read('assets/palette.json'));
const hiscoreFixture = JSON.parse(fs.readFileSync(
  path.join(__dirname, '..', 'notes', 'hiscore_fixtures.json'), 'utf8'));
const v1Fixture = JSON.parse(fs.readFileSync(
  path.join(__dirname, '..', 'notes', 'persist_v1_fixture.json'), 'utf8'));

let failures = 0;
function assert(cond, msg) {
  if (!cond) { console.error('FAIL:', msg); failures++; }
  else console.log('PASS', msg);
}
function key(ch) { return typeof ch === 'number' ? ch : ch.charCodeAt(0); }

function mockStorage() {
  const m = new Map();
  return {
    failWrites: false,
    getItem(k) { return m.has(k) ? m.get(k) : null; },
    setItem(k, v) {
      if (this.failWrites) throw new Error('quota');
      m.set(k, String(v));
    },
    removeItem(k) { m.delete(k); },
    raw() { return m; }
  };
}

async function makeInstance() {
  const { instance } = await WebAssembly.instantiate(wasm, {});
  const e = instance.exports;
  const mem = () => new Uint8Array(e.memory.buffer);
  const copy = (b) => { const p = e.cc_alloc(b.length); mem().set(b, p); return p; };
  const sput = (s) => {
    const p = e.cc_alloc(s.length + 1);
    const m = mem();
    for (let i = 0; i < s.length; i++) m[p + i] = s.charCodeAt(i);
    m[p + s.length] = 0;
    return p;
  };
  e.cc_set_assets(copy(gfx), copy(levels));
  e.cc_set_cells(copy(cells));
  const ovp = e.cc_alloc(overlay.length);
  mem().set(overlay, ovp);
  e.cc_set_level_overlay(ovp);
  e.cc_set_screens(copy(screens));
  e.cc_text_set_fonts(copy(f1), copy(f2), copy(spl));
  const pp = e.cc_alloc(64);
  const p32 = new Uint32Array(e.memory.buffer, pp, 16);
  pal.colors.forEach(([r, g, b], i) => { p32[i] = (r << 16) | (g << 8) | b; });
  e.cc_set_palette(pp);
  /* The persistence tests do not exercise audio; disable it so the finale
   * ID-26 wait does not hold the shell in the story state. */
  e.cc_sound_init();
  e.cc_sound_set_enabled(0);
  const stateSize = e.cc_state_size();
  const stateP = e.cc_alloc(stateSize);
  const tmpP = e.cc_alloc(stateSize);
  const guardP = e.cc_alloc(64);
  return { e, mem, copy, sput, stateSize, stateP, tmpP, guardP };
}

function stateBytes(inst) {
  const n = inst.e.cc_state_save(inst.stateP, inst.stateSize);
  return inst.mem().slice(inst.stateP, inst.stateP + n);
}

function bytesEqual(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

function crc32(bytes) {
  let c = 0xFFFFFFFF;
  for (let i = 0; i < bytes.length; i++) {
    c ^= bytes[i];
    for (let k = 0; k < 8; k++)
      c = (c >>> 1) ^ (0xEDB88320 & (0 - (c & 1)));
  }
  return (c ^ 0xFFFFFFFF) >>> 0;
}

function fixCrc(buf) {
  const c = crc32(buf.subarray(12));
  buf[8] = c & 0xFF;
  buf[9] = (c >>> 8) & 0xFF;
  buf[10] = (c >>> 16) & 0xFF;
  buf[11] = (c >>> 24) & 0xFF;
}

function drive(e, frames, start) {
  for (let i = 0; i < frames; i++) {
    const t = start + i;
    e.cc_input_set(0x338F, 1);
    e.cc_input_set(0x3390, (t % 7 === 0) ? 1 : 0);
    e.cc_objects_step_full();
  }
}

function toMenu(e) {
  e.cc_shell_init();
  for (let i = 0; i < 3; i++) e.cc_shell_step_ticks(1000);
  while (e.cc_shell_state() !== 2) e.cc_shell_key(13);
}

function toGame(e) {
  toMenu(e);
  e.cc_shell_key(key('n'));
  for (let i = 0; i < 20 && e.cc_shell_state() === 3; i++) e.cc_shell_key(13);
}

function cstr(e, p) {
  const m = new Uint8Array(e.memory.buffer);
  let s = '';
  for (let i = 0; i < 64 && m[p + i]; i++) s += String.fromCharCode(m[p + i]);
  return s;
}

function buildConfig(e) {
  return {
    version: 2,
    sound: e.cc_shell_sound_on(),
    joystick: e.cc_shell_joystick_on(),
    video: e.cc_shell_video_alt(),
    keys: [0, 1, 2, 3].map(i => e.cc_shell_key_for_action(i))
  };
}

function applyConfig(e, cfg) {
  e.cc_shell_set_config(cfg.sound, cfg.joystick, cfg.video);
  for (let i = 0; i < 4; i++) e.cc_shell_set_key(i, cfg.keys[i]);
}

(async () => {
  const A = await makeInstance();
  const e = A.e;

  // ---- format / size -------------------------------------------------------
  assert(A.stateSize === 23548, 'state buffer size covers v1 and v2');
  e.cc_objects_init(2);
  const n0 = e.cc_state_save(A.stateP, A.stateSize);
  assert(n0 === 23022, 'v2 save length is 12 + 23010');
  {
    const b = A.mem().slice(A.stateP, A.stateP + 12);
    assert(b[0] === 0x43 && b[1] === 0x43 && b[2] === 0x53 && b[3] === 0x32,
           'v2 magic is CCS2');
    assert(b[4] === 2 && b[5] === 0, 'v2 version is 2');
    assert(b[6] === (23010 & 0xFF) && b[7] === (23010 >> 8),
           'v2 payload size is 23010');
  }

  // ---- save must not mutate live state ------------------------------------
  e.cc_objects_init(2);
  drive(e, 20, 0);
  const xBefore = e.cc_obj_x(1);
  const logBefore = e.cc_sound_log_count();
  const s1 = stateBytes(A);
  const s2 = stateBytes(A);
  assert(bytesEqual(s1, s2), 'two consecutive saves are byte-identical');
  assert(e.cc_obj_x(1) === xBefore, 'saving does not corrupt the player slot');
  assert(e.cc_sound_log_count() === logBefore, 'save leaves the sound log alone');

  // ---- exact hub round-trip + subsequent frames ---------------------------
  e.cc_objects_init(2);
  drive(e, 40, 0);
  const T = stateBytes(A);
  drive(e, 60, 40);
  const after = stateBytes(A);
  A.mem().set(T, A.tmpP);
  assert(e.cc_state_load(A.tmpP, T.length) === 1, 'hub save loads');
  assert(bytesEqual(stateBytes(A), T), 'hub restore reproduces the saved state');
  drive(e, 60, 40);
  assert(bytesEqual(stateBytes(A), after),
         'hub restore continues identically for 60 frames');

  // ---- exact cave round-trips ---------------------------------------------
  for (const level of [3, 18]) {
    e.cc_objects_init(level);
    drive(e, 30, 0);
    const t = stateBytes(A);
    drive(e, 40, 30);
    const a = stateBytes(A);
    A.mem().set(t, A.tmpP);
    assert(e.cc_state_load(A.tmpP, t.length) === 1, 'cave ' + level + ' loads');
    assert(bytesEqual(stateBytes(A), t),
           'cave ' + level + ' restore reproduces the saved state');
    drive(e, 40, 30);
    assert(bytesEqual(stateBytes(A), a),
           'cave ' + level + ' continues identically for 40 frames');
  }

  // ---- corruption is atomic and reports the reason ------------------------
  e.cc_objects_init(2);
  drive(e, 10, 0);
  const good = stateBytes(A);
  const liveBefore = stateBytes(A);
  /* Payload offsets from the frozen v2 layout: obj_cursor 2700,
   * cur_level 22762. */
  const cases = [
    ['magic', 0, 0x58, 2, false],
    ['version', 4, 3, 3, false],
    ['size', 6, 0, 4, false],
    ['crc', 12, good[12] ^ 0xFF, 5, false],
    ['level', 12 + 22762, 99, 6, true],
    ['cursor', 12 + 2700, 60, 6, true]
  ];
  for (const [name, off, val, err, fix] of cases) {
    const bad = good.slice();
    bad[off] = val;
    if (fix) fixCrc(bad);
    A.mem().set(bad, A.tmpP);
    const r = e.cc_state_load(A.tmpP, bad.length);
    assert(r === 0, 'corrupt ' + name + ' is rejected');
    assert(e.cc_state_last_error() === err,
           'corrupt ' + name + ' reports error ' + err);
    assert(bytesEqual(stateBytes(A), liveBefore),
           'corrupt ' + name + ' leaves live state unchanged');
  }

  // ---- v1 migration --------------------------------------------------------
  {
    const v1 = new Uint8Array(8 + 23540);
    v1[0] = 0x43; v1[1] = 0x43; v1[2] = 0x53; v1[3] = 0x31;
    v1[4] = 1; v1[5] = 0;
    v1[6] = 23540 & 0xFF; v1[7] = 23540 >> 8;
    v1.set(good.subarray(12, 12 + 23010), 8);
    v1.fill(0xAA, 8 + 23010);
    A.mem().set(v1, A.tmpP);
    assert(e.cc_state_load(A.tmpP, v1.length) === 1, 'v1 save migrates');
    assert(bytesEqual(stateBytes(A), good), 'v1 migration preserves v2 state');
    assert(e.cc_state_last_error() === 0, 'v1 migration reports success');
    assert(e.cc_state_load(A.tmpP, v1.length - 1) === 0,
           'truncated v1 save is rejected');
    assert(e.cc_state_last_error() === 4, 'truncated v1 reports ERR_SIZE');
  }
  {
    /* Real Phase 5 v1 save captured from commit 33f292f. */
    const bytes = Buffer.from(v1Fixture.v1_b64, 'base64');
    assert(bytes.length === 23548, 'real v1 fixture has the legacy length');
    A.mem().set(bytes, A.tmpP);
    assert(e.cc_state_load(A.tmpP, bytes.length) === 1,
           'real Phase 5 v1 save migrates');
    const v2 = stateBytes(A);
    const sha = crypto.createHash('sha256')
      .update(Buffer.from(v2.subarray(12))).digest('hex');
    assert(sha === v1Fixture.v2_payload_sha256,
           'real v1 save produces the expected v2 payload');
    assert(e.cc_cur_level() === v1Fixture.level &&
           e.cc_player_x() === v1Fixture.player_x &&
           e.cc_player_y() === v1Fixture.player_y &&
           (e.cc_score() >>> 0) === (v1Fixture.score >>> 0),
           'real v1 save restores level/player/score');
  }

  // ---- hub-only flag -------------------------------------------------------
  e.cc_objects_init(3);
  drive(e, 10, 0);
  const cave = stateBytes(A);
  A.mem().set(cave, A.tmpP);
  assert(e.cc_state_load_ex(A.tmpP, cave.length, 1) === 0,
         'hub-only load rejects a cave save');
  assert(e.cc_state_last_error() === 7, 'hub-only rejection reports ERR_NOT_HUB');
  assert(e.cc_state_load_ex(A.tmpP, cave.length, 0) === 1,
         'general load accepts a cave save');

  // ---- high-score persistence ---------------------------------------------
  const hsStore = mockStorage();
  {
    const inst = await makeInstance();
    inst.e.cc_shell_init();
    inst.e.cc_shell_hiscore_add(inst.sput('P7 TEST'), 1234567);
    assert(CCPersist.saveHighScores(hsStore, inst.e) === true,
           'high scores save');
    const raw = JSON.parse(hsStore.getItem(CCPersist.HS_KEY));
    assert(Array.isArray(raw) && raw.length === 10,
           'high-score file has exactly ten rows');
    assert(raw[0].name === 'P7 TEST' && raw[0].score === 1234567,
           'high-score row round-trips through JSON');
  }
  {
    const inst = await makeInstance();
    inst.e.cc_shell_init();
    assert(CCPersist.loadHighScores(hsStore, inst.e, inst.sput) === true,
           'high scores load');
    const name = inst.mem();
    const p = inst.e.cc_shell_hiscore_name(0);
    let s = '';
    for (let i = 0; i < 64 && name[p + i]; i++) s += String.fromCharCode(name[p + i]);
    assert(s === 'P7 TEST' && (inst.e.cc_shell_hiscore_score(0) >>> 0) === 1234567,
           'high scores survive a reload');
  }

  const corruptTables = [
    null,
    [],
    new Array(9).fill({ name: 'x', score: 1 }),
    new Array(11).fill({ name: 'x', score: 1 }),
    new Array(10).fill({ name: 'x', score: '1' }),
    new Array(10).fill({ name: 'x', score: -1 }),
    new Array(10).fill({ name: 'x', score: 1.5 }),
    new Array(10).fill({ name: 7, score: 1 })
  ];
  for (let i = 0; i < corruptTables.length; i++) {
    const store = mockStorage();
    if (corruptTables[i] !== null)
      store.setItem(CCPersist.HS_KEY, JSON.stringify(corruptTables[i]));
    const inst = await makeInstance();
    inst.e.cc_shell_init();
    assert(CCPersist.loadHighScores(store, inst.e, inst.sput) === false,
           'corrupt high-score table ' + i + ' falls back to defaults');
    let ok = inst.e.cc_shell_hiscore_count() === 10;
    for (let r = 0; r < 10; r++) {
      const p = inst.e.cc_shell_hiscore_name(r);
      let s = '';
      const m = inst.mem();
      for (let j = 0; j < 64 && m[p + j]; j++) s += String.fromCharCode(m[p + j]);
      if (s !== hiscoreFixture.defaults.names[r] ||
          (inst.e.cc_shell_hiscore_score(r) >>> 0) !==
            (hiscoreFixture.defaults.scores[r] >>> 0)) ok = false;
    }
    assert(ok, 'corrupt high-score table ' + i + ' leaves the original defaults');
  }

  // valid custom table, including 20-char name and 0xFFFFFFFF
  {
    const store = mockStorage();
    const rows = [];
    for (let i = 0; i < 10; i++)
      rows.push({ name: ('ROW' + i).padEnd(20, '.'), score: 1000000 - i });
    rows[9].score = 0xFFFFFFFF;
    store.setItem(CCPersist.HS_KEY, JSON.stringify(rows));
    const inst = await makeInstance();
    inst.e.cc_shell_init();
    assert(CCPersist.loadHighScores(store, inst.e, inst.sput) === true,
           'valid custom high-score table loads');
    assert((inst.e.cc_shell_hiscore_score(9) >>> 0) === 0xFFFFFFFF,
           '0xFFFFFFFF high score round-trips');
  }

  {
    // Full name-entry -> high-score -> reload path.
    const store = mockStorage();
    const inst = await makeInstance();
    toGame(inst.e);
    const score = 200000;
    inst.e.cc_global_set(0x50A5, score & 0xFFFF);
    inst.e.cc_global_set(0x50A7, (score >>> 16) & 0xFFFF);
    inst.e.cc_shell_finale_done();
    inst.e.cc_shell_step();
    for (let i = 0; i < 10 && inst.e.cc_shell_state() === 3; i++)
      inst.e.cc_shell_key(13);
    inst.e.cc_shell_key(13);
    assert(inst.e.cc_shell_state() === 12,
           'qualifying finale score enters name entry');
    for (const ch of 'P7 NAME') inst.e.cc_shell_key(key(ch));
    inst.e.cc_shell_key(13);
    assert(inst.e.cc_shell_state() === 11,
           'name entry returns to the high-score screen');
    CCPersist.saveHighScores(store, inst.e);
    const inst2 = await makeInstance();
    inst2.e.cc_shell_init();
    assert(CCPersist.loadHighScores(store, inst2.e, inst2.sput) === true,
           'name-entry high score reloads');
    assert(cstr(inst2.e, inst2.e.cc_shell_hiscore_name(0)) === 'P7 NAME' &&
           (inst2.e.cc_shell_hiscore_score(0) >>> 0) === score,
           'name-entry row survives reload');
  }

  // ---- config persistence / migration -------------------------------------
  {
    const store = mockStorage();
    const cfg = CCPersist.loadConfig(store);
    assert(cfg.version === 2 && cfg.sound === 1 && cfg.joystick === 0 &&
           cfg.video === 0 && cfg.keys.join(',') === '75,77,29,56',
           'config defaults match the original');
    store.setItem(CCPersist.CFG_KEY,
                  JSON.stringify({ version: 2, sound: 0, joystick: 1,
                                   video: 1, keys: [0x1E, 0x20, 0x39, 0x1C] }));
    const c2 = CCPersist.loadConfig(store);
    assert(c2.sound === 0 && c2.joystick === 1 && c2.video === 1 &&
           c2.keys.join(',') === '30,32,57,28',
           'valid config loads');
    store.removeItem(CCPersist.CFG_KEY);
    store.setItem(CCPersist.OLD_CFG_KEY,
                  JSON.stringify({ sound: 0, joystick: 1, video: 1,
                                   keys: [37, 39, 17, 38] }));
    const c3 = CCPersist.loadConfig(store);
    assert(c3.sound === 0 && c3.joystick === 1 && c3.video === 1 &&
           c3.keys.join(',') === '75,77,29,56',
           'v1 config migrates to DOS key defaults');
    store.removeItem(CCPersist.OLD_CFG_KEY);
    store.setItem(CCPersist.CFG_KEY,
                  JSON.stringify({ version: 2, sound: 'yes', joystick: 2,
                                   video: null, keys: [1, 2] }));
    const c4 = CCPersist.loadConfig(store);
    assert(c4.sound === 1 && c4.joystick === 0 && c4.video === 0 &&
           c4.keys.join(',') === '75,77,29,56',
           'corrupt config fields fall back to defaults');
  }
  {
    const store = mockStorage();
    const inst = await makeInstance();
    inst.e.cc_shell_init();
    inst.e.cc_shell_set_config(0, 1, 1);
    const newKeys = [0x1E, 0x20, 0x39, 0x1C];
    for (let i = 0; i < 4; i++) inst.e.cc_shell_set_key(i, newKeys[i]);
    const cfg = buildConfig(inst.e);
    assert(CCPersist.saveConfig(store, cfg) === true, 'config saves');
    const inst2 = await makeInstance();
    inst2.e.cc_shell_init();
    const loaded = CCPersist.loadConfig(store);
    applyConfig(inst2.e, loaded);
    assert(inst2.e.cc_shell_sound_on() === 0 &&
           inst2.e.cc_shell_joystick_on() === 1 &&
           inst2.e.cc_shell_video_alt() === 1 &&
           inst2.e.cc_shell_key_for_action(0) === 0x1E &&
           inst2.e.cc_shell_key_for_action(3) === 0x1C,
           'config survives a reload');
  }
  {
    // Sound/joystick toggles in the help menu persist.
    const store = mockStorage();
    const inst = await makeInstance();
    toGame(inst.e);
    inst.e.cc_shell_key(27);       // help
    inst.e.cc_shell_key(key('t')); // sound off
    inst.e.cc_shell_key(key('j')); // joystick on
    const cfg = buildConfig(inst.e);
    CCPersist.saveConfig(store, cfg);
    const inst2 = await makeInstance();
    inst2.e.cc_shell_init();
    applyConfig(inst2.e, CCPersist.loadConfig(store));
    assert(inst2.e.cc_shell_sound_on() === 0 &&
           inst2.e.cc_shell_joystick_on() === 1,
           'help sound/joystick toggles persist');
  }
  {
    // Key redefine confirmation persists.
    const store = mockStorage();
    const inst = await makeInstance();
    toGame(inst.e);
    inst.e.cc_shell_key(27);
    inst.e.cc_shell_key(key('k'));
    for (const k of [0x1E, 0x20, 0x39, 0x1C]) inst.e.cc_shell_key_scan(k);
    inst.e.cc_shell_key_scan(0x15); // Y
    assert(inst.e.cc_shell_key_for_action(0) === 0x1E,
           'redefined key is active');
    CCPersist.saveConfig(store, buildConfig(inst.e));
    const inst2 = await makeInstance();
    inst2.e.cc_shell_init();
    applyConfig(inst2.e, CCPersist.loadConfig(store));
    assert(inst2.e.cc_shell_key_for_action(0) === 0x1E &&
           inst2.e.cc_shell_key_for_action(3) === 0x1C,
           'redefined keys survive a reload');
  }

  // ---- slot persistence / shell flow --------------------------------------
  const slotStore = mockStorage();
  {
    const inst = await makeInstance();
    toGame(inst.e);
    inst.e.cc_global_set(0x50A5, 4321);
    drive(inst.e, 15, 0);
    const saved = stateBytes(inst);
    assert(CCPersist.saveSlot(slotStore, 0, inst.e, inst.stateP,
                              inst.stateSize, inst.mem) === true,
           'hub save slot writes');
    const inst2 = await makeInstance();
    assert(CCPersist.loadSlot(slotStore, 0, inst2.e, inst2.stateP,
                              inst2.stateSize, inst2.mem, true) === true,
           'hub save slot loads with hub-only flag');
    assert(bytesEqual(stateBytes(inst2), saved),
           'slot reload reproduces the saved state');
  }
  {
    // Cave slot: rejected for the shell, accepted by the general loader.
    const inst = await makeInstance();
    inst.e.cc_objects_init(3);
    drive(inst.e, 10, 0);
    assert(CCPersist.saveSlot(slotStore, 1, inst.e, inst.stateP,
                              inst.stateSize, inst.mem) === true,
           'cave slot writes');
    const inst2 = await makeInstance();
    assert(CCPersist.loadSlot(slotStore, 1, inst2.e, inst2.stateP,
                              inst2.stateSize, inst2.mem, true) === false,
           'cave slot is rejected by the hub-only shell loader');
    assert(CCPersist.loadSlot(slotStore, 1, inst2.e, inst2.stateP,
                              inst2.stateSize, inst2.mem, false) === true,
           'cave slot loads through the general API');
  }
  {
    const inst = await makeInstance();
    assert(CCPersist.loadSlot(mockStorage(), 9, inst.e, inst.stateP,
                              inst.stateSize, inst.mem, true) === false,
           'empty slot is rejected');
  }
  {
    // Corrupt and oversized slots never touch live state / memory.
    const inst = await makeInstance();
    toGame(inst.e);
    drive(inst.e, 10, 0);
    const before = stateBytes(inst);
    const raw = slotStore.getItem(CCPersist.SAVE_PREFIX + '0');
    const bytes = Buffer.from(raw, 'base64');
    bytes[20] ^= 0xFF;
    slotStore.setItem(CCPersist.SAVE_PREFIX + '0', bytes.toString('base64'));
    assert(CCPersist.loadSlot(slotStore, 0, inst.e, inst.stateP,
                              inst.stateSize, inst.mem, true) === false,
           'corrupt slot is rejected');
    assert(bytesEqual(stateBytes(inst), before),
           'corrupt slot leaves live state unchanged');
    const huge = Buffer.alloc(inst.stateSize + 1, 0x5A);
    slotStore.setItem(CCPersist.SAVE_PREFIX + '2', huge.toString('base64'));
    inst.mem().fill(0x5A, inst.guardP, inst.guardP + 64);
    assert(CCPersist.loadSlot(slotStore, 2, inst.e, inst.stateP,
                              inst.stateSize, inst.mem, true) === false,
           'oversized slot is rejected');
    let guardOk = true;
    const m = inst.mem();
    for (let i = 0; i < 64; i++) if (m[inst.guardP + i] !== 0x5A) guardOk = false;
    assert(guardOk, 'oversized slot does not overwrite past the state buffer');
  }
  {
    // v1 slot migration through the browser layer.
    const inst = await makeInstance();
    inst.e.cc_objects_init(2);
    drive(inst.e, 10, 0);
    const v2 = stateBytes(inst);
    const v1 = Buffer.alloc(8 + 23540, 0xAA);
    v1[0] = 0x43; v1[1] = 0x43; v1[2] = 0x53; v1[3] = 0x31;
    v1[4] = 1; v1[5] = 0; v1[6] = 23540 & 0xFF; v1[7] = 23540 >> 8;
    Buffer.from(v2.subarray(12, 12 + 23010)).copy(v1, 8);
    slotStore.setItem(CCPersist.SAVE_PREFIX + '3', v1.toString('base64'));
    const inst2 = await makeInstance();
    assert(CCPersist.loadSlot(slotStore, 3, inst2.e, inst2.stateP,
                              inst2.stateSize, inst2.mem, true) === true,
           'v1 slot migrates through the browser layer');
    assert(bytesEqual(stateBytes(inst2), v2),
           'v1 slot migration preserves state');
  }

  {
    // Storage failures are reported without throwing.
    const store = mockStorage();
    store.failWrites = true;
    const inst = await makeInstance();
    toGame(inst.e);
    assert(CCPersist.saveSlot(store, 0, inst.e, inst.stateP,
                              inst.stateSize, inst.mem) === false,
           'save slot reports a storage failure');
    assert(CCPersist.saveConfig(store, CCPersist.defaultConfig()) === false,
           'config save reports a storage failure');
    assert(CCPersist.saveHighScores(store, inst.e) === false,
           'high-score save reports a storage failure');
  }

  // ---- shell save/restore messages and restrictions -----------------------
  {
    const inst = await makeInstance();
    toGame(inst.e);
    inst.e.cc_global_set(0x50A5, 9876);
    drive(inst.e, 10, 0);
    const x0 = inst.e.cc_obj_x(1);
    inst.e.cc_shell_key(27);       // help
    inst.e.cc_shell_key(key('s'));
    assert(inst.e.cc_shell_state() === 8, 'save prompt');
    inst.e.cc_shell_key(key('0'));
    assert(inst.e.cc_shell_request_pending() === 1 &&
           inst.e.cc_shell_request_kind() === 1 &&
           inst.e.cc_shell_request_slot() === 0,
           'hub save request');
    assert(CCPersist.saveSlot(slotStore, 0, inst.e, inst.stateP,
                              inst.stateSize, inst.mem) === true,
           'shell save writes slot 0');
    inst.e.cc_shell_request_done(1);
    assert(inst.e.cc_shell_state() === 14, 'save result message');
    assert(inst.e.cc_shell_message_block() === 30,
           'save result uses the Game saved window');
    inst.e.cc_shell_key(13);       // Enter must be ignored
    assert(inst.e.cc_shell_state() === 14,
           'Game saved message ignores Enter');
    inst.e.cc_shell_key(27);       // Esc returns
    assert(inst.e.cc_shell_state() === 4, 'Game saved message exits on Esc');
    assert(inst.e.cc_obj_x(1) === x0, 'shell save does not corrupt the player');
  }
  {
    // Cave save/restore restrictions.
    const inst = await makeInstance();
    inst.e.cc_shell_warp(1);
    assert(inst.e.cc_cur_level() === 3, 'warped to cave 1');
    inst.e.cc_shell_key(27);
    inst.e.cc_shell_key(key('s'));
    inst.e.cc_shell_key(key('0'));
    assert(inst.e.cc_shell_request_pending() === 0,
           'cave save raises no request');
    assert(inst.e.cc_shell_state() === 14 &&
           inst.e.cc_shell_message_block() === 31,
           'cave save shows the save-hub-only window');
    inst.e.cc_shell_key(13);
    assert(inst.e.cc_shell_state() === 4, 'cave save message dismisses');
    inst.e.cc_shell_key(27);
    inst.e.cc_shell_key(key('r'));
    inst.e.cc_shell_key(key('0'));
    assert(inst.e.cc_shell_request_pending() === 0,
           'cave restore raises no request');
    assert(inst.e.cc_shell_state() === 14 &&
           inst.e.cc_shell_message_block() === 33,
           'cave restore shows the restore-hub-only window');
    inst.e.cc_shell_key(13);       // Enter ignored
    assert(inst.e.cc_shell_state() === 14,
           'restore-hub-only ignores Enter');
    inst.e.cc_shell_key(27);
    assert(inst.e.cc_shell_state() === 4,
           'restore-hub-only exits on Esc');
  }
  {
    // Main-menu restore.
    const inst = await makeInstance();
    toGame(inst.e);
    inst.e.cc_global_set(0x50A5, 2468);
    drive(inst.e, 12, 0);
    const saved = stateBytes(inst);
    assert(CCPersist.saveSlot(slotStore, 5, inst.e, inst.stateP,
                              inst.stateSize, inst.mem) === true,
           'main-menu test saves slot 5');
    inst.e.cc_shell_key(27);       // help
    inst.e.cc_shell_key(key('q')); // quit
    inst.e.cc_shell_key(key('t')); // title
    assert(inst.e.cc_shell_state() === 1, 'quit to title');
    inst.e.cc_shell_key(13);       // title -> menu
    assert(inst.e.cc_shell_state() === 2, 'title to menu');
    inst.e.cc_shell_key(key('r'));
    assert(inst.e.cc_shell_state() === 9, 'main-menu restore prompt');
    inst.e.cc_shell_key(key('5'));
    assert(inst.e.cc_shell_request_pending() === 1 &&
           inst.e.cc_shell_request_kind() === 2 &&
           inst.e.cc_shell_request_slot() === 5,
           'main-menu restore request');
    assert(CCPersist.loadSlot(slotStore, 5, inst.e, inst.stateP,
                              inst.stateSize, inst.mem, true) === true,
           'main-menu restore loads the hub slot');
    inst.e.cc_shell_request_done(1);
    assert(inst.e.cc_shell_state() === 4, 'restore returns to game');
    assert(bytesEqual(stateBytes(inst), saved),
           'main-menu restore reproduces the saved state');
  }
  {
    // Corrupt restore shows File Error.
    const store = mockStorage();
    store.setItem(CCPersist.SAVE_PREFIX + '4', 'not base64 !!!');
    const inst = await makeInstance();
    toMenu(inst.e);
    inst.e.cc_shell_key(key('r'));
    inst.e.cc_shell_key(key('4'));
    assert(inst.e.cc_shell_request_pending() === 1, 'corrupt restore request');
    assert(CCPersist.loadSlot(store, 4, inst.e, inst.stateP,
                              inst.stateSize, inst.mem, true) === false,
           'corrupt restore slot fails');
    inst.e.cc_shell_request_done(0);
    assert(inst.e.cc_shell_state() === 14 &&
           inst.e.cc_shell_message_block() === 22,
           'corrupt restore shows File Error');
  }

  // ---- browser "not available" screens ------------------------------------
  {
    const inst = await makeInstance();
    toMenu(inst.e);
    for (const k of ['o', 'f', 'c', 'v', 'a']) {
      inst.e.cc_shell_key(key(k));
      assert(inst.e.cc_shell_state() === 3,
             'menu option ' + k + ' opens a text screen');
      assert(inst.e.cc_shell_request_pending() === 0,
             'menu option ' + k + ' performs no file I/O');
      let guard = 0;
      while (inst.e.cc_shell_state() === 3 && guard++ < 40)
        inst.e.cc_shell_key(13);
      assert(inst.e.cc_shell_state() === 2,
             'menu option ' + k + ' returns to the menu');
    }
  }

  if (failures) {
    console.error('\n' + failures + ' persistence check(s) failed');
    process.exit(1);
  }
  console.log('\nPASS: Phase 7 persistence, high-score and config checks');
})();
