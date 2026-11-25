// Pixel-exact Episode 2 static-level regression.
//
// Renders each level with the SAME wasm build used for Episode 1, fed with the
// portable Episode 2 assets, and compares against out/ep2/oracle_LNN.png (the
// independent Python oracle render).  This proves the renderer is fully data
// driven and that the exported cells/levels are correct.
//
// Episode 2 data is bring-your-own and git-ignored, so this target SKIPS
// (exit 0) unless both out/portable_ep2/ and out/ep2/oracle_L*.png exist:
//
//   python3 ../tools/export_assets.py --episode ep2
//   python3 ../tools/render_oracle.py --episode ep2 $(seq 0 18)
'use strict';

const { execFileSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const DIR = __dirname;
const ROOT = path.join(DIR, '..');
const ASSETS = path.join(ROOT, 'out', 'portable_ep2');
const ORACLE = path.join(ROOT, 'out', 'ep2');
const LEVELS = 19;

if (!fs.existsSync(path.join(ASSETS, 'levels.bin'))) {
  console.log('SKIP test-render-ep2: out/portable_ep2/levels.bin missing (BYO data)');
  process.exit(0);
}
if (!fs.existsSync(path.join(ORACLE, 'oracle_L00.png'))) {
  console.log('SKIP test-render-ep2: out/ep2/oracle_L*.png missing ' +
              '(run tools/render_oracle.py --episode ep2)');
  process.exit(0);
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'cc2-render-'));
let failures = 0;

for (let level = 0; level < LEVELS; level++) {
  const nn = String(level).padStart(2, '0');
  const out = path.join(tmp, `wasm_L${nn}.png`);
  const oracle = path.join(ORACLE, `oracle_L${nn}.png`);
  try {
    execFileSync(process.execPath, [path.join(DIR, 'test_render.js'), String(level)], {
      cwd: DIR,
      stdio: 'pipe',
      env: { ...process.env, CC_RENDER_ASSETS: ASSETS, CC_RENDER_OUT: out },
    });
    const cmp = execFileSync(process.execPath,
      [path.join(DIR, 'compare_png.js'), out, oracle], { cwd: DIR }).toString();
    if (!/differing_pixels=0\b/.test(cmp)) {
      console.error(`FAIL level ${level}: ${cmp.trim()}`);
      failures++;
    } else {
      console.log(`ok  level ${level} pixel-exact`);
    }
  } catch (err) {
    const msg = (err.stdout || '').toString() + (err.stderr || '').toString();
    console.error(`FAIL level ${level}: ${msg.trim() || err.message}`);
    failures++;
  }
}

fs.rmSync(tmp, { recursive: true, force: true });
if (failures) {
  console.error(`\n${failures} episode-2 level(s) differ from the oracle`);
  process.exit(1);
}
console.log('\nALL 19 EPISODE 2 LEVELS PIXEL-EXACT');
