/* cosim.js - checks that a Wire circuit and its VHDL describe the same thing.
 *
 *   node tests/vhdl/cosim.js build/wire [file.wire ...] [--steps N] [--keep]
 *
 * With no files it takes every .wire under examples/ that has a .vhd next
 * to it. For each one it reads the ports of the VHDL entity named like the
 * file, runs the Wire circuit on random inputs (and clock edges, if it has a
 * clock), and writes a VHDL testbench that applies the same inputs and
 * asserts the same outputs, before and after every rising edge. GHDL runs
 * it; any difference fails. Unknown bits in Wire are don't-cares.
 *
 * In the .wire file:
 *   # vhdl ports: hexin=hex          a Wire name that is a VHDL port of another name
 *   # test: steps=400 reset=0.02 start=0.05     how many steps; how often a
 *                                    1-bit input is 1 (default: flips 30% of steps)
 *   # test: hold start=8             keep an input at its value for 8 steps
 *   # test: apart s r                never 1 at the same time
 * The input called reset (or clr) is 1 for the first two steps. */
"use strict";
const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync, spawnSync } = require("child_process");
const WireSim = require("../../src/wire_sim.js");

const args = process.argv.slice(2);
const wire = args[0];
if (!wire) { console.error("usage: node tests/vhdl/cosim.js build/wire [file.wire ...] [--steps N] [--keep]"); process.exit(2); }
let STEPS = 300, KEEP = false;
const files = [];
for (let i = 1; i < args.length; i++) {
  if (args[i] === "--steps") STEPS = +args[++i];
  else if (args[i] === "--keep") KEEP = true;
  else files.push(args[i]);
}
const root = path.join(__dirname, "..", "..");
if (!files.length) {
  const walk = d => fs.readdirSync(d, { withFileTypes: true }).flatMap(e =>
    e.isDirectory() ? (e.name === "lib" ? [] : walk(path.join(d, e.name))) : e.name.endsWith(".wire") ? [path.join(d, e.name)] : []);
  for (const f of walk(path.join(root, "examples")).sort())
    if (fs.existsSync(f.replace(/\.wire$/, ".vhd"))) files.push(path.relative(process.cwd(), f));
}
const ghdl = spawnSync("ghdl", ["--version"]).status === 0;
if (!ghdl) { console.log("cosim: ghdl not found, skipping the VHDL checks"); process.exit(0); }

const mask = w => w >= 32 ? 0xFFFFFFFF : ((1 << w) >>> 0) - 1;
function rng(seed) {
  let s = seed >>> 0 || 1;
  return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; };
}

/* the ports of entity `name`: [{ name, dir, w }] (w = 0 for std_logic) */
function vhdlPorts(text, name) {
  const t = text.replace(/--[^\n]*/g, "");
  const m = new RegExp(`entity\\s+${name}\\s+is([\\s\\S]*?)end\\b`, "i").exec(t);
  if (!m) throw new Error(`no entity ${name}`);
  const generics = {};
  const g = /generic\s*\(([\s\S]*?)\)\s*;/i.exec(m[1]);
  if (g) for (const d of g[1].split(";")) {
    const gm = /(\w+)\s*:\s*\w+\s*:=\s*(\d+)/.exec(d);
    if (gm) generics[gm[1].toLowerCase()] = +gm[2];
  }
  const p = /port\s*\(([\s\S]*)\)\s*;\s*$/i.exec(m[1].trim());
  if (!p) throw new Error(`entity ${name} has no ports`);
  const ports = [];
  const ev = e => Function(...Object.keys(generics), `return (${e.replace(/\*\*/g, "**")});`)(...Object.values(generics));
  for (const decl of p[1].split(";")) {
    const dm = /^\s*([\w\s,]+?)\s*:\s*(in|out|inout|buffer)\s+(\w+)\s*(?:\(\s*(.+?)\s+(downto|to)\s+(.+?)\s*\))?\s*$/i.exec(decl.trim());
    if (!dm) { if (decl.trim()) throw new Error(`cannot read port "${decl.trim()}"`); continue; }
    let w = 0;
    if (dm[4]) w = Math.abs(ev(dm[4].toLowerCase()) - ev(dm[6].toLowerCase())) + 1;
    for (const n of dm[1].split(",").map(x => x.trim()).filter(Boolean)) ports.push({ name: n, dir: dm[2].toLowerCase(), w, type: dm[3].toLowerCase() });
  }
  return ports;
}

const lit = (w, v, k) => {
  if (!w) return (k & 1) ? `'${v & 1}'` : "'-'";
  let s = "";
  for (let b = w - 1; b >= 0; b--) s += (k >>> b) & 1 ? (v >>> b) & 1 : "-";
  return `"${s}"`;
};

let failed = 0;
for (const file of files) {
  const vfile = file.replace(/\.wire$/, ".vhd"), top = path.basename(file, ".wire");
  const src = fs.readFileSync(file, "utf8"), vtext = fs.readFileSync(vfile, "utf8");
  const fail = msg => { failed++; console.log(`FAIL ${file}\n  ${msg}`); };
  let ports;
  try { ports = vhdlPorts(vtext, top); } catch (e) { fail(`${vfile}: ${e.message}`); continue; }
  /* options from the .wire file */
  const rename = {}, prob = {}, hold = {}, apart = [];
  let steps = STEPS;
  for (const m of src.matchAll(/^#\s*vhdl ports:(.*)$/gm)) for (const kv of m[1].trim().split(/\s+/)) { const [a, b] = kv.split("="); rename[a] = b; }
  for (const m of src.matchAll(/^#\s*test:(.*)$/gm)) {
    let h = false;
    if (/^\s*apart\s/.test(m[1])) { apart.push(m[1].trim().split(/\s+/).slice(1)); continue; }
    for (const kv of m[1].trim().split(/\s+/)) {
      if (kv === "hold") { h = true; continue; }
      const [a, b] = kv.split("=");
      if (a === "steps") steps = +b; else if (h) hold[a] = +b; else prob[a] = +b;
    }
  }
  let D;
  try { D = JSON.parse(execFileSync(wire, ["--json", file], { stdio: ["ignore", "pipe", "pipe"] }).toString()); }
  catch (e) { fail(String(e.stderr || e.message).trim()); continue; }
  const vname = n => rename[n] || n;
  const top0 = D.nodes.map((n, j) => ({ n, j })).filter(x => x.n.inst === 0);
  const wins = top0.filter(x => ["input", "button"].includes(x.n.kind)).map(x => ({ j: x.j, name: vname(x.n.name), w: D.nets[x.n.out[0]].w }));
  const clocks = top0.filter(x => x.n.kind === "clock").map(x => ({ j: x.j, name: vname(x.n.name) }));
  const wouts = top0.filter(x => ["output", "led", "hex", "seg7"].includes(x.n.kind)).map(x => ({ net: x.n.in[0], name: vname(x.n.name), w: D.nets[x.n.in[0]].w }));
  /* every VHDL input must be a Wire input or clock; outputs are compared where both have them */
  let bad = null;
  const ins = [], outs = [];
  for (const p of ports) {
    if (p.dir === "in") {
      const c = clocks.find(c => c.name === p.name);
      const i = wins.find(i => i.name === p.name);
      if (c) { if (p.w) bad = `${p.name} is a clock but ${p.w} bits in VHDL`; continue; }
      if (!i) { bad = `the VHDL input ${p.name} is not an input of the Wire circuit`; break; }
      if ((i.w === 1 ? 0 : i.w) !== p.w && !(i.w === 1 && p.w === 1)) { bad = `${p.name} is ${i.w} bit(s) in Wire, ${p.w || 1} in VHDL`; break; }
      ins.push({ ...i, vw: p.w });
    } else {
      const o = wouts.find(o => o.name === p.name);
      if (!o) continue;
      if ((o.w === 1 ? 0 : o.w) !== p.w && !(o.w === 1 && p.w === 1)) { bad = `${p.name} is ${o.w} bit(s) in Wire, ${p.w || 1} in VHDL`; break; }
      outs.push({ ...o, vw: p.w });
    }
  }
  if (!bad && !outs.length) bad = "no outputs to compare (names differ?)";
  if (bad) { fail(bad); continue; }

  /* run Wire, writing the testbench as we go */
  const sim = WireSim.createSync(D);
  D.nets.forEach((n, i) => { sim.setV(i, 0); sim.setK(i, mask(n.w)); });
  const rand = rng(0x51ab ^ top.length * 7919);
  const val = {}, held = {};
  ins.forEach(i => { val[i.name] = 0; held[i.name] = 0; });
  const isReset = n => /^(reset|clr|rst)$/i.test(n);
  const body = [];
  const check = (when, step) => {
    for (const o of outs) {
      const v = sim.v(o.net), k = sim.k(o.net);
      if (!k) continue;
      body.push(`    assert std_match(${o.name}, ${lit(o.vw, v, k)}) report "step ${step} ${when}: ${o.name} is " & to_string(${o.name}) & ", Wire says ${lit(o.vw, v, k).replace(/"/g, "")}" severity error;`);
    }
  };
  for (let step = 0; step < steps; step++) {
    for (const i of ins) {
      if (held[i.name] > 0) { held[i.name]--; continue; }
      let v = val[i.name];
      if (isReset(i.name)) v = step < 2 ? 1 : rand() < (prob[i.name] ?? 0.01) ? 1 : 0;
      else if (i.w === 1) v = prob[i.name] !== undefined ? (rand() < prob[i.name] ? 1 : 0) : (rand() < 0.3 ? v ^ 1 : v);
      else if (rand() < (prob[i.name] ?? 0.4)) v = Math.floor(rand() * 2 ** i.w) >>> 0;
      if (v !== val[i.name] && hold[i.name]) held[i.name] = hold[i.name];
      val[i.name] = v;
    }
    for (const g of apart) { let one = false; for (const n of g) if (val[n]) { if (one) val[n] = 0; one = true; } }
    for (const i of ins) sim.setState(i.j, val[i.name]);
    sim.simulate();
    body.push(`    -- step ${step}`);
    body.push("    " + ins.map(i => `${i.name} <= ${lit(i.vw, val[i.name], mask(i.w))};`).join(" ") + " wait for 1 ns;");
    check("before the clock", step);
    if (clocks.length) {
      clocks.forEach(c => sim.setState(c.j, 1)); sim.simulate();
      body.push(`    ${clocks.map(c => `${c.name} <= '1';`).join(" ")} wait for 1 ns;`);
      check("after the rising edge", step);
      clocks.forEach(c => sim.setState(c.j, 0)); sim.simulate();
      body.push(`    ${clocks.map(c => `${c.name} <= '0';`).join(" ")} wait for 1 ns;`);
    }
  }
  const sig = p => p.vw ? `std_logic_vector(${p.vw - 1} downto 0)` : "std_logic";
  const tb = `library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity cosim_tb is end cosim_tb;
architecture sim of cosim_tb is
${clocks.map(c => `  signal ${c.name} : std_logic := '0';`).join("\n")}
${ins.map(i => `  signal ${i.name} : ${sig(i)} := ${i.vw ? "(others => '0')" : "'0'"};`).join("\n")}
${outs.map(o => `  signal ${o.name} : ${sig(o)};`).join("\n")}
begin
  dut: entity work.${top} port map(${[...clocks.map(c => c.name), ...ins.map(i => i.name), ...outs.map(o => o.name)].map(n => `${n} => ${n}`).join(", ")});
  process
  begin
${body.join("\n")}
    report "cosim: done" severity note;
    wait;
  end process;
end sim;
`;
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "cosim-"));
  fs.writeFileSync(path.join(dir, "tb.vhd"), tb);
  const run = (a, opts) => spawnSync("ghdl", a, { cwd: dir, encoding: "utf8", ...opts });
  let r = run(["-a", "--std=08", "-frelaxed", path.resolve(vfile), "tb.vhd"]);
  if (r.status !== 0) { fail(`GHDL could not read ${vfile}:\n  ${(r.stderr || r.stdout).trim().split("\n").slice(0, 6).join("\n  ")}`); continue; }
  r = run(["--elab-run", "--std=08", "-frelaxed", "cosim_tb", "--ieee-asserts=disable"]);
  const out = (r.stdout || "") + (r.stderr || "");
  const errs = out.split("\n").filter(l => /\(assertion error\)|error:/.test(l));
  if (!/cosim: done/.test(out) || errs.length) {
    fail(`${errs.length} difference(s) between Wire and VHDL; the first:\n  ${(errs[0] || out.trim().split("\n").slice(-3).join("\n  ")).replace(/^.*\(assertion error\):\s*/, "")}` + (KEEP ? `\n  testbench: ${dir}/tb.vhd` : ""));
    continue;
  }
  if (!KEEP) fs.rmSync(dir, { recursive: true, force: true });
  console.log(`ok   ${file}: ${steps} steps, ${outs.length} output(s) agree with ${path.basename(vfile)}${clocks.length ? "" : " (no clock)"}`);
}
if (failed) { console.log(`${failed} of ${files.length} examples differ from their VHDL`); process.exit(1); }
console.log(`all ${files.length} examples agree with their VHDL`);
