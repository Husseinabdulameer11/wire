/* rv32im.addon.js: made by `node tools/riscv.js build/wire` from asm/rvasm.js,
 * programs/*.s and asm/panel.js. Change those and run it again. wire puts
 * this script into the page of rv32im.wire, after the viewer. */
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

const RV_PROGRAMS = {
 "hello.s": "# Hello: text on the text screen, a picture on the colour screen, then,\n# for ever, the switches times themselves on the display.\n        .data\nline1:  .asciz \"Hello, RISC-V!\"\nline2:  .asciz \"An RV32IM processor, drawn and simulated by Wire.\"\nline3:  .asciz \"Flip the switches (sw): the display shows sw * sw.\"\n\n        .text\nmain:   li   sp, 0x2000          # the stack: the top of the ram\n        la   a0, line1\n        li   a1, 0x3000          # the text screen, row 0\n        call puts\n        la   a0, line2\n        li   a1, 0x3080          # row 2: 64 characters a row\n        call puts\n        la   a0, line3\n        li   a1, 0x30C0          # row 3\n        call puts\n\n# the colour screen, 64 x 64, a byte per pixel: RRRGGGBB\n        li   t2, 0x2000          # pixel (0, 0)\n        li   t5, 64\n        li   t0, 0               # y\nyloop:  li   t1, 0               # x\nxloop:  srli t3, t1, 3           # red: x / 8\n        slli t3, t3, 5\n        srli t4, t0, 3           # green: y / 8\n        slli t4, t4, 2\n        or   t3, t3, t4\n        xor  t4, t0, t1          # blue: from x xor y\n        srli t4, t4, 4\n        andi t4, t4, 3\n        or   t3, t3, t4\n        sb   t3, 0(t2)\n        addi t2, t2, 1\n        addi t1, t1, 1\n        blt  t1, t5, xloop\n        addi t0, t0, 1\n        blt  t0, t5, yloop\n\n# for ever: the display shows the switches squared\n        li   t0, 0x4000\nshow:   lw   t1, 8(t0)           # the switches\n        mul  t1, t1, t1\n        sw   t1, 0(t0)           # the display\n        j    show\n\n# puts: the string at a0 onto the screen at a1\nputs:   lbu  t0, 0(a0)\n        beqz t0, done\n        sb   t0, 0(a1)\n        addi a0, a0, 1\n        addi a1, a1, 1\n        j    puts\ndone:   ret\n",
 "bounce.s": "# Bounce: a dot bouncing round the colour screen, leaving a trail that\n# changes colour. Run it at Max.\n        .text\nmain:   li   s0, 3               # x\n        li   s1, 0               # y\n        li   s2, 1               # dx\n        li   s3, 1               # dy\n        li   s4, 0x2000          # the screen\n        li   s5, 63              # the last row and column\n        li   s6, 0x03            # the colour, RRRGGGBB\nloop:   slli t0, s1, 6           # the pixel at (x, y): 0x2000 + 64 y + x\n        add  t0, t0, s0\n        add  t0, t0, s4\n        sb   s6, 0(t0)\n        addi s6, s6, 7           # the next colour\n        andi s6, s6, 255\n        li   t2, 20              # wait a little\nwait:   addi t2, t2, -1\n        bnez t2, wait\n        add  s0, s0, s2\n        add  s1, s1, s3\n        beqz s0, flipx\n        bne  s0, s5, checky\nflipx:  neg  s2, s2\nchecky: beqz s1, flipy\n        bne  s1, s5, loop\nflipy:  neg  s3, s3\n        j    loop\n",
 "fib.s": "# Fibonacci: every third Fibonacci number, in decimal, a row each. The\n# digits come from divu and remu (RV32M); the program stops at the end.\n        .text\nmain:   li   sp, 0x2000\n        li   s0, 0               # n\n        li   s1, 0               # F(n)\n        li   s2, 1               # F(n + 1)\n        li   s3, 0x3000          # the start of the row\n        li   s4, 0x4000\nrow:    mv   a1, s3\n        li   t0, 'F'\n        sb   t0, 0(a1)\n        addi a1, a1, 1\n        mv   a0, s0\n        call putnum\n        li   t0, '='\n        sb   t0, 1(a1)\n        addi a1, a1, 3\n        mv   a0, s1\n        call putnum\n        sw   s1, 0(s4)           # the display shows it too\n        li   t1, 3               # three steps on\nstep:   add  t0, s1, s2\n        mv   s1, s2\n        mv   s2, t0\n        addi s0, s0, 1\n        addi t1, t1, -1\n        bnez t1, step\n        addi s3, s3, 64          # the next row\n        li   t0, 0x3400          # 16 rows\n        blt  s3, t0, row\n        ecall                    # stop\n\n# putnum: a0 in decimal, at a1; a1 ends up after it\nputnum: addi sp, sp, -16\n        mv   t1, sp              # the digits, the last one first\n        li   t2, 10\ndigit:  remu t3, a0, t2\n        addi t3, t3, '0'\n        sb   t3, 0(t1)\n        addi t1, t1, 1\n        divu a0, a0, t2\n        bnez a0, digit\ncopy:   addi t1, t1, -1\n        lbu  t3, 0(t1)\n        sb   t3, 0(a1)\n        addi a1, a1, 1\n        bne  t1, sp, copy\n        addi sp, sp, 16\n        ret\n",
 "keys.s": "# Keys: type on the keypad (or the keyboard: 0-9, a-f) and the keys show\n# on the text screen, the last one on the display.\n        .data\nprompt: .asciz \"Press keys (click the keypad, or type 0-9 and a-f):\"\n\n        .text\nmain:   li   sp, 0x2000\n        li   s1, 0x4000\n        la   a0, prompt\n        li   a1, 0x3000\n        call puts\n        li   s0, 0x3040          # where the next key goes: row 1\n        li   s2, 0x3400          # the end of the screen\n\ndown:   lw   t0, 12(s1)          # the keypad: bit 4 while a key is held\n        andi t1, t0, 16\n        beqz t1, down\n        andi t0, t0, 15          # which key\n        sw   t0, 0(s1)\n        li   t1, 10\n        blt  t0, t1, digit\n        addi t0, t0, 'A' - 10\n        j    put\ndigit:  addi t0, t0, '0'\nput:    sb   t0, 0(s0)\n        addi s0, s0, 1\n        blt  s0, s2, up\n        li   s0, 0x3040          # full: start again under the prompt\nup:     lw   t0, 12(s1)          # wait until it is let go\n        andi t1, t0, 16\n        bnez t1, up\n        j    down\n\nputs:   lbu  t0, 0(a0)\n        beqz t0, done\n        sb   t0, 0(a1)\n        addi a0, a0, 1\n        addi a1, a1, 1\n        j    puts\ndone:   ret\n",
 "primes.s": "# Primes: the sieve of Eratosthenes in the ram, then every prime below\n# 1000 on the text screen, and how many on the display. Each prime lights\n# its pixel on the colour screen too (pixel n is prime n). Stops at the end.\n        .equ N, 1000\n        .data\nsieve:  .space N                 # sieve[n] = 1: n is crossed out\n\n        .text\nmain:   li   sp, 0x2000\n        la   s0, sieve\n        li   s1, 2               # p\n        li   s2, N\ncross:  mul  t0, s1, s1          # from p * p on\n        bge  t0, s2, show\n        add  t1, s0, s1\n        lbu  t1, 0(t1)\n        bnez t1, nextp           # p is crossed out: not a prime\n        li   t2, 1\nmark:   add  t1, s0, t0\n        sb   t2, 0(t1)\n        add  t0, t0, s1\n        blt  t0, s2, mark\nnextp:  addi s1, s1, 1\n        j    cross\n\nshow:   li   s1, 2               # n\n        li   s3, 0x3000          # where the next number goes\n        li   s4, 0               # how many\n        li   s5, 0x2000          # the colour screen\neach:   add  t1, s0, s1\n        lbu  t1, 0(t1)\n        bnez t1, skip\n        addi s4, s4, 1\n        add  t0, s5, s1          # pixel n\n        li   t1, 0xFC            # yellow\n        sb   t1, 0(t0)\n        mv   a0, s1\n        mv   a1, s3\n        call putnum\n        addi s3, a1, 1           # and a space\nskip:   addi s1, s1, 1\n        blt  s1, s2, each\n        li   t0, 0x4000\n        sw   s4, 0(t0)           # 168 of them\n        ebreak                   # stop\n\n# putnum: a0 in decimal, at a1; a1 ends up after it\nputnum: addi sp, sp, -16\n        mv   t1, sp\n        li   t2, 10\ndigit:  remu t3, a0, t2\n        addi t3, t3, '0'\n        sb   t3, 0(t1)\n        addi t1, t1, 1\n        divu a0, a0, t2\n        bnez a0, digit\ncopy:   addi t1, t1, -1\n        lbu  t3, 0(t1)\n        sb   t3, 0(a1)\n        addi a1, a1, 1\n        bne  t1, sp, copy\n        addi sp, sp, 16\n        ret\n"
};
/* panel.js - the Assembly panel of the RISC-V computer (rv32im.wire).
 *
 * Write a program (or pick one of programs/), Assemble and run: the words
 * go into the rom (instr) and the .data bytes into the ram's four byte
 * lanes (ram_b0 ... ram_b24), the computer is reset and the clock runs.
 * Underneath: the two screens, big, with the display and the switches;
 * the registers (the wires regs_1 ... regs_31 of the register file); and
 * the listing, with the instruction at the pc lit up. It works the page
 * only through wirePage (see wire_view.html). */
(function () {
"use strict";
const ABI = "zero ra sp gp tp t0 t1 t2 s0 s1 a0 a1 a2 a3 a4 a5 a6 a7 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 t3 t4 t5 t6".split(" ");
const ROM_WORDS = 1024, RAM_BASE = 0x1000, RAM_BYTES = 4096;
const STORE = "wire-rv32im-";
const store = {
  get(k) { try { return localStorage.getItem(STORE + k); } catch (e) { return null; } },
  set(k, v) { try { if (v === null) localStorage.removeItem(STORE + k); else localStorage.setItem(STORE + k, v); } catch (e) {} }
};
const hex = (v, n = 8) => (v >>> 0).toString(16).toUpperCase().padStart(n, "0");

wirePage.ready(page => {
  const rom = page.find("instr", "rom");
  const lanes = [0, 8, 16, 24].map(b => page.find("ram_b" + b, "ram"));
  const pixels = page.find("pixels", "ram"), chars = page.find("chars", "ram");
  const out = name => { const n = page.find(name, "output"); return n ? page.nets[n.in[0]] : null; };
  const netNamed = name => page.nets.find(n => n.name === name) || null;
  const haltedNet = out("halted"), pcNet = out("pc"), displayNet = out("display"), cyclesNet = netNamed("cycles");
  const regs = ABI.map((_, i) => netNamed("regs_" + i));
  if (!rom || lanes.some(l => !l)) return;   /* not the computer this panel is for */

  /* ------------------------------------------------------------ the panel */
  const css = document.createElement("style");
  css.textContent = `
@media (min-width: 900px) { #asmdrawer { width: min(700px, 58vw); } }
#asmdrawer .scroll { display: flex; flex-direction: column; overflow: hidden; }
#asmdrawer .rv-bar, #asmdrawer .rv-status, #asmdrawer .rv-ed, #asmdrawer .rv-tabs { flex: none; }
.rv-bar { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; padding: 8px 12px; border-bottom: 1px solid var(--panel-line); }
.rv-bar select, .rv-bar button { font: inherit; font-size: 14px; color: var(--ink); background: var(--paper); border: 1px solid var(--panel-line); border-radius: 4px; min-height: 32px; padding: 0 10px; }
.rv-bar button.go { background: var(--ink); color: var(--paper); border-color: var(--ink); font-weight: 600; }
.rv-bar button:hover { background: var(--hi-soft); color: var(--ink); }
.rv-bar button.go:hover { background: var(--ink); color: var(--paper); box-shadow: 0 0 0 3px var(--hi-soft); }
.rv-bar .edited { font-size: 12px; color: var(--ink-soft); }
.rv-status { padding: 6px 12px; font-size: 13px; color: var(--ink-soft); min-height: 1.6em; border-bottom: 1px solid var(--panel-line); }
.rv-status.bad { color: #c0392b; }
.rv-status a { color: inherit; cursor: pointer; text-decoration: underline; display: block; }
.rv-ed { display: flex; height: min(260px, 32vh); min-height: 120px; resize: vertical; overflow: hidden; border-bottom: 1px solid var(--panel-line); font: 13px/1.5 "IBM Plex Mono", ui-monospace, monospace; }
.rv-gut { margin: 0; padding: 6px 8px 6px 6px; text-align: right; color: var(--ink-soft); background: var(--paper); user-select: none; overflow: hidden; min-width: 3.2em; box-sizing: border-box; font: inherit; }
.rv-gut .err { color: #fff; background: #c0392b; border-radius: 2px; }
.rv-ed textarea { flex: 1; border: 0; margin: 0; padding: 6px 8px; resize: none; font: inherit; color: var(--ink); background: var(--panel); outline: none; white-space: pre; overflow: auto; tab-size: 8; }
.rv-tabs { display: flex; gap: 0; padding: 0 12px; border-bottom: 1px solid var(--panel-line); }
.rv-tabs button { font: inherit; font-size: 14px; color: var(--ink-soft); background: none; border: 0; border-bottom: 2px solid transparent; padding: 8px 10px; min-height: 0; }
.rv-tabs button[aria-selected="true"] { color: var(--ink); border-bottom-color: var(--hi); font-weight: 600; }
.rv-pane { padding: 10px 12px; flex: 1 1 auto; min-height: 0; overflow: auto; }
.rv-pane[hidden] { display: none; }
.rv-screens { display: flex; flex-wrap: wrap; gap: 12px; align-items: flex-start; }
.rv-screens canvas { image-rendering: pixelated; background: #05090a; border: 1.5px solid var(--ink); border-radius: 2px; max-width: 100%; }
.rv-screens figure { margin: 0; }
.rv-screens figcaption, .rv-io { font-size: 12px; color: var(--ink-soft); margin-top: 3px; }
.rv-io { display: flex; flex-wrap: wrap; gap: 6px 16px; align-items: center; margin-top: 10px; font-size: 13px; }
.rv-io b { font: 600 15px "IBM Plex Mono", ui-monospace, monospace; color: var(--ink); }
.rv-sw { display: inline-flex; gap: 2px; }
.rv-sw button { width: 22px; min-height: 26px; padding: 0; font: 12px "IBM Plex Mono", ui-monospace, monospace; border: 1px solid var(--panel-line); border-radius: 3px; background: var(--paper); color: var(--ink); }
.rv-sw button[aria-pressed="true"] { background: var(--hi); color: #000; border-color: var(--hi); }
.rv-regs { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 2px 10px; font: 12.5px/1.6 "IBM Plex Mono", ui-monospace, monospace; }
.rv-regs div { display: flex; gap: 6px; padding: 0 4px; border-radius: 3px; }
.rv-regs .n { color: var(--ink-soft); width: 6.5em; }
.rv-regs .chg { background: var(--hi-soft); }
.rv-list { font: 12.5px/1.55 "IBM Plex Mono", ui-monospace, monospace; }
.rv-list div { display: flex; gap: 12px; white-space: pre; padding: 0 6px; cursor: pointer; }
.rv-list div:hover { background: var(--hi-soft); }
.rv-list .a { color: var(--ink-soft); }
.rv-list .w { color: var(--ink-soft); }
.rv-list .cur { background: var(--hi-soft); box-shadow: inset 3px 0 var(--hi); }
.rv-follow { font-size: 13px; color: var(--ink-soft); margin-bottom: 6px; display: block; }
`;
  document.head.append(css);

  const d = page.addDrawer("asmdrawer", "Assembly", "Write a program for this computer, assemble it and run it");
  const body = d.body;
  body.innerHTML = `
<div class="rv-bar">
  <select aria-label="Program"></select><span class="edited"></span>
  <button class="go" title="Assemble the program, put it in the rom and ram, reset the computer and run the clock">Assemble and run</button>
  <button class="asm" title="Assemble and load it, then reset; Step goes one instruction at a time">Assemble</button>
  <button class="rev" title="Back to the program as it comes">Revert</button>
</div>
<div class="rv-status" role="status"></div>
<div class="rv-ed"><pre class="rv-gut" aria-hidden="true"></pre><textarea spellcheck="false" autocomplete="off" autocapitalize="off" aria-label="The program, in RISC-V assembly"></textarea></div>
<div class="rv-tabs" role="tablist">
  <button role="tab" data-p="scr" aria-selected="true">Screens</button>
  <button role="tab" data-p="reg" aria-selected="false">Registers</button>
  <button role="tab" data-p="lst" aria-selected="false">Listing</button>
</div>
<div class="rv-pane" data-p="scr">
  <div class="rv-screens">
    <figure><canvas class="px" width="64" height="64" style="width:192px;height:192px"></canvas><figcaption>colour screen, 64 × 64 (0x2000)</figcaption></figure>
    <figure><canvas class="tx"></canvas><figcaption>text screen, 64 × 16 (0x3000)</figcaption></figure>
  </div>
  <div class="rv-io"><span>display (0x4000) <b class="disp">0</b></span><span>switches (0x4008) <span class="rv-sw"></span></span><span class="cyc"></span></div>
</div>
<div class="rv-pane" data-p="reg" hidden><div class="rv-regs"></div></div>
<div class="rv-pane" data-p="lst" hidden><label class="rv-follow"><input type="checkbox" checked> follow the pc</label><div class="rv-list"></div></div>`;
  const $ = s => body.querySelector(s);
  const sel = $("select"), ta = $("textarea"), gut = $(".rv-gut"), status = $(".rv-status"), editedMark = $(".edited");

  /* ------------------------------------------------------------ programs */
  const names = Object.keys(RV_PROGRAMS);
  names.forEach(n => sel.append(new Option(n, n)));
  let current = names.includes(store.get("last")) ? store.get("last") : names[0];
  sel.value = current;
  const saved = name => store.get("prog:" + name);
  function loadText(name) {
    current = name;
    ta.value = saved(name) !== null ? saved(name) : RV_PROGRAMS[name];
    markEdited();
    gutter([]);
    store.set("last", name);
  }
  function markEdited() { editedMark.textContent = ta.value !== RV_PROGRAMS[current] ? "(edited)" : ""; }
  sel.onchange = () => { loadText(sel.value); setStatus(`${current}: Assemble and run to load it.`); };
  $(".rev").onclick = () => { store.set("prog:" + current, null); loadText(current); setStatus(`${current}, as it comes.`); };

  /* --------------------------------------------------------------- editor */
  function gutter(errLines) {
    const n = ta.value.split("\n").length, bad = new Set(errLines);
    let s = "";
    for (let i = 1; i <= n; i++) s += (bad.has(i) ? `<span class="err">${i}</span>` : i) + "\n";
    gut.innerHTML = s;
    gut.scrollTop = ta.scrollTop;
  }
  ta.addEventListener("scroll", () => { gut.scrollTop = ta.scrollTop; });
  ta.addEventListener("input", () => {
    if (ta.value === RV_PROGRAMS[current]) store.set("prog:" + current, null); else store.set("prog:" + current, ta.value);
    markEdited();
    gutter([]);
  });
  ta.addEventListener("keydown", e => {
    if (e.key === "Tab" && !e.shiftKey) {   /* to the next column of 8 */
      e.preventDefault();
      const s = ta.selectionStart, lineStart = ta.value.lastIndexOf("\n", s - 1) + 1, n = 8 - ((s - lineStart) % 8);
      ta.setRangeText(" ".repeat(n), s, ta.selectionEnd, "end");
      ta.dispatchEvent(new Event("input"));
    } else if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) { e.preventDefault(); go(true); }
    e.stopPropagation();   /* typing here is not the keypad, and space is not Step */
  });
  function goLine(ln) {
    const lines = ta.value.split("\n");
    let at = 0;
    for (let i = 0; i < ln - 1 && i < lines.length; i++) at += lines[i].length + 1;
    ta.focus();
    ta.setSelectionRange(at, at + (lines[ln - 1] || "").length);
    ta.scrollTop = Math.max(0, (ln - 4) * parseFloat(getComputedStyle(ta).lineHeight));
  }
  function setStatus(text, bad) { status.className = "rv-status" + (bad ? " bad" : ""); status.textContent = text; }

  /* -------------------------------------------------------- assemble, run */
  let listing = [], rowOf = new Map(), lastRow = -1, ranProgram = null;
  function go(run) {
    const r = RVAsm.assemble(ta.value, { textBase: 0, dataBase: RAM_BASE });
    if (!r.errors.length && r.text.length > ROM_WORDS) r.errors.push({ line: 1, msg: `the program is ${r.text.length} instructions; the rom holds ${ROM_WORDS}` });
    if (!r.errors.length && r.data.length > RAM_BYTES) r.errors.push({ line: 1, msg: `the data is ${r.data.length} bytes; the ram holds ${RAM_BYTES}` });
    if (r.errors.length) {
      status.className = "rv-status bad";
      status.textContent = "";
      r.errors.slice(0, 6).forEach(e => {
        const a = document.createElement("a");
        a.textContent = `line ${e.line}: ${e.msg}`;
        a.onclick = () => goLine(e.line);
        status.append(a);
      });
      if (r.errors.length > 6) status.append(`… and ${r.errors.length - 6} more`);
      gutter(r.errors.map(e => e.line));
      return;
    }
    gutter([]);
    const text = new Array(ROM_WORDS).fill(0);
    r.text.forEach((w, i) => { text[i] = w >>> 0; });
    page.setContents(rom, text);
    lanes.forEach((n, k) => {
      const bytes = new Array(RAM_BYTES / 4).fill(0);
      for (let i = 0; i < RAM_BYTES / 4; i++) bytes[i] = r.data[4 * i + k] || 0;
      page.setContents(n, bytes);
    });
    page.reset();
    buildListing(r.listing);
    ranProgram = current;
    const what = `${r.text.length} instruction${r.text.length === 1 ? "" : "s"}${r.data.length ? `, ${r.data.length} bytes of data` : ""}`;
    if (run) {
      page.speed("max");
      page.run(true);
      setStatus(`${current}: ${what}. Running.`);
    } else setStatus(`${current}: ${what}, loaded. Step runs one instruction; Run runs the clock.`);
  }
  $(".go").onclick = () => go(true);
  $(".asm").onclick = () => go(false);

  /* -------------------------------------------------------------- listing */
  const list = $(".rv-list"), follow = $(".rv-follow input");
  function buildListing(entries) {
    listing = entries; rowOf = new Map(); lastRow = -1;
    list.textContent = "";
    entries.forEach((e, i) => {
      const row = document.createElement("div");
      const a = Object.assign(document.createElement("span"), { className: "a", textContent: hex(e.addr, 4) });
      const w = Object.assign(document.createElement("span"), { className: "w", textContent: e.hex.padEnd(11) });
      const s = Object.assign(document.createElement("span"), { textContent: e.src.replace(/\s+$/, "") });
      row.append(a, w, s);
      row.onclick = () => goLine(e.line);
      list.append(row);
      if (/^[0-9a-f]{8}$/.test(e.hex) && e.addr < RAM_BASE && !rowOf.has(e.addr)) rowOf.set(e.addr, i);
    });
  }
  /* the rom as it comes holds hello.s: its listing, before anything is assembled */
  buildListing(RVAsm.assemble(RV_PROGRAMS[names[0]], { textBase: 0, dataBase: RAM_BASE }).listing);

  /* ------------------------------------------------------------ registers */
  const regBox = $(".rv-regs"), regCells = [], lastVals = new Array(34).fill(-1);
  const regRow = (name, title) => {
    const div = document.createElement("div");
    div.innerHTML = `<span class="n"></span><span class="v">--------</span>`;
    div.querySelector(".n").textContent = name;
    div.title = title || "";
    regBox.append(div);
    return div;
  };
  regCells.push(regRow("pc", "the address of the instruction being run"));
  ABI.forEach((a, i) => { if (i) regCells.push(regRow(`x${i} ${a}`)); });

  /* -------------------------------------------------------------- screens */
  const pxCanvas = $("canvas.px"), pxCtx = pxCanvas.getContext("2d"), pxImg = pxCtx.createImageData(64, 64);
  const txCanvas = $("canvas.tx"), CW = 8, CH = 13;
  txCanvas.width = 64 * CW * 2; txCanvas.height = 16 * CH * 2;
  txCanvas.style.width = 64 * CW + "px"; txCanvas.style.height = 16 * CH + "px";
  const txCtx = txCanvas.getContext("2d");
  const swBox = $(".rv-sw"), swNode = page.find("sw", "input"), swBtns = [];
  for (let b = 7; b >= 0; b--) {
    const btn = Object.assign(document.createElement("button"), { textContent: b, title: `switch ${b}` });
    btn.onclick = () => { if (swNode) page.setInput("sw", swNode.state ^ (1 << b)); };
    swBox.append(btn); swBtns[b] = btn;
  }
  let lastPx = null, lastTx = null;
  const same = (a, b) => { if (!a || a.length !== b.length) return false; for (let i = 0; i < b.length; i++) if (a[i] !== b[i]) return false; return true; };
  function drawScreens() {
    const pw = pixels && page.words(pixels);
    if (pw && !same(lastPx, pw)) {
      lastPx = Uint32Array.from(pw);
      const px = new Uint32Array(pxImg.data.buffer);
      for (let i = 0; i < 4096; i++) {   /* RRRGGGBB */
        const c = pw[i] & 255, r = (c >> 5) * 255 / 7, g = ((c >> 2) & 7) * 255 / 7, bl = (c & 3) * 255 / 3;
        px[i] = 0xFF000000 | (bl << 16) | (g << 8) | r;
      }
      pxCtx.putImageData(pxImg, 0, 0);
    }
    const cw = chars && page.words(chars);
    if (cw && !same(lastTx, cw)) {
      lastTx = Uint32Array.from(cw);
      txCtx.fillStyle = "#05090a"; txCtx.fillRect(0, 0, txCanvas.width, txCanvas.height);
      txCtx.font = `${11 * 2}px ui-monospace, "SF Mono", Menlo, Consolas, monospace`;
      txCtx.textBaseline = "top"; txCtx.textAlign = "center"; txCtx.fillStyle = "rgb(90,240,120)";
      for (let i = 0; i < cw.length; i++) {
        const c = cw[i] & 255;
        if (c <= 32 || c >= 127) continue;
        txCtx.fillText(String.fromCharCode(c), ((i % 64) * CW + CW / 2) * 2, Math.floor(i / 64) * CH * 2 + 2);
      }
    }
    if (displayNet) $(".disp").textContent = "0x" + hex(displayNet.v);
    if (swNode) for (let b = 0; b < 8; b++) swBtns[b].setAttribute("aria-pressed", (swNode.state >> b) & 1 ? "true" : "false");
    if (cyclesNet) $(".cyc").textContent = `${cyclesNet.v.toLocaleString("en")} cycles`;
  }

  /* --------------------------------------------- following the computer */
  let wasHalted = false, tab = "scr";
  function tick() {
    if (d.section.classList.contains("open")) {
      const pc = pcNet ? pcNet.v : 0;
      if (tab === "scr") drawScreens();
      if (tab === "reg") {
        const vals = [pc, ...regs.slice(1).map(n => (n ? n.v : 0))];
        vals.forEach((v, i) => {
          if (v === lastVals[i]) { regCells[i].classList.remove("chg"); return; }
          if (lastVals[i] !== -1) regCells[i].classList.add("chg");
          lastVals[i] = v;
          regCells[i].querySelector(".v").textContent = hex(v);
        });
      }
      if (tab === "lst") {
        const row = rowOf.has(pc) ? rowOf.get(pc) : -1;
        if (row !== lastRow) {
          list.querySelectorAll(".cur").forEach(x => x.classList.remove("cur"));
          if (row >= 0 && list.children[row]) {
            list.children[row].classList.add("cur");
            if (follow.checked) list.children[row].scrollIntoView({ block: "nearest" });
          }
          lastRow = row;
        }
      }
    }
    /* stopped by ecall, ebreak or a 1 at 0x4004: stop the clock too */
    const halted = haltedNet && haltedNet.v === 1;
    if (halted && !wasHalted && page.running()) {
      page.run(false);
      setStatus(`${ranProgram || "The program"} stopped (ecall, ebreak or 0x4004) after ${cyclesNet ? cyclesNet.v.toLocaleString("en") + " cycles" : "a while"}.`);
    }
    wasHalted = halted;
    requestAnimationFrame(tick);
  }
  body.querySelectorAll(".rv-tabs button").forEach(b => b.onclick = () => {
    tab = b.dataset.p;
    body.querySelectorAll(".rv-tabs button").forEach(x => x.setAttribute("aria-selected", x === b));
    body.querySelectorAll(".rv-pane").forEach(p => { p.hidden = p.dataset.p !== tab; });
    lastVals.fill(-1); lastRow = -1; lastPx = lastTx = null;
  });

  loadText(current);
  setStatus(ta.value === RV_PROGRAMS[names[0]] && current === names[0]
    ? "The rom holds hello.s: Run the clock, or Assemble and run. Pick another program above, or write your own."
    : `${current}: Assemble and run to load it (the rom holds hello.s).`);
  requestAnimationFrame(tick);
  if (innerWidth >= 1200) page.openDrawer("asmdrawer");   /* room for the circuit beside it */
});
})();
