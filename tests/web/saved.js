/* saved.js - the playground's saved chips: a circuit saved for later stays in
 * this browser apart from the circuit on screen, and placing it in another
 * drawing (after New, or another example) gives a circuit that works.
 *
 *   node tests/web/saved.js build/wire
 *
 * The playground's script runs in a plain vm as in draw.js, with build/wire
 * standing in for the WebAssembly compiler and a real localStorage. */
"use strict";
const fs = require("fs");
const os = require("os");
const path = require("path");
const vm = require("vm");
const { spawnSync } = require("child_process");
const WireSim = require("../../src/wire_sim.js");

const root = path.join(__dirname, "..", "..");
const wireExe = process.argv[2];
if (!wireExe) { console.error("usage: node tests/web/saved.js build/wire"); process.exit(2); }
const run = (args, cwd) => spawnSync(path.resolve(wireExe), args, { cwd, encoding: "utf8", maxBuffer: 1 << 28 });

/* ---- the page around the playground (as in draw.js) */
const html = fs.readFileSync(path.join(root, "src", "playground.html"), "utf8");
const script = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]).sort((a, b) => b.length - a.length)[0];
const stub = () => {
  const store = { value: "", textContent: "", innerHTML: "", hidden: false, dataset: {}, style: {}, options: [] };
  return new Proxy(function () {}, {
    get(t, k) {
      if (k === Symbol.iterator) return function* () {};
      if (k === Symbol.toPrimitive) return () => "";
      if (k in store) return store[k];
      return stub();
    },
    set(t, k, v) { store[k] = v; return true; },
    apply: () => stub(), construct: () => stub()
  });
};
const els = new Map();
const el = id => { if (!els.has(id)) els.set(id, stub()); return els.get(id); };
const document = new Proxy({}, { get(t, k) {
  if (k === "getElementById") return el;
  if (k === "querySelectorAll") return () => [];
  return stub();
} });
const kept = new Map();
let answer = "";               /* what prompt() is answered with */
const ctx = {
  document, console, setTimeout: () => 0, clearTimeout() {}, setInterval: () => 0, clearInterval() {},
  matchMedia: () => ({ matches: false, addEventListener() {}, addListener() {} }),
  fetch: () => new Promise(() => {}), WireCompiler: () => new Promise(() => {}),
  localStorage: { getItem: k => kept.has(k) ? kept.get(k) : null, setItem: (k, v) => kept.set(k, String(v)), removeItem: k => kept.delete(k) },
  location: { href: "http://localhost/playground.html", hash: "", search: "" },
  history: { replaceState() {} }, addEventListener() {}, removeEventListener() {},
  navigator: { clipboard: {} }, URL: class extends URL { static createObjectURL() { return "blob:x"; } static revokeObjectURL() {} }, URLSearchParams, Blob: function () {}, Option: function () { return stub(); },
  requestAnimationFrame: () => 0, confirm: () => true, alert() {}, prompt: () => answer
};
ctx.window = ctx; ctx.self = ctx; ctx.parent = ctx;
vm.createContext(ctx);
vm.runInContext(script + `
;globalThis.__t = {
  netlistToModel, wireDefs, genCode, saveChip, placeSaved, readLibrary, STARTER_DRAWING,
  get model() { return model; }, set model(m) { model = m; },
  set mode(m) { mode = m; }, set dir(d) { dir = d; },
  get library() { return library; }, msg: () => msg.textContent
};`, ctx, { filename: "playground.html" });
const P = ctx.__t;
/* the compiler: build/wire on the circuit, written where its imports are */
ctx.wire = async (args, mainPath, mainText) => {
  const full = path.join(root, mainPath), tmp = path.join(path.dirname(full), `.saved_${process.pid}_` + path.basename(full));
  fs.mkdirSync(path.dirname(full), { recursive: true });
  fs.writeFileSync(tmp, mainText);
  const out = path.join(os.tmpdir(), `saved_${process.pid}.out`);
  const a = args.map(x => x === mainPath ? path.basename(tmp) : x === "/out.vhd" ? out : x);
  const r = run(a, path.dirname(full));
  fs.unlinkSync(tmp);
  return { code: r.status, out: r.stdout, err: r.stderr.split(path.basename(tmp)).join(mainPath),
           read: p => { try { return fs.readFileSync(p === "/out.vhd" ? out : p, "utf8"); } catch (e) { return null; } } };
};

let failed = 0;
const check = (ok, what) => { if (!ok) { console.log("saved: " + what); failed++; } };
function open(file) {   /* an example, as Draw shows it */
  const dir = path.dirname(path.join(root, file)), base = path.basename(file);
  const j = run(["--json", "--keep-unused", base], dir);
  const wtext = base.endsWith(".vhd") ? run(["--to-wire", base], dir).stdout : fs.readFileSync(path.join(root, file), "utf8");
  const m = P.netlistToModel(JSON.parse(j.stdout), null, P.wireDefs(wtext), path.dirname(file));
  if (m.why) throw new Error(file + ": " + m.why);
  return m.model;
}
const mask = w => w >= 32 ? 0xFFFFFFFF : ((1 << w) >>> 0) - 1;
async function netlist(m) {
  const r = await ctx.wire(["--json", "t.wire"], "t.wire", P.genCode(m));
  if (r.code !== 0) throw new Error(r.err.trim().split("\n")[0]);
  return JSON.parse(r.out);
}
/* the main sheet: inputs in, the chip, outputs out, named after its pins */
function wireUp(m, chip) {
  for (const [n, w] of chip.ins) {
    const id = m.next++;
    m.parts.push({ id, kind: "input", name: "i_" + n, x: 0, y: 0, bits: w });
    m.links.push({ from: { id, pin: "out" }, to: { id: chip.id, pin: n } });
  }
  for (const [n, w] of chip.outs) {
    const id = m.next++;
    m.parts.push({ id, kind: "output", name: "o_" + n, x: 0, y: 0, bits: w });
    m.links.push({ from: { id: chip.id, pin: n }, to: { id, pin: "in" } });
  }
}
function sim(D, set) {
  const s = WireSim.createSync(D);
  D.nets.forEach((n, i) => { s.setV(i, 0); s.setK(i, mask(n.w)); });
  s.simulate();
  const node = name => D.nodes.findIndex(n => n.inst === 0 && n.name === name);
  for (const [k, v] of Object.entries(set)) s.setState(node(k), v);
  s.simulate();
  return name => s.v(D.nodes[node(name)].in[0]);
}

(async () => {
  /* 1. an example with an imported chip and hex lights, saved as a chip */
  P.model = open("examples/notes/addsub8.wire"); P.mode = "draw";
  ctx.document.getElementById("name").value = "addsub8_demo";
  answer = "my_alu";
  await P.saveChip();
  const e = P.library.my_alu;
  check(e, "addsub8.wire was not saved: " + P.msg());
  if (!e) { console.log("saved: stopped"); process.exit(1); }
  check(!e.top.parts.some(p => ["hex", "led", "button", "clock"].includes(p.kind)), "the saved chip still has lights inside");
  check(e.top.parts.some(p => p.kind === "output" && p.name === "s_hi" && p.bits === 4), "s_hi did not become a 4-bit output");
  check(e.chips.addsub8, "the imported chip addsub8 was not drawn into the saved chip");
  check(JSON.parse(kept.get("wire-saved-chips")).chips.my_alu, "the saved chip is not in localStorage");

  /* 2. New, then the saved chip dragged in: it works on its own */
  P.model = P.STARTER_DRAWING(); P.model.parts = []; P.model.links = [];
  P.readLibrary();   /* as a reload would */
  P.placeSaved("my_alu", 100, 100);
  let chip = P.model.parts.find(p => p.kind === "chip");
  check(chip && chip.comp === "my_alu", "placing the saved chip put no chip in the drawing");
  wireUp(P.model, chip);
  const D = await netlist(P.model);
  let s = 0x2545f491;
  const rnd = () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; return s >>>= 0; };
  for (let k = 0; k < 40; k++) {
    const a = rnd() & 255, b = rnd() & 255, su = rnd() & 1;
    const out = sim(D, { i_a: a, i_b: b, i_su: su }), want = (su ? a - b : a + b) & 255;
    if (out("o_s") !== want) { check(false, `my_alu: ${a} ${su ? "-" : "+"} ${b} gave ${out("o_s")}, not ${want}`); break; }
    if (out("o_s_hi") !== want >> 4) { check(false, `my_alu: s_hi is ${out("o_s_hi")}, not ${want >> 4}`); break; }
  }

  /* 3. placed again: the same chips, not copies */
  const before = Object.keys(P.model.chips).length;
  P.placeSaved("my_alu", 300, 100);
  check(Object.keys(P.model.chips).length === before, "placing it twice copied its chips again");

  /* 4. a drawing with another chip of the same name: the saved one comes in renamed */
  P.model = P.STARTER_DRAWING();
  P.model.chips = { addsub8: { next: 3, parts: [{ id: 1, kind: "input", name: "a", x: 0, y: 0, bits: 1 },
    { id: 2, kind: "output", name: "y", x: 100, y: 0, bits: 1 }], links: [{ from: { id: 1, pin: "out" }, to: { id: 2, pin: "in" } }] } };
  P.placeSaved("my_alu", 100, 200);
  chip = P.model.parts.find(p => p.kind === "chip");
  check(P.model.chips.addsub8.parts.length === 2, "the drawing's own addsub8 was overwritten");
  check(P.model.chips.addsub8_2 && P.model.chips.my_alu.parts.some(p => p.comp === "addsub8_2"), "the saved addsub8 did not come in as addsub8_2");
  try { await netlist(P.model); } catch (err) { check(false, "with a clashing name the circuit does not compile: " + err.message); }

  /* 5. written VHDL saved, then used in a drawing */
  P.mode = "write"; P.dir = "examples/notes";
  ctx.document.getElementById("name").value = "full_adder";
  ctx.document.getElementById("src").value = fs.readFileSync(path.join(root, "examples/notes/full_adder.vhd"), "utf8");
  answer = "fa";
  await P.saveChip();
  check(P.library.fa, "full_adder.vhd was not saved: " + P.msg());
  check(P.library.my_alu, "saving another chip lost the first");
  P.mode = "draw"; P.dir = "";
  P.model = P.STARTER_DRAWING(); P.model.parts = []; P.model.links = [];
  P.placeSaved("fa", 0, 0);
  chip = P.model.parts.find(p => p.kind === "chip");
  wireUp(P.model, chip);
  try {
    const F = await netlist(P.model);
    const names = chip.ins.map(x => x[0]), outs = chip.outs.map(x => x[0]);
    for (let v = 0; v < 8; v++) {
      const set = {}; names.forEach((n, i) => { set["i_" + n] = (v >> i) & 1; });
      const out = sim(F, set), total = (v & 1) + ((v >> 1) & 1) + ((v >> 2) & 1);
      const got = outs.map(n => out("o_" + n));
      if (!got.includes(total & 1) || !got.includes(total >> 1)) { check(false, `fa: inputs ${v.toString(2)} gave ${got}`); break; }
    }
  } catch (err) { check(false, "the saved VHDL chip does not compile in a drawing: " + err.message); }

  /* 6. a name of Wire's own is refused */
  answer = "and";
  await P.saveChip();
  check(!P.library.and, "a chip called 'and' was saved");

  if (failed) { console.log(`saved: ${failed} checks failed`); process.exit(1); }
  console.log(`saved: ${Object.keys(P.library).join(", ")} saved;`, "kept apart from the circuit, they work wherever they are placed");
  process.exit(0);
})().catch(e => { console.log("saved: " + (e.stack || e)); process.exit(1); });
