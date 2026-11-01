/* gnu.js - checks the assembler of the RISC-V example against GNU as:
 * thousands of random RV32IM instructions (and the simple pseudo-
 * instructions, and li of every size) assembled by both must give the same
 * words. make test runs it when riscv64-unknown-elf-as is installed
 * (Ubuntu: apt install binutils-riscv64-unknown-elf).
 *
 *   node tests/riscv/gnu.js build/wire */
"use strict";
const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");
const wire = process.argv[2];
if (!wire) { console.error("usage: node tests/riscv/gnu.js path/to/wire"); process.exit(2); }
const RVAsm = require(path.join(__dirname, "..", "..", "tools", "isa.js"))(wire, path.join(__dirname, "..", "..", "examples", "riscv", "rv32im.isa"));

const R = "add sub sll slt sltu xor srl sra or and mul mulh mulhsu mulhu div divu rem remu".split(" ");
const I = "addi slti sltiu xori ori andi".split(" "), SH = "slli srli srai".split(" ");
const LD = "lb lh lw lbu lhu".split(" "), ST = "sb sh sw".split(" "), BR = "beq bne blt bge bltu bgeu bgt ble bgtu bleu".split(" ");
const BZ = "beqz bnez blez bgez bltz bgtz".split(" "), P2 = "mv not neg seqz snez sltz sgtz".split(" ");
const regs = "zero ra sp gp tp t0 t1 t2 s0 s1 a0 a1 a2 a3 a4 a5 a6 a7 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 t3 t4 t5 t6".split(" ");
let seed = 1234567;
const rnd = n => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return (seed >>> 8) % n; };   /* the high bits: the low ones repeat */
const r = () => rnd(2) ? regs[rnd(32)] : "x" + rnd(32), rli = () => regs[1 + rnd(31)], imm = b => rnd(1 << b) - (1 << (b - 1));
const lines = [];
for (let i = 0; i < 4000; i++) {
  if (i % 50 === 0) lines.push(`L${i / 50}:`);
  const lab = () => `L${Math.min(80, Math.max(0, Math.floor(i / 50) + rnd(9) - 4))}`;   /* near: a branch reaches 4 KB */
  switch (rnd(15)) {
  case 0: lines.push(`${R[rnd(R.length)]} ${r()}, ${r()}, ${r()}`); break;
  case 1: lines.push(`${I[rnd(I.length)]} ${r()}, ${r()}, ${imm(12)}`); break;
  case 2: lines.push(`${SH[rnd(3)]} ${r()}, ${r()}, ${rnd(32)}`); break;
  case 3: lines.push(`${LD[rnd(5)]} ${r()}, ${imm(12)}(${r()})`); break;
  case 4: lines.push(`${ST[rnd(3)]} ${r()}, ${imm(12)}(${r()})`); break;
  case 5: lines.push(`${BR[rnd(BR.length)]} ${r()}, ${r()}, ${lab()}`); break;
  case 6: lines.push(`${BZ[rnd(6)]} ${r()}, ${lab()}`); break;
  case 7: lines.push(`${P2[rnd(7)]} ${r()}, ${r()}`); break;
  case 8: lines.push(`lui ${r()}, ${rnd(1 << 20)}`); break;
  case 9: lines.push(`auipc ${r()}, ${rnd(1 << 20)}`); break;
  case 10: lines.push(rnd(2) ? `jal ${r()}, ${lab()}` : `j ${lab()}`); break;
  case 11: lines.push(rnd(2) ? `jalr ${r()}, ${imm(12)}(${r()})` : rnd(2) ? `jr ${r()}` : "ret"); break;
  case 12: lines.push(["nop", "ecall", "ebreak"][rnd(3)]); break;
  case 13: lines.push(`li ${rli()}, ${imm(12)}`); break;
  default: {   /* li of any size: one or two instructions, as GNU as picks */
    const k = rnd(3), v = k === 0 ? (rnd(0x7fffffff) * (rnd(2) ? 1 : -1)) | 0 : k === 1 ? (rnd(0xFFFFF) << 12) | 0 : imm(12);
    lines.push(`li ${rli()}, ${v}`);   /* not x0: GNU as puts an addi after the lui there, which does nothing */
  }
  }
}
lines.push("L80:");
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "rvgnu-"));
const src = lines.join("\n") + "\n";
fs.writeFileSync(path.join(tmp, "t.s"), src);
execFileSync("riscv64-unknown-elf-as", ["-march=rv32im", "-mabi=ilp32", "-mno-relax", "-o", path.join(tmp, "t.o"), path.join(tmp, "t.s")]);
execFileSync("riscv64-unknown-elf-objcopy", ["-O", "binary", "-j", ".text", path.join(tmp, "t.o"), path.join(tmp, "t.bin")]);
const gnu = fs.readFileSync(path.join(tmp, "t.bin"));
fs.rmSync(tmp, { recursive: true });
const ours = RVAsm.assemble(src);
if (ours.errors.length) { console.log(`FAIL the assembler: line ${ours.errors[0].line}: ${ours.errors[0].msg}`); process.exit(1); }
let bad = 0;
const n = Math.max(gnu.length / 4, ours.text.length);
for (let i = 0; i < n; i++) {
  const g = i < gnu.length / 4 ? gnu.readUInt32LE(4 * i) : -1, o = ours.text[i] === undefined ? -1 : ours.text[i] >>> 0;
  if (g !== o && bad++ < 5) {
    const e = ours.listing.find(x => x.addr === 4 * i);
    console.log(`  at 0x${(4 * i).toString(16)} (${e ? e.src.trim() : "?"}): GNU as 0x${(g >>> 0).toString(16)}, ours 0x${(o >>> 0).toString(16)}`);
  }
}
if (bad) { console.log(`FAIL ${bad} of ${n} words differ from GNU as`); process.exit(1); }
console.log(`ok   the assembler agrees with GNU as on ${n} words (${lines.length} lines of random RV32IM)`);
