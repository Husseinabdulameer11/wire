/* inside.js - the inside of every built-in part does what the part does.
 *
 *   node tests/sim/inside.js build/wire
 *
 * For each part that opens (wire_inside.js), in many sizes and with every
 * option: run the circuit a while, then do what the page does when you open
 * the part (add its inside, make a new simulator, copy the state across,
 * set the inside to what the part holds) and run both on the same random
 * inputs, clock edges and resets, checking after every step that each
 * output of the inside equals that output of the part. Then open every
 * part inside the inside too, and so on down to the gates. */
"use strict";
const { execFileSync } = require("child_process");
const fs = require("fs"), os = require("os"), path = require("path");
const WireSim = require("../../src/wire_sim.js");
const WireInside = require("../../src/wire_inside.js");

const wire = process.argv[2] || "build/wire";
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "wireinside-"));
const mask = w => w >= 32 ? 0xFFFFFFFF : 2 ** w - 1;

const C = [];
const add = (name, body) => C.push({ name, src: `clock clk\nbutton rst\n${body}\n` });
for (const w of [1, 2, 4, 8, 32]) {
  add(`add${w}`, `input a[${w}], b[${w}]\ns = add(a, b)\noutput y[${w}], co\ny = s\nco = s.cout`);
  add(`addc${w}`, `input a[${w}], b[${w}], c\ns = add(a, b, cin = c)\noutput y[${w}], co\ny = s\nco = s.cout`);
  add(`cmp${w}`, `input a[${w}], b[${w}]\nc = compare(a, b)\noutput l, e, g\nl = c.lt\ne = c.eq\ng = c.gt`);
  add(`dff${w}`, `input d[${w}]\nq = dff(d, clk)\noutput y[${w}], ny[${w}]\ny = q\nny = q.nq`);
  add(`dffer${w}`, `input d[${w}], e\nq = dff(d, clk, en = e, rst = rst)\noutput y[${w}], ny[${w}]\ny = q\nny = q.nq`);
  add(`dffr${w}`, `input d[${w}]\nq = dff(d, clk, rst = rst)\noutput y[${w}]\ny = q`);
  add(`mux2_${w}`, `input a[${w}], b[${w}], s\noutput y[${w}]\ny = mux(a, b, sel = s)`);
  add(`mux8_${w}`, `input a[${w}], b[${w}], c[${w}], d[${w}], s[3]\noutput y[${w}]\ny = mux(a, b, c, d, b, a, d, c, sel = s)`);
  add(`demux${w}`, `input d[${w}], s[2]\nm = demux(d, sel = s)\noutput o0[${w}], o1[${w}], o2[${w}], o3[${w}]\no0 = m[0]\no1 = m[1]\no2 = m[2]\no3 = m[3]`);
}
for (const w of [1, 3, 4, 8, 16]) {
  add(`ctr${w}`, `c = counter(clk, bits = ${w})\noutput y[${w}], cy\ny = c\ncy = c.carry`);
  add(`ctrall${w}`, `input e, l, dn, d[${w}]\nc = counter(clk, en = e, rst = rst, load = l, d = d, down = dn)\noutput y[${w}], cy\ny = c\ncy = c.carry`);
  add(`ctren${w}`, `input e, dn\nc = counter(clk, bits = ${w}, en = e, down = dn)\noutput y[${w}], cy\ny = c\ncy = c.carry`);
}
for (const s of [1, 2, 3, 5]) {
  add(`dec${s}`, `input s[${s}]\noutput y[${2 ** s}]\ny = decoder(s)`);
  add(`dece${s}`, `input s[${s}], e\noutput y[${2 ** s}]\ny = decoder(s, en = e)`);
  add(`enc${s}`, `input x[${2 ** s}]\ne = encoder(x)\noutput y[${s}], v\ny = e\nv = e.valid`);
}
add("tff", `input t\nq = tff(t, clk)\noutput y, ny\ny = q\nny = q.nq`);
add("tffr", `input t\nq = tff(t, clk, rst = rst)\noutput y\ny = q`);
add("jkff", `input j, k\nq = jkff(j, k, clk)\noutput y, ny\ny = q\nny = q.nq`);
add("jkffr", `input j, k\nq = jkff(j, k, clk, rst = rst)\noutput y\ny = q`);

/* a simple random source: the same every run */
let seed = 12345;
const rnd = () => { seed = (Math.imul(seed, 1103515245) + 12345) >>> 0; return seed; };

function fresh(D) {
  const s = WireSim.createSync(D);
  D.nets.forEach((n, i) => { s.setV(i, 0); s.setK(i, mask(n.w)); });
  return s;
}
/* one step: new inputs (rst now and then), settle, and maybe flip the
 * clock (so a part is opened with the clock high as well as low) */
function stimulus(D) {
  const st = [];
  D.nodes.forEach((n, j) => {
    if (n.kind === "input") st.push([j, rnd() & mask(D.nets[n.out[0]].w)]);
    if (n.kind === "button") st.push([j, rnd() % 13 === 0 ? 1 : 0]);
  });
  return { st, edge: rnd() % 3 !== 0 };
}
function step(s, stim) {
  stim.st.forEach(([j, v]) => s.setState(j, v));
  s.simulate();
  if (stim.edge) s.toggle(1);
}
/* what the page does when a part is opened */
function open(D, s, j) {
  const nNets = D.nets.length, nNodes = D.nodes.length;
  const D2 = JSON.parse(JSON.stringify(D));
  const e = WireInside.expand(D2, j);
  const s2 = WireSim.createSync(D2);
  D2.nets.forEach((n, i) => {
    if (i < nNets) { s2.setV(i, s.v(i)); s2.setK(i, s.k(i)); } else { s2.setV(i, 0); s2.setK(i, mask(n.w)); }
  });
  for (let i = 0; i < nNodes; i++) { s2.setState(i, s.state(i)); s2.setLastClk(i, s.lastClk(i)); }
  D.nodes.forEach((n, i) => { const a = s.words(i), b = s2.words(i); if (a) b.set(a); });
  for (const [what, x, v] of e.init(s2.state(j), s2.lastClk(j))) {
    if (what === "state") s2.setState(x, v);
    else if (what === "lastClk") s2.setLastClk(x, v);
    else { s2.setV(x, v); s2.setK(x, mask(D2.nets[x].w)); }
  }
  s2.settle();
  return { D2, s2, inst: e.inst };
}

let checked = 0, failed = 0;
function check(name, D, s, j, depth) {
  const n = D.nodes[j];
  const { D2, s2, inst } = open(D, s, j);
  const pins = new Map(D2.nodes.filter(x => x.inst === inst && x.kind === "pin_out").map(x => [x.name, x.in[0]]));
  const where = `${name}: ${n.kind}${n.name ? " " + n.name : ""}${depth ? ` (${depth} deep)` : ""}`;
  const same = when => {
    for (let i = 0; i < n.out.length; i++) {
      const o = n.out[i], p = pins.get(n.outNames[i]);
      if (p == null) { console.log(`FAIL ${where}: the inside has no ${n.outNames[i]}`); return false; }
      if (s2.v(o) !== s2.v(p) || s2.k(o) !== s2.k(p)) {
        console.log(`FAIL ${where}: ${n.outNames[i]} is ${s2.v(o)} (known ${s2.k(o)}) but inside ${s2.v(p)} (known ${s2.k(p)}) ${when}`);
        return false;
      }
    }
    return true;
  };
  if (!same("when opened")) { failed++; return; }
  for (let t = 0; t < 60; t++) {
    step(s2, stimulus(D2));
    if (!same(`after step ${t + 1}`)) { failed++; return; }
  }
  checked++;
  /* open the parts inside too (once per kind, to keep it quick) */
  if (depth < 4) {
    const seen = new Set();
    D2.nodes.forEach((m, k) => {
      if (m.inst !== inst || !WireInside.canOpen(m)) return;
      const key = m.kind + D2.nets[m.out[0]].w + m.inNames.join();
      if (seen.has(key)) return;
      seen.add(key);
      check(name, D2, s2, k, depth + 1);
    });
  }
}

for (const c of C) {
  const f = path.join(dir, c.name + ".wire");
  fs.writeFileSync(f, c.src);
  let D;
  try { D = JSON.parse(execFileSync(wire, ["--json", f], { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], maxBuffer: 1 << 28 })); }
  catch (e) { console.log(`FAIL ${c.name}: ${String(e.stderr || e.message).trim()}`); failed++; continue; }
  D.nodes.forEach((n, j) => {
    if (!WireInside.canOpen(n)) return;
    const s = fresh(D);
    s.settle();
    for (let t = 0; t < 1 + (rnd() % 40); t++) step(s, stimulus(D));
    check(c.name, D, s, j, 0);
  });
}
fs.rmSync(dir, { recursive: true, force: true });
if (failed) { console.log(`${failed} inside(s) differ from their part`); process.exit(1); }
console.log(`all ${checked} insides agree with their parts (${C.length} circuits, opened down to the gates)`);
