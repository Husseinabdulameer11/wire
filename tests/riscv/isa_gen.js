/* isa_gen.js - writes isa.s: every RV32IM instruction on edge values, small
 * enough for the rom (loops over a table of values in the ram). After each
 * block a checksum of its results is stored at the next word from 0x1000,
 * so run.js can say which block differs between the circuit and ref.js.
 *   node tests/riscv/isa_gen.js     (isa.s is kept in the repository) */
"use strict";
const V = [0, 1, -1, 2, -2, 7, -7, 0x7FFFFFFF, -0x80000000, 0x12345678, -0x12345678, 0xFF, 0x8000, 31, 32, 0x55AA55AA];
const R = "add sub sll slt sltu xor srl sra or and mul mulh mulhsu mulhu div divu rem remu".split(" ");
const I = "addi slti sltiu xori ori andi".split(" "), IMM = [0, -1, 2047, -2048, 5];
const out = [], p = s => out.push(s);
let L = 0;
const mix = r => { p(`        slli t5, s8, 5`); p(`        srli t6, s8, 27`); p(`        or   s8, t5, t6`); p(`        xor  s8, s8, ${r}`); };
const save = () => { p("        sw   s8, 0(s0)"); p("        addi s0, s0, 4"); p("        li   s8, 0"); };
/* for every value a (and b): body uses t1 (a) and t2 (b) */
const overValues = (body, pairs) => {
  const l = L++;
  p("        li   s2, 0");
  p(`i${l}:    add  t3, s1, s2`); p("        lw   t1, 0(t3)");
  if (pairs) { p("        li   s3, 0"); p(`j${l}:    add  t3, s1, s3`); p("        lw   t2, 0(t3)"); }
  body();
  if (pairs) { p("        addi s3, s3, 4"); p(`        blt  s3, t4, j${l}`); }
  p("        addi s2, s2, 4"); p(`        blt  s2, t4, i${l}`);
  save();
};
p("# isa.s: made by tests/riscv/isa_gen.js. Every RV32IM instruction on edge");
p("# values; after each block a checksum of its results goes to the next word");
p("# from 0x1000, so tests/riscv/run.js can say which block differs between");
p("# the circuit and ref.js. Ends with ebreak.");
p("        .data");
p("        .space 512               # the checksums");
p(`vals:   .word ${V.map(v => v | 0).join(", ")}`);
p("        .text");
p("main:   li   s0, 0x1000");
p("        la   s1, vals");
p(`        li   t4, ${4 * V.length}`);
p("        li   s8, 0");
for (const op of R) overValues(() => { p(`        ${op} t0, t1, t2`); mix("t0"); }, true);
for (const op of I) overValues(() => IMM.forEach(k => { p(`        ${op} t0, t1, ${k}`); mix("t0"); }), false);
for (const op of ["slli", "srli", "srai"]) overValues(() => [0, 1, 13, 31].forEach(k => { p(`        ${op} t0, t1, ${k}`); mix("t0"); }), false);
/* branches: taken or not, for every pair */
for (const op of "beq bne blt bge bltu bgeu".split(" ")) overValues(() => {
  const l = L++;
  p("        li   t0, 1"); p(`        ${op} t1, t2, k${l}`); p("        li   t0, 2"); p(`k${l}:`); mix("t0");
}, true);
/* x0, and an instruction whose rd is its source */
overValues(() => { p("        add  zero, t1, t1"); p("        mv   t0, zero"); mix("t0"); p("        sub  t3, t1, t1"); mix("t3"); p("        mv   t3, t1"); p("        mul  t3, t3, t3"); mix("t3"); }, false);
for (const v of [0, 1, 0x80000, 0xFFFFF, 0x12345]) { p(`        lui  t0, ${v}`); mix("t0"); p(`        auipc t0, ${v}`); mix("t0"); }
save();
/* jal and jalr: the link, and where they go */
p("        jal  ra, sub1"); mix("a0");
p("        la   t3, sub2"); p("        jalr ra, 4(t3)"); mix("a0");
p("        auipc t0, 0"); p("        jal  t1, link1"); p("link1:  sub  t0, t1, t0"); mix("t0");   /* the link is the pc + 4 */
save();
/* loads and stores: every size and lane */
p("        li   s5, 0x1F00");
p("        li   t1, 0x89ABCDEF"); p("        sw   t1, 0(s5)");
for (const [op, off] of [["lb",0],["lb",1],["lb",2],["lb",3],["lbu",0],["lbu",3],["lh",0],["lh",2],["lhu",0],["lhu",2],["lw",0]]) { p(`        ${op} t0, ${off}(s5)`); mix("t0"); }
p("        li   t1, 0x5A"); p("        sb   t1, 1(s5)"); p("        sb   t1, 3(s5)"); p("        lw   t0, 0(s5)"); mix("t0");
p("        li   t1, 0x1234"); p("        sh   t1, 2(s5)"); p("        lw   t0, 0(s5)"); mix("t0");
p("        sh   t1, 0(s5)"); p("        lw   t0, 0(s5)"); mix("t0");
p("        li   t1, -1"); p("        sw   t1, 4(s5)"); p("        lh   t0, 6(s5)"); mix("t0"); p("        lbu  t0, 5(s5)"); mix("t0");
p("        lw   t0, -4(s5)"); mix("t0");
save();
/* the io area and the screens */
p("        li   s6, 0x4000"); p("        li   t1, 0xCAFE"); p("        sw   t1, 0(s6)"); p("        lw   t0, 0(s6)"); mix("t0");
p("        lw   t0, 16(s6)"); mix("t0");   /* the cycle counter: the same count in both */
p("        li   s7, 0x3000"); p("        li   t1, 0x41"); p("        sb   t1, 5(s7)"); p("        lbu  t0, 5(s7)"); mix("t0");
p("        li   t1, 0x4342"); p("        sh   t1, 6(s7)"); p("        lbu  t0, 6(s7)"); mix("t0"); p("        lw   t0, 4(s7)"); mix("t0");
p("        li   s7, 0x2000"); p("        li   t1, 0x112233E0"); p("        sw   t1, 64(s7)"); p("        lbu  t0, 64(s7)"); mix("t0");
save();
p("        ebreak");
p("sub1:   li   a0, 77"); p("        ret");
p("sub2:   li   a0, 1"); p("        li   a0, 99"); p("        ret");
require("fs").writeFileSync(require("path").join(__dirname, "isa.s"), out.join("\n") + "\n");
