// Headless-browser smoke test for wasm/game.html.
//
//   node test_browser.js            # compare against browser_expectations.json
//   node test_browser.js --update   # rewrite the framebuffer hashes
//
// Requires Playwright (`npm install` + `npx playwright install chromium`).
// Without it the test prints SKIP and exits 0 so `make test-browser` stays
// convenient on machines that only build the game.
//
// The page is opened with ?test=1, which disables requestAnimationFrame and
// exposes window.__cc.  Input still goes through real browser key events, so
// the keydown/keyup -> scan-code -> game-input path is exercised; stepping is
// explicit so the run is deterministic.
const fs = require('fs');
const http = require('http');
const path = require('path');

const ROOT = __dirname;
const UPDATE = process.argv.includes('--update');
const EXPECT_FILE = path.join(ROOT, 'browser_expectations.json');
// Message block ids come from the exported text table so inserting blocks
// cannot silently shift the expected id (same derivation as test_messages.js).
const blockTable = JSON.parse(
  fs.readFileSync(path.join(ROOT, '..', 'out', 'portable', 'shell_text.json')));
const BLOCK = {};
blockTable.blocks.forEach((b, i) => { BLOCK[b.id] = i; });
const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json',
  '.wasm': 'application/wasm',
  '.bin': 'application/octet-stream',
  '.png': 'image/png',
};

let playwright;
try {
  playwright = require('playwright');
} catch (_) {
  console.log('SKIP: playwright is not installed (npm install && npx playwright install chromium)');
  process.exit(0);
}

let failures = 0;
const seen = {};
function check(cond, msg) {
  if (cond) {
    console.log('PASS', msg);
  } else {
    failures++;
    console.error('FAIL', msg);
  }
}

function startServer() {
  const server = http.createServer((req, res) => {
    let rel = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    if (rel === '/') rel = '/game.html';
    const file = path.join(ROOT, rel);
    if (!file.startsWith(ROOT + path.sep)) {
      res.writeHead(403);
      res.end('forbidden');
      return;
    }
    fs.readFile(file, (err, data) => {
      if (err) {
        res.writeHead(404);
        res.end('not found');
        return;
      }
      res.writeHead(200, {
        'Content-Type': MIME[path.extname(file).toLowerCase()] ||
                        'application/octet-stream',
        'Cache-Control': 'no-store',
      });
      res.end(data);
    });
  });
  return new Promise((resolve) => {
    server.listen(0, '127.0.0.1', () => resolve(server));
  });
}

(async () => {
  if (!fs.existsSync(path.join(ROOT, 'render.wasm'))) {
    console.error('render.wasm missing: run `make clang` first');
    process.exit(1);
  }
  const expectations = UPDATE
    ? { version: 1, note: 'FNV-1a hashes of the 320x200 RGBA view framebuffer' }
    : JSON.parse(fs.readFileSync(EXPECT_FILE, 'utf8'));

  const server = await startServer();
  const port = server.address().port;
  const base = `http://127.0.0.1:${port}`;
  const browser = await playwright.chromium.launch({
    channel: process.env.CC_BROWSER_CHANNEL || undefined,
    headless: true,
  });
  const context = await browser.newContext({ viewport: { width: 960, height: 720 } });
  const page = await context.newPage();
  const pageErrors = [];
  page.on('pageerror', (err) => pageErrors.push(String(err)));

  const state = () => page.evaluate(() => window.__cc.state());
  const level = () => page.evaluate(() => window.__cc.level());
  const player = () => page.evaluate(() => window.__cc.player());
  const fbHash = () => page.evaluate(() => window.__cc.fbHash());
  const nonBlack = () => page.evaluate(() => window.__cc.nonBlack());
  const stateHash = () => page.evaluate(() => window.__cc.stateHash());
  const config = () => page.evaluate(() => window.__cc.config());
  const step = (n) => page.evaluate((n) => window.__cc.step(n), n);
  const render = () => page.evaluate(() => window.__cc.render());
  const handleRequest = () => page.evaluate(() => window.__cc.handleRequest());

  async function waitReady() {
    await page.waitForFunction(
      () => window.__cc && window.__cc.ready, null, { timeout: 30000 });
  }
  async function boot(fresh) {
    await page.goto(base + '/game.html?test=1', { waitUntil: 'load' });
    await waitReady();
    if (fresh) {
      await page.evaluate(() => localStorage.clear());
      await page.reload({ waitUntil: 'load' });
      await waitReady();
    }
  }
  async function press(key) {
    await page.keyboard.press(key);
    await render();
  }
  async function expectHash(name) {
    const got = await fbHash();
    seen[name] = got;
    if (!UPDATE) {
      const want = expectations[name];
      check(want !== undefined && got === want,
        `${name} framebuffer hash 0x${got.toString(16)}` +
        (want === undefined ? ' (missing expectation)' :
          ` (want 0x${want.toString(16)})`));
    }
  }
  async function newGame() {
    await press('Enter');                       // logo -> attract
    check((await state()) === 15, 'logo -> attract');
    await press('Enter');                       // attract -> menu
    check((await state()) === 2, 'attract -> menu');
    await press('n');
    check((await state()) === 3, 'new game enters intro story');
    for (let i = 0; i < 8 && (await state()) === 3; i++) await press('Enter');
    check((await state()) === 4, 'intro story reaches the game');
  }

  try {
    await boot(true);                           // clean localStorage

    // ---- boot / shell screens ------------------------------------------
    check((await state()) === 0, 'boots to the logo screen');
    check((await nonBlack()) > 1000, 'logo screen renders content');
    await expectHash('logo');

    await press('Enter');
    check((await state()) === 15, 'logo -> attract');
    check((await nonBlack()) > 1000, 'title frame renders content');
    await expectHash('attract_title');

    await page.evaluate(() => window.__cc.stepTicks(433 + 400));
    await render();
    await expectHash('attract_credits');

    await press('Enter');
    check((await state()) === 2, 'attract -> menu');
    await expectHash('menu');

    await press('n');
    check((await state()) === 3, 'new game enters intro story');
    await expectHash('intro');
    for (let i = 0; i < 8 && (await state()) === 3; i++) await press('Enter');
    check((await state()) === 4, 'intro story reaches the game');
    check((await level()) === 2, 'intro loads the hub');
    check((await page.evaluate(() => window.__cc.hudActive())) === 1,
      'HUD active in game');
    const start = await player();
    check(start[0] === 32 && start[1] === 64, `hub start ${start}`);
    await expectHash('hub');

    // ---- real keyboard input into the game ------------------------------
    await page.keyboard.down('ArrowRight');
    await step(20);
    await page.keyboard.up('ArrowRight');
    const afterWalk = await player();
    check(afterWalk[0] > start[0],
      `right arrow moves player (${start[0]} -> ${afterWalk[0]})`);

    // Jump first with Space (the macOS-safe alias), then steer in the air.
    const ground = await player();
    await page.keyboard.down('Space');
    await step(3);
    const rising = await player();
    await page.keyboard.down('ArrowRight');
    await step(6);
    const airborne = await player();
    check(rising[1] < ground[1],
      `Space jumps (${ground[1]} -> ${rising[1]})`);
    check(airborne[0] > rising[0] && airborne[1] < ground[1],
      `right steers in the air after jumping first (${rising[0]} -> ${airborne[0]})`);
    await page.keyboard.up('ArrowRight');
    await page.keyboard.up('Space');
    await step(30);
    const landed = await player();
    await step(10);
    check((await player())[1] === landed[1],
      'jump keys release cleanly after landing');

    const ammo0 = await page.evaluate(() => window.__cc.ammo());
    await page.keyboard.down('AltLeft');
    await step(2);
    await page.keyboard.up('AltLeft');
    const ammo1 = await page.evaluate(() => window.__cc.ammo());
    check(ammo1 === ammo0 - 1, `fire consumes ammo (${ammo0} -> ${ammo1})`);
    check((await state()) === 4, 'still in game after movement and fire');

    // ---- locked-exit message must not latch movement --------------------
    // The original polls the live keyboard every frame, so a direction held
    // through the "Don't give up now Mylo" exit message is still held when
    // play resumes, and a direction released during the message is not.
    // Dropping those events glued Mylo to the door: the stale direction and
    // the key the player pressed to walk away cancelled each other out.
    const EXIT_CELL = [30, 20];
    const cellByte = (c, r, o) => page.evaluate(([c, r, o]) =>
      window.__cc.exports.cc_cell_byte(c, r, o), [c, r, o]);
    const cellSet = (c, r, o, v) => page.evaluate(([c, r, o, v]) =>
      window.__cc.exports.cc_cell_set(c, r, o, v), [c, r, o, v]);
    const setWord = async (c, r, off, val) => {
      await cellSet(c, r, off, val & 0xFF);
      await cellSet(c, r, off + 1, (val >> 8) & 0xFF);
    };
    const globalGet = (off) => page.evaluate((off) =>
      window.__cc.exports.cc_global_get(off), off);
    const globalSet = (off, v) => page.evaluate(([off, v]) =>
      window.__cc.exports.cc_global_set(off, v), [off, v]);
    const fireCellEvent = (x, y) => page.evaluate(([x, y]) =>
      window.__cc.exports.cc_test_cell_event(x, y), [x, y]);

    const cellImage = [];
    for (let off = 0; off < 20; off++)
      cellImage.push(await cellByte(EXIT_CELL[0], EXIT_CELL[1], off));
    const savedA106 = await globalGet(0xA106);
    const savedA0F5 = await globalGet(0xA0F5);

    const armExitMessage = async () => {
      await setWord(EXIT_CELL[0], EXIT_CELL[1], 0x06, 0x0D); /* w3 exit tile */
      await setWord(EXIT_CELL[0], EXIT_CELL[1], 0x0A, 0x0C); /* w5 exit layer */
      await globalSet(0xA0F5, 0);              /* exit still locked */
      await globalSet(0xA106, 1);              /* exit-open latch */
      await fireCellEvent(EXIT_CELL[0] * 16 + 3, EXIT_CELL[1] * 16);
    };

    // Stand at the hub start, where walking right is already proven clear.
    await globalSet(0x4611, 32);
    await globalSet(0x4613, 64);
    await step(2);

    // A direction pressed while the message is up dismisses it and walks.
    await armExitMessage();
    check((await state()) === 14, 'locked exit opens the more-crystals message');
    check((await page.evaluate(() =>
      window.__cc.exports.cc_shell_message_block())) ===
      BLOCK.message_more_crystals, 'exit message is the more-crystals block');
    const pressed0 = await player();
    await page.keyboard.down('ArrowRight');
    check((await state()) === 4, 'direction key dismisses the exit message');
    await step(10);
    const pressed1 = await player();
    check(pressed1[0] > pressed0[0],
      `direction pressed in the message walks on return (${pressed0[0]} -> ${pressed1[0]})`);
    await page.keyboard.up('ArrowRight');
    await step(2);

    // A direction released while the message is up must not stay latched.
    await page.keyboard.down('ArrowRight');
    await armExitMessage();
    check((await state()) === 14, 'exit message re-opens');
    await page.keyboard.up('ArrowRight');
    await press('Escape');
    check((await state()) === 4, 'exit message dismisses back to the game');
    const latched0 = await player();
    await step(12);
    const latched1 = await player();
    check(latched1[0] === latched0[0],
      `direction released in the message stays released (${latched0[0]} -> ${latched1[0]})`);

    for (let off = 0; off < 20; off++)
      await cellSet(EXIT_CELL[0], EXIT_CELL[1], off, cellImage[off]);
    await globalSet(0xA106, savedA106);
    await globalSet(0xA0F5, savedA0F5);

    // ---- save -> reload -> restore through localStorage ------------------
    await press('Escape');
    check((await state()) === 6, 'Esc opens help');
    await press('s');
    check((await state()) === 8, 'help -> save prompt');
    await press('0');
    check(await page.evaluate(() => window.__cc.exports.cc_shell_request_pending()) === 1,
      'save request pending for slot 0');
    await handleRequest();
    check((await state()) === 14, 'save result message shown');
    const savedScore = await page.evaluate(() => window.__cc.score());
    const savedHash = await stateHash();
    await press('Escape');
    check((await state()) === 4, 'save message returns to game');

    await page.evaluate(() => window.__cc.exports.cc_global_set(0x50A5, 99));
    check((await page.evaluate(() => window.__cc.score())) === 99,
      'mutated score before restore');

    await boot(false);                          // reload; localStorage survives
    check((await state()) === 0, 'reload returns to the logo');
    await press('Enter');
    await press('Enter');
    check((await state()) === 2, 'reload reaches the main menu');
    await press('r');
    check((await state()) === 9, 'menu -> restore prompt');
    await press('0');
    check(await page.evaluate(() => window.__cc.exports.cc_shell_request_pending()) === 1,
      'restore request pending for slot 0');
    await handleRequest();
    check((await state()) === 4, 'restore returns to game');
    check((await page.evaluate(() => window.__cc.score())) === savedScore,
      `restore recovers score ${savedScore}`);
    check((await stateHash()) === savedHash, 'restored state hashes identically');

    // ---- config persistence across reload ------------------------------
    await press('Escape');
    check((await state()) === 6, 'help before config toggle');
    const cfg0 = await config();
    check(cfg0[0] === 1, 'sound starts enabled');
    await press('t');
    check((await config())[0] === 0, 'T disables sound');
    await press('j');
    check((await config())[1] === 1, 'J enables joystick');
    const storedCfg = await page.evaluate(() => window.__cc.storage().config);
    check(!!storedCfg && JSON.parse(storedCfg).sound === 0 &&
      JSON.parse(storedCfg).joystick === 1, 'config written to localStorage');

    // ---- high-score persistence across reload --------------------------
    await page.evaluate(() => {
      window.__cc.addHiscore('BROWSER', 1234567);
      window.__cc.saveHighScores();
    });
    await boot(false);
    const cfg1 = await config();
    check(cfg1[0] === 0 && cfg1[1] === 1, 'config survives reload');
    const hs = await page.evaluate(() => window.__cc.hiscore(0));
    check(hs.name === 'BROWSER' && hs.score === 1234567,
      `high score survives reload (${hs.name} ${hs.score})`);

    // ---- title -> finale flow ------------------------------------------
    await newGame();
    await page.evaluate(() => {
      window.__cc.exports.cc_global_set(0xA0FB, 1);
      window.__cc.step(1);
    });
    check((await state()) === 3, 'all-caves flag starts the finale story');
    for (let i = 0; i < 20 && (await state()) === 3; i++) await press('Enter');
    check((await state()) === 13, 'finale story reaches the ending screen');
    await press('Enter');
    const afterEnding = await state();
    check(afterEnding === 11 || afterEnding === 12,
      `ending proceeds to high scores/name entry (state ${afterEnding})`);

    check(pageErrors.length === 0,
      `no page errors${pageErrors.length ? ': ' + pageErrors.join('; ') : ''}`);
  } finally {
    await browser.close();
    server.close();
  }

  if (failures) {
    console.error(`\n${failures} browser smoke check(s) failed`);
    process.exit(1);
  }
  if (UPDATE) {
    Object.assign(expectations, seen);
    fs.writeFileSync(EXPECT_FILE, JSON.stringify(expectations, null, 2) + '\n');
    console.log(`updated ${path.relative(process.cwd(), EXPECT_FILE)}`);
  }
  console.log('\nPASS: browser smoke (title -> hub -> save/restore -> finale)');
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
