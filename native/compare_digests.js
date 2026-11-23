// Assemble native cc_digest group outputs and compare them structurally
// against the committed wasm golden master (notes/refactor_digests.json).
//
//   node compare_digests.js <fixture.json> <group.json> [more.json...]
//
// Group files are named <group>.json and fill the same top-level keys the
// JS harness produces (static_render, gameplay, transitions, warp, shell,
// save_round_trip).
const fs = require('fs');
const path = require('path');

function compare(expected, actual, pathParts = [], diffs = []) {
  if (diffs.length >= 20) return diffs;
  if (Array.isArray(expected) || Array.isArray(actual)) {
    if (!Array.isArray(expected) || !Array.isArray(actual)) {
      diffs.push(`${pathParts.join('.')}: type mismatch`);
      return diffs;
    }
    if (expected.length !== actual.length) {
      diffs.push(`${pathParts.join('.')}: length ${actual.length}, expected ${expected.length}`);
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
  for (const item of Object.values(value))
    if (item && typeof item === 'object') n += countSnapshots(item);
  return n;
}

const fixture = process.argv[2];
const groups = process.argv.slice(3);
if (!fixture || !groups.length) {
  console.error('usage: node compare_digests.js <fixture.json> <group.json>...');
  process.exit(2);
}
const expected = JSON.parse(fs.readFileSync(fixture, 'utf8'));
const actual = { version: expected.version, note: expected.note };
for (const f of groups) {
  const name = path.basename(f, '.json');
  actual[name] = JSON.parse(fs.readFileSync(f, 'utf8'));
}
const diffs = compare(expected, actual);
if (diffs.length) {
  for (const d of diffs) console.error(`FAIL ${d}`);
  console.error(`\n${diffs.length} digest mismatch(es) between native and wasm`);
  process.exit(1);
}
console.log(`PASS: ${countSnapshots(actual)} native digest snapshots match ${fixture}`);
