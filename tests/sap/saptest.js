/* saptest.js - runs programs on the SAP-2 and SAP-3 (Wire circuits, on
 * their microcode) and on a plain 8080 emulator (emu8080.js), and checks
 * that registers, flags, ports and all 64 KB of memory end up the same.
 *
 *   node tests/sap/saptest.js build/wire [--random N] [--seed S] [sap2|sap3]
 *
 * Programs: tests/sap/progs/*.asm (a line `; inputs: in1=3 in2=4 key=5`
 * sets the inputs; `; sap2` says it also runs on the SAP-2; `; runs
 * forever` leaves it out), the examples
 * in examples/sap/*.asm, and N random programs per computer (default 150),
 * written as assembly so a failure can be read. */
"use strict";
const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");
const WireSim = require("../../src/wire_sim.js");
const { assemble } = require("../../tools/asm8080.js");
const { Emu8080 } = require("./emu8080.js");

const args = process.argv.slice(2);
const wire = args[0];
if (!wire) { console.error("usage: node tests/sap/saptest.js build/wire [--random N] [--seed S] [sap2|sap3]"); process.exit(2); }
let NRAND = 150, SEED = 12345, COVER = false;
const only = [];
for (let i = 1; i < args.length; i++) {
  if (args[i] === "--random") NRAND = +args[++i];
  else if (args[i] === "--seed") SEED = +args[++i];
  else if (args[i] === "--coverage") COVER = true;
  else only.push(args[i]);
}
const root = path.join(__dirname, "..", "..");
const mask = w => w >= 32 ? 0xFFFFFFFF : ((1 << w) >>> 0) - 1;

/* ---- a Wire computer ---------------------------------------------------------- */
function machine(file, regs, where) {
  const D = JSON.parse(execFileSync(wire, ["--json", file], { stdio: ["ignore", "pipe", "inherit"] }).toString());
  /* a part by its path: "B" at the top, or "r.B" inside the chip named r */
  const top = (name, kinds) => {
    const parts = (where[name] || name).split(".");
    let inst = 0;
    for (const chip of parts.slice(0, -1)) {
      inst = D.insts.findIndex((x, i) => i > 0 && x.parent === inst && x.name === chip);
      if (inst < 0) throw new Error(`${file}: no chip called ${chip}`);
    }
    const last = parts[parts.length - 1];
    const j = D.nodes.findIndex(n => n.inst === inst && n.name === last && kinds.includes(n.kind));
    if (j < 0) throw new Error(`${file}: no ${kinds.join("/")} called ${where[name] || name}`);
    return j;
  };
  /* the SAP-3 keeps its flags in the flagreg chip f (s z ac p cy); the
   * SAP-2 in a top-level register fl (s z) */
  /* the flag register: S Z AC P CY (5 bits, SAP-3) or S Z (2 bits, SAP-2) */
  const fl = top("fl", ["dff"]), five = D.nets[D.nodes[fl].out[0]].w === 5;
  const reg = Object.fromEntries(regs.map(r => [r, top(r, ["dff"])]));
  const pc = top("pc", ["counter"]), mem = top("mem", ["ram"]);   /* where says the paths */
  const sp = regs.includes("D") ? top("sp", ["counter"]) : -1;
  const ports = [top("port3", ["dff"]), top("port4", ["dff"])];      /* the output ports */
  const ins = { in1: top("in1", ["input"]), in2: top("in2", ["input"]), key: top("key", ["keypad"]) };
  const halted = D.nodes.find(n => n.kind === "led" && n.name === "halted").in[0];
  const c = WireSim.compile(D);
  const module = new WebAssembly.Module(c.bytes);
  return {
    D,
    run(segments, inputs, maxCycles) {
      D.nodes[mem].init = segments;
      const sim = WireSim.instantiate(c, module, D);
      D.nets.forEach((n, i) => { sim.setV(i, 0); sim.setK(i, mask(n.w)); });
      for (const [k, v] of Object.entries(inputs)) sim.setState(ins[k], v);
      sim.simulate();
      let cycles = 0;
      while (cycles < maxCycles && !(sim.v(halted) === 1 && sim.k(halted) === 1)) { sim.toggle(2); cycles++; }
      const st = { halted: sim.v(halted) === 1, cycles, conflict: sim.conflict() };
      for (const r of regs) st[r] = sim.state(reg[r]);
      const f = sim.state(fl);
      if (five) { st.S = f >> 4 & 1; st.Z = f >> 3 & 1; st.AC = f >> 2 & 1; st.P = f >> 1 & 1; st.CY = f & 1; }
      else { st.S = f >> 1 & 1; st.Z = f & 1; }
      st.PC = sim.state(pc);
      if (sp >= 0) st.SP = sim.state(sp);
      st.port3 = sim.state(ports[0]); st.port4 = sim.state(ports[1]);
      st.mem = sim.words(mem);
      return st;
    }
  };
}

function emulate(segments, inputs, sap2, maxSteps) {
  const e = new Emu8080({ sap2 });
  segments.forEach(([at, words]) => e.load(words, at));
  e.in[1] = inputs.in1 || 0; e.in[2] = inputs.in2 || 0; e.in[3] = inputs.key || 0;
  let steps = 0;
  while (!e.halted && steps < maxSteps) { if (COVER) cover[sap2 ? 1 : 0][e.mem[e.pc]]++; e.step(); steps++; }
  const st = { halted: e.halted, steps };
  ["B", "C", "D", "E", "H", "L"].forEach((r, i) => { st[r] = e.r[i]; });
  st.A = e.r[7];
  st.S = e.s; st.Z = e.z; st.AC = e.ac; st.P = e.p; st.CY = e.cy;
  st.PC = e.pc; st.SP = e.sp; st.port3 = e.out[3]; st.port4 = e.out[4];
  st.mem = e.mem;
  return st;
}

function segmentsOf(res) {
  const segs = [];
  for (const l of res.lines) {
    if (!l.bytes.length) continue;
    const last = segs[segs.length - 1];
    if (last && last[0] + last[1].length === l.addr) last[1].push(...l.bytes);
    else segs.push([l.addr, [...l.bytes]]);
  }
  return segs;
}

const cover = [new Array(256).fill(0), new Array(256).fill(0)];

/* compare; returns a list of differences */
function compare(w, e, fields) {
  const diff = [];
  if (!w.halted) diff.push(`the Wire computer did not halt in ${w.cycles} cycles`);
  if (w.conflict >= 0) diff.push("bus conflict");
  for (const k of fields) if (w[k] !== e[k]) diff.push(`${k}: wire ${hx(w[k])}, emulator ${hx(e[k])}`);
  let n = 0;
  for (let a = 0; a < 65536 && n < 6; a++)
    if (w.mem[a] !== e.mem[a]) { diff.push(`mem[${hx(a, 4)}]: wire ${hx(w.mem[a])}, emulator ${hx(e.mem[a])}`); n++; }
  return diff;
}
const hx = (v, d = 2) => v === undefined ? "-" : "0x" + v.toString(16).toUpperCase().padStart(d, "0");

/* ---- random programs ---------------------------------------------------------------- */
function rng(seed) {
  let s = seed >>> 0 || 1;
  return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s; };
}

function randomSap3(rand) {
  const pick = a => a[rand() % a.length], byte = () => rand() & 255, n = k => rand() % k;
  const R = ["B", "C", "D", "E", "H", "L", "M", "A"];
  const data = () => `80${hx(byte()).slice(2)}H`;
  const plain = () => {
    switch (n(30)) {
    case 0: case 1: case 2: { const d = pick(R), s = pick(R); return d === "M" && s === "M" ? "MOV A,M" : `MOV ${d},${s}`; }
    case 3: case 4: return `MVI ${pick(R)},${byte()}`;
    case 5: case 6: return `${pick(["ADD", "ADC", "SUB", "SBB", "ANA", "XRA", "ORA", "CMP"])} ${pick(R)}`;
    case 7: case 8: return `${pick(["ADI", "ACI", "SUI", "SBI", "ANI", "XRI", "ORI", "CPI"])} ${byte()}`;
    case 9: return `${pick(["INR", "DCR"])} ${pick(R)}`;
    case 10: return pick(["RLC", "RRC", "RAL", "RAR", "DAA", "CMA", "STC", "CMC", "DAA"]);
    case 11: return `${pick(["LDAX", "STAX"])} ${pick(["B", "D"])}`;
    case 12: return `${pick(["LDA", "STA", "LHLD", "SHLD"])} ${data()}`;
    case 13: return `${pick(["INX", "DCX"])} ${pick(["B", "D", "H", "SP"])}`;
    case 14: return `DAD ${pick(["B", "D", "H", "SP"])}`;
    case 15: case 16: return `LXI ${pick(["B", "D", "H"])},${data()}`;
    case 17: return `PUSH ${pick(["B", "D", "H", "PSW"])}`;
    case 18: return `POP ${pick(["B", "D", "H", "PSW"])}`;
    case 19: return pick(["XTHL", "XCHG", "NOP", "EI", "DI"]);
    case 20: return `IN ${pick([1, 2, 3, 5])}`;
    case 21: return `OUT ${pick([3, 4, 6])}`;
    case 22: return `LXI SP,0F0${hx(byte()).slice(2)}H`;
    case 23: return `${pick(["ADD", "SUB", "ANA", "ORA", "CMP"])} M`;
    default: return `${pick(["MOV A,", "MOV B,", "MOV M,", "ADD ", "XRA "])}${pick(R.filter(r => r !== "M"))}`;
    }
  };
  const lines = ["        ORG 0", "        JMP main"];
  for (let v = 1; v < 8; v++) {
    lines.push(`        ORG ${v * 8}`);
    for (let i = 0; i < 2; i++) lines.push("        " + plain().replace(/^(PUSH|POP|XTHL|LXI SP).*/, "NOP"));
    lines.push("        RET");
  }
  lines.push("        ORG 40H");
  for (let s = 0; s < 4; s++) {
    lines.push(`sub${s}:`);
    for (let i = 0; i < 1 + n(5); i++) {
      if (n(5) === 0) lines.push(`        R${pick(["NZ", "Z", "NC", "C", "PO", "PE", "P", "M"])}`);
      else lines.push("        " + plain().replace(/^(PUSH|POP|XTHL|LXI SP).*/, "NOP"));
    }
    lines.push("        RET");
  }
  lines.push("        ORG 100H", "main:   LXI SP,0F000H", `        LXI B,${data()}`, `        LXI D,${data()}`, `        LXI H,${data()}`, `        MVI A,${byte()}`);
  const body = 20 + n(60);
  let label = 0;
  const pending = [];
  for (let i = 0; i < body; i++) {
    while (pending.length && pending[0].at <= i) lines.push(`${pending.shift().name}:`);
    const k = n(12);
    if (k === 0) {
      const name = `f${label++}`;
      pending.push({ name, at: i + 1 + n(3) });
      pending.sort((x, y) => x.at - y.at);
      lines.push(`        ${pick(["JMP", "JNZ", "JZ", "JNC", "JC", "JPO", "JPE", "JP", "JM"])} ${name}`);
    } else if (k === 1) lines.push(`        ${pick(["CALL", "CNZ", "CZ", "CNC", "CC", "CPO", "CPE", "CP", "CM"])} sub${n(4)}`);
    else if (k === 2) lines.push(`        RST ${1 + n(7)}`);
    else lines.push("        " + plain());
  }
  pending.forEach(p => lines.push(`${p.name}:`));
  lines.push("        HLT");
  return lines.join("\n");
}

/* the SAP-2 has only its 42 instructions: A, B, C, flags S and Z */
function randomSap2(rand) {
  const pick = a => a[rand() % a.length], byte = () => rand() & 255, n = k => rand() % k;
  const plain = () => pick([
    () => pick(["MOV A,B", "MOV A,C", "MOV B,A", "MOV B,C", "MOV C,A", "MOV C,B"]),
    () => `MVI ${pick(["A", "B", "C"])},${byte()}`,
    () => `${pick(["ADD", "SUB", "ANA", "ORA", "XRA"])} ${pick(["B", "C"])}`,
    () => `${pick(["ANI", "ORI", "XRI"])} ${byte()}`,
    () => `${pick(["INR", "DCR"])} ${pick(["A", "B", "C"])}`,
    () => pick(["CMA", "RAL", "RAR", "NOP"]),
    () => `${pick(["LDA", "STA"])} 80${hx(byte()).slice(2)}H`,
    () => `IN ${pick([1, 2])}`,
    () => `OUT ${pick([3, 4])}`
  ])();
  const lines = ["        JMP main"];
  for (let s = 0; s < 3; s++) {
    lines.push(`sub${s}:`);
    for (let i = 0; i < 1 + n(4); i++) lines.push("        " + plain());
    lines.push("        RET");
  }
  lines.push("main:");
  let label = 0;
  const pending = [];
  const body = 15 + n(40);
  for (let i = 0; i < body; i++) {
    while (pending.length && pending[0].at <= i) lines.push(`${pending.shift().name}:`);
    const k = n(10);
    if (k === 0) {
      const name = `f${label++}`;
      pending.push({ name, at: i + 1 + n(3) });
      pending.sort((x, y) => x.at - y.at);
      lines.push(`        ${pick(["JMP", "JNZ", "JZ", "JM"])} ${name}`);
    } else if (k === 1) lines.push(`        CALL sub${n(3)}`);
    else lines.push("        " + plain());
  }
  pending.forEach(p => lines.push(`${p.name}:`));
  lines.push("        HLT");
  return lines.join("\n");
}

/* ---- run ---------------------------------------------------------------------------------- */
const COMPUTERS = {
  sap3: { file: "examples/sap3.wire", regs: ["A", "B", "C", "D", "E", "H", "L"], sap2: false,
    where: { A: "r.A", B: "r.B", C: "r.C", D: "r.D", E: "r.E", H: "r.H", L: "r.L", pc: "n.PC", sp: "n.SP", port3: "io.p3", port4: "io.p4", fl: "k.f.fl", mem: "m.mem" },
    fields: ["A", "B", "C", "D", "E", "H", "L", "S", "Z", "AC", "P", "CY", "PC", "SP", "port3", "port4"], random: randomSap3 },
  sap2: { file: "examples/sap2.wire", regs: ["A", "B", "C"], sap2: true,
    where: { A: "r.A", B: "r.B", C: "r.C", pc: "n.PC", port3: "io.p3", port4: "io.p4", fl: "k.fl", mem: "m.mem" },
    fields: ["A", "B", "C", "S", "Z", "PC", "port3", "port4"], random: randomSap2 }
};

const progDir = path.join(__dirname, "progs");
const progs = [];
for (const dir of [progDir, path.join(root, "examples", "sap")]) {
  if (!fs.existsSync(dir)) continue;
  for (const f of fs.readdirSync(dir).filter(f => f.endsWith(".asm")).sort()) {
    const text = fs.readFileSync(path.join(dir, f), "utf8");
    const inputs = {};
    const m = /;\s*inputs:([^\n]*)/.exec(text);
    if (m) for (const kv of m[1].trim().split(/\s+/)) { const [k, v] = kv.split("="); inputs[k] = Number(v); }
    if (/;\s*runs forever/.test(text)) continue;      /* an interactive demo */
    progs.push({ name: path.relative(root, path.join(dir, f)), text, inputs, sap2: /;\s*sap2\b/.test(text) });
  }
}

let failed = 0, total = 0;
for (const [name, cfg] of Object.entries(COMPUTERS)) {
  if (only.length && !only.includes(name)) continue;
  if (!fs.existsSync(path.join(root, cfg.file))) { console.log(`skip ${name}: no ${cfg.file}`); continue; }
  const m = machine(path.join(root, cfg.file), cfg.regs, cfg.where);
  const one = (label, text, inputs) => {
    total++;
    const res = assemble(text, label), segs = segmentsOf(res);
    const e = emulate(segs, inputs, cfg.sap2, 20000);
    if (!e.halted) return "loops";
    const w = m.run(segs, inputs, e.steps * 20 + 200);
    const diff = compare(w, e, cfg.fields);
    if (diff.length) {
      failed++;
      console.log(`FAIL ${name} ${label} (${e.steps} instructions)\n  ${diff.join("\n  ")}`);
      if (label.startsWith("random")) console.log(text.split("\n").map((l, i) => `  ${String(i + 1).padStart(3)} ${l}`).join("\n"));
      return "fail";
    }
    return { steps: e.steps, cycles: w.cycles };
  };
  for (const p of progs) {
    if (name === "sap2" && !p.sap2) continue;
    const r = one(p.name, p.text, p.inputs);
    if (typeof r === "object") console.log(`ok   ${name} ${p.name}: ${r.steps} instructions, ${r.cycles} cycles`);
    else if (r === "loops") console.log(`??   ${name} ${p.name}: does not halt on the emulator`);
  }
  const rand = rng(SEED + (name === "sap2" ? 1 : 0));
  let ran = 0, loops = 0, instr = 0;
  for (let i = 0; i < NRAND; i++) {
    const inputs = { in1: rand() & 255, in2: rand() & 255, key: rand() & 31 };
    const r = one(`random #${i}`, cfg.random(rand), inputs);
    if (r === "loops") loops++;
    else if (typeof r === "object") { ran++; instr += r.steps; }
  }
  if (COVER) {
    const { sap2, sap3 } = require("../../examples/sap/microcode.js");
    const def = cfg.sap2 ? sap2 : sap3, cnt = cover[cfg.sap2 ? 1 : 0], low = [];
    for (let op = 0; op < 256; op++) { const nm = def(op).name; if (nm !== "NOP*" && cnt[op] < 20) low.push(`${nm} (${cnt[op]})`); }
    console.log(`     ${name}: instructions run fewer than 20 times: ${low.join(", ") || "none"}`);
  }
  if (NRAND) console.log(`ok   ${name}: ${ran} random programs agree (${instr} instructions; ${loops} never halted and were skipped)`);
}
if (failed) { console.log(`${failed} of ${total} programs differ`); process.exit(1); }
console.log("the SAP computers agree with the emulator");
