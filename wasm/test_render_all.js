// Pixel-exact regression for all 19 static levels.
// Renders each level with the wasm renderer and compares it to the
// checked-in oracle PNG (out/oracle_LNN.png). Fails on any differing pixel.
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const DIR = __dirname;
const ROOT = path.join(DIR, '..');
let failures = 0;

for (let level = 0; level < 19; level++) {
  const nn = String(level).padStart(2, '0');
  const out = path.join(DIR, `out_wasm_level${nn}.png`);
  const oracle = path.join(ROOT, 'out', `oracle_L${nn}.png`);
  if (!fs.existsSync(oracle)) {
    console.error(`MISSING oracle ${oracle}`);
    failures++;
    continue;
  }
  try {
    execFileSync(process.execPath, [path.join(DIR, 'test_render.js'), String(level)], {
      cwd: DIR, stdio: 'pipe',
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

if (failures) {
  console.error(`\n${failures} level(s) differ from the oracle`);
  process.exit(1);
}
console.log('\nALL 19 LEVELS PIXEL-EXACT');
