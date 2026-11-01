/* Shared keyboard legend for game.html and play.html.
 *
 * The four action bindings can be redefined from the in-game help screen, so
 * the legend is rebuilt from the live shell state instead of hard-coded key
 * names.  Styles are injected once so both pages show identical key caps.
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.CCLegend = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  /* IBM PC set-1 scan code -> key-cap label (same codes as keymap.js).
   * The game's own name table (shell_exact_data.h) leaves many bindable keys
   * as "NULL"; this one covers everything CCKeymap can produce. */
  const NAMES = {
    0x01: 'Esc',
    0x0C: '-', 0x0D: '=', 0x0E: 'Bksp', 0x0F: 'Tab',
    0x1A: '[', 0x1B: ']', 0x1C: 'Enter', 0x1D: 'Ctrl',
    0x27: ';', 0x28: "'", 0x29: '`', 0x2A: 'Shift', 0x2B: '\\',
    0x36: 'R Shift', 0x37: '*', 0x38: 'Alt', 0x39: 'Space',
    0x3A: 'Caps',
    0x47: 'Home', 0x48: '↑', 0x49: 'PgUp',
    0x4B: '←', 0x4D: '→', 0x4F: 'End',
    0x50: '↓', 0x51: 'PgDn', 0x52: 'Ins', 0x53: 'Del'
  };
  '1234567890'.split('').forEach((c, i) => { NAMES[0x02 + i] = c; });
  'QWERTYUIOP'.split('').forEach((c, i) => { NAMES[0x10 + i] = c; });
  'ASDFGHJKL'.split('').forEach((c, i) => { NAMES[0x1E + i] = c; });
  'ZXCVBNM'.split('').forEach((c, i) => { NAMES[0x2C + i] = c; });
  for (let i = 1; i <= 10; i++) NAMES[0x3A + i] = 'F' + i;

  const STYLE = `
.cc-legend {
  display: flex; flex-wrap: wrap; align-items: center; justify-content: center;
  gap: 8px 20px; font-size: 14px; line-height: 1.5; color: #c8c8c8;
}
.cc-grp { display: inline-flex; align-items: center; gap: 5px;
          white-space: nowrap; }
.cc-legend kbd {
  display: inline-block; min-width: 1.5em; padding: 1px 7px 2px;
  color: #f0f0f0; background: #262626; text-align: center;
  border: 1px solid #5a5a5a; border-bottom-width: 3px; border-radius: 4px;
}
.cc-act { color: #d8d8d8; }
.cc-note { flex-basis: 100%; margin-top: 2px; font-size: 12px;
           color: #8f8f8f; text-align: center; }
`;

  function installStyles() {
    if (typeof document === 'undefined') return;
    if (document.getElementById('cc-legend-style')) return;
    const style = document.createElement('style');
    style.id = 'cc-legend-style';
    style.textContent = STYLE;
    document.head.appendChild(style);
  }

  function keyName(scan) {
    if (!scan) return '?';
    return NAMES[scan] || ('0x' + scan.toString(16).toUpperCase());
  }

  function uniq(list) {
    const out = [];
    for (const value of list) if (value && out.indexOf(value) < 0) out.push(value);
    return out;
  }

  function group(label, keys) {
    const node = document.createElement('span');
    node.className = 'cc-grp';
    for (const scan of uniq(keys)) {
      const cap = document.createElement('kbd');
      cap.textContent = keyName(scan);
      node.appendChild(cap);
    }
    const text = document.createElement('span');
    text.className = 'cc-act';
    text.textContent = label;
    node.appendChild(text);
    return node;
  }

  /* opts.keyForAction(action)  -> scan code bound to left/right/jump/fire
   * opts.actionForScan(scan)   -> bound action index, or -1
   * opts.jumpAliases           -> extra scans that also jump when unbound
   * opts.fixed                 -> [{ label, keys }] fixed entries
   * opts.note                  -> muted line under the caps
   */
  function render(node, opts) {
    if (!node) return;
    opts = opts || {};
    const keyForAction = opts.keyForAction || (() => 0);
    const actionForScan = opts.actionForScan || (() => -1);
    const fixed = opts.fixed || [
      { label: 'Help & key setup', keys: [0x3B, 0x01] }, /* F1, Esc */
      { label: 'Pause', keys: [0x19] }                   /* P */
    ];

    const jump = [keyForAction(2)];
    for (const scan of opts.jumpAliases || [])
      if (actionForScan(scan) < 0) jump.push(scan);

    installStyles();
    node.className = 'cc-legend';
    node.textContent = '';
    node.appendChild(group('Move', [keyForAction(0), keyForAction(1)]));
    node.appendChild(group('Jump', jump));
    node.appendChild(group('Fire', [keyForAction(3)]));
    for (const entry of fixed) node.appendChild(group(entry.label, entry.keys));
    if (opts.note) {
      const note = document.createElement('span');
      note.className = 'cc-note';
      note.textContent = opts.note;
      node.appendChild(note);
    }
  }

  return { keyName, render };
});
