/* rvasm.js - a small RV32IM assembler, written plainly so it can be read.
 * Its encodings were checked against GNU as (riscv64-unknown-elf-as
 * -march=rv32im) on thousands of random instructions.
 *
 *   const { assemble } = RVAsm;  assemble(source, { textBase, dataBase })
 *   -> { text: [words], data: [bytes], symbols, listing, errors: [{ line, msg }] }
 *
 * Two sections: .text (instructions, from textBase) and .data (bytes, from
 * dataBase). Labels end with a colon. Comments start with # or ;
 *
 * Instructions: all of RV32I and RV32M (lui auipc jal jalr, the branches,
 * the loads and stores, the ALU ops with and without an immediate, mul
 * mulh mulhsu mulhu div divu rem remu, ecall ebreak fence).
 * Pseudo-instructions: nop li la mv not neg seqz snez sltz sgtz
 * beqz bnez blez bgez bltz bgtz bgt ble bgtu bleu j jr ret call tail.
 * Directives: .text .data .word .half .byte .ascii .asciz .string
 * .space .zero .align .equ .set .globl (ignored).
 * Values: 42, -7, 0x2A, 0b101, 'A', a label, label+4, %hi(label), %lo(label). */
"use strict";
const RVAsm = (() => {

const ABI = "zero ra sp gp tp t0 t1 t2 s0 s1 a0 a1 a2 a3 a4 a5 a6 a7 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 t3 t4 t5 t6".split(" ");
const REGS = new Map();
ABI.forEach((n, i) => { REGS.set(n, i); REGS.set("x" + i, i); });
REGS.set("fp", 8);

const R = { add: [0, 0], sub: [0x20, 0], sll: [0, 1], slt: [0, 2], sltu: [0, 3], xor: [0, 4], srl: [0, 5], sra: [0x20, 5],
            or: [0, 6], and: [0, 7], mul: [1, 0], mulh: [1, 1], mulhsu: [1, 2], mulhu: [1, 3], div: [1, 4], divu: [1, 5],
            rem: [1, 6], remu: [1, 7] };
const I = { addi: 0, slti: 2, sltiu: 3, xori: 4, ori: 6, andi: 7 };
const SH = { slli: [0, 1], srli: [0, 5], srai: [0x20, 5] };
const LD = { lb: 0, lh: 1, lw: 2, lbu: 4, lhu: 5 };
const ST = { sb: 0, sh: 1, sw: 2 };
const BR = { beq: 0, bne: 1, blt: 4, bge: 5, bltu: 6, bgeu: 7 };

class AsmError extends Error {}
const fail = msg => { throw new AsmError(msg); };

/* split "a, 4(sp)" into ["a", "4(sp)"], keeping strings and parentheses whole */
function splitArgs(s) {
  const out = [];
  let cur = "", depth = 0, q = null;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (q) { cur += c; if (c === "\\") { cur += s[++i] || ""; } else if (c === q) q = null; continue; }
    if (c === '"' || c === "'") { q = c; cur += c; continue; }
    if (c === "(") depth++;
    if (c === ")") depth--;
    if (c === "," && depth === 0) { out.push(cur.trim()); cur = ""; continue; }
    cur += c;
  }
  if (cur.trim()) out.push(cur.trim());
  return out;
}

/* a comment starts at # or ; outside a string */
function stripComment(line) {
  let q = null;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (q) { if (c === "\\") i++; else if (c === q) q = null; continue; }
    if (c === '"' || c === "'") q = c;
    else if (c === "#" || c === ";") return line.slice(0, i);
  }
  return line;
}

function unescape(s) {
  return s.replace(/\\(n|t|r|0|\\|"|'|x[0-9a-fA-F]{2})/g, (_, e) =>
    e === "n" ? "\n" : e === "t" ? "\t" : e === "r" ? "\r" : e === "0" ? "\0" : e[0] === "x" ? String.fromCharCode(parseInt(e.slice(1), 16)) : e);
}

function assemble(src, opts = {}) {
  const textBase = opts.textBase || 0, dataBase = opts.dataBase === undefined ? 0x1000 : opts.dataBase;
  const lines = src.split(/\r?\n/);
  const symbols = new Map(), errors = [], liForm = new Map();
  let curLn = 0;
  let pass, sec, pc, text, data, listing;

  /* a value: numbers, chars, symbols, a + b - c, %hi(x), %lo(x) */
  const value = (s, need) => {
    s = s.trim();
    let m;
    if ((m = /^%(hi|lo)\((.*)\)$/.exec(s))) {
      const v = value(m[2], need);
      return m[1] === "hi" ? ((v + 0x800) >>> 12) & 0xFFFFF : ((v & 0xFFF) << 20) >> 20;
    }
    const terms = s.replace(/\s+/g, "").match(/[+-]?[^+-]+/g);
    if (!terms) fail(`expected a value, got "${s}"`);
    let total = 0;
    for (let t of terms) {
      let sign = 1;
      if (t[0] === "+" || t[0] === "-") { if (t[0] === "-") sign = -1; t = t.slice(1); }
      let v;
      if (/^0x[0-9a-f_]+$/i.test(t)) v = parseInt(t.slice(2).replace(/_/g, ""), 16);
      else if (/^0b[01_]+$/i.test(t)) v = parseInt(t.slice(2).replace(/_/g, ""), 2);
      else if (/^[0-9]+$/.test(t)) v = parseInt(t, 10);
      else if (/^'(\\.|[^\\'])'$/.test(t)) v = unescape(t.slice(1, -1)).charCodeAt(0);
      else if (/^[A-Za-z_.$][\w.$]*$/.test(t)) {
        if (symbols.has(t)) v = symbols.get(t);
        else if (pass === 1 && !need) v = 0;   /* not yet known: worked out in pass 2 */
        else fail(`there is no label or constant called ${t}`);
      } else fail(`"${t}" is not a number or a label`);
      total += sign * v;
    }
    return total;
  };
  const known = s => { try { value(s, true); return true; } catch (e) { return false; } };
  const reg = s => {
    const r = REGS.get(s.trim().toLowerCase());
    if (r === undefined) fail(`"${s}" is not a register (x0-x31, or zero ra sp gp tp t0-t6 s0-s11 a0-a7)`);
    return r;
  };
  const imm = (v, bits, what) => {
    const lo = -(1 << (bits - 1)), hi = (1 << (bits - 1)) - 1;
    if (v < lo || v > hi) fail(`${what} ${v} does not fit in ${bits} bits (${lo} to ${hi})`);
    return v;
  };
  const mem = s => {   /* 8(sp), (a0), label */
    const m = /^(.*)\(\s*([A-Za-z0-9]+)\s*\)$/.exec(s.trim());
    if (!m) fail(`expected an address like 8(sp), got "${s}"`);
    return [m[1].trim() ? value(m[1]) : 0, reg(m[2])];
  };
  const need = (a, n, form) => { if (a.length !== n) fail(`expected ${form}`); };

  const enc = {
    r: (f7, f3, rd, rs1, rs2) => (f7 << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | 0x33,
    i: (op, f3, rd, rs1, v) => ((v & 0xFFF) << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op,
    s: (f3, rs1, rs2, v) => (((v >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | ((v & 31) << 7) | 0x23,
    b: (f3, rs1, rs2, v) => (((v >> 12) & 1) << 31) | (((v >> 5) & 0x3F) << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) |
                            (((v >> 1) & 0xF) << 8) | (((v >> 11) & 1) << 7) | 0x63,
    u: (op, rd, v) => ((v & 0xFFFFF) << 12) | (rd << 7) | op,
    j: (rd, v) => (((v >> 20) & 1) << 31) | (((v >> 1) & 0x3FF) << 21) | (((v >> 11) & 1) << 20) | (((v >> 12) & 0xFF) << 12) | (rd << 7) | 0x6F
  };
  const target = (s, bits) => {   /* a branch or jump target: a label (or a number), as an offset */
    const off = value(s) - pc;
    if (pass === 2) {
      if (off & 1) fail("a jump target must be at an even address");
      const lo = -(1 << (bits - 1)), hi = (1 << (bits - 1)) - 2;
      if (off < lo || off > hi)
        fail(`${s} is ${off} bytes away, too far for a ${bits === 13 ? "branch (it reaches -4096 to 4094; branch over a j instead)" : "jump (it reaches about 1 MB either way)"}`);
    }
    return off;
  };

  /* one instruction (or pseudo-instruction) -> its words */
  function instr(op, a) {
    const r3 = form => { need(a, 3, form); return [reg(a[0]), reg(a[1]), reg(a[2])]; };
    if (op in R) { const [rd, rs1, rs2] = r3(`${op} rd, rs1, rs2`); return [enc.r(R[op][0], R[op][1], rd, rs1, rs2)]; }
    if (op in I) { need(a, 3, `${op} rd, rs1, imm`); return [enc.i(0x13, I[op], reg(a[0]), reg(a[1]), imm(value(a[2]), 12, "the immediate"))]; }
    if (op in SH) {
      need(a, 3, `${op} rd, rs1, shamt`);
      const k = value(a[2]);
      if (k < 0 || k > 31) fail("a shift amount is 0 to 31");
      return [(SH[op][0] << 25) | (k << 20) | (reg(a[1]) << 15) | (SH[op][1] << 12) | (reg(a[0]) << 7) | 0x13];
    }
    if (op in LD) {
      need(a, 2, `${op} rd, offset(rs1)`);
      if (!/\(/.test(a[1])) {   /* lw a0, label: the address in rd first */
        const v = value(a[1]), rd = reg(a[0]);
        return [enc.u(0x17, rd, ((v - pc + 0x800) >>> 12) & 0xFFFFF), enc.i(0x03, LD[op], rd, rd, ((v - pc) << 20) >> 20)];
      }
      const [o, rs1] = mem(a[1]);
      return [enc.i(0x03, LD[op], reg(a[0]), rs1, imm(o, 12, "the offset"))];
    }
    if (op in ST) { need(a, 2, `${op} rs2, offset(rs1)`); const [o, rs1] = mem(a[1]); return [enc.s(ST[op], rs1, reg(a[0]), imm(o, 12, "the offset"))]; }
    if (op in BR) { need(a, 3, `${op} rs1, rs2, label`); return [enc.b(BR[op], reg(a[0]), reg(a[1]), target(a[2], 13))]; }
    switch (op) {
    case "lui": case "auipc": {
      need(a, 2, `${op} rd, imm`);
      const v = value(a[1]);
      if (pass === 2 && (v < -(1 << 19) || v > 0xFFFFF)) fail(`the immediate ${v} does not fit in 20 bits`);
      return [enc.u(op === "lui" ? 0x37 : 0x17, reg(a[0]), v)];
    }
    case "jal":
      if (a.length === 1) return [enc.j(1, target(a[0], 21))];
      need(a, 2, "jal rd, label"); return [enc.j(reg(a[0]), target(a[1], 21))];
    case "jalr":
      if (a.length === 1) return [enc.i(0x67, 0, 1, reg(a[0]), 0)];
      if (a.length === 2) { const [o, rs1] = mem(a[1]); return [enc.i(0x67, 0, reg(a[0]), rs1, imm(o, 12, "the offset"))]; }
      need(a, 3, "jalr rd, rs1, imm"); return [enc.i(0x67, 0, reg(a[0]), reg(a[1]), imm(value(a[2]), 12, "the offset"))];
    case "ecall": need(a, 0, "ecall"); return [0x00000073];
    case "ebreak": need(a, 0, "ebreak"); return [0x00100073];
    case "fence": return [0x0FF0000F];
    case "nop": need(a, 0, "nop"); return [enc.i(0x13, 0, 0, 0, 0)];
    case "li": {
      need(a, 2, "li rd, value");
      const rd = reg(a[0]);
      /* one instruction when the value fits in 12 bits; the first pass
       * decides, so every address stays where it was worked out */
      if (pass === 1) {
        const v = known(a[1]) ? value(a[1]) | 0 : null;
        liForm.set(curLn, v === null ? 2 : v >= -2048 && v < 2048 ? 0 : (v << 20) >> 20 ? 2 : 1);
      }
      const v = value(a[1]) | 0, form = liForm.get(curLn);
      if (form === 0) return [enc.i(0x13, 0, rd, 0, v)];
      const hi = ((v + 0x800) >>> 12) & 0xFFFFF, lo = (v << 20) >> 20;
      return form === 1 ? [enc.u(0x37, rd, hi)] : [enc.u(0x37, rd, hi), enc.i(0x13, 0, rd, rd, lo)];
    }
    case "la": {
      need(a, 2, "la rd, label");
      const rd = reg(a[0]), v = value(a[1]) | 0;
      return [enc.u(0x37, rd, ((v + 0x800) >>> 12) & 0xFFFFF), enc.i(0x13, 0, rd, rd, (v << 20) >> 20)];
    }
    case "mv": need(a, 2, "mv rd, rs"); return [enc.i(0x13, 0, reg(a[0]), reg(a[1]), 0)];
    case "not": need(a, 2, "not rd, rs"); return [enc.i(0x13, 4, reg(a[0]), reg(a[1]), -1)];
    case "neg": need(a, 2, "neg rd, rs"); return [enc.r(0x20, 0, reg(a[0]), 0, reg(a[1]))];
    case "seqz": need(a, 2, "seqz rd, rs"); return [enc.i(0x13, 3, reg(a[0]), reg(a[1]), 1)];
    case "snez": need(a, 2, "snez rd, rs"); return [enc.r(0, 3, reg(a[0]), 0, reg(a[1]))];
    case "sltz": need(a, 2, "sltz rd, rs"); return [enc.r(0, 2, reg(a[0]), reg(a[1]), 0)];
    case "sgtz": need(a, 2, "sgtz rd, rs"); return [enc.r(0, 2, reg(a[0]), 0, reg(a[1]))];
    case "beqz": need(a, 2, "beqz rs, label"); return [enc.b(0, reg(a[0]), 0, target(a[1], 13))];
    case "bnez": need(a, 2, "bnez rs, label"); return [enc.b(1, reg(a[0]), 0, target(a[1], 13))];
    case "blez": need(a, 2, "blez rs, label"); return [enc.b(5, 0, reg(a[0]), target(a[1], 13))];
    case "bgez": need(a, 2, "bgez rs, label"); return [enc.b(5, reg(a[0]), 0, target(a[1], 13))];
    case "bltz": need(a, 2, "bltz rs, label"); return [enc.b(4, reg(a[0]), 0, target(a[1], 13))];
    case "bgtz": need(a, 2, "bgtz rs, label"); return [enc.b(4, 0, reg(a[0]), target(a[1], 13))];
    case "bgt": need(a, 3, "bgt rs, rt, label"); return [enc.b(4, reg(a[1]), reg(a[0]), target(a[2], 13))];
    case "ble": need(a, 3, "ble rs, rt, label"); return [enc.b(5, reg(a[1]), reg(a[0]), target(a[2], 13))];
    case "bgtu": need(a, 3, "bgtu rs, rt, label"); return [enc.b(6, reg(a[1]), reg(a[0]), target(a[2], 13))];
    case "bleu": need(a, 3, "bleu rs, rt, label"); return [enc.b(7, reg(a[1]), reg(a[0]), target(a[2], 13))];
    case "j": need(a, 1, "j label"); return [enc.j(0, target(a[0], 21))];
    case "jr": need(a, 1, "jr rs"); return [enc.i(0x67, 0, 0, reg(a[0]), 0)];
    case "ret": need(a, 0, "ret"); return [enc.i(0x67, 0, 0, 1, 0)];
    case "call": need(a, 1, "call label"); return [enc.j(1, target(a[0], 21))];
    case "tail": need(a, 1, "tail label"); return [enc.j(0, target(a[0], 21))];
    }
    fail(`there is no instruction called ${op}`);
  }

  const emitData = (bytes, ln, srcText) => {
    if (pass === 2) {
      listing.push({ line: ln, addr: pc, hex: bytes.slice(0, 8).map(b => b.toString(16).padStart(2, "0")).join(" ") + (bytes.length > 8 ? " …" : ""), src: srcText });
      bytes.forEach((b, i) => { data[pc - dataBase + i] = b & 255; });
    }
    pc += bytes.length;
  };

  for (pass = 1; pass <= 2; pass++) {
    sec = "text"; text = []; data = []; listing = [];
    const pcs = { text: textBase, data: dataBase };
    pc = textBase;
    const swap = s => { pcs[sec] = pc; sec = s; pc = pcs[s]; };
    for (let ln = 1; ln <= lines.length; ln++) {
      const srcText = lines[ln - 1];
      curLn = ln;
      let l = stripComment(srcText).trim();
      try {
        let m;
        while ((m = /^([A-Za-z_.$][\w.$]*)\s*:\s*/.exec(l))) {   /* labels */
          if (pass === 1) {
            if (symbols.has(m[1])) fail(`the label ${m[1]} is defined twice`);
            symbols.set(m[1], pc);
          }
          l = l.slice(m[0].length);
        }
        if (!l) continue;
        const sp = l.search(/\s/), op = (sp < 0 ? l : l.slice(0, sp)).toLowerCase(), rest = sp < 0 ? "" : l.slice(sp + 1).trim();
        const a = splitArgs(rest);
        if (op[0] === ".") {
          switch (op) {
          case ".text": case ".data": swap(op.slice(1)); break;
          case ".globl": case ".global": case ".section": case ".type": case ".size": break;
          case ".equ": case ".set":
            if (a.length !== 2) fail(`expected ${op} NAME, value`);
            if (pass === 1) symbols.set(a[0], value(a[1], true)); break;
          case ".align": {
            const n = 1 << value(a[0], true);
            const pad = (n - (pc % n)) % n;
            if (sec === "text") { if (pad % 4) fail(".align in .text must keep instructions on words"); for (let i = 0; i < pad / 4; i++) { if (pass === 2) text[(pc - textBase) / 4] = 0x13; pc += 4; } }
            else emitData(new Array(pad).fill(0), ln, srcText);
            break;
          }
          case ".word": case ".half": case ".byte": {
            const size = op === ".word" ? 4 : op === ".half" ? 2 : 1, bytes = [];
            a.forEach(x => { const v = value(x); for (let i = 0; i < size; i++) bytes.push((v >>> (8 * i)) & 255); });
            if (sec === "text") {
              if (size !== 4) fail(`${op} goes in .data; .text holds words`);
              a.forEach(x => { if (pass === 2) { text[(pc - textBase) / 4] = value(x) >>> 0; listing.push({ line: ln, addr: pc, hex: (value(x) >>> 0).toString(16).padStart(8, "0"), src: srcText }); } pc += 4; });
            } else emitData(bytes, ln, srcText);
            break;
          }
          case ".ascii": case ".asciz": case ".string": {
            if (sec === "text") fail("strings go in .data");
            const m2 = /^"((?:\\.|[^"\\])*)"$/.exec(rest);
            if (!m2) fail(`expected ${op} "text"`);
            const bytes = [...unescape(m2[1])].map(c => c.charCodeAt(0) & 255);
            if (op !== ".ascii") bytes.push(0);
            emitData(bytes, ln, srcText);
            break;
          }
          case ".space": case ".zero": {
            const n = value(a[0], true);
            if (sec === "text") fail(`${op} goes in .data`);
            emitData(new Array(n).fill(0), ln, srcText);
            break;
          }
          default: fail(`there is no directive called ${op}`);
          }
          continue;
        }
        if (sec !== "text") fail("instructions go in .text");
        const words = instr(op, a);
        if (pass === 2) {
          words.forEach((w, i) => {
            text[(pc - textBase) / 4 + i] = w >>> 0;
            listing.push({ line: ln, addr: pc + 4 * i, hex: (w >>> 0).toString(16).padStart(8, "0"), src: i ? "" : srcText });
          });
        }
        pc += 4 * words.length;
      } catch (e) {
        if (!(e instanceof AsmError)) throw e;
        if (!errors.some(x => x.line === ln)) errors.push({ line: ln, msg: e.message });
      }
    }
  }   /* both passes even after an error: the second finds the labels that are missing */
  for (let i = 0; i < text.length; i++) if (text[i] === undefined) text[i] = 0;
  for (let i = 0; i < data.length; i++) if (data[i] === undefined) data[i] = 0;
  return { text, data, symbols, listing, errors: errors.sort((x, y) => x.line - y.line) };
}

return { assemble };
})();
if (typeof module !== "undefined") module.exports = RVAsm;
