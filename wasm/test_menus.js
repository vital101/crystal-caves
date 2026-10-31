// Menu information screens: ordering/foreign/BBS/about/video/quit-to-DOS.
//
// Compares the C page lines with the real handler buffers captured in
// notes/menu_fixtures.json (tools/shell_exactness.py --menus), and checks the
// video Y/N toggle and the quit-to-DOS confirmation flow.
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
const fixtures = JSON.parse(read('../notes/menu_fixtures.json'));

const MENU = 2, TEXT = 3, GAME = 4, HELP = 6, QUIT = 7,
      QUIT_CONFIRM = 18, VIDEO = 19, DOS = 20;
const key = (ch) => typeof ch === 'number' ? ch : ch.charCodeAt(0);

let fail = 0;
function check(cond, msg) {
  if (!cond) { console.error('FAIL:', msg); fail++; }
  else console.log('PASS', msg);
}

(async () => {
  const { instance } = await WebAssembly.instantiate(wasm, {});
  const e = instance.exports;
  const mem = () => new Uint8Array(e.memory.buffer);
  const copy = (b) => { const p = e.cc_alloc(b.length); mem().set(b, p); return p; };
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

  const cstr = (p) => {
    const m = mem();
    let s = '';
    for (let i = 0; p + i < m.length && m[p + i]; i++) s += String.fromCharCode(m[p + i]);
    return s;
  };
  const pageLines = () => {
    const out = [];
    const n = e.cc_shell_page_line_count();
    for (let i = 0; i < n; i++) out.push(cstr(e.cc_shell_page_line(i)));
    return out;
  };
  // PAGE_LINES pads a block page with empty rows; compare the real lines and
  // require the padding to be empty.
  const sameLines = (got, want) =>
    got.length >= want.length &&
    want.every((v, i) => got[i] === v) &&
    got.slice(want.length).every((v) => v === "");
  const toMenu = () => {
    e.cc_shell_init();
    e.cc_shell_key(13);   // logo -> attract
    e.cc_shell_key(13);   // attract -> menu
  };
  const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

  // Drive one menu option and compare every page against the oracle buffer.
  function checkPages(name, ch, expected, opts = {}) {
    toMenu();
    check(e.cc_shell_state() === MENU, `${name}: at menu`);
    e.cc_shell_key(key(ch));
    const wantState = opts.state === undefined ? TEXT : opts.state;
    check(e.cc_shell_state() === wantState, `${name}: entered state ${wantState}`);
    if (wantState !== TEXT) return;
    for (let p = 0; p < expected.length; p++) {
      check(e.cc_shell_page_index() === p, `${name}: page ${p} index`);
      const got = pageLines();
      if (!sameLines(got, expected[p])) {
        console.error(`  got:  ${JSON.stringify(got)}`);
        console.error(`  want: ${JSON.stringify(expected[p])}`);
      }
      check(sameLines(got, expected[p]), `${name}: page ${p} lines match the oracle`);
      e.cc_shell_key(13);
    }
    check(e.cc_shell_state() === MENU, `${name}: returns to the menu`);
  }

  const pages = fixtures.pages;
  checkPages('ordering', 'o',
             [pages.ordering_episodes, pages.ordering_offer,
              pages.ordering_disk]);
  checkPages('foreign', 'f',
             [pages.foreign, pages.foreign_au, pages.foreign_de,
              pages.foreign_uk]);
  checkPages('bbs', 'c', [pages.bbs]);
  checkPages('about', 'a', [pages.about]);
  checkPages('main menu', 'q', [], { state: QUIT_CONFIRM });

  // ---- video compatibility prompt -------------------------------------
  toMenu();
  check(e.cc_shell_video_alt() === 0, 'video starts off');
  e.cc_shell_key(key('v'));
  check(e.cc_shell_state() === VIDEO, 'video prompt opened from the menu');
  check(eq(pageLines(), pages.menu_video), 'video prompt lines match');
  e.cc_shell_config_clear_dirty();
  e.cc_shell_key(key('y'));
  check(e.cc_shell_state() === MENU, 'video Y returns to the menu');
  check(e.cc_shell_video_alt() === 1, 'video Y enables the alternate mode');
  check(e.cc_shell_config_dirty() === 1, 'video Y marks config dirty');

  e.cc_shell_config_clear_dirty();
  e.cc_shell_key(key('v'));
  e.cc_shell_key(key('n'));
  check(e.cc_shell_video_alt() === 0, 'video N disables the alternate mode');
  check(e.cc_shell_config_dirty() === 1, 'video N marks config dirty');

  e.cc_shell_config_clear_dirty();
  e.cc_shell_key(key('v'));
  e.cc_shell_key(27);   // Esc
  check(e.cc_shell_state() === MENU, 'video Esc returns to the menu');
  check(e.cc_shell_config_dirty() === 0, 'video Esc leaves config unchanged');

  // ---- quit to DOS ----------------------------------------------------
  toMenu();
  e.cc_shell_key(key('q'));
  check(e.cc_shell_state() === QUIT_CONFIRM, 'quit prompt opened');
  check(eq(pageLines(), pages.menu_quit_dos), 'quit prompt lines match');
  e.cc_shell_key(key('n'));
  check(e.cc_shell_state() === MENU, 'quit N returns to the menu');

  e.cc_shell_key(key('q'));
  e.cc_shell_key(key('y'));
  check(e.cc_shell_state() === DOS, 'quit Y enters the DOS exit screen');
  e.cc_shell_render();
  {
    const fb = new Uint8Array(e.memory.buffer, e.cc_view_framebuffer(),
                              320 * 200 * 4);
    let ink = 0;
    for (let i = 0; i < 320 * 200; i++)
      if (fb[i * 4] || fb[i * 4 + 1] || fb[i * 4 + 2]) ink++;
    check(ink > 100, 'DOS exit screen renders its message');
  }
  e.cc_shell_key(13);
  check(e.cc_shell_state() === 15, 'key at the DOS screen restarts the attract');

  // ---- in-game quit submenu -> D)os confirm ---------------------------
  toMenu();
  e.cc_shell_key(key('n'));            // intro story
  for (let i = 0; i < 40 && e.cc_shell_state() === TEXT; i++)
    e.cc_shell_key(13);
  check(e.cc_shell_state() === GAME, 'reached the game from the intro');
  e.cc_shell_key(27);                  // Esc -> help
  check(e.cc_shell_state() === HELP, 'help menu opened');

  // Help -> video prompt is a modal overlay over the help menu.
  e.cc_shell_key(key('v'));
  check(e.cc_shell_state() === VIDEO, 'video prompt opened from help');
  check(e.cc_shell_overlay() === 1, 'help video prompt renders as an overlay');
  e.cc_shell_key(key('y'));
  check(e.cc_shell_state() === HELP, 'help video Y returns to the help menu');
  check(e.cc_shell_video_alt() === 1, 'help video Y enables the alternate mode');

  e.cc_shell_key(key('q'));
  check(e.cc_shell_state() === QUIT, 'quit submenu opened');
  check(eq(pageLines(), pages.menu_quit), 'quit submenu lines match');
  e.cc_shell_key(key('d'));
  check(e.cc_shell_state() === QUIT_CONFIRM, 'D)os opens the confirm');
  e.cc_shell_key(key('n'));
  check(e.cc_shell_state() === QUIT, 'confirm N returns to the quit submenu');
  e.cc_shell_key(key('d'));
  e.cc_shell_key(key('y'));
  check(e.cc_shell_state() === DOS, 'confirm Y exits to DOS');

  if (fail) {
    console.error(`FAIL: ${fail} menu checks`);
    process.exit(1);
  }
  console.log('PASS: menu information screens match the oracle');
})();
