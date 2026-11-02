/* Crystal Caves - browser entry point.
 *
 * Loads the wasm module and portable assets, wires the persistent config and
 * high scores, then drives the shell/game with the original 17.5 Hz frame and
 * 140.06 Hz sound ticks.  Rendering, save/restore requests and input live in
 * game/render.js, game/requests.js and game/input.js so this file stays the
 * boot + frame-loop entry point.
 */
(() => {
  'use strict';

  const W = CCGameRender.W, H = CCGameRender.H;
  const TEST_MODE = new URLSearchParams(location.search).has('test');
  const cv = document.getElementById('cv');
  const ctx = cv.getContext('2d');
  const diag = document.getElementById('diag');
  const legend = document.getElementById('legend');
  const stage = document.getElementById('stage');
  const fsBtn = document.getElementById('fsBtn');

  function fullscreenElement() {
    return document.fullscreenElement || document.webkitFullscreenElement || null;
  }
  function syncFullscreenButton() {
    const active = !!fullscreenElement();
    fsBtn.setAttribute('aria-pressed', active ? 'true' : 'false');
    fsBtn.title = active ? 'Exit fullscreen (Esc)' : 'Enter fullscreen';
  }
  document.addEventListener('fullscreenchange', syncFullscreenButton);
  document.addEventListener('webkitfullscreenchange', syncFullscreenButton);
  syncFullscreenButton();

  const STEP_MS = 1000 * 8 * 0x2147 / 1193182;
  const TICK_MS = 1000 / 140.06;

  (async () => {
    const load = (p) => fetch(p + '?v=' + Date.now()).then(r => r.arrayBuffer());
    const [wasm, gfx, levels, cells, overlay, screens, f1, f2, spl, pal, snd] =
      await Promise.all([
        load('render.wasm'), load('assets/gfx.bin'), load('assets/levels.bin'),
        load('assets/cells.bin'), load('assets/level_overlay.bin'),
        load('assets/screens.bin'), load('assets/mni_f1.bin'),
        load('assets/mni_f2.bin'), load('assets/mni_spl.bin'),
        fetch('assets/palette.json').then(r => r.json()),
        fetch('assets/sound.json').then(r => r.json()),
      ]);

    const { instance } = await WebAssembly.instantiate(wasm, {});
    const e = instance.exports;
    const mem = () => new Uint8Array(e.memory.buffer);

    const audio = new CCAudio();
    audio.load(snd);
    audio.onended = (id) => e.cc_sound_end(id);

    /* Modern host convenience: fullscreen only the game stage, leaving the
     * page chrome and legend outside the element.  The request stays in the
     * click's user gesture, so the audio unlock happens afterwards.
     * iOS Safari has no element Fullscreen API, so the button falls back to a
     * CSS-only immersive mode there. */
    const fsRequest = stage.requestFullscreen || stage.webkitRequestFullscreen;
    const fsEnabled = document.fullscreenEnabled !== false &&
                      document.webkitFullscreenEnabled !== false;
    fsBtn.addEventListener('keydown', (ev) => {
      if (ev.key === 'Enter' || ev.key === ' ') ev.stopPropagation();
    });
    fsBtn.addEventListener('keyup', (ev) => {
      if (ev.key === 'Enter' || ev.key === ' ') ev.stopPropagation();
    });
    if (!fsRequest || !fsEnabled) {
      fsBtn.textContent = 'Immersive';
      fsBtn.title = 'Toggle immersive mode';
      fsBtn.addEventListener('click', () => {
        document.body.classList.toggle('immersive');
        audio.unlock();
        fsBtn.blur();
      });
    } else {
      fsBtn.addEventListener('click', () => {
        try {
          if (fullscreenElement()) {
            const exit = document.exitFullscreen || document.webkitExitFullscreen;
            if (exit) {
              const p = exit.call(document);
              if (p && typeof p.catch === 'function')
                p.catch((err) => console.warn('fullscreen:', err));
            }
          } else {
            const p = fsRequest.call(stage);
            if (p && typeof p.catch === 'function')
              p.catch((err) => console.warn('fullscreen:', err));
          }
        } catch (err) {
          console.warn('fullscreen:', err);
        }
        audio.unlock();
        fsBtn.blur();
      });
    }
    const copyIn = (buf) => {
      const p = e.cc_alloc(buf.byteLength);
      mem().set(new Uint8Array(buf), p);
      return p;
    };
    const copyString = (s) => {
      const bytes = new TextEncoder().encode(s);
      const p = e.cc_alloc(bytes.length + 1);
      const m = mem();
      m.set(bytes, p);
      m[p + bytes.length] = 0;
      return p;
    };

    e.cc_set_assets(copyIn(gfx), copyIn(levels));
    e.cc_set_cells(copyIn(cells));
    const ovp = e.cc_alloc(overlay.byteLength);
    mem().set(new Uint8Array(overlay), ovp);
    e.cc_set_level_overlay(ovp);
    e.cc_set_screens(copyIn(screens));
    e.cc_text_set_fonts(copyIn(f1), copyIn(f2), copyIn(spl));

    const palP = e.cc_alloc(64);
    const pal32 = new Uint32Array(e.memory.buffer, palP, 16);
    pal.colors.forEach(([r, g, b], i) => { pal32[i] = (r << 16) | (g << 8) | b; });
    e.cc_set_palette(palP);

    const stateSize = e.cc_state_size();
    const stateP = e.cc_alloc(stateSize);
    // Scratch buffer for the test hook's cc_state_save (mirrors the harnesses,
    // which allocate a separate save buffer; saveSlot keeps using stateP).
    const saveScratchP = e.cc_alloc(stateSize);

    // ---- persistent config / high scores -------------------------------
    e.cc_sound_init();
    e.cc_shell_init();
    const cfg = CCPersist.loadConfig(localStorage);
    e.cc_shell_set_config(cfg.sound, cfg.joystick, cfg.video);
    for (let i = 0; i < 4; i++) e.cc_shell_set_key(i, cfg.keys[i]);
    audio.setEnabled(cfg.sound);
    CCPersist.loadHighScores(localStorage, e, copyString);
    CCPersist.saveConfig(localStorage, cfg);

    /* The four action keys are redefinable from the help screen, so the
     * legend is rebuilt from the live bindings instead of listing them. */
    function renderLegend() {
      /* The legend is cosmetic: never let a missing/stale legend.js stop the
       * game from booting (the production image enumerates its static files). */
      if (typeof CCLegend === 'undefined') return;
      CCLegend.render(legend, {
        keyForAction: (action) => e.cc_shell_key_for_action(action),
        actionForScan: (scan) => e.cc_shell_action_for_scan(scan),
        jumpAliases: CCGameInput.JUMP_ALIAS_SCANS,
        note: 'Click the game once to enable sound; every control is on the keyboard.'
      });
    }
    renderLegend();

    function persistConfig() {
      const c = {
        version: 2,
        sound: e.cc_shell_sound_on(),
        joystick: e.cc_shell_joystick_on(),
        video: e.cc_shell_video_alt(),
        keys: [0, 1, 2, 3].map(i => e.cc_shell_key_for_action(i))
      };
      audio.setEnabled(c.sound);
      CCPersist.saveConfig(localStorage, c);
      renderLegend();
    }
    function persistHighScores() {
      CCPersist.saveHighScores(localStorage, e);
    }

    // ---- rendering ------------------------------------------------------
    const renderer = CCGameRender.create({ e, ctx });
    const { renderGame, renderShell, render } = renderer;

    // ---- save/restore handshake ----------------------------------------
    const requests = CCGameRequests.create({
      e, persist: CCPersist, storage: localStorage,
      stateP, stateSize, mem, render, renderLegend
    });
    const handleRequest = requests.handleRequest;

    // ---- input ----------------------------------------------------------
    const input = CCGameInput.attach({
      e, audio, persistConfig, persistHighScores,
      fullscreenElement, stage, canvas: cv
    });
    const touch = input.touch;

    // ---- sound event pump ----------------------------------------------
    function pumpSound() {
      const n = e.cc_sound_event_count();
      for (let i = 0; i < n; i++) audio.play(e.cc_sound_event_at(i));
      if (n) e.cc_sound_event_clear();
      if (!e.cc_sound_playing()) audio.stop();
    }

    // ---- main loop ------------------------------------------------------
    let last = 0, accum = 0, soundAccum = 0;
    function frame(now) {
      if (!last) last = now;
      let dt = now - last; last = now;
      if (dt > 250) dt = 250;
      /* The original sound ISR runs on the same 140.06 Hz PIT as the game
       * timer, independently of the 17.5 Hz game frame. */
      soundAccum += dt;
      let soundTicks = Math.floor(soundAccum / TICK_MS);
      if (soundTicks > 32) soundTicks = 32;
      soundAccum -= soundTicks * TICK_MS;
      handleRequest();
      input.pollFrame(e.cc_shell_state());
      /* Advance the sound driver at 140.06 Hz.  In shell states the shell
       * timer advances on the same tick, after the sound tick, so a
       * wait-for-sound (ID 26) observes the cleared playing flag. */
      for (let i = 0; i < soundTicks; i++) {
        e.cc_sound_tick();
        if (e.cc_shell_state() !== 4) e.cc_shell_step_ticks(1);
      }

      if (e.cc_shell_state() === 4) {
        e.cc_shell_step();
        accum += dt;
        let steps = 0;
        while (accum >= STEP_MS && steps < 4) {
          e.cc_objects_step_full();
          e.cc_player_draw_sound();
          e.cc_hud_tick();
          e.cc_shell_step();
          accum -= STEP_MS;
          steps++;
          if (e.cc_shell_state() !== 4) break;
        }
        pumpSound();
        if (steps) renderGame();
      } else {
        pumpSound();
        if (e.cc_shell_overlay()) renderGame();
        else renderShell();
      }
      requestAnimationFrame(frame);
    }

    // ---- automated test hook (only with ?test=1) ------------------------
    /* The browser smoke test drives the game explicitly instead of waiting on
     * requestAnimationFrame: fixed inputs, fixed step counts, stable output.
     * Normal play never sees this object and keeps the rAF loop. */
    if (TEST_MODE) {
      const readStr = (p) => {
        const m = mem();
        let s = '';
        for (let i = 0; i < 64; i++) {
          const c = m[p + i];
          if (!c) break;
          s += String.fromCharCode(c);
        }
        return s;
      };
      const hashBytes = (u8) => {
        let h = 0x811c9dc5;
        for (let i = 0; i < u8.length; i++) {
          h ^= u8[i];
          h = Math.imul(h, 0x01000193);
        }
        return h >>> 0;
      };
      const fbHash = () => {
        const fb = e.cc_view_framebuffer();
        return hashBytes(new Uint8Array(e.memory.buffer, fb, W * H * 4));
      };
      const nonBlack = () => {
        const fb = e.cc_view_framebuffer();
        const u8 = new Uint8Array(e.memory.buffer, fb, W * H * 4);
        let n = 0;
        for (let i = 0; i < W * H; i++)
          if (u8[i * 4] || u8[i * 4 + 1] || u8[i * 4 + 2]) n++;
        return n;
      };
      const stateHash = () => {
        const n = e.cc_state_save(saveScratchP, stateSize);
        if (!(n > 0)) return 0;
        return hashBytes(new Uint8Array(e.memory.buffer, saveScratchP, n));
      };
      const step = (n) => {
        for (let i = 0; i < n; i++) {
          e.cc_objects_step_full();
          e.cc_player_draw_sound();
          e.cc_hud_tick();
          e.cc_shell_step();
        }
        render();
      };
      window.__cc = {
        ready: false,
        exports: e,
        state: () => e.cc_shell_state(),
        level: () => e.cc_cur_level(),
        player: () => [e.cc_player_x(), e.cc_player_y()],
        ammo: () => e.cc_ammo(),
        score: () => e.cc_score() >>> 0,
        hudActive: () => e.cc_shell_hud_active(),
        key: (k) => e.cc_shell_key(k),
        keyScan: (s) => e.cc_shell_key_scan(s),
        input: (scan, down) => e.cc_input_scan(scan, down),
        stepTicks: (n) => e.cc_shell_step_ticks(n),
        step,
        render,
        touch,
        handleRequest,
        fbHash,
        nonBlack,
        stateHash,
        saveSlot: (slot) =>
          CCPersist.saveSlot(localStorage, slot, e, stateP, stateSize, mem),
        loadSlot: (slot, hubOnly) =>
          CCPersist.loadSlot(localStorage, slot, e, stateP, stateSize, mem,
                             hubOnly !== false),
        saveConfig: persistConfig,
        saveHighScores: persistHighScores,
        setConfig: (sound, joystick, video) =>
          e.cc_shell_set_config(sound, joystick, video),
        config: () => [e.cc_shell_sound_on(), e.cc_shell_joystick_on(),
                       e.cc_shell_video_alt()],
        addHiscore: (name, score) =>
          e.cc_shell_hiscore_add(copyString(name), score),
        hiscore: (i) => ({
          name: readStr(e.cc_shell_hiscore_name(i)),
          score: e.cc_shell_hiscore_score(i) >>> 0,
        }),
        storage: () => ({
          config: localStorage.getItem(CCPersist.CFG_KEY),
          hiscores: localStorage.getItem(CCPersist.HS_KEY),
          save0: localStorage.getItem(CCPersist.SAVE_PREFIX + '0'),
        }),
      };
      diag.textContent = 'test-ready';
      renderShell();
      window.__cc.ready = true;
      return;
    }

    diag.hidden = true;
    renderShell();
    requestAnimationFrame(frame);
  })().catch((err) => {
    diag.hidden = false;
    diag.textContent = 'error: ' + err.message;
    console.error(err);
  });
})();
