// Wasm export-stability check (Phase 4).
//
// The Makefile's EXPORTS list is the source of truth for the public surface.
// Fail if the built module drops an export or grows a new one unexpectedly.
const fs = require('fs');
const path = require('path');

const DIR = __dirname;
const makefile = fs.readFileSync(path.join(DIR, 'Makefile'), 'utf8').split(/\r?\n/);

const expected = [];
let inExports = false;
for (const raw of makefile) {
  const line = raw.replace(/\s+#.*$/, '').trimEnd();
  if (!inExports) {
    const m = line.match(/^EXPORTS\s*=\s*(.*)$/);
    if (!m) continue;
    inExports = true;
    const content = m[1];
    expected.push(...content.replace(/\\$/, '').split(/\s+/).filter(Boolean));
    if (!content.endsWith('\\')) break;
    continue;
  }
  expected.push(...line.replace(/\\$/, '').split(/\s+/).filter(Boolean));
  if (!line.endsWith('\\')) break;
}
expected.push('memory');

const mod = new WebAssembly.Module(fs.readFileSync(path.join(DIR, 'render.wasm')));
const actual = WebAssembly.Module.exports(mod).map((e) => e.name).sort();
const want = expected.slice().sort();

let failures = 0;
for (const name of want) {
  if (!actual.includes(name)) {
    console.error('FAIL: missing export', name);
    failures++;
  }
}
for (const name of actual) {
  if (!want.includes(name)) {
    console.error('FAIL: unexpected export', name);
    failures++;
  }
}

if (failures) {
  console.error(`\n${failures} export check(s) failed (${actual.length} actual, ${want.length} expected)`);
  process.exit(1);
}
console.log(`PASS: ${actual.length} wasm exports match the Makefile EXPORTS list`);
