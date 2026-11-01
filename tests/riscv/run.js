/* run.js - runs programs on the RISC-V computer of examples/riscv, in the
 * circuit (Wire's simulator, as the page runs it) and in ref.js (the same
 * computer written plainly from the RISC-V spec), and checks that they end
 * the same: the pc, the display, halted, the 31 registers, the ram and both
 * screens.
 *
 *   node tests/riscv/run.js build/wire [prog.s ...]
 *
 * With no files: every program in examples/riscv/programs, and isa.s (every
 * instruction on edge values; made by isa_gen.js). The programs are put into
 * the rom and ram as the page's Assembly panel puts them. */
"use strict";
const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");
const root = path.join(__dirname, "..", "..");
const WireSim = require(path.join(root, "src", "wire_sim.js"));
const ref = require("./ref.js");

const [wire, ...given] = process.argv.slice(2);
if (!wire) { console.error("usage: node tests/riscv/run.js path/to/wire [prog.s ...]"); process.exit(2); }
const RVAsm = require(path.join(root, "tools", "isa.js"))(wire, path.join(root, "examples", "riscv", "rv32im.isa"));
const progDir = path.join(root, "examples", "riscv", "programs");
const files = given.length ? given : [...fs.readdirSync(progDir).filter(f => f.endsWith(".s")).sort().map(f => path.join(progDir, f)),
                                      path.join(__dirname, "isa.s")];

/* how long each runs, and what is pressed when (a program that stops stops sooner) */
const plan = {
  "keys.s": { cycles: 9000, inputs: c => ({ key: c >= 3000 && c < 3400 ? 16 | 0xC : c >= 3400 && c < 5000 ? 0xC : c >= 5000 && c < 5300 ? 16 | 7 : 7 }) },
  "hello.s": { cycles: 60000, inputs: () => ({ sw: 13 }) },
  "bounce.s": { cycles: 20000 },
  default: { cycles: 200000 }
};

const D = JSON.parse(execFileSync(wire, ["--json", path.join(root, "examples", "riscv", "rv32im.wire")],
                                  { stdio: ["ignore", "pipe", "inherit"], maxBuffer: 1 << 28 }).toString());
const idx = (name, kind) => D.nodes.findIndex(n => n.name === name && (!kind || n.kind === kind));
const rom = idx("instr", "rom"), lanes = [0, 8, 16, 24].map(b => idx("ram_b" + b, "ram"));
const pixels = idx("pixels", "ram"), chars = idx("chars", "ram");
const sw = idx("sw", "input"), key = idx("key", "keypad");
const out = name => D.nodes[idx(name, "output")].in[0];
const regs = Array.from({ length: 32 }, (_, i) => D.nets.findIndex(n => n.name === "regs_" + i));
if ([rom, ...lanes, pixels, chars, sw, key].some(i => i < 0) || regs.slice(1).some(i => i < 0)) {
  console.error("run.js: rv32im.wire does not have the parts this test looks for"); process.exit(1);
}
const compiled = WireSim.compile(D), module_ = new WebAssembly.Module(compiled.bytes);
const mask = w => w >= 32 ? 0xFFFFFFFF : ((1 << w) >>> 0) - 1;

let failed = 0;
for (const file of files) {
  const name = path.basename(file), pl = plan[name] || plan.default;
  const r = RVAsm.assemble(fs.readFileSync(file, "utf8"), { textBase: 0, dataBase: 0x1000 });
  if (r.errors.length) { console.log(`FAIL ${name}: ${r.errors[0].line}: ${r.errors[0].msg}`); failed++; continue; }
  /* the rom and the ram as the Assembly panel fills them */
  D.nodes[rom].init = [[0, Array.from({ length: 1024 }, (_, i) => r.text[i] >>> 0 || 0)]];
  lanes.forEach((j, k) => { D.nodes[j].init = [[0, Array.from({ length: 1024 }, (_, i) => r.data[4 * i + k] || 0)]]; });
  const sim = WireSim.instantiate(compiled, module_, D);
  D.nets.forEach((n, i) => { sim.setV(i, 0); sim.setK(i, mask(n.w)); });
  const inputs = pl.inputs || (() => ({}));
  const want = ref.run(r.text, r.data, pl.cycles, inputs);
  const cycles = want.halted ? want.cycles + 4 : pl.cycles;   /* a few more: stopped, it stays as it is */
  const t0 = process.hrtime.bigint();
  sim.simulate();
  for (let c = 0; c < cycles; c++) {
    const v = inputs(c);
    sim.setState(sw, v.sw || 0); sim.setState(key, v.key || 0);
    sim.toggle(2);
  }
  const ms = Number(process.hrtime.bigint() - t0) / 1e6;
  const bad = [];
  const cmp = (what, got, exp) => { if ((got >>> 0) !== (exp >>> 0)) bad.push(`${what}: the circuit has 0x${(got >>> 0).toString(16)}, ref.js 0x${(exp >>> 0).toString(16)}`); };
  cmp("pc", sim.v(out("pc")), want.pc);
  cmp("display", sim.v(out("display")), want.display);
  cmp("halted", sim.v(out("halted")), want.halted ? 1 : 0);
  for (let i = 1; i < 32; i++) cmp(`x${i}`, sim.v(regs[i]), want.x[i]);
  const lw = lanes.map(j => sim.words(j));
  for (let a = 0; a < 4096 && bad.length < 8; a++) {
    const got = lw[a & 3][a >> 2], exp = want.ram[a];
    if (got !== exp) {
      const blk = name === "isa.s" && a < 512 ? ` (checksum of block ${a >> 2} in isa.s)` : "";
      bad.push(`ram byte 0x${(0x1000 + a).toString(16)}${blk}: the circuit has 0x${got.toString(16)}, ref.js 0x${exp.toString(16)}`);
    }
  }
  const pw = sim.words(pixels), cw = sim.words(chars);
  for (let i = 0; i < 4096; i++) if (pw[i] !== want.pixels[i]) { bad.push(`pixel ${i}: ${pw[i]} vs ${want.pixels[i]}`); break; }
  for (let i = 0; i < 1024; i++) if (cw[i] !== want.chars[i]) { bad.push(`character ${i}: ${cw[i]} vs ${want.chars[i]}`); break; }
  const text = [];
  for (let y = 0; y < 16; y++) {
    let l = "";
    for (let x = 0; x < 64; x++) { const ch = cw[y * 64 + x]; l += ch > 32 && ch < 127 ? String.fromCharCode(ch) : " "; }
    if (l.trim()) text.push(l.trimEnd());
  }
  const shown = text.length ? ` "${text[0].trim().slice(0, 40)}"${text.length > 1 ? ` (+${text.length - 1} lines)` : ""}` : "";
  if (bad.length) { failed++; console.log(`FAIL ${name}:\n  ${bad.join("\n  ")}`); }
  else console.log(`ok   ${name}: ${cycles} cycles in ${ms.toFixed(0)} ms${want.halted ? ", stopped" : ""}; display 0x${(sim.v(out("display")) >>> 0).toString(16)}${shown}`);
}
if (failed) { console.log(`${failed} of ${files.length} programs end differently in the circuit`); process.exit(1); }
console.log(`all ${files.length} programs end the same in the circuit and in ref.js`);
