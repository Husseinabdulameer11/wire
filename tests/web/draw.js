/* draw.js - does every circuit open in the playground's Draw mode, and
 * does the drawing work like the circuit it was made from?
 *
 *   node tests/web/draw.js build/wire [file.wire|file.vhd ...]
 *
 * With no files it takes every example and every circuit under tests/ that
 * Wire reads (.wire and .vhd). For each, it does what the playground does when
 * you switch to Draw: the netlist (wire --json) goes through the playground's
 * own netlistToModel, and the drawing back through its genCode. That Wire must
 * pass wire --check, and the two circuits get the same random inputs and clock
 * ticks: every output must agree, floating (z) and unknown bits too.
 *
 * The playground's script runs here in a plain vm, with a stand-in for the
 * page around it: no browser and no packages needed. */
"use strict";
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const { spawnSync } = require("child_process");
const WireSim = require("../../src/wire_sim.js");

const root = path.join(__dirname, "..", "..");
const wire = process.argv[2];
if (!wire) { console.error("usage: node tests/web/draw.js build/wire [file ...]"); process.exit(2); }
const walk = d => fs.readdirSync(d, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name)).flatMap(e =>
  e.isDirectory() ? walk(path.join(d, e.name)) : /\.(wire|vhd)$/.test(e.name) ? [path.join(d, e.name)] : []);
let files = process.argv.slice(3);
if (!files.length) files = ["examples", "tests/sim", "tests/vhdl_in", "tests/vhdl"].map(d => path.join(root, d)).filter(fs.existsSync).flatMap(walk);

/* ---- the playground, in a vm ---------------------------------------------
 * Anything it asks the page for is a stand-in that takes every call and
 * property; the file name box holds a real value (the circuit is named after it). */
const html = fs.readFileSync(path.join(root, "src", "playground.html"), "utf8");
const script = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]).sort((a, b) => b.length - a.length)[0];
const stub = () => {
  const f = function () {};
  const store = { value: "", textContent: "", innerHTML: "", hidden: false, dataset: {}, style: {}, options: [] };
  return new Proxy(f, {
    get(t, k) {
      if (k === Symbol.iterator) return function* () {};
      if (k === Symbol.toPrimitive) return () => "";
      if (k in store) return store[k];
      return stub();
    },
    set(t, k, v) { store[k] = v; return true; },
    apply: () => stub(),
    construct: () => stub()
  });
};
const els = new Map();
const document = new Proxy({}, {
  get(t, k) {
    if (k === "getElementById") return id => { if (!els.has(id)) els.set(id, stub()); return els.get(id); };
    if (k === "querySelectorAll") return () => [];
    return stub();
  }
});
const ctx = {
  document, console, setTimeout: () => 0, clearTimeout() {}, setInterval: () => 0, clearInterval() {},
  matchMedia: () => ({ matches: false, addEventListener() {}, addListener() {} }),
  fetch: () => new Promise(() => {}), WireCompiler: () => new Promise(() => {}),
  localStorage: { getItem: () => null, setItem() {}, removeItem() {} },
  location: { href: "http://localhost/playground.html", hash: "", search: "" },
  history: { replaceState() {} }, addEventListener() {}, removeEventListener() {},
  navigator: { clipboard: {} }, URL, URLSearchParams, Blob: function () {}, Option: function () { return stub(); },
  requestAnimationFrame: () => 0, confirm: () => false, alert() {}
};
ctx.window = ctx; ctx.self = ctx; ctx.parent = ctx;
vm.createContext(ctx);
vm.runInContext(script + "\n;globalThis.__draw = { netlistToModel, wireDefs, genCode };", ctx, { filename: "playground.html" });
const P = ctx.__draw;

/* ---- the two circuits, side by side --------------------------------------- */
const mask = w => w >= 32 ? 0xFFFFFFFF : ((1 << w) >>> 0) - 1;
function compare(A, B) {
  const mk = D => { const s = WireSim.createSync(D); D.nets.forEach((n, i) => { s.setV(i, 0); s.setK(i, mask(n.w)); }); s.simulate(); return s; };
  const a = mk(A), b = mk(B);
  /* the circuit's inputs and outputs, and those of every chip not used yet
   * (it is in the netlist, with inputs of its own, to be tried on its own) */
  const where = (D, n) => n.inst === 0 ? "" : D.insts[n.inst].parent === -2 ? D.insts[n.inst].comp + "." : null;
  const top = (D, kinds) => D.nodes.map((n, j) => ({ n, j, at: where(D, n) })).filter(x => x.at != null && kinds.includes(x.n.kind) && (x.at === "" || x.n.kind !== "pin_in"));
  const pair = kinds => top(A, kinds).map(x => ({ x, y: top(B, kinds).find(y => y.at === x.at && y.n.name === x.n.name) }));
  const ins = pair(["input", "button", "keypad"]), outs = pair(["output", "led", "hex", "seg7", "pin_out"]).filter(p => p.x.at === "" || p.x.n.kind === "pin_out");
  const lost = [...ins, ...outs].filter(p => !p.y);
  if (lost.length) return `the drawing has no ${lost.map(p => p.x.at + p.x.n.name).join(", ")}`;
  const clocked = A.nodes.some(n => n.kind === "clock");
  let s = 0x2545f491;
  const rnd = () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; return s >>>= 0; };
  const val = (sim, D, net) => sim.k(net) === mask(D.nets[net].w) ? String(sim.v(net)) : `${sim.v(net)} (known ${sim.k(net)})`;
  for (let step = 0; step < 300; step++) {
    if (clocked && step % 3 === 2) { a.toggle(1); b.toggle(1); }
    else if (ins.length) {
      const p = ins[rnd() % ins.length], w = p.x.n.kind === "keypad" ? 5 : A.nets[p.x.n.out[0]].w, v = rnd() & mask(w);
      a.setState(p.x.j, v); b.setState(p.y.j, v); a.simulate(); b.simulate();
    }
    for (const p of outs) {
      const va = val(a, A, p.x.n.in[0]), vb = val(b, B, p.y.n.in[0]);
      if (va !== vb) return `after ${step + 1} steps ${p.x.at}${p.x.n.name} is ${va}, but ${vb} in the drawing`;
    }
  }
  return null;
}

const run = (args, cwd) => spawnSync(path.resolve(wire), args, { cwd, encoding: "utf8", maxBuffer: 1 << 28 });
let checked = 0, failed = 0;
for (const f of files) {
  const dir = path.dirname(path.resolve(f)), base = path.basename(f), rel = path.relative(root, path.resolve(f));
  const j = run(["--json", "--keep-unused", base], dir);
  if (j.status !== 0) continue;   /* an error test, or VHDL Wire does not read: not a circuit to draw */
  checked++;
  const bad = why => { console.log(`draw: ${rel}: ${why}`); failed++; };
  /* the chips as Wire: the file itself, or the Wire its VHDL is turned into */
  const wtext = base.endsWith(".vhd") ? run(["--to-wire", base], dir).stdout : fs.readFileSync(f, "utf8");
  els.get("name").value = base.replace(/\.\w+$/, "");
  let m;
  try { m = P.netlistToModel(JSON.parse(j.stdout), null, P.wireDefs(wtext), ""); }
  catch (e) { bad(`the playground failed: ${e.message}`); continue; }
  if (m.why) { bad(m.why); continue; }
  const tmp = path.join(dir, `.draw_${process.pid}.wire`);
  fs.writeFileSync(tmp, P.genCode(m.model));
  const back = run(["--json", "--keep-unused", path.basename(tmp)], dir);
  fs.unlinkSync(tmp);
  if (back.status !== 0) { bad(`the drawing's Wire does not work: ${back.stderr.trim().split("\n")[0].split(path.basename(tmp) + ":").join("line ")}`); continue; }
  const diff = compare(JSON.parse(j.stdout), JSON.parse(back.stdout));
  if (diff) bad(diff);
}
if (failed) { console.log(`draw: ${failed} of ${checked} circuits do not open in Draw as they are`); process.exit(1); }
console.log(`draw: all ${checked} circuits open in Draw and work the same there`);
