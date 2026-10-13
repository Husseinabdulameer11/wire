#!/usr/bin/env node
/* asm8080.js - an assembler for the 8080 and 8085 (and so for the SAP-2
 * and SAP-3 in examples/). It writes the hex text that a Wire rom or ram
 * reads with file = "...".
 *
 *   node tools/asm8080.js program.asm            writes program.hex
 *   node tools/asm8080.js program.asm -o out.hex
 *   node tools/asm8080.js program.asm -l         also prints a listing
 *
 * The language, as Intel wrote it:
 *
 *   ; a comment
 *   start:  MVI  A, 5          ; label, instruction, operands
 *           LXI  H, table+2
 *           JNZ  start
 *   count   EQU  10            ; a name for a number
 *           ORG  100H          ; continue at address 0x100
 *   table:  DB   1, 2, 'hi', 0 ; bytes (and text)
 *           DW   start, 1234H  ; 16-bit words, low byte first
 *           DS   16            ; leave 16 bytes
 *
 * Numbers: 42, 2AH, 0x2A, 101010B, 0b101010, 'A'; $ is the address of the
 * current line. Expressions use + - * / % ( ), and HIGH(x), LOW(x).
 * Names and mnemonics may be written in either case. */
"use strict";
const fs = require("fs");

const REG = { B: 0, C: 1, D: 2, E: 3, H: 4, L: 5, M: 6, A: 7 };
const RP = { B: 0, D: 1, H: 2, SP: 3 };           /* LXI, INX, DCX, DAD */
const RPS = { B: 0, D: 1, H: 2, PSW: 3 };         /* PUSH, POP */
const RPX = { B: 0, D: 1 };                       /* LDAX, STAX */
const COND = { NZ: 0, Z: 1, NC: 2, C: 3, PO: 4, PE: 5, P: 6, M: 7 };

/* mnemonic -> [form, base opcode]; forms say what operands follow */
const OPS = {
  NOP: ["-", 0x00], HLT: ["-", 0x76], RLC: ["-", 0x07], RRC: ["-", 0x0F], RAL: ["-", 0x17],
  RAR: ["-", 0x1F], DAA: ["-", 0x27], CMA: ["-", 0x2F], STC: ["-", 0x37], CMC: ["-", 0x3F],
  RET: ["-", 0xC9], XCHG: ["-", 0xEB], XTHL: ["-", 0xE3], PCHL: ["-", 0xE9], SPHL: ["-", 0xF9],
  EI: ["-", 0xFB], DI: ["-", 0xF3], RIM: ["-", 0x20], SIM: ["-", 0x30],
  MOV: ["r,r", 0x40], MVI: ["r,n", 0x06], INR: ["r3", 0x04], DCR: ["r3", 0x05],
  ADD: ["r0", 0x80], ADC: ["r0", 0x88], SUB: ["r0", 0x90], SBB: ["r0", 0x98],
  ANA: ["r0", 0xA0], XRA: ["r0", 0xA8], ORA: ["r0", 0xB0], CMP: ["r0", 0xB8],
  ADI: ["n", 0xC6], ACI: ["n", 0xCE], SUI: ["n", 0xD6], SBI: ["n", 0xDE],
  ANI: ["n", 0xE6], XRI: ["n", 0xEE], ORI: ["n", 0xF6], CPI: ["n", 0xFE],
  IN: ["n", 0xDB], OUT: ["n", 0xD3],
  LXI: ["rp,nn", 0x01], INX: ["rp", 0x03], DCX: ["rp", 0x0B], DAD: ["rp", 0x09],
  PUSH: ["rps", 0xC5], POP: ["rps", 0xC1], LDAX: ["rpx", 0x0A], STAX: ["rpx", 0x02],
  LDA: ["nn", 0x3A], STA: ["nn", 0x32], LHLD: ["nn", 0x2A], SHLD: ["nn", 0x22],
  JMP: ["nn", 0xC3], CALL: ["nn", 0xCD], RST: ["rst", 0xC7]
};
for (const [c, n] of Object.entries(COND)) {
  OPS["J" + c] = ["nn", 0xC2 | n << 3];
  OPS["C" + c] = ["nn", 0xC4 | n << 3];
  OPS["R" + c] = ["-", 0xC0 | n << 3];
}
const SIZE = { "-": 1, "r,r": 1, r3: 1, r0: 1, rp: 1, rps: 1, rpx: 1, rst: 1, n: 2, "r,n": 2, "rp,nn": 3, nn: 3 };

class AsmError extends Error {}

/* split operands on commas outside quotes */
function splitOps(s) {
  const out = [];
  let cur = "", q = null;
  for (const ch of s) {
    if (q) { cur += ch; if (ch === q) q = null; continue; }
    if (ch === "'" || ch === '"') { q = ch; cur += ch; continue; }
    if (ch === ",") { out.push(cur.trim()); cur = ""; continue; }
    cur += ch;
  }
  if (cur.trim() || out.length) out.push(cur.trim());
  return out;
}

/* evaluate an expression; names come from syms. On the first pass unknown
 * names count as 0 (pass is 1); on the second they are errors. */
function evaluate(text, syms, here, pass) {
  const toks = [];
  const re = /\s*(?:('(?:[^'\\]|\\.)')|(\$)|([0-9][0-9A-Za-z_]*)|([A-Za-z_.?@][A-Za-z0-9_.?@]*)|(<<|>>|[-+*/%()&|^~]))/y;
  let m, pos = 0;
  const t = text.trim();
  while (pos < t.length) {
    re.lastIndex = pos;
    if (!(m = re.exec(t))) throw new AsmError(`cannot read "${t.slice(pos)}"`);
    pos = re.lastIndex;
    if (m[1]) toks.push({ n: m[1].length === 3 ? m[1].charCodeAt(1) : m[1].charCodeAt(2) });
    else if (m[2]) toks.push({ n: here });
    else if (m[3]) toks.push({ n: number(m[3]) });
    else if (m[4]) toks.push({ id: m[4].toUpperCase() });
    else toks.push({ op: m[5] });
    while (pos < t.length && t[pos] === " ") pos++;
  }
  let i = 0;
  const peek = () => toks[i], take = () => toks[i++];
  const prim = () => {
    const k = take();
    if (!k) throw new AsmError(`"${t}" ends too soon`);
    if (k.op === "(") { const v = expr(); if (!take() || toks[i - 1].op !== ")") throw new AsmError(`missing ) in "${t}"`); return v; }
    if (k.op === "-") return -prim();
    if (k.op === "+") return prim();
    if (k.op === "~") return ~prim();
    if (k.n !== undefined) return k.n;
    if (k.id === "HIGH" || k.id === "LOW") {
      const v = prim();
      return k.id === "HIGH" ? (v >> 8) & 255 : v & 255;
    }
    if (k.id !== undefined) {
      if (k.id in syms) return syms[k.id];
      if (pass === 1) return 0;
      throw new AsmError(`${k.id} is not defined`);
    }
    throw new AsmError(`unexpected ${k.op} in "${t}"`);
  };
  const LEVELS = [["|"], ["^"], ["&"], ["<<", ">>"], ["+", "-"], ["*", "/", "%"]];
  const level = l => {
    if (l === LEVELS.length) return prim();
    let v = level(l + 1);
    while (peek() && LEVELS[l].includes(peek().op)) {
      const o = take().op, r = level(l + 1);
      v = o === "+" ? v + r : o === "-" ? v - r : o === "*" ? v * r : o === "/" ? Math.trunc(v / r)
        : o === "%" ? v % r : o === "&" ? v & r : o === "|" ? v | r : o === "^" ? v ^ r : o === "<<" ? v << r : v >> r;
    }
    return v;
  };
  const expr = () => level(0);
  const v = expr();
  if (i !== toks.length) throw new AsmError(`cannot read "${t}"`);
  return v;
}

function number(s) {
  const u = s.toUpperCase().replace(/_/g, "");
  let m;
  if ((m = /^0X([0-9A-F]+)$/.exec(u))) return parseInt(m[1], 16);
  if ((m = /^0B([01]+)$/.exec(u))) return parseInt(m[1], 2);
  if ((m = /^([0-9][0-9A-F]*)H$/.exec(u))) return parseInt(m[1], 16);
  if ((m = /^([01]+)B$/.exec(u))) return parseInt(m[1], 2);
  if ((m = /^([0-7]+)[OQ]$/.exec(u))) return parseInt(m[1], 8);
  if ((m = /^([0-9]+)D?$/.exec(u))) return parseInt(m[1], 10);
  throw new AsmError(`${s} is not a number (write hex as 2AH or 0x2A)`);
}

function assemble(src, name = "program") {
  const lines = src.split(/\r?\n/);
  const syms = {};
  let out = [];
  for (let pass = 1; pass <= 2; pass++) {
    let here = 0;
    out = [];
    lines.forEach((raw, ln) => {
      const fail = msg => { throw new AsmError(`${name}:${ln + 1}: ${msg}`); };
      try {
        /* strip the comment (not inside quotes) */
        let text = "", q = null;
        for (const ch of raw) {
          if (q) { text += ch; if (ch === q) q = null; continue; }
          if (ch === "'" || ch === '"') q = ch;
          if (ch === ";") break;
          text += ch;
        }
        let m, label = null;
        text = text.trim();
        if ((m = /^([A-Za-z_.?@][A-Za-z0-9_.?@]*)\s*:\s*(.*)$/.exec(text))) { label = m[1].toUpperCase(); text = m[2]; }
        /* NAME EQU value */
        if ((m = /^([A-Za-z_.?@][A-Za-z0-9_.?@]*)\s+(EQU|SET)\s+(.+)$/i.exec(text))) {
          const k = m[1].toUpperCase();
          if (pass === 1 && k in syms && m[2].toUpperCase() === "EQU") fail(`${m[1]} is defined twice`);
          syms[k] = evaluate(m[3], syms, here, pass);
          out.push({ ln, addr: here, bytes: [], raw });
          return;
        }
        if (label) {
          if (pass === 1 && label in syms) fail(`${label} is defined twice`);
          if (label in REG || label in RP || label === "PSW" || label in OPS) fail(`${label} is a register or instruction name`);
          syms[label] = here;
        }
        if (!text) { out.push({ ln, addr: here, bytes: [], raw }); return; }
        const sp = text.search(/\s/);
        const mn = (sp < 0 ? text : text.slice(0, sp)).toUpperCase();
        const args = sp < 0 ? [] : splitOps(text.slice(sp + 1));
        const ev = a => evaluate(a, syms, here, pass);
        const bytes = [];
        const byte = (v, what) => {
          if (pass === 2 && (v < -128 || v > 255)) fail(`${what || "the value"} ${v} does not fit in a byte`);
          bytes.push(v & 255);
        };
        const word16 = v => {
          if (pass === 2 && (v < -32768 || v > 65535)) fail(`${v} does not fit in 16 bits`);
          bytes.push(v & 255, (v >> 8) & 255);
        };
        if (mn === "ORG") { here = ev(args[0]); out.push({ ln, addr: here, bytes: [], raw, org: true }); return; }
        if (mn === "END") { out.push({ ln, addr: here, bytes: [], raw }); return; }
        if (mn === "DS") { const n = ev(args[0]); out.push({ ln, addr: here, bytes: [], raw }); here += n; out.push({ ln, addr: here, bytes: [], raw: "", org: true }); return; }
        if (mn === "DB") {
          for (const a of args) {
            if ((m = /^(['"])(.*)\1$/.exec(a)) && m[2].length !== 1) for (const ch of m[2]) bytes.push(ch.charCodeAt(0) & 255);
            else byte(ev(a));
          }
        } else if (mn === "DW") {
          for (const a of args) word16(ev(a));
        } else {
          const def = OPS[mn];
          if (!def) fail(`${mn} is not an 8080 instruction`);
          const [form, base] = def;
          const want = { "-": 0, "r,r": 2, "r,n": 2, "rp,nn": 2 }[form] ?? 1;
          if (args.length !== want) fail(`${mn} takes ${want} operand${want === 1 ? "" : "s"}, not ${args.length}`);
          const reg = (a, table, what) => {
            const k = a.toUpperCase();
            if (!(k in table)) fail(`${mn}: ${a} is not ${what} (${Object.keys(table).join(", ")})`);
            return table[k];
          };
          switch (form) {
          case "-": bytes.push(base); break;
          case "r,r": {
            const d = reg(args[0], REG, "a register"), s = reg(args[1], REG, "a register");
            if (d === 6 && s === 6) fail("MOV M,M is not an instruction (it is HLT)");
            bytes.push(base | d << 3 | s); break;
          }
          case "r,n": bytes.push(base | reg(args[0], REG, "a register") << 3); byte(ev(args[1])); break;
          case "r3": bytes.push(base | reg(args[0], REG, "a register") << 3); break;
          case "r0": bytes.push(base | reg(args[0], REG, "a register")); break;
          case "n": bytes.push(base); byte(ev(args[0])); break;
          case "rp,nn": bytes.push(base | reg(args[0], RP, "a register pair") << 4); word16(ev(args[1])); break;
          case "rp": bytes.push(base | reg(args[0], RP, "a register pair") << 4); break;
          case "rps": bytes.push(base | reg(args[0], RPS, "a register pair") << 4); break;
          case "rpx": bytes.push(base | reg(args[0], RPX, "B or D") << 4); break;
          case "nn": bytes.push(base); word16(ev(args[0])); break;
          case "rst": {
            const n = ev(args[0]);
            if (pass === 2 && (n < 0 || n > 7)) fail("RST takes 0 to 7");
            bytes.push(base | (n & 7) << 3); break;
          }
          }
          if (pass === 1 && bytes.length !== SIZE[form]) throw new Error("size");
        }
        out.push({ ln, addr: here, bytes, raw });
        here += bytes.length;
        if (here > 0x10000) fail("the program runs past FFFF");
      } catch (e) {
        if (e instanceof AsmError && !/^\S+:\d+: /.test(e.message)) fail(e.message);
        throw e;
      }
    });
  }
  return { lines: out, syms };
}

/* the hex text for Wire: @address, then the bytes of each line with the
 * line as a comment */
function toHex(res, name) {
  const text = [`# ${name}, assembled by tools/asm8080.js`];
  let next = -1;
  for (const l of res.lines) {
    if (!l.bytes.length) continue;
    if (l.addr !== next) text.push(`@${l.addr.toString(16).toUpperCase().padStart(4, "0")}`);
    const hex = l.bytes.map(b => b.toString(16).toUpperCase().padStart(2, "0")).join(" ");
    text.push(`${hex.padEnd(12)} # ${l.addr.toString(16).toUpperCase().padStart(4, "0")}  ${l.raw.trim()}`);
    next = l.addr + l.bytes.length;
  }
  return text.join("\n") + "\n";
}

function listing(res) {
  return res.lines.map(l => {
    const a = l.bytes.length || l.org ? l.addr.toString(16).toUpperCase().padStart(4, "0") : "    ";
    const hex = l.bytes.slice(0, 4).map(b => b.toString(16).toUpperCase().padStart(2, "0")).join(" ");
    return `${a}  ${hex.padEnd(12)} ${l.raw}`;
  }).join("\n");
}

if (require.main === module) {
  const args = process.argv.slice(2);
  let input = null, output = null, list = false;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "-o") output = args[++i];
    else if (args[i] === "-l") list = true;
    else if (args[i] === "-h" || args[i] === "--help") { console.log("usage: node tools/asm8080.js program.asm [-o program.hex] [-l]"); process.exit(0); }
    else input = args[i];
  }
  if (!input) { console.error("usage: node tools/asm8080.js program.asm [-o program.hex] [-l]"); process.exit(2); }
  try {
    const res = assemble(fs.readFileSync(input, "utf8"), input);
    output = output || input.replace(/\.[^./\\]*$/, "") + ".hex";
    fs.writeFileSync(output, toHex(res, input.replace(/^.*[\\/]/, "")));
    if (list) console.log(listing(res));
    const used = res.lines.reduce((n, l) => n + l.bytes.length, 0);
    console.error(`${input}: ${used} bytes, wrote ${output}`);
  } catch (e) {
    if (e instanceof AsmError) { console.error(e.message); process.exit(1); }
    throw e;
  }
}
module.exports = { assemble, toHex, listing, AsmError };
