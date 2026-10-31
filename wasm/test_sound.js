// Phase 6 sound regression: C driver state, exported trace, JS synth hashes.
//   node test_sound.js
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");
const CCSynth = require("./sound_synth.js");

const DIR = __dirname;
const ROOT = path.join(DIR, "..");
const FIXTURE = JSON.parse(fs.readFileSync(
  path.join(ROOT, "notes", "sound_fixtures.json"), "utf8"));
const SYNTH = JSON.parse(fs.readFileSync(
  path.join(ROOT, "notes", "sound_synth_fixtures.json"), "utf8"));
const ASSET = JSON.parse(fs.readFileSync(
  path.join(ROOT, "out", "portable", "sound.json"), "utf8"));

let failures = 0;
function fail(tag, msg) {
  console.error(`${tag}: ${msg}`);
  if (++failures > 40) process.exit(1);
}
function eq(tag, got, want) {
  if (got !== want) fail(tag, `C=${got} fixture=${want}`);
}

function hashInt16(samples) {
  const buf = Buffer.from(samples.buffer, samples.byteOffset,
                          samples.byteLength);
  return crypto.createHash("sha256").update(buf).digest("hex");
}

async function main() {
  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")), {});
  const e = instance.exports;

  // ---- exported asset matches the oracle trace -----------------------
  eq("asset.count", ASSET.count, FIXTURE.count);
  eq("asset.ids", ASSET.sounds.length, FIXTURE.count);
  const byId = {};
  for (const s of ASSET.sounds) byId[s.id] = s;
  for (let id = 1; id <= 36; id++) {
    const want = FIXTURE.ids[String(id)];
    const got = byId[id];
    if (!want || !got) { fail("asset.id." + id, "missing"); continue; }
    eq(`asset.${id}.bank`, got.bank, want.bank);
    eq(`asset.${id}.slot`, got.slot, want.slot);
    eq(`asset.${id}.priority`, got.priority, want.priority);
    eq(`asset.${id}.gate`, got.gate, want.gate);
    eq(`asset.${id}.ticks`, got.ticks, want.ticks);
    if (JSON.stringify(got.trace) !== JSON.stringify(want.trace))
      fail("asset." + id, "trace differs from fixture");
  }

  // ---- C driver selection/preemption matrix --------------------------
  for (const test of FIXTURE.selection) {
    e.cc_sound_init();
    let step = 0;
    for (const op of test.steps) {
      switch (op.op) {
        case "play": e.cc_sound_play(op.id); break;
        case "enable": e.cc_sound_set_enabled(1); break;
        case "disable": e.cc_sound_set_enabled(0); break;
        case "stop": e.cc_sound_stop(); break;
        case "ticks":
          for (let i = 0; i < op.n; i++) e.cc_sound_tick();
          break;
        default: fail("selection." + test.name, "unknown op " + op.op);
      }
      const want = test.want[step++];
      const tag = `selection.${test.name}[${step - 1}]`;
      eq(tag + ".enabled", e.cc_sound_enabled(), want.enabled);
      eq(tag + ".playing", e.cc_sound_playing(), want.playing);
      eq(tag + ".bank", e.cc_sound_current_bank(), want.bank);
      eq(tag + ".slot", e.cc_sound_current_slot(), want.slot);
    }
  }

  // ---- accepted event queue ------------------------------------------
  e.cc_sound_init();
  e.cc_sound_reset();
  e.cc_sound_play(1);
  e.cc_sound_play(3);
  eq("events.count", e.cc_sound_event_count(), 2);
  eq("events.0", e.cc_sound_event_at(0), 1);
  eq("events.1", e.cc_sound_event_at(1), 3);
  e.cc_sound_event_clear();
  eq("events.cleared", e.cc_sound_event_count(), 0);

  // ---- unknown IDs are counted, not accepted -------------------------
  e.cc_sound_init();
  eq("unknown.zero.play", e.cc_sound_play(0), 0);
  eq("unknown.high.play", e.cc_sound_play(37), 0);
  eq("unknown.hits", e.cc_sound_unknown_hits(), 2);

  // ---- JS synthesizer hashes -----------------------------------------
  for (const rateStr of Object.keys(SYNTH.sample_rates)) {
    const rate = Number(rateStr);
    const rec = SYNTH.sample_rates[rateStr];
    for (let id = 1; id <= 36; id++) {
      const trace = FIXTURE.ids[String(id)].trace;
      const pcm = CCSynth.renderInt16(trace, rate, SYNTH.amplitude);
      const tag = `synth.${rate}.${id}`;
      eq(tag + ".len", pcm.length, rec.samples[String(id)]);
      const got = hashInt16(pcm);
      const wantHash = rec.sha256[String(id)];
      if (got !== wantHash)
        fail(tag, "sha256 " + got + " != " + wantHash);
    }
  }

  if (failures) {
    console.error(`FAIL: ${failures} sound checks`);
    process.exit(1);
  }
  console.log("PASS: sound driver, trace asset, and JS synth hashes");
}

main().catch((err) => { console.error(err); process.exit(1); });
