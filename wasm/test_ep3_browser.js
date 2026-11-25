// Headless-browser smoke test for the Episode 3 build (game.html?ep=3).
//
//   node test_ep3_browser.js            # compare against browser_expectations_ep3.json
//   node test_ep3_browser.js --update   # rewrite the ep3 framebuffer hashes
//
// Skips (exit 0) unless render_ep3.wasm, assets_ep3/ and the ep3
// portable artifacts exist.  Requires
// Playwright; without it the test skips too.
const fs = require('fs');
const http = require('http');
const path = require('path');
const { createAsserts, readJson } = require('./test_helpers.js');

const ROOT = __dirname;
const UPDATE = process.argv.includes('--update');
const EXPECT_FILE = path.join(ROOT, 'browser_expectations_ep3.json');
const WASM = path.join(ROOT, 'render_ep3.wasm');
const ASSETS = path.join(ROOT, 'assets_ep3');
const TEXT = path.join(ROOT, '..', 'out', 'portable_ep3', 'shell_text.json');

for (const p of [WASM, ASSETS, TEXT]) {
  if (!fs.existsSync(p)) {
    console.log(`SKIP test-ep3-browser: missing ${path.relative(ROOT, p)} (BYO data)`);
    process.exit(0);
  }
}

let playwright;
try {
  playwright = require('playwright');
} catch (_) {
  console.log('SKIP test-ep3-browser: playwright is not installed');
  process.exit(0);
}

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json',
  '.wasm': 'application/wasm',
  '.bin': 'application/octet-stream',
  '.png': 'image/png',
};

const asserts = createAsserts();
const check = asserts.check;
const seen = {};
const blockTable = readJson('../out/portable_ep3/shell_text.json');
const BLOCK = {};
blockTable.blocks.forEach((b, i) => { BLOCK[b.id] = i; });
const expectations = UPDATE
  ? { version: 1, note: 'Episode 3 FNV-1a hashes of the 320x200 RGBA view framebuffer' }
  : (fs.existsSync(EXPECT_FILE) ? JSON.parse(fs.readFileSync(EXPECT_FILE, 'utf8'))
                                : { version: 1 });

function startServer() {
  return new Promise((resolve) => {
    const server = http.createServer((req, res) => {
      let rel = decodeURIComponent(new URL(req.url, 'http://x').pathname);
      if (rel === '/') rel = '/game.html';
      const file = path.join(ROOT, path.normalize(rel).replace(/^(\.\.[/\\])+/, ''));
      fs.readFile(file, (err, data) => {
        if (err) { res.writeHead(404); res.end('not found'); return; }
        res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream' });
        res.end(data);
      });
    });
    server.listen(0, '127.0.0.1', () => resolve(server));
  });
}

(async () => {
  const server = await startServer();
  const base = `http://127.0.0.1:${server.address().port}`;
  const browser = await playwright.chromium.launch();
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));

  const state = () => page.evaluate(() => window.__cc.state());
  const level = () => page.evaluate(() => window.__cc.level());
  const player = () => page.evaluate(() => window.__cc.player());
  const fbHash = () => page.evaluate(() => window.__cc.fbHash());
  const stateHash = () => page.evaluate(() => window.__cc.stateHash());
  const storage = () => page.evaluate(() => window.__cc.storage());
  const render = () => page.evaluate(() => window.__cc.render());
  const handleRequest = () => page.evaluate(() => window.__cc.handleRequest());
  const step = (n) => page.evaluate((n) => window.__cc.step(n), n);

  async function waitReady() {
    await page.waitForFunction(() => window.__cc && window.__cc.ready, null,
                               { timeout: 30000 });
  }
  async function boot(fresh) {
    await page.goto(base + '/game.html?test=1&ep=3', { waitUntil: 'load' });
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
        `${name} ep3 framebuffer hash 0x${got.toString(16)}` +
        (want === undefined ? ' (missing expectation)' :
          ` (want 0x${want.toString(16)})`));
    }
  }
  async function introPage() {
    return page.evaluate(() => {
      const e = window.__cc.exports;
      const m = new Uint8Array(e.memory.buffer);
      const line = (i) => {
        const p = e.cc_shell_page_line(i);
        let s = '';
        for (let j = 0; j < 64; j++) { const c = m[p + j]; if (!c) break; s += String.fromCharCode(c); }
        return s;
      };
      return { count: e.cc_shell_page_count(), first: line(0) };
    });
  }
  async function newGame() {
    await press('Enter');                 // logo -> attract
    check((await state()) === 15, 'logo -> attract');
    await press('Enter');                 // attract -> menu
    check((await state()) === 2, 'attract -> menu');
    await press('n');
    check((await state()) === 3, 'new game enters intro story');
  }
  async function finishIntro() {
    for (let i = 0; i < 8 && (await state()) === 3; i++) await press('Enter');
    check((await state()) === 4, 'intro reaches the game');
  }

  try {
    await boot(true);

    // ---- page chrome / episode wiring -----------------------------------
    check((await page.title()).indexOf('Ep.3') >= 0, 'page title says Ep.3');
    const chrome = await page.evaluate(() => {
      const legend = document.getElementById('legend');
      return {
        caps: [...legend.querySelectorAll('kbd')].map((k) => k.textContent),
        actions: [...legend.querySelectorAll('.cc-act')].map((k) => k.textContent),
        ep: document.getElementById('epBtn')?.textContent,
      };
    });
    check(chrome.caps.join(' ') === '← → Ctrl Space ↑ Alt F1 Esc P',
          'key legend shows the default bindings');
    check(chrome.actions.join('|') === 'Move|Jump|Fire|Help & key setup|Pause',
          'key legend groups the actions');
    check(chrome.ep === 'Episode 1', 'episode button offers Episode 1');
    await expectHash('logo');

    // ---- intro: compiled Episode 2 story pages -------------------------
    await press('Enter');
    await press('Enter');
    check((await state()) === 2, 'attract -> menu');
    await expectHash('menu');
    await press('n');
    const intro = await introPage();
    check(intro.count === 4, `ep3 intro has 4 pages (${intro.count})`);
    check(/STARDATE 2121\.93/.test(intro.first),
          'ep3 intro starts with the 2121.93 ship log');
    await expectHash('intro');
    await finishIntro();
    check((await level()) === 2, 'new game starts in the hub');
    const p0 = await player();
    check(p0[0] === 32 && p0[1] === 64, `ep3 hub start is (32,64) (${p0})`);
    await expectHash('hub');

    // ---- compiled Episode 2 high-score defaults ------------------------
    const hs0 = await page.evaluate(() => window.__cc.hiscore(0));
    check(hs0.name === 'Play all 3 episodes' && hs0.score === 150000,
          `ep3 high-score defaults (${hs0.name} ${hs0.score})`);

    // ---- save / restore with cc2.* keys --------------------------------
    await press('Escape');
    check((await state()) === 6, 'Esc opens help');
    await press('s');
    check((await state()) === 8, 'help -> save prompt');
    await press('0');
    check(await page.evaluate(() => window.__cc.exports.cc_shell_request_pending()) === 1,
      'save request pending');
    await handleRequest();
    check((await state()) === 14, 'save result message shown');
    const savedScore = await page.evaluate(() => window.__cc.score());
    const savedHash = await stateHash();
    let st = await storage();
    check(!!st.save0, 'save written to cc3.save.0');
    check(await page.evaluate(() => localStorage.getItem('cc2.save.0')) === null,
      'ep2 save namespace untouched');
    await press('Escape');
    check((await state()) === 4, 'save message returns to game');

    await page.evaluate(() => window.__cc.exports.cc_global_set(0x50A5, 99));
    check((await page.evaluate(() => window.__cc.score())) === 99,
      'mutated score before restore');
    await boot(false);
    await press('Enter');
    await press('Enter');
    check((await state()) === 2, 'reload reaches the menu');
    await press('r');
    check((await state()) === 9, 'menu -> restore prompt');
    await press('0');
    await handleRequest();
    check((await state()) === 4, 'restore returns to the game');
    check((await page.evaluate(() => window.__cc.score())) === savedScore,
      `restore recovers score ${savedScore}`);
    check((await stateHash()) === savedHash, 'restored state hashes identically');

    // ---- config namespace ----------------------------------------------
    await press('Escape');
    check((await state()) === 6, 'Esc opens help again');
    await press('t');
    check((await page.evaluate(() => window.__cc.config()))[0] === 0,
          'T disables sound');
    st = await storage();
    check(!!st.config && JSON.parse(st.config).sound === 0,
          'config written to cc3.config.v2');

    // ---- high score persistence ---------------------------------------
    await page.evaluate(() => {
      window.__cc.addHiscore('EP3TEST', 999999);
      window.__cc.saveHighScores();
    });
    await boot(false);
    const hs1 = await page.evaluate(() => window.__cc.hiscore(0));
    check(hs1.name === 'EP3TEST' && hs1.score === 999999,
          `ep3 high score survives reload (${hs1.name} ${hs1.score})`);

    // ---- finale story pages -------------------------------------------
    await newGame();
    await finishIntro();
    await page.evaluate(() => {
      window.__cc.exports.cc_global_set(0xA0FB, 1);
      window.__cc.step(1);
    });
    check((await state()) === 3, 'all-caves flag starts the finale story');
    const fin = await introPage();
    check(fin.count === 12, `ep3 finale has 12 pages (${fin.count})`);
    check(/Your dreams of the most/.test(fin.first),
          'ep3 finale starts with the vacation purchase');
    await expectHash('finale');
    for (let i = 0; i < 20 && (await state()) === 3; i++) await press('Enter');
    check((await state()) === 13, 'finale reaches the ending screen');
    await expectHash('ending1');
    await press('Enter');
    check((await state()) === 13, 'second ending screen shown');
    await expectHash('ending2');
    await press('Enter');
    check((await state()) === 13, 'third ending screen shown');
    await expectHash('ending3');
    await press('Enter');
    check((await state()) !== 13, 'ending screens finish');

    check(errors.length === 0,
          `no page errors${errors.length ? ': ' + errors.join('; ') : ''}`);
  } finally {
    await browser.close();
    server.close();
  }

  if (asserts.failures) {
    console.error(`\n${asserts.failures} episode-3 browser check(s) failed`);
    process.exit(1);
  }
  if (UPDATE) {
    Object.assign(expectations, seen);
    fs.writeFileSync(EXPECT_FILE, JSON.stringify(expectations, null, 2) + '\n');
    console.log(`updated ${path.relative(process.cwd(), EXPECT_FILE)}`);
  }
  console.log('\nPASS: episode-3 browser smoke (title -> intro -> hub -> save/restore -> finale)');
})().catch((err) => { console.error(err); process.exit(1); });
