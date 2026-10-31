// Frame-budget / wasm-size measurement for the browser build.
//
//   node measure_perf.js [--json]
//
// Runs the same per-frame work as game.html (view cells, object blits, HUD)
// plus the shell renderer, and checks the result against the 17.51 Hz game
// budget (8 x 0x2147 PIT ticks = 57.1 ms).  Thresholds live in
// perf_baseline.json; timings are machine-dependent, so they are reported
// rather than compared across machines.
const fs = require('fs');
const path = require('path');

const DIR = __dirname;
const JSON_OUT = process.argv.includes('--json');

function read(p) { return fs.readFileSync(path.join(DIR, p)); }

(async () => {
  const thresholds = JSON.parse(read('perf_baseline.json'));
  const wasmBytes = fs.statSync(path.join(DIR, 'render.wasm')).size;

  const { instance } = await WebAssembly.instantiate(read('render.wasm'), {});
  const e = instance.exports;
  const mem = () => new Uint8Array(e.memory.buffer);
  const copy = (b) => { const p = e.cc_alloc(b.length); mem().set(b, p); return p; };

  e.cc_set_assets(copy(read('assets/gfx.bin')), copy(read('assets/levels.bin')));
  e.cc_set_cells(copy(read('assets/cells.bin')));
  e.cc_set_screens(copy(read('assets/screens.bin')));
  e.cc_text_set_fonts(copy(read('assets/mni_f1.bin')),
                      copy(read('assets/mni_f2.bin')),
                      copy(read('assets/mni_spl.bin')));
  const pal = JSON.parse(read('assets/palette.json'));
  const pp = e.cc_alloc(64);
  const p32 = new Uint32Array(e.memory.buffer, pp, 16);
  pal.colors.forEach(([r, g, b], i) => { p32[i] = (r << 16) | (g << 8) | b; });
  e.cc_set_palette(pp);

  const levelOverlay = read('assets/level_overlay.bin');
  const ovp = e.cc_alloc(levelOverlay.length);
  mem().set(levelOverlay, ovp);
  e.cc_set_level_overlay(ovp);

  function gameFrame() {
    const level = e.cc_cur_level();
    const camX = e.cc_cam_x(), camY = e.cc_cam_y();
    e.cc_render_view_cells(level, camX, camY);
    for (let s = 0; s < e.cc_obj_count(); s++) {
      if (e.cc_obj_state(s) === 2 || e.cc_obj_kind(s) === 0) continue;
      const blk = e.cc_obj_tile(s);
      const sh = e.cc_obj_draw_shape(s);
      if (!blk || !sh) continue;
      e.cc_draw_object_view(blk, sh, e.cc_obj_x(s) - camX, e.cc_obj_y(s) - camY);
    }
    if (e.cc_shell_hud_active()) e.cc_hud_render_screen();
  }

  function stepFrame() {
    e.cc_objects_step_full();
    e.cc_hud_tick();
    gameFrame();
  }

  function time(iters, warmup, fn) {
    for (let i = 0; i < warmup; i++) fn();
    let total = 0, worst = 0;
    for (let i = 0; i < iters; i++) {
      const t0 = process.hrtime.bigint();
      fn();
      const ms = Number(process.hrtime.bigint() - t0) / 1e6;
      total += ms;
      if (ms > worst) worst = ms;
    }
    return { avg: total / iters, worst };
  }

  const results = [];
  for (const level of [2, 3]) {
    e.cc_objects_init(level);
    if (level === 2) e.cc_input_set(0x338F, 0);   /* idle, no transitions */
    const r = time(300, 30, stepFrame);
    results.push({ name: `level ${level} frame`, ...r });
  }

  e.cc_shell_init();
  results.push({ name: 'shell render', ...time(300, 30, () => e.cc_shell_render()) });

  const memPages = e.memory.buffer.byteLength / 65536;
  const out = {
    wasm_bytes: wasmBytes,
    initial_memory_bytes: e.memory.buffer.byteLength,
    initial_memory_pages: memPages,
    budget_ms: thresholds.game_step_budget_ms,
    results,
  };

  if (JSON_OUT) {
    console.log(JSON.stringify(out, null, 2));
  } else {
    console.log(`render.wasm: ${wasmBytes} bytes`);
    console.log(`memory: ${memPages} pages (${e.memory.buffer.byteLength} bytes)`);
    console.log(`game frame budget: ${thresholds.game_step_budget_ms} ms ` +
                `(17.51 Hz, 8 x 0x2147 PIT ticks)`);
    for (const r of results) {
      console.log(`  ${r.name.padEnd(18)} avg ${r.avg.toFixed(3)} ms  ` +
                  `worst ${r.worst.toFixed(3)} ms`);
    }
  }

  let failed = 0;
  if (wasmBytes > thresholds.max_wasm_bytes) {
    console.error(`FAIL wasm size ${wasmBytes} > ${thresholds.max_wasm_bytes}`);
    failed++;
  }
  for (const r of results) {
    if (!r.name.startsWith('level ')) continue;
    if (r.avg > thresholds.max_avg_step_ms) {
      console.error(`FAIL ${r.name} avg ${r.avg.toFixed(3)} ms > ` +
                    `${thresholds.max_avg_step_ms}`);
      failed++;
    }
    if (r.worst > thresholds.max_worst_step_ms) {
      console.error(`FAIL ${r.name} worst ${r.worst.toFixed(3)} ms > ` +
                    `${thresholds.max_worst_step_ms}`);
      failed++;
    }
  }
  if (failed) process.exit(1);
  if (!JSON_OUT) console.log('PASS: wasm size and frame budget within thresholds');
})().catch((err) => { console.error(err); process.exit(1); });
