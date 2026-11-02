/* Touch controls for game.html.
 *
 * Adds a virtual pad (left/right/jump/fire), a soft keyboard for menus and
 * text entry, and tap-to-advance on the canvas.  The layer only translates
 * pointer events into the same cc_input_set()/cc_shell_key() calls the
 * keyboard and gamepad paths already use, so the C port and its fixtures are
 * untouched.
 *
 * Visibility is auto-detected from the pointer type, with a manual on/off
 * toggle persisted under its own localStorage key.  That key is deliberately
 * separate from cc1.config.v2: the v2 document mirrors the original CC1.CFG
 * fields and is validated strictly by persist.js.
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.CCTouch = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  const STORAGE_KEY = 'cc1.touch.v1';

  /* The four action flags are the same ones the gamepad path in game.html
   * drives; they are independent of the redefinable keyboard bindings. */
  const FLAGS = {
    left: 0x338E,
    right: 0x338F,
    jump: 0x338B,
    fire: 0x3390
  };

  /* Shell states where a tap on the canvas counts as "any key".  These are
   * the read-only screens that advance on Enter; the menu and text-entry
   * states keep the soft keyboard instead. */
  const TAP_ADVANCE = [0, 1, 3, 5, 11, 13, 14, 15];

  /* Shell states that need letters or digits: main menu, save, restore and
   * high-score name entry.  The keyboard slides up automatically here. */
  const KEYBOARD = [2, 8, 9, 12];

  /** @returns {boolean} true when a coarse pointer or touch events are present. */
  function detectTouch() {
    if (typeof window === 'undefined') return false;
    const coarse = window.matchMedia &&
                   window.matchMedia('(pointer: coarse)').matches;
    const touch = navigator.maxTouchPoints > 0 || 'ontouchstart' in window;
    return !!(coarse || touch);
  }

  function loadEnabled() {
    try {
      const v = localStorage.getItem(STORAGE_KEY);
      if (v === '1') return true;
      if (v === '0') return false;
    } catch (_) {}
    return detectTouch();
  }

  function saveEnabled(v) {
    try { localStorage.setItem(STORAGE_KEY, v ? '1' : '0'); } catch (_) {}
  }

  /* opts:
   *   stage, canvas, controls, keys, keyboard, toggle  DOM nodes
   *   keymap          { BACKSPACE, ENTER, ESC, SPACE }
   *   onInputSet(flag, pressed)   -> cc_input_set
   *   onShellKey(key)             -> cc_shell_key
   *   getShellState()             -> cc_shell_state
   *   releaseAll()                -> clear the four action flags
   */
  /**
   * Attach the pointer pad, soft keys and canvas tap-to-advance.
   * @param {object} opts DOM nodes plus cc_input_set/cc_shell_key callbacks.
   * @returns {{setEnabled: function(boolean): void,
   *            update: function(): void,
   *            enabled: function(): boolean}}
   */
  function attach(opts) {
    const {
      stage, canvas, controls, keys, keyboard, toggle,
      keymap, onInputSet, onShellKey, getShellState, releaseAll
    } = opts;

    let enabled = loadEnabled();
    let lastState = -1;

    const activePointers = new Map();

    function setButtonFlag(btn, action, down) {
      const flag = FLAGS[action];
      if (!flag) return;
      onInputSet(flag, down ? 1 : 0);
      btn.classList.toggle('active', down);
    }

    function bindButton(btn, action) {
      const pointers = new Set();
      function set(down, ev) {
        if (down) pointers.add(ev.pointerId);
        else pointers.delete(ev.pointerId);
        setButtonFlag(btn, action, pointers.size > 0);
      }
      btn.addEventListener('pointerdown', (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        set(true, ev);
        try { btn.setPointerCapture(ev.pointerId); } catch (_) {}
      });
      const release = (ev) => {
        if (!pointers.has(ev.pointerId)) return;
        set(false, ev);
        try { btn.releasePointerCapture(ev.pointerId); } catch (_) {}
      };
      btn.addEventListener('pointerup', release);
      btn.addEventListener('pointercancel', release);
      btn.addEventListener('lostpointercapture', release);
    }

    function bindSoftKey(btn) {
      const key = Number(btn.dataset.key);
      if (!key) return;
      btn.addEventListener('pointerdown', (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        onShellKey(key);
        btn.classList.add('active');
        try { btn.setPointerCapture(ev.pointerId); } catch (_) {}
      });
      const release = (ev) => {
        btn.classList.remove('active');
        try { btn.releasePointerCapture(ev.pointerId); } catch (_) {}
      };
      btn.addEventListener('pointerup', release);
      btn.addEventListener('pointercancel', release);
      btn.addEventListener('lostpointercapture', release);
    }

    function buildKeyboard() {
      if (!keyboard) return;
      const rows = [
        'QWERTYUIOP',
        'ASDFGHJKL',
        'ZXCVBNM',
        '0123456789'
      ];
      keyboard.textContent = '';
      for (const row of rows) {
        for (const ch of row) {
          const btn = document.createElement('button');
          btn.type = 'button';
          btn.textContent = ch;
          btn.dataset.key = String(ch.charCodeAt(0));
          bindSoftKey(btn);
          keyboard.appendChild(btn);
        }
      }
      const specials = [
        ['Space', keymap.SPACE],
        ['Bksp', keymap.BACKSPACE],
        ['Enter', keymap.ENTER],
        ['Esc', keymap.ESC]
      ];
      for (const [label, key] of specials) {
        const btn = document.createElement('button');
        btn.type = 'button';
        btn.textContent = label;
        btn.dataset.key = String(key);
        bindSoftKey(btn);
        keyboard.appendChild(btn);
      }
    }

    function onCanvasPointerDown(ev) {
      if (!enabled) return;
      const st = getShellState();
      if (TAP_ADVANCE.indexOf(st) >= 0) {
        ev.preventDefault();
        onShellKey(keymap.ENTER);
      }
    }

    function updateVisibility() {
      if (controls) controls.hidden = !enabled;
      if (keys) keys.hidden = !enabled;
      if (!enabled && keyboard) keyboard.classList.remove('open');
      if (toggle) {
        toggle.textContent = enabled ? 'Touch controls: on' : 'Touch controls: off';
        toggle.setAttribute('aria-pressed', enabled ? 'true' : 'false');
        toggle.hidden = false;
      }
    }

    function setEnabled(v) {
      enabled = v;
      saveEnabled(v);
      lastState = -1;              /* force update() to re-read the state */
      updateVisibility();
      if (!enabled) {
        /* The hidden attribute alone is not enough: the author display rule
         * (#touch-controls { display:flex }) beats the UA [hidden] rule, so
         * clear the inline display as well. */
        if (controls) controls.style.display = 'none';
        if (keys) keys.style.display = 'none';
        releaseAll();
      }
      update();
    }

    function update() {
      if (!enabled) return;
      const st = getShellState();
      if (st === lastState) return;
      lastState = st;
      if (st !== 4) releaseAll();
      if (controls) controls.style.display = st === 4 ? '' : 'none';
      /* The soft-key strip is the always-visible part of the touch UI: it
       * gives immediate feedback when the controls are switched on and keeps
       * Esc/Enter available on every screen. */
      if (keys) keys.style.display = '';
      if (keyboard) {
        if (KEYBOARD.indexOf(st) >= 0) keyboard.classList.add('open');
        else keyboard.classList.remove('open');
      }
    }

    updateVisibility();
    buildKeyboard();
    if (controls) {
      for (const btn of controls.querySelectorAll('.touch-btn')) {
        bindButton(btn, btn.dataset.action);
      }
    }
    if (keys) {
      for (const btn of keys.querySelectorAll('.touch-key')) {
        bindSoftKey(btn);
      }
    }
    if (canvas) canvas.addEventListener('pointerdown', onCanvasPointerDown);
    if (toggle) toggle.addEventListener('click', () => setEnabled(!enabled));

    setInterval(update, 100);
    update();

    return { setEnabled, update, enabled: () => enabled };
  }

  return { attach, detectTouch, STORAGE_KEY };
});
