/* Crystal Caves - browser keyboard, gamepad and touch wiring.
 *
 * The original game polls the live keyboard every frame.  This module keeps
 * that model: real key events are translated to set-1 scan codes, action keys
 * are latched while they are down, and releases are always forwarded so a key
 * released during a modal never stays stuck.  Touch and gamepad input feed the
 * same cc_input_set()/cc_shell_key() entry points.
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.CCGameInput = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const KEY = {
    BACKSPACE: 8, ENTER: 13, ESC: 27, SPACE: 32, F1: 0x13B
  };

  /* DOS binds jump to Ctrl, but on macOS Ctrl+Arrow is the Spaces shortcut,
   * so an arrow pressed while jump is held never reaches the page.  Space
   * and Up also jump unless the player bound them to another action. */
  const JUMP_ALIAS_SCANS = [0x39, 0x48];   /* Space, Up arrow */

  const JUMP_FLAG = 0x338B;
  const JUMP_ACTION = 2;

  /**
   * Attach keyboard, gamepad and touch input to one running game instance.
   * @param {{
   *   e: object,
   *   audio: object,
   *   persistConfig: function(): void,
   *   persistHighScores: function(): void,
   *   fullscreenElement: function(): (Element|null),
   *   stage: Element,
   *   canvas: HTMLCanvasElement
   * }} opts
   * @returns {{
   *   touch: object,
   *   releaseJump: function(): void,
   *   pollFrame: function(number): void
   * }}
   */
  function attach(opts) {
    const e = opts.e;
    const audio = opts.audio;
    const persistConfig = opts.persistConfig;
    const persistHighScores = opts.persistHighScores;
    const fullscreenElement = opts.fullscreenElement;

    function sendShellKey(ch) { e.cc_shell_key(ch); }

    const jumpHeld = new Set();

    function isJumpScan(scan) {
      if (!scan) return false;
      if (scan === e.cc_shell_key_for_action(JUMP_ACTION)) return true;
      return JUMP_ALIAS_SCANS.includes(scan) &&
             e.cc_shell_action_for_scan(scan) < 0;
    }
    function setJumpSource(scan, down) {
      if (down) jumpHeld.add(scan);
      else jumpHeld.delete(scan);
      e.cc_input_set(JUMP_FLAG, jumpHeld.size ? 1 : 0);
    }
    function releaseJump() {
      jumpHeld.clear();
      e.cc_input_set(JUMP_FLAG, 0);
    }

    window.addEventListener('keydown', (ev) => {
      audio.unlock();
      const st = e.cc_shell_state();
      const scan = CCKeymap.scanFromEvent(ev);
      if (ev.key === 'F1') { ev.preventDefault(); sendShellKey(KEY.F1); return; }
      if (ev.key === 'Escape') {
        /* Let the browser exit fullscreen first; the next Esc opens help. */
        if (fullscreenElement()) return;
        ev.preventDefault();
        sendShellKey(KEY.ESC);
        return;
      }
      if (st === 16 || st === 17) { /* keyboard redefine / confirm */
        ev.preventDefault();
        e.cc_shell_key_scan(scan || (ev.key === ' ' ? 0x39 : 0));
        if (e.cc_shell_state() === 6) persistConfig();
        return;
      }
      if (st === 4) { /* game */
        if (ev.key === 'p' || ev.key === 'P') { e.cc_shell_key(112); return; }
        if (scan) {
          ev.preventDefault();
          if (isJumpScan(scan)) setJumpSource(scan, true);
          else e.cc_input_scan(scan, 1);
        }
        return;
      }
      if (st === 12) { /* name entry */
        if (ev.key === 'Enter') sendShellKey(KEY.ENTER);
        else if (ev.key === 'Backspace') sendShellKey(KEY.BACKSPACE);
        else if (ev.key === ' ') sendShellKey(KEY.SPACE);
        else sendShellKey(ev.key.length === 1 ? ev.key.charCodeAt(0) : 0);
      } else if (ev.key === 'Enter') {
        sendShellKey(KEY.ENTER);
      } else if (ev.key === 'Backspace') {
        sendShellKey(KEY.BACKSPACE);
      } else {
        const code = ev.key.length === 1 ? ev.key.charCodeAt(0) : 0;
        if (st === 3 || st === 14) sendShellKey(code || KEY.ENTER);
        else sendShellKey(code || (ev.key === ' ' ? KEY.ENTER : 0));
      }
      /* The original polls the live keyboard every frame, so an action key
       * pressed while a modal (pause/help/message) is up is already held
       * when play resumes.  Latch it here for every state that is not text
       * entry; the keyup handler below always releases it. */
      if (scan && st !== 12) {
        if (isJumpScan(scan)) setJumpSource(scan, true);
        else e.cc_input_scan(scan, 1);
      }
      if (e.cc_shell_state() === 11 || e.cc_shell_state() === 12) persistHighScores();
      if (e.cc_shell_config_dirty()) {
        persistConfig();
        e.cc_shell_config_clear_dirty();
      }
    });
    window.addEventListener('pointerdown', () => { audio.unlock(); });
    window.addEventListener('keyup', (ev) => {
      const scan = CCKeymap.scanFromEvent(ev);
      if (!scan) return;
      /* Do not swallow the browser's fullscreen-exit sequence. */
      if (ev.key === 'Escape' && fullscreenElement()) return;
      /* Always pass the release through, whatever the shell state: the
       * original polls the live keyboard, so a key released while a modal
       * is up is simply not held afterwards.  Dropping that release left
       * the direction latched -- after the "more crystals" exit message
       * Mylo stayed glued to the door, because the stuck direction and
       * the key pressed to walk away cancelled each other out. */
      if (e.cc_shell_state() === 4 || isJumpScan(scan) || jumpHeld.has(scan))
        ev.preventDefault();
      if (isJumpScan(scan) || jumpHeld.has(scan)) setJumpSource(scan, false);
      else e.cc_input_scan(scan, 0);
    });
    window.addEventListener('blur', () => {
      releaseJump();
      for (let i = 0; i < 4; i++)
        e.cc_input_scan(e.cc_shell_key_for_action(i), 0);
    });

    const touch = CCTouch.attach({
      stage: opts.stage, canvas: opts.canvas,
      controls: document.getElementById('touch-controls'),
      keys: document.getElementById('touch-keys'),
      keyboard: document.getElementById('touch-keyboard'),
      toggle: document.getElementById('touchToggle'),
      keymap: KEY,
      onInputSet: (flag, pressed) => e.cc_input_set(flag, pressed),
      onShellKey: (key) => e.cc_shell_key(key),
      getShellState: () => e.cc_shell_state(),
      releaseAll: () => {
        e.cc_input_set(0x338E, 0);
        e.cc_input_set(0x338F, 0);
        e.cc_input_set(0x338B, 0);
        e.cc_input_set(0x3390, 0);
      }
    });

    /**
     * Run the per-frame input housekeeping the original frame loop expects.
     * @param {number} shellState current cc_shell_state()
     */
    function pollFrame(shellState) {
      if (shellState !== 4 && jumpHeld.size) releaseJump();
      if (shellState === 4 && e.cc_shell_joystick_on() && navigator.getGamepads) {
        const pads = navigator.getGamepads();
        let gp = null;
        for (const p of pads) { if (p) { gp = p; break; } }
        if (gp) {
          const ax = gp.axes[0] || 0, ay = gp.axes[1] || 0;
          e.cc_input_set(0x338E, ax < -0.4 ? 1 : 0);
          e.cc_input_set(0x338F, ax > 0.4 ? 1 : 0);
          e.cc_input_set(0x338B, (gp.buttons[0] && gp.buttons[0].pressed) || ay < -0.5 ? 1 : 0);
          e.cc_input_set(0x3390, gp.buttons[2] && gp.buttons[2].pressed ? 1 : 0);
        }
      }
    }

    return { touch, releaseJump, pollFrame };
  }

  return { KEY, JUMP_ALIAS_SCANS, attach };
});
