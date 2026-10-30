/* Browser physical key -> IBM PC set-1 scan code.
 *
 * The original game reads port 0x60 and compares scan codes to the four
 * bindings in [0x2B6C]/[0x2B6E]/[0x2B70]/[0x2B7A].  Using KeyboardEvent.code
 * keeps that mapping physical and independent of the browser layout.
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.CCKeymap = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  const CODE = {
    Escape: 0x01,
    Digit1: 0x02, Digit2: 0x03, Digit3: 0x04, Digit4: 0x05,
    Digit5: 0x06, Digit6: 0x07, Digit7: 0x08, Digit8: 0x09,
    Digit9: 0x0A, Digit0: 0x0B,
    Minus: 0x0C, Equal: 0x0D, Backspace: 0x0E, Tab: 0x0F,
    KeyQ: 0x10, KeyW: 0x11, KeyE: 0x12, KeyR: 0x13,
    KeyT: 0x14, KeyY: 0x15, KeyU: 0x16, KeyI: 0x17,
    KeyO: 0x18, KeyP: 0x19,
    BracketLeft: 0x1A, BracketRight: 0x1B, Enter: 0x1C,
    ControlLeft: 0x1D, ControlRight: 0x1D,
    KeyA: 0x1E, KeyS: 0x1F, KeyD: 0x20, KeyF: 0x21,
    KeyG: 0x22, KeyH: 0x23, KeyJ: 0x24, KeyK: 0x25,
    KeyL: 0x26, Semicolon: 0x27, Quote: 0x28, Backquote: 0x29,
    ShiftLeft: 0x2A, Backslash: 0x2B,
    KeyZ: 0x2C, KeyX: 0x2D, KeyC: 0x2E, KeyV: 0x2F,
    KeyB: 0x30, KeyN: 0x31, KeyM: 0x32,
    Comma: 0x33, Period: 0x34, Slash: 0x35,
    ShiftRight: 0x36,
    AltLeft: 0x38, AltRight: 0x38,
    Space: 0x39,
    F1: 0x3B, F2: 0x3C, F3: 0x3D, F4: 0x3E, F5: 0x3F,
    F6: 0x40, F7: 0x41, F8: 0x42, F9: 0x43, F10: 0x44,
    Home: 0x47, ArrowUp: 0x48, PageUp: 0x49,
    ArrowLeft: 0x4B, ArrowRight: 0x4D,
    End: 0x4F, ArrowDown: 0x50, PageDown: 0x51,
    Insert: 0x52, Delete: 0x53
  };

  const LETTER_SCAN = [
    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23,
    0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19,
    0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D,
    0x15, 0x2C
  ];

  const KEYCODE = {
    8: 0x0E, 9: 0x0F, 13: 0x1C, 16: 0x2A, 17: 0x1D, 18: 0x38,
    27: 0x01, 32: 0x39,
    33: 0x49, 34: 0x51, 35: 0x4F, 36: 0x47, 37: 0x4B,
    38: 0x48, 39: 0x4D, 40: 0x50, 45: 0x52, 46: 0x53,
    112: 0x3B, 113: 0x3C, 114: 0x3D, 115: 0x3E, 116: 0x3F,
    117: 0x40, 118: 0x41, 119: 0x42, 120: 0x43, 121: 0x44
  };

  function scanFromCode(code) {
    if (!code) return 0;
    if (CODE[code] !== undefined) return CODE[code];
    if (/^Key[A-Z]$/.test(code)) {
      return LETTER_SCAN[code.charCodeAt(3) - 65];
    }
    if (/^Digit[0-9]$/.test(code)) {
      const digit = code.charCodeAt(5) - 48;
      return digit === 0 ? 0x0B : 0x02 + digit - 1;
    }
    if (/^F([1-9]|1[0-2])$/.test(code)) {
      const n = parseInt(code.slice(1), 10);
      return n <= 10 ? 0x3A + n : 0;
    }
    return 0;
  }

  function scanFromEvent(ev) {
    if (!ev) return 0;
    const scan = scanFromCode(ev.code);
    if (scan) return scan;
    const kc = ev.keyCode | 0;
    if (KEYCODE[kc]) return KEYCODE[kc];
    if (kc >= 65 && kc <= 90) return LETTER_SCAN[kc - 65];
    if (kc >= 48 && kc <= 57) return kc === 48 ? 0x0B : kc - 48 + 1;
    return 0;
  }

  return { CODE, scanFromCode, scanFromEvent };
});
