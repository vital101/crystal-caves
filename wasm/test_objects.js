// Node harness: step the object simulation and dump object state as text.
//   node test_objects.js [level] [frames]
// Validates the C3 update loop against the golden-oracle catalog.
const fs = require("fs");
const path = require("path");

const DIR = __dirname;

(async () => {
  const level = parseInt(process.argv[2] || "0", 10);
  const frames = parseInt(process.argv[3] || "20", 10);

  const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(path.join(DIR, "render.wasm")),
    {}
  );
  const e = instance.exports;

  e.cc_objects_init(level);

  const slots = [];
  for (let s = 0; s < e.cc_obj_count(); s++) {
    const st = e.cc_obj_state(s);
    const kind = e.cc_obj_kind(s);
    if (st !== 2 && kind !== 0) slots.push(s);
  }

  const row = (s) => {
  let x = e.cc_obj_x(s);
  if (x > 32767) x -= 65536;
  let y = e.cc_obj_y(s);
  if (y > 32767) y -= 65536;
  return `slot ${String(s).padStart(2)} state=${String(e.cc_obj_state(s)).padStart(4)} ` +
    `x=${String(x).padStart(5)} y=${String(y).padStart(5)} ` +
    `kind=${String(e.cc_obj_kind(s)).padStart(3)} shape=${String(e.cc_obj_shape(s)).padStart(3)} ` +
    `tile=${String(e.cc_obj_tile(s)).padStart(3)} p10=${String(e.cc_obj_p10(s)).padStart(3)} ` +
    `p18=${String(e.cc_obj_p18(s)).padStart(5)} ready=${e.cc_obj_ready(s)} vx=${e.cc_obj_vx(s)}`;
};
  console.log(`level ${level}: active slots ${slots.join(", ")} cursor=${e.cc_obj_cursor()}`);
  console.log("frame 0:");
  for (const s of slots) console.log("  " + row(s));

  for (let f = 1; f <= frames; f++) {
    e.cc_objects_step();
    if (f <= 5 || f % 10 === 0 || f === frames) {
      console.log(`frame ${f}:`);
      for (const s of slots) console.log("  " + row(s));
    }
  }
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
