// Phase 5 shell/HUD/text/save regression.
const fs = require('fs');
const path = require('path');

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

function key(ch) { return typeof ch === 'number' ? ch : ch.charCodeAt(0); }

function assert(cond, msg) {
  if (!cond) { console.error('FAIL:', msg); process.exitCode = 1; }
  else console.log('PASS', msg);
}

(async () => {
  const { instance } = await WebAssembly.instantiate(wasm, {});
  const e = instance.exports;
  const mem = () => new Uint8Array(e.memory.buffer);
  const copy = (b) => { const p = e.cc_alloc(b.length); mem().set(b, p); return p; };
  e.cc_set_assets(copy(gfx), copy(levels));
  e.cc_set_cells(copy(cells));
  const ovp = e.cc_alloc(overlay.length); mem().set(overlay, ovp); e.cc_set_level_overlay(ovp);
  e.cc_set_screens(copy(screens));
  e.cc_text_set_fonts(copy(f1), copy(f2), copy(spl));
  const pp = e.cc_alloc(64);
  const p32 = new Uint32Array(e.memory.buffer, pp, 16);
  pal.colors.forEach(([r, g, b], i) => { p32[i] = (r << 16) | (g << 8) | b; });
  e.cc_set_palette(pp);

  const nonBlack = () => {
    const fb = e.cc_view_framebuffer();
    const u8 = new Uint8Array(e.memory.buffer, fb, 320 * 200 * 4);
    let n = 0;
    for (let i = 0; i < 320 * 200; i++)
      if (u8[i * 4] || u8[i * 4 + 1] || u8[i * 4 + 2]) n++;
    return n;
  };

  e.cc_shell_init();
  assert(e.cc_shell_state() === 0, 'shell starts at logo');
  e.cc_shell_render();
  assert(nonBlack() > 1000, 'logo screen renders');

  e.cc_shell_key(13);
  assert(e.cc_shell_state() === 15, 'logo -> credits');
  e.cc_shell_render();
  assert(nonBlack() > 1000, 'credits screen renders');
  e.cc_shell_key(13);
  assert(e.cc_shell_state() === 1, 'credits -> title');
  e.cc_shell_key(13);
  assert(e.cc_shell_state() === 2, 'title -> menu');
  e.cc_shell_render();
  assert(nonBlack() > 5000, 'main menu renders over title');

  // New game: intro story pages, then hub.
  e.cc_shell_key(key('n'));
  assert(e.cc_shell_state() === 3, 'new game enters intro story');
  e.cc_shell_render();
  assert(nonBlack() > 1000, 'intro story renders');
  for (let i = 0; i < 8 && e.cc_shell_state() === 3; i++) e.cc_shell_key(13);
  assert(e.cc_shell_state() === 4, 'intro story ends in game');
  assert(e.cc_cur_level() === 2, 'intro loads the hub');
  assert(e.cc_shell_hud_active() === 1, 'HUD active in game');

  // Pause / help.
  e.cc_shell_key(key('p'));
  assert(e.cc_shell_state() === 5, 'pause state');
  e.cc_shell_key(key('x'));
  assert(e.cc_shell_state() === 4, 'pause resumes');
  e.cc_shell_key(27);
  assert(e.cc_shell_state() === 6, 'help from game');
  e.cc_shell_key(key('t'));
  assert(e.cc_shell_state() === 6, 'help sound toggle stays in help');
  e.cc_shell_key(27);
  assert(e.cc_shell_state() === 4, 'help returns to game');

  // Key redefine and warp.
  e.cc_shell_key(27);                 // help
  e.cc_shell_key(key('k'));
  assert(e.cc_shell_state() === 16, 'key redefine screen');
  for (const k of [65, 66, 67, 68]) e.cc_shell_key(k);
  assert(e.cc_shell_state() === 6, 'key redefine returns to help');
  assert(e.cc_shell_key_for_action(0) === 65, 'key redefine stores codes');
  e.cc_shell_key(key('w'));
  assert(e.cc_shell_state() === 10, 'warp prompt');
  e.cc_shell_key(key('1'));
  e.cc_shell_key(key('6'));
  e.cc_shell_key(13);
  assert(e.cc_shell_state() === 4 && e.cc_cur_level() === 18,
         'warp 16 loads cave 18');
  e.cc_load_arg(0);
  assert(e.cc_cur_level() === 2, 'return to hub after warp test');

  // Save/restore handshake.
  const size = e.cc_state_size();
  const sp = e.cc_alloc(size);
  e.cc_global_set(0x50A5, 4321);
  e.cc_shell_key(27);      // help
  e.cc_shell_key(key('s'));     // save
  assert(e.cc_shell_state() === 8, 'save prompt');
  e.cc_shell_key(key('0'));
  assert(e.cc_shell_request_pending() === 1, 'save request pending');
  assert(e.cc_shell_request_kind() === 1 && e.cc_shell_request_slot() === 0,
         'save request has slot 0');
  {
    const n = e.cc_state_save(sp, size);
    assert(n > 0, 'state save succeeds');
    e.cc_shell_request_done(1);
    assert(e.cc_shell_state() === 14, 'save result message');
    e.cc_shell_key(13);
    assert(e.cc_shell_state() === 4, 'save message returns to game');
  }
  e.cc_global_set(0x50A5, 99);
  e.cc_shell_key(27);      // help
  e.cc_shell_key(key('r'));     // restore
  assert(e.cc_shell_state() === 9, 'restore prompt');
  e.cc_shell_key(key('0'));
  assert(e.cc_shell_request_pending() === 1 &&
         e.cc_shell_request_kind() === 2, 'restore request pending');
  assert(e.cc_state_load(sp, size) === 1, 'state load succeeds');
  e.cc_shell_request_done(1);
  assert(e.cc_shell_state() === 4, 'restore returns to game');
  assert((e.cc_global_get(0x50A5) & 0xFFFF) === 4321, 'restore recovers score');

  // High score table.
  const before = e.cc_shell_hiscore_count();
  e.cc_shell_hiscore_add('TEST', 1234567);
  assert(e.cc_shell_hiscore_name(0) && e.cc_shell_hiscore_score(0) === 1234567,
         'high score inserted at top');

  // Menu sub-screens.
  e.cc_shell_key(27);             // game -> help
  e.cc_shell_key(key('q'));       // help -> quit submenu
  assert(e.cc_shell_state() === 7, 'quit submenu');
  e.cc_shell_key(key('t'));       // quit -> title
  assert(e.cc_shell_state() === 1, 'quit to title');

  e.cc_shell_key(13);             // title -> menu
  e.cc_shell_key(key('i'));       // instructions
  assert(e.cc_shell_state() === 3, 'instructions screen');
  for (let i = 0; i < 30 && e.cc_shell_state() === 3; i++) e.cc_shell_key(13);
  assert(e.cc_shell_state() === 2, 'instructions return to menu');

  // Finale transition: all-caves flag -> story -> ending -> high scores.
  e.cc_shell_key(key('n'));
  for (let i = 0; i < 8 && e.cc_shell_state() === 3; i++) e.cc_shell_key(13);
  assert(e.cc_shell_state() === 4, 'new game reaches hub for finale test');
  e.cc_global_set(0xA0FB, 1);
  e.cc_objects_step_full();
  e.cc_shell_step();
  assert(e.cc_shell_state() === 3, 'all-caves flag starts finale story');
  for (let i = 0; i < 20 && e.cc_shell_state() === 3; i++) e.cc_shell_key(13);
  assert(e.cc_shell_state() === 13, 'finale story reaches ending screen');
  e.cc_shell_key(13);
  assert(e.cc_shell_state() === 11 || e.cc_shell_state() === 12,
         'ending proceeds to high scores/name entry');

  if (process.exitCode) process.exit(1);
})();
