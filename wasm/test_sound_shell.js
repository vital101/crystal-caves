// Phase 6 shell sound integration / finale wait regression.
const fs = require("fs");
const path = require("path");

const DIR = __dirname;
const SHELL = JSON.parse(fs.readFileSync(
  path.join(DIR, "..", "notes", "sound_shell_fixtures.json"), "utf8"));
const ENTER = 13;
const N = 0x6e;
const H = 0x68;

let failures = 0;
function fail(tag, msg) {
  console.error(`${tag}: ${msg}`);
  if (++failures > 20) process.exit(1);
}
function eq(tag, got, want) {
  if (got !== want) fail(tag, `C=${got} want=${want}`);
}

async function main() {
  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")), {});
  const e = instance.exports;

  const log = () => {
    const out = [];
    for (let i = 0; i < e.cc_sound_log_count(); i++)
      out.push(e.cc_sound_log_at(i));
    return out;
  };
  const includes = (arr, v) => arr.indexOf(v) >= 0;

  // ---- boot: logo ID 32, silent attract, ID 33 on menu accept ---------
  e.cc_sound_init();
  e.cc_shell_set_config(1, 0, 0);
  e.cc_sound_log_clear();
  e.cc_shell_init();
  eq("logo.state", e.cc_shell_state(), 0);
  if (!includes(log(), 32)) fail("logo.sound32", "missing " + log());
  e.cc_sound_log_clear();
  for (let i = 0; i < 2000; i++) e.cc_shell_step_ticks(1);
  eq("attract.silent", e.cc_sound_log_count(), 0);
  e.cc_shell_key(ENTER);
  eq("attract.to_menu", e.cc_shell_state(), 2);
  e.cc_sound_log_clear();
  e.cc_shell_key(N);
  const introLog = log();
  eq("menu.page_state", e.cc_shell_state(), 3);
  // The original also calls ID 33 on N/Enter/Space; the immediately
  // following story load clears [0x513F] (0xEFCF), so it is not audible.
  if (!includes(introLog, 35)) fail("menu.sound35", "missing " + introLog);

  // ---- story pages -> level start: ID 35 per page, 36 on load ---------
  e.cc_sound_log_clear();
  let guard = 0;
  while (e.cc_shell_state() === 3 && guard++ < 32) e.cc_shell_key(ENTER);
  eq("intro.end_state", e.cc_shell_state(), 4);
  const introEndLog = log();
  if (!includes(introEndLog, 35)) fail("intro.sound35", "missing " + introEndLog);
  if (!includes(introEndLog, 36)) fail("intro.sound36", "missing " + introEndLog);
  eq("level_start.current", e.cc_sound_current_id(), 36);

  // ---- ID 7: player sprite-draw effect --------------------------------
  // 0x0F74C plays ID 7 only on the animated draw path (object state != 0),
  // i.e. a hit/transform/dissolve, never while walking.
  e.cc_sound_log_clear();
  e.cc_global_set(0xA0EA, 0);          /* not invulnerable */
  e.cc_obj_set(1, 0, 1);               /* player draw state = 1 */
  e.cc_player_draw_sound();
  e.cc_obj_set(1, 0, 0);
  if (!includes(log(), 7)) fail("draw.sound7", "missing " + log());

  // ...and the standing (state 0) path must stay silent.
  e.cc_sound_log_clear();
  e.cc_player_draw_sound();
  if (includes(log(), 7)) fail("draw.sound7.idle", "unexpected " + log());

  // ---- all caves -> ID 26 fanfare + wait ------------------------------
  e.cc_global_set(0xA0FB, 1);
  e.cc_objects_step_full();
  e.cc_shell_step();
  eq("finale.state", e.cc_shell_state(), 3);
  // Walk the finale pages; the last page starts ID 26 (0x1A) and the
  // ending sequence waits for [0x513F] to clear.
  let guard2 = 0;
  while (e.cc_sound_current_id() !== 26 && e.cc_shell_state() === 3 &&
         guard2++ < 32) {
    e.cc_shell_key(ENTER);
    /* Let the short page effect (ID 35) finish before the next page, as a
     * human reader would. */
    if (e.cc_sound_current_id() !== 26)
      while (e.cc_sound_playing()) e.cc_sound_tick();
  }
  eq("finale.sound", e.cc_sound_current_id(), 26);
  eq("finale.playing", e.cc_sound_playing(), 1);
  const pageBefore = e.cc_shell_page_index();
  e.cc_shell_key(ENTER);
  eq("finale.key_blocked_page", e.cc_shell_page_index(), pageBefore);
  const finaleTicks = e.cc_sound_current_ticks();
  eq("finale.ticks", finaleTicks, SHELL.finale_wait.ticks);
  for (let i = 0; i < finaleTicks; i++) e.cc_sound_tick();
  eq("finale.ended", e.cc_sound_playing(), 0);
  e.cc_shell_step_ticks(1);
  eq("finale.ending", e.cc_shell_state(), 13);

  // ---- high-score display: ID 34 -------------------------------------
  e.cc_sound_init();
  e.cc_shell_set_config(1, 0, 0);
  e.cc_shell_init();
  e.cc_shell_key(ENTER);   /* logo -> attract */
  e.cc_shell_key(ENTER);   /* attract -> menu */
  e.cc_sound_log_clear();
  e.cc_shell_key(H);
  eq("hiscore.state", e.cc_shell_state(), 11);
  if (!includes(log(), 34)) fail("hiscore.sound34", "missing " + log());

  // ---- disabled sound: finale wait exits immediately -----------------
  e.cc_sound_init();
  e.cc_shell_set_config(0, 0, 0);
  e.cc_shell_init();
  e.cc_shell_warp(1);
  eq("disabled.state", e.cc_shell_state(), 4);
  e.cc_global_set(0xA0FB, 1);
  e.cc_objects_step_full();
  e.cc_shell_step();
  eq("disabled.finale_state", e.cc_shell_state(), 3);
  eq("disabled.playing", e.cc_sound_playing(), 0);
  const disabledPage = e.cc_shell_page_index();
  e.cc_shell_key(ENTER);
  if (e.cc_shell_page_index() === disabledPage)
    fail("disabled.key", "page did not advance with sound off");

  // ---- ID 2 is an unused bank entry ----------------------------------
  e.cc_sound_init();
  e.cc_sound_log_clear();
  e.cc_shell_init();
  e.cc_shell_key(ENTER);
  e.cc_shell_key(ENTER);
  e.cc_shell_key(N);
  let guard3 = 0;
  while (e.cc_shell_state() === 3 && guard3++ < 32) e.cc_shell_key(ENTER);
  if (includes(log(), 2)) fail("id2.unused", "ID 2 was emitted");

  if (failures) {
    console.error(`FAIL: ${failures} shell sound checks`);
    process.exit(1);
  }
  console.log("PASS: shell sounds and finale wait");
}

main().catch((err) => { console.error(err); process.exit(1); });
