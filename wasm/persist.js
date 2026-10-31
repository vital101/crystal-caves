/* Crystal Caves - browser persistence layer.
 *
 * The original game used CC1.CFG, CC1.HS and CC1-<slot>.sav files.  This
 * module keeps the same observable rules but stores the data in localStorage:
 *
 *   cc1.config.v2      JSON config (sound/joystick/video/keys)
 *   cc1.config.v1      legacy browser keyCode config (migrated on load)
 *   cc1.hiscores.v1    JSON array of exactly ten { name, score } rows
 *   cc1.save.<slot>    base64 of the versioned cc_state_save() bytes
 *
 * It is deliberately DOM-free so the same code runs in the browser and in the
 * Node regression harness (see test_persist.js).
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.CCPersist = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  const CFG_KEY = 'cc1.config.v2';
  const OLD_CFG_KEY = 'cc1.config.v1';
  const HS_KEY = 'cc1.hiscores.v1';
  const SAVE_PREFIX = 'cc1.save.';
  const HS_COUNT = 10;
  const HS_NAME_MAX = 20;

  const DEFAULT_KEYS = [0x4B, 0x4D, 0x1D, 0x38]; /* left, right, Ctrl, Alt */

  function isObject(v) {
    return v !== null && typeof v === 'object' && !Array.isArray(v);
  }

  function isInt(v, lo, hi) {
    return typeof v === 'number' && isFinite(v) && Math.floor(v) === v &&
           v >= lo && v <= hi;
  }

  function bool01(v, dflt) {
    if (v === true) return 1;
    if (v === false) return 0;
    if (v === 0 || v === 1) return v;
    return dflt;
  }

  function defaultConfig() {
    return {
      version: 2,
      sound: 1,
      joystick: 0,
      video: 0,
      keys: DEFAULT_KEYS.slice()
    };
  }

  function normalizeConfig(raw) {
    const cfg = defaultConfig();
    if (!isObject(raw)) return cfg;
    /* A future version stored under the same key is not ours. */
    if (raw.version !== undefined && raw.version !== 2) return cfg;
    cfg.sound = bool01(raw.sound, cfg.sound);
    cfg.joystick = bool01(raw.joystick, cfg.joystick);
    cfg.video = bool01(raw.video, cfg.video);
    if (Array.isArray(raw.keys) && raw.keys.length === 4) {
      const keys = [];
      for (let i = 0; i < 4; i++) {
        if (!isInt(raw.keys[i], 0, 0x7F)) { keys.length = 0; break; }
        keys.push(raw.keys[i]);
      }
      if (keys.length === 4) cfg.keys = keys;
    }
    return cfg;
  }

  function loadConfig(storage) {
    let raw = null;
    try {
      const text = storage.getItem(CFG_KEY);
      if (text !== null) {
        raw = JSON.parse(text);
        return normalizeConfig(raw);
      }
      /* Phase 5 v1 stored browser keyCode values; the original DOS defaults
       * are used for the bindings, while the three toggles are preserved. */
      const old = storage.getItem(OLD_CFG_KEY);
      if (old !== null) {
        const o = JSON.parse(old);
        const cfg = defaultConfig();
        if (isObject(o)) {
          cfg.sound = bool01(o.sound, cfg.sound);
          cfg.joystick = bool01(o.joystick, cfg.joystick);
          cfg.video = bool01(o.video, cfg.video);
        }
        return cfg;
      }
    } catch (_) {}
    return defaultConfig();
  }

  function saveConfig(storage, cfg) {
    const norm = normalizeConfig(cfg);
    try {
      storage.setItem(CFG_KEY, JSON.stringify(norm));
      return true;
    } catch (_) {
      return false;
    }
  }

  function sanitizeName(s) {
    if (typeof s !== 'string') return null;
    let out = '';
    for (let i = 0; i < s.length && out.length < HS_NAME_MAX; i++) {
      const c = s.charCodeAt(i);
      if (c >= 0x20 && c <= 0x7E) out += s[i];
    }
    return out;
  }

  function validScore(v) {
    return isInt(v, 0, 0xFFFFFFFF);
  }

  function readName(e, p) {
    const m = new Uint8Array(e.memory.buffer);
    let s = '';
    for (let i = 0; i < 64; i++) {
      const c = m[p + i];
      if (!c) break;
      s += String.fromCharCode(c);
    }
    return s;
  }

  function saveHighScores(storage, e) {
    const arr = [];
    for (let i = 0; i < HS_COUNT; i++) {
      arr.push({
        name: readName(e, e.cc_shell_hiscore_name(i)),
        score: e.cc_shell_hiscore_score(i) >>> 0
      });
    }
    try {
      storage.setItem(HS_KEY, JSON.stringify(arr));
      return true;
    } catch (_) {
      return false;
    }
  }

  function loadHighScores(storage, e, copyString) {
    let arr = null;
    try {
      const text = storage.getItem(HS_KEY);
      if (text !== null) arr = JSON.parse(text);
    } catch (_) {}
    let ok = Array.isArray(arr) && arr.length === HS_COUNT;
    const rows = [];
    if (ok) {
      for (let i = 0; i < HS_COUNT; i++) {
        const row = arr[i];
        if (!isObject(row) || !validScore(row.score)) { ok = false; break; }
        const name = sanitizeName(row.name);
        if (name === null) { ok = false; break; }
        rows.push({ name: name, score: row.score >>> 0 });
      }
    }
    if (!ok) {
      /* CC1.HS was all-or-nothing: a missing/corrupt file was replaced by the
       * default ten-row table. */
      saveHighScores(storage, e);
      return false;
    }
    for (let i = 0; i < HS_COUNT; i++)
      e.cc_shell_hiscore_set(i, copyString(rows[i].name), rows[i].score);
    return true;
  }

  function b64encode(bytes) {
    let s = '';
    const chunk = 0x8000;
    for (let i = 0; i < bytes.length; i += chunk)
      s += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
    return btoa(s);
  }

  function b64decode(str) {
    const s = atob(str);
    const out = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
    return out;
  }

  function saveSlot(storage, slot, e, stateP, stateSize, mem) {
    if (!isInt(slot, 0, 9) || !stateP || stateSize <= 0) return false;
    const n = e.cc_state_save(stateP, stateSize);
    if (!(n > 0 && n <= stateSize)) return false;
    try {
      storage.setItem(SAVE_PREFIX + slot,
                      b64encode(mem().subarray(stateP, stateP + n)));
      return true;
    } catch (_) {
      return false;
    }
  }

  function loadSlot(storage, slot, e, stateP, stateSize, mem, hubOnly) {
    if (!isInt(slot, 0, 9) || !stateP || stateSize <= 0) return false;
    let raw = null;
    try {
      raw = storage.getItem(SAVE_PREFIX + slot);
    } catch (_) {
      return false;
    }
    if (typeof raw !== 'string' || raw.length === 0) return false;
    let bytes;
    try {
      bytes = b64decode(raw);
    } catch (_) {
      return false;
    }
    /* Check before touching wasm memory: a corrupt/foreign slot must never
     * overwrite allocations past the state buffer. */
    if (bytes.length > stateSize) return false;
    try {
      mem().set(bytes, stateP);
      return e.cc_state_load_ex(stateP, bytes.length, hubOnly ? 1 : 0) ? true : false;
    } catch (_) {
      return false;
    }
  }

  return {
    CFG_KEY: CFG_KEY,
    OLD_CFG_KEY: OLD_CFG_KEY,
    HS_KEY: HS_KEY,
    SAVE_PREFIX: SAVE_PREFIX,
    HS_COUNT: HS_COUNT,
    HS_NAME_MAX: HS_NAME_MAX,
    DEFAULT_KEYS: DEFAULT_KEYS.slice(),
    defaultConfig: defaultConfig,
    normalizeConfig: normalizeConfig,
    loadConfig: loadConfig,
    saveConfig: saveConfig,
    sanitizeName: sanitizeName,
    loadHighScores: loadHighScores,
    saveHighScores: saveHighScores,
    saveSlot: saveSlot,
    loadSlot: loadSlot
  };
});
