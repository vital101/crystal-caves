// P5.R5 exactness regression: timing, story pages, high scores, scan codes.
const fs = require('fs');
const path = require('path');

const read = (p) => fs.readFileSync(path.join(__dirname, p));
const fixture = JSON.parse(fs.readFileSync(
  path.join(__dirname, '..', 'notes', 'shell_exactness_fixtures.json'), 'utf8'));
const keymapFixture = JSON.parse(fs.readFileSync(
  path.join(__dirname, '..', 'notes', 'keymap_fixtures.json'), 'utf8'));
const zlib = require('zlib');

function assert(cond, msg) {
  if (!cond) { console.error('FAIL:', msg); process.exitCode = 1; }
  else console.log('PASS', msg);
}
function key(ch) { return typeof ch === 'number' ? ch : ch.charCodeAt(0); }

(async () => {
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

  const { instance } = await WebAssembly.instantiate(wasm, {});
  const e = instance.exports;
  /* P5 exactness covers layout/timing, not audio; keep the Phase 6 shell
   * fanfare/wait from delaying page transitions in this fixture. */
  e.cc_sound_set_enabled(0);
  const mem = () => new Uint8Array(e.memory.buffer);
  const copy = (b) => { const p = e.cc_alloc(b.length); mem().set(b, p); return p; };
  const sput = (s) => {
    const p = e.cc_alloc(s.length + 1);
    const m = mem();
    for (let i = 0; i < s.length; i++) m[p + i] = s.charCodeAt(i);
    m[p + s.length] = 0;
    return p;
  };
  const cstr = (p) => {
    const m = mem(); let s = '';
    for (let i = 0; p && m[p + i]; i++) s += String.fromCharCode(m[p + i]);
    return s;
  };
  const toMenu = () => {
    for (let i = 0; i < 3; i++) e.cc_shell_step_ticks(1000);
    if (e.cc_shell_state() !== 2) {
      // A key on the title/credits states also advances one stage.
      while (e.cc_shell_state() !== 2) e.cc_shell_key(13);
    }
  };
  const toGame = () => {
    toMenu();
    e.cc_shell_key(key('n'));
    for (let i = 0; i < 20 && e.cc_shell_state() === 3; i++) e.cc_shell_key(13);
  };

  e.cc_set_assets(copy(gfx), copy(levels));
  e.cc_set_cells(copy(cells));
  const ovp = e.cc_alloc(overlay.length); mem().set(overlay, ovp);
  e.cc_set_level_overlay(ovp);
  e.cc_set_screens(copy(screens));
  e.cc_text_set_fonts(copy(f1), copy(f2), copy(spl));
  const pp = e.cc_alloc(64);
  const p32 = new Uint32Array(e.memory.buffer, pp, 16);
  pal.colors.forEach(([r, g, b], i) => { p32[i] = (r << 16) | (g << 8) | b; });
  e.cc_set_palette(pp);

  // ---- boot / screen timing ------------------------------------------------
  e.cc_shell_init();
  assert(e.cc_shell_state() === 0, 'boot starts at APG');
  e.cc_shell_step_ticks(fixture.flow.timing_ticks.apogee);
  assert(e.cc_shell_state() === 0, 'APG lasts exactly its tick count');
  e.cc_shell_step_ticks(1);
  assert(e.cc_shell_state() === 15, 'APG -> credits');
  e.cc_shell_step_ticks(fixture.flow.timing_ticks.credits);
  assert(e.cc_shell_state() === 15, 'credits lasts exactly its tick count');
  e.cc_shell_step_ticks(1);
  assert(e.cc_shell_state() === 1, 'credits -> title');
  e.cc_shell_step_ticks(fixture.flow.timing_ticks.title);
  assert(e.cc_shell_state() === 1, 'title lasts exactly its tick count');
  e.cc_shell_step_ticks(1);
  assert(e.cc_shell_state() === 2, 'title -> main menu');

  // ---- high-score screen pixel oracle --------------------------------------
  e.cc_shell_init();
  toMenu();
  e.cc_shell_key(key('h'));
  assert(e.cc_shell_state() === 11, 'main menu reaches high scores');
  e.cc_shell_render();
  {
    const idx = zlib.inflateSync(Buffer.from(fixture.highscore.screen_fb_b64, 'base64'));
    const fb = new Uint8Array(e.memory.buffer, e.cc_view_framebuffer(), 320 * 200 * 4);
    let diff = 0;
    for (let i = 0; i < 320 * 200; i++) {
      const c = pal.colors[idx[i]];
      if (fb[i * 4] !== c[0] || fb[i * 4 + 1] !== c[1] || fb[i * 4 + 2] !== c[2]) diff++;
    }
    assert(diff === 0, 'high-score screen matches original framebuffer');
  }

  // ---- intro page sequence -------------------------------------------------
  e.cc_shell_init();
  toMenu();
  e.cc_shell_key(key('n'));
  assert(e.cc_shell_state() === 3, 'new game enters intro');
  const intro = fixture.flow.intro_pages;
  assert(e.cc_shell_page_count() === intro.length,
         'intro page count matches original');
  for (let p = 0; p < intro.length; p++) {
    const lines = intro[p];
    let ok = e.cc_shell_page_line_count() === lines.length;
    for (let i = 0; ok && i < lines.length; i++)
      if (cstr(e.cc_shell_page_line(i)) !== lines[i]) ok = false;
    assert(ok, 'intro page ' + p + ' matches original line buffer');
    e.cc_shell_key(13);
  }
  assert(e.cc_shell_state() === 4, 'intro ends in the hub');

  // ---- high score defaults / qualification / tie order ---------------------
  e.cc_shell_init();
  const d = fixture.highscore.defaults;
  let defaultsOk = e.cc_shell_hiscore_count() === 10;
  for (let i = 0; i < 10; i++)
    if (cstr(e.cc_shell_hiscore_name(i)) !== d.names[i] ||
        e.cc_shell_hiscore_score(i) !== d.scores[i]) defaultsOk = false;
  assert(defaultsOk, 'default high-score table matches original');

  for (const c of fixture.highscore.qualify_cases) {
    e.cc_shell_init();
    const q = e.cc_shell_hiscore_qualifies(c.score) ? 1 : 0;
    assert(q === (c.qualifies ? 1 : 0),
           'qualification for ' + c.score);
  }

  e.cc_shell_init();
  e.cc_shell_hiscore_add(sput('LOW'), 60000);
  assert(cstr(e.cc_shell_hiscore_name(9)) === 'J. Kirk',
         'equal-to-last score is rejected');
  e.cc_shell_hiscore_add(sput('TOP'), 150000);
  assert(cstr(e.cc_shell_hiscore_name(0)) === 'Play all 3 episodes',
         'equal-to-existing score keeps the older entry first');
  e.cc_shell_init();
  e.cc_shell_hiscore_add(sput('NEW'), 150001);
  assert(cstr(e.cc_shell_hiscore_name(0)) === 'NEW' &&
         e.cc_shell_hiscore_score(0) === 150001,
         'higher score inserts at the top');
  assert(cstr(e.cc_shell_hiscore_name(9)) === 'D. Nukem',
         'dropped entry is the previous last row');

  // ---- scan codes / key redefine ------------------------------------------
  e.cc_shell_init();
  assert(e.cc_shell_key_for_action(0) === 0x4B &&
         e.cc_shell_key_for_action(1) === 0x4D &&
         e.cc_shell_key_for_action(2) === 0x1D &&
         e.cc_shell_key_for_action(3) === 0x38,
         'DOS scan-code defaults');
  assert(e.cc_shell_action_for_scan(0x4B) === 0, 'left scan code maps to left');
  assert(e.cc_shell_key_name(0x4B, 0) === 1, 'LEFT ARROW is a named key');
  assert(e.cc_shell_key_name(0x10, 0) === 0, 'Q is unavailable');

  // Enter help -> redefine, try a duplicate, then accept new bindings.
  e.cc_shell_init();
  toGame();
  assert(e.cc_shell_state() === 4, 'reached game for redefine test');
  e.cc_shell_key(27);
  e.cc_shell_key(key('k'));
  assert(e.cc_shell_state() === 16, 'redefine prompt');
  e.cc_shell_key_scan(0x1E);
  e.cc_shell_key_scan(0x1E);
  assert(e.cc_shell_state() === 14, 'duplicate key shows unavailable message');
  e.cc_shell_key(13);
  assert(e.cc_shell_state() === 16, 'unavailable message returns to redefine');
  e.cc_shell_key_scan(0x20);
  e.cc_shell_key_scan(0x39);
  e.cc_shell_key_scan(0x1C);
  assert(e.cc_shell_state() === 17, 'four keys reach confirmation');
  e.cc_shell_key_scan(0x31);   // N
  assert(e.cc_shell_state() === 16 &&
         e.cc_shell_key_for_action(0) === 0x4B,
         'N rejects the new bindings');
  for (const k of [0x1E, 0x20, 0x39, 0x1C]) e.cc_shell_key_scan(k);
  e.cc_shell_key_scan(0x15);   // Y
  assert(e.cc_shell_state() === 6 &&
         e.cc_shell_key_for_action(0) === 0x1E &&
         e.cc_shell_key_for_action(3) === 0x1C,
         'Y commits the new scan codes');

  assert(keymapFixture.names['75'] === 'LEFT ARROW' &&
         keymapFixture.names['29'] === 'CTRL' &&
         keymapFixture.names['57'] === 'SPACE',
         'generated scan-code name table matches labels');

  // ---- name-entry rules ----------------------------------------------------
  const toNameEntry = (score) => {
    e.cc_shell_init();
    toGame();
    e.cc_global_set(0x50A5, score & 0xFFFF);
    e.cc_global_set(0x50A7, (score >>> 16) & 0xFFFF);
    e.cc_shell_finale_done();
    e.cc_shell_step();
    for (let i = 0; i < 10 && e.cc_shell_state() === 3; i++) e.cc_shell_key(13);
    e.cc_shell_key(13);
    return e.cc_shell_state();
  };
  assert(toNameEntry(200000) === 12, 'qualifying score asks for a name');
  for (const ch of 'ABCDEFGHIJKLMNOPQRSTUVWXY') e.cc_shell_key(key(ch));
  e.cc_shell_key(13);
  assert(cstr(e.cc_shell_hiscore_name(0)) === 'ABCDEFGHIJKLMNOPQRST',
         'name entry truncates at 20 characters');
  assert(toNameEntry(200000) === 12, 'second qualifying score asks for a name');
  e.cc_shell_key(13);
  assert(cstr(e.cc_shell_hiscore_name(0)) === '',
         'empty name is preserved');

  // ---- finale page sequence ------------------------------------------------
  e.cc_shell_init();
  toGame();
  assert(e.cc_shell_state() === 4, 'reached hub for finale');
  e.cc_shell_finale_done();
  e.cc_shell_step();
  assert(e.cc_shell_state() === 3, 'finale starts');
  const finale = fixture.flow.finale_pages;
  assert(e.cc_shell_page_count() === finale.length,
         'finale page count matches original');
  for (let p = 0; p < finale.length; p++) {
    const lines = finale[p];
    let ok = e.cc_shell_page_line_count() === lines.length;
    for (let i = 0; ok && i < lines.length; i++)
      if (cstr(e.cc_shell_page_line(i)) !== lines[i]) ok = false;
    assert(ok, 'finale page ' + p + ' matches original line buffer');
    e.cc_shell_key(13);
  }
  assert(e.cc_shell_state() === 13, 'finale reaches ending');

  if (process.exitCode) process.exit(1);
})();
