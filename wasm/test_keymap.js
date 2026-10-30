// Browser physical-key -> DOS set-1 scan-code regression.
const keymap = require('./keymap.js');

function assert(cond, msg) {
  if (!cond) { console.error('FAIL:', msg); process.exitCode = 1; }
  else console.log('PASS', msg);
}

const cases = [
  ['ArrowLeft', 0x4B], ['ArrowRight', 0x4D],
  ['ArrowUp', 0x48], ['ArrowDown', 0x50],
  ['KeyA', 0x1E], ['KeyZ', 0x2C],
  ['KeyQ', 0x10], ['KeyW', 0x11],
  ['ControlLeft', 0x1D], ['AltLeft', 0x38],
  ['Space', 0x39], ['Enter', 0x1C], ['Escape', 0x01],
  ['Digit1', 0x02], ['Digit0', 0x0B],
  ['F1', 0x3B], ['Home', 0x47], ['Delete', 0x53]
];
for (const [code, scan] of cases)
  assert(keymap.scanFromCode(code) === scan, code + ' -> ' + scan);

assert(keymap.scanFromEvent({ code: 'KeyA', keyCode: 65 }) === 0x1E,
       'event uses code');
assert(keymap.scanFromEvent({ keyCode: 65 }) === 0x1E,
       'legacy keyCode fallback');
assert(keymap.scanFromEvent({ keyCode: 66 }) === 0x30,
       'legacy keyCode B fallback');
assert(keymap.scanFromEvent({ keyCode: 27 }) === 0x01,
       'legacy escape fallback');
assert(keymap.scanFromEvent({ code: 'Unknown', keyCode: 0 }) === 0,
       'unknown key yields 0');

if (process.exitCode) process.exit(1);
