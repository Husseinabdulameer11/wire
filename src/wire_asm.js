/* wire_asm.js - an assembler for any CPU, driven by its instruction set.
 *
 * The instruction set is a .isa file (src/isa.gram). wire parses it and puts
 * the syntax tree in the page (D.cpu.isa.tree); the page and tests/asm hand
 * that tree to load(), and get back a description they can assemble with:
 *
 *   const isa = WireAsm.load(tree);          throws on a mistake in the .isa
 *   WireAsm.assemble(isa, source, { textBase, dataBase })
 *   -> { text: [words], data: [bytes], symbols, listing, errors: [{ line, msg }] }
 *
 * Two sections: .text (instructions, from textBase) and .data (bytes, from
 * dataBase). Labels end with a colon. Comments start with # or ;
 * Values: 42, -7, 0x2A, 0b101, 'A', a label, label+4, %hi(label) ... whatever
 * the .isa defines with func. Directives: .text .data .ascii .asciz .string
 * .space .zero .align .equ .set .globl (ignored), and the ones the .isa gives
 * with `data` (.byte .half .word). The result has the shape rvasm.js had. */
"use strict";
const WireAsm = (() => {

class AsmError extends Error {}
const fail = msg => { throw new AsmError(msg); };
class Unknown extends AsmError {}

const num = s => /^0x/i.test(s) ? parseInt(s.slice(2).replace(/_/g, ""), 16)
              : /^0b/i.test(s) ? parseInt(s.slice(2).replace(/_/g, ""), 2) : parseInt(s, 10);
const unescape = s => s.replace(/\\(n|t|r|0|\\|"|'|x[0-9a-fA-F]{2})/g, (_, e) =>
  e === "n" ? "\n" : e === "t" ? "\t" : e === "r" ? "\r" : e === "0" ? "\0" : e[0] === "x" ? String.fromCharCode(parseInt(e.slice(1), 16)) : e);

/* ---- expressions: one small AST, built from the .isa tree or from a program's text */
function fromTree(n) {
  if (!n.c) {
    if (n.t === "number") return { k: "n", v: num(n.x) };
    if (n.t === "char") return { k: "n", v: unescape(n.x.slice(1, -1)).charCodeAt(0) };
    if (n.t === "ident") return { k: "s", name: n.x };
    fail(`unexpected ${n.t} "${n.x}" in an expression`);
  }
  const c = n.c;
  switch (n.t) {
  case "neg": return { k: "u", op: "-", a: fromTree(c[0]) };
  case "inv": return { k: "u", op: "~", a: fromTree(c[0]) };
  case "call": return { k: "f", name: c[0].x.slice(1), args: c.slice(1).map(fromTree) };
  case "orx": case "xorx": case "andx": case "prodx": {
    const op = { orx: "|", xorx: "^", andx: "&", prodx: "*" }[n.t];
    return c.map(fromTree).reduce((a, b) => ({ k: "b", op, a, b }));
  }
  case "shiftx": case "sumx": {
    let a = fromTree(c[0]);
    for (let i = 1; i < c.length; i += 2) a = { k: "b", op: c[i].x, a, b: fromTree(c[i + 1]) };
    return a;
  }
  }
  fail(`unexpected ${n.t} in an expression`);
}

/* a program's text -> the same AST (numbers, chars, symbols, %func(..), + - * & | ^ << >> ~, ( ), and ==, != etc.) */
function parseExpr(s) {
  const re = /\s*(0x[0-9a-f_]+|0b[01_]+|[0-9]+|'(?:\\.|[^\\'])'|%[A-Za-z_]\w*|[A-Za-z_.$][\w.$]*|<<|>>|==|!=|<=|>=|[-+*&|^~(),<>])/iy;
  const toks = []; let i = 0, m;
  s = s.trim();
  while (i < s.length) {
    re.lastIndex = i; m = re.exec(s);
    if (!m) fail(`"${s}" is not a number or a label`);
    toks.push(m[1]); i = re.lastIndex;
    while (i < s.length && /\s/.test(s[i])) i++;
  }
  let p = 0;
  const peek = () => toks[p], eat = () => toks[p++];
  const lv = [["==", "!=", "<=", ">=", "<", ">"], ["|"], ["^"], ["&"], ["<<", ">>"], ["+", "-"], ["*"]];
  function bin(l) {
    if (l === lv.length) return unary();
    let a = bin(l + 1);
    while (lv[l].includes(peek())) { const op = eat(); a = { k: "b", op, a, b: bin(l + 1) }; }
    return a;
  }
  function unary() {
    const t = eat();
    if (t === undefined) fail(`expected a value, got "${s}"`);
    if (t === "-" || t === "~") return { k: "u", op: t, a: unary() };
    if (t === "+") return unary();
    if (t === "(") { const e = bin(0); if (eat() !== ")") fail(`missing ) in "${s}"`); return e; }
    if (t[0] === "%") {
      if (eat() !== "(") fail(`expected ( after ${t}`);
      const args = [];
      if (peek() !== ")") { args.push(bin(0)); while (peek() === ",") { eat(); args.push(bin(0)); } }
      if (eat() !== ")") fail(`missing ) in "${s}"`);
      return { k: "f", name: t.slice(1), args };
    }
    if (/^(0x|0b|[0-9])/i.test(t)) return { k: "n", v: num(t) };
    if (t[0] === "'") return { k: "n", v: unescape(t.slice(1, -1)).charCodeAt(0) };
    if (/^[A-Za-z_.$]/.test(t)) return { k: "s", name: t };
    fail(`"${t}" is not a number or a label`);
  }
  const e = bin(0);
  if (p < toks.length) fail(`"${s}" is not a number or a label`);
  return e;
}

const KIND = /^(?:(reg)|(any)|rel(\d+)(?:@(\d+))?|([sub])(\d+))$/;
function parseKind(k) {
  const m = KIND.exec(k);
  if (!m) fail(`unknown operand kind ${k}`);
  if (m[1]) return { type: "reg" };
  if (m[2]) return { type: "any" };
  if (m[3]) return { type: "rel", bits: +m[3], align: m[4] ? +m[4] : 1 };
  return { type: m[5], bits: +m[6] };
}
const fits = (v, k) => {
  if (k.type === "s") return v >= -(2 ** (k.bits - 1)) && v < 2 ** (k.bits - 1);
  if (k.type === "u") return v >= 0 && v < 2 ** k.bits;
  if (k.type === "b") return v >= -(2 ** (k.bits - 1)) && v < 2 ** k.bits;
  return false;
};
const bitsOf = k => k.type === "reg" || k.type === "any" ? 0 : k.bits;

/* ---- loading a parsed .isa --------------------------------------------- */
function load(tree) {
  const isa = { name: "", width: 32, unit: 0, endian: "little", regs: new Map(), nreg: 0, formats: new Map(),
                funcs: new Map(), data: new Map(), over: new Map() };
  const regNames = [];                 /* index -> primary name, for names/alias */
  for (const st of tree.c) {
    const c = st.c;
    try { stmt(st, c); } catch (e) { throw lined(e, st); }
  }
  function stmt(st, c) {
    switch (st.t) {
    case "header":
      isa.name = c[0].x.slice(1, -1);
      for (const o of (c[1] ? c[1].c : [])) {
        const v = o.c[1];
        isa[o.c[0].x] = v.t === "number" ? num(v.x) : v.t === "string" ? v.x.slice(1, -1) : v.x;
      }
      break;
    case "regs": {
      const [pre, a, b] = [c[0].x, num(c[1].x), num(c[2].x)];
      for (let i = a; i <= b; i++) { isa.regs.set(pre + i, i - a); regNames[i - a] = pre + i; }
      isa.nreg = Math.max(isa.nreg, b - a + 1);
      break;
    }
    case "names": c.forEach((n, i) => { isa.regs.set(n.x, i); }); break;
    case "alias": {
      const v = c[1].t === "number" ? num(c[1].x) : isa.regs.get(c[1].x);
      if (v === undefined) fail(`alias ${c[0].x}: there is no register ${c[1].x}`);
      isa.regs.set(c[0].x, v);
      break;
    }
    case "datadef": c.forEach(d => isa.data.set("." + d.c[0].x, num(d.c[1].x))); break;
    case "func": isa.funcs.set(c[0].x.slice(1), { param: c[1].x, body: fromTree(c[2]) }); break;
    case "format": {
      const f = { name: c[0].x, fields: [], width: 0, spans: new Map() };
      for (const fl of c.slice(1)) {
        if (fl.t === "widthfield") f.fields.push({ name: fl.c[0].x, width: num(fl.c[1].x), slices: null });
        else {
          const slices = fl.c.slice(1).map(s => { const hi = num(s.c[0].x), lo = s.c[1] ? num(s.c[1].x) : hi; return [hi, lo]; });
          const width = slices.reduce((t, [h, l]) => t + h - l + 1, 0);
          f.fields.push({ name: fl.c[0].x, width, slices });
          f.spans.set(fl.c[0].x, Math.max(f.spans.get(fl.c[0].x) || 0, ...slices.map(s => s[0] + 1)));
        }
      }
      f.width = f.fields.reduce((t, x) => t + x.width, 0);
      if (isa.formats.has(f.name)) fail(`format ${f.name} is defined twice`);
      isa.formats.set(f.name, f);
      break;
    }
    case "instr": case "pseudo": {
      const ent = { op: c[0].x.toLowerCase(), pseudo: st.t === "pseudo", pattern: [], line: 0 };
      let i = 1;
      if (c[i] && c[i].t === "pattern") {
        ent.pattern = c[i].c.map(o => ({ name: o.c[0].x, kind: parseKind(o.c[1].x), mem: o.c[2] ? { name: o.c[2].x, kind: parseKind(o.c[3].x) } : null }));
        i++;
      }
      if (ent.pseudo) {
        ent.body = [];
        while (c[i] && c[i].t === "asmop") {
          ent.body.push({ op: c[i].c[0].x.toLowerCase(), args: c[i].c.slice(1).map(a => ({ text: null, e: fromTree(a.c ? a.c[0] : a), mem: a.t === "arg" && a.c && a.c.length > 1 ? fromTree(a.c[1]) : null })) });
          i++;
        }
      } else {
        ent.format = c[i++].x;
        ent.assigns = [];
        while (c[i] && c[i].t === "assign") { ent.assigns.push({ name: c[i].c[0].x, e: fromTree(c[i].c[1]) }); i++; }
      }
      if (c[i] && c[i].t === "guard") {
        const g = c[i].c[0];
        ent.guard = g.c.length === 2 && g.c[1].t === "kind" ? { fits: parseKind(g.c[1].x), a: fromTree(g.c[0]) }
                  : g.c.length === 3 ? { cmp: g.c[1].x, a: fromTree(g.c[0]), b: fromTree(g.c[2]) } : { a: fromTree(g.c[0]) };
      }
      ent.line = st.l || 0;
      (isa.over.get(ent.op) || isa.over.set(ent.op, []).get(ent.op)).push(ent);
      break;
    }
    }
  }
  if (!isa.unit) isa.unit = isa.width;
  isa.regbits = Math.max(1, Math.ceil(Math.log2(isa.nreg || 2)));
  isa.iunits = isa.width / isa.unit;
  /* check every instruction against its format now, so a mistake in the .isa is found at once */
  for (const list of isa.over.values()) for (const e of list) {
    try { if (!e.pseudo) check(isa, e); else checkPseudo(isa, e); } catch (x) { throw lined(x, { l: e.line }); }
  }
  return isa;
}
/* a mistake in the .isa, with the line it is on */
function lined(e, st) {
  if (e instanceof AsmError && st.l && !e.line) { const x = new AsmError(`line ${st.l}: ${e.message}`); x.line = st.l; return x; }
  return e;
}
function checkPseudo(isa, e) {
  const names = new Set(["pc"]);
  for (const o of e.pattern) { names.add(o.name); if (o.mem) names.add(o.mem.name); }
  for (const b of e.body) if (!isa.over.has(b.op)) fail(`${e.op} becomes ${b.op}, which the instruction set does not have`);
}

function check(isa, e) {
  const f = isa.formats.get(e.format);
  if (!f) fail(`${e.op}: there is no format ${e.format}`);
  if (f.width !== isa.width) fail(`${e.op}: format ${f.name} is ${f.width} bits, the instruction is ${isa.width}`);
  const given = new Map();
  for (const o of e.pattern) { given.set(o.name, o.kind); if (o.mem) given.set(o.mem.name, o.mem.kind); }
  for (const a of e.assigns) { if (given.has(a.name)) fail(`${e.op}: ${a.name} is both an operand and fixed`); given.set(a.name, null); }
  for (const [n, k] of [...given]) if (k && k.type === "any") given.delete(n);
  const seen = new Set();
  for (const fl of f.fields) {
    if (!given.has(fl.name)) fail(`${e.op}: ${fl.name} in format ${f.name} is neither an operand nor fixed`);
    seen.add(fl.name);
    const k = given.get(fl.name);
    const span = fl.slices ? f.spans.get(fl.name) : fl.width;
    if (k && k.type === "reg" && isa.regbits > span) fail(`${e.op}: register ${fl.name} needs ${isa.regbits} bits, the field has ${span}`);
    if (k && k.type !== "reg" && bitsOf(k) > span) fail(`${e.op}: ${fl.name} is ${bitsOf(k)} bits, the field only holds ${span}`);
  }
  for (const n of given.keys()) if (!seen.has(n)) fail(`${e.op}: ${n} is not a field of format ${f.name}`);
}

/* ---- assembling --------------------------------------------------------- */
function stripComment(line) {
  let q = null;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (q) { if (c === "\\") i++; else if (c === q) q = null; continue; }
    if (c === '"' || c === "'") q = c; else if (c === "#" || c === ";") return line.slice(0, i);
  }
  return line;
}
function splitArgs(s) {
  const out = []; let cur = "", depth = 0, q = null;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (q) { cur += c; if (c === "\\") cur += s[++i] || ""; else if (c === q) q = null; continue; }
    if (c === '"' || c === "'") { q = c; cur += c; continue; }
    if (c === "(") depth++; if (c === ")") depth--;
    if (c === "," && depth === 0) { out.push(cur.trim()); cur = ""; continue; }
    cur += c;
  }
  if (cur.trim()) out.push(cur.trim());
  return out;
}

function assemble(isa, src, opts = {}) {
  const textBase = opts.textBase || 0, dataBase = opts.dataBase === undefined ? 0x1000 : opts.dataBase;
  const lines = src.split(/\r?\n/), symbols = new Map(), errors = [], frozen = new Map();
  const U = isa.iunits, wmask = isa.width >= 32 ? 0xFFFFFFFF : 2 ** isa.width - 1;
  let curLn = 0, pass, sec, pc, text, data, listing;

  const regOf = s => { const r = isa.regs.get(s.trim()) ?? isa.regs.get(s.trim().toLowerCase()); return r; };
  const int = v => (v | 0);

  /* an expression's value; unknown symbols are 0 in pass 1 (unless `need`) */
  function evalE(e, env, need) {
    switch (e.k) {
    case "n": return e.v;
    case "s":
      if (env && env.has(e.name)) return env.get(e.name);
      if (symbols.has(e.name)) return symbols.get(e.name);
      if (pass === 1 && !need) return 0;
      throw new Unknown(`there is no label or constant called ${e.name}`);
    case "u": { const v = evalE(e.a, env, need); return e.op === "-" ? -v : ~v; }
    case "b": {
      const a = evalE(e.a, env, need), b = evalE(e.b, env, need);
      switch (e.op) {
      case "+": return a + b; case "-": return a - b; case "*": return a * b;
      case "&": return int(a) & int(b); case "|": return int(a) | int(b); case "^": return int(a) ^ int(b);
      case "<<": return int(a) << b; case ">>": return int(a) >> b;
      case "==": return +(a === b); case "!=": return +(a !== b); case "<": return +(a < b);
      case "<=": return +(a <= b); case ">": return +(a > b); case ">=": return +(a >= b);
      }
      break;
    }
    case "f": {
      const args = e.args.map(a => evalE(a, env, need));
      if (e.name === "sext") { const n = args[1]; return (args[0] << (32 - n)) >> (32 - n); }
      const fn = isa.funcs.get(e.name);
      if (!fn) fail(`there is no function %${e.name}`);
      return evalE(fn.body, new Map([[fn.param, args[0]]]), need);
    }
    }
    fail("bad expression");
  }
  const val = (s, need, env) => evalE(parseExpr(s), env, need);

  /* "8(sp)" -> ["8", "sp"] when the last group is a register */
  const memSplit = s => {
    const m = /^(.*)\(\s*([A-Za-z0-9_.$]+)\s*\)$/.exec(s.trim());
    return m && regOf(m[2]) !== undefined ? [m[1].trim(), m[2]] : null;
  };

  /* the register names, for a message: x0-x31, or zero ra sp ... */
  const regHelp = () => {
    const names = [...isa.regs.keys()], pre = names.length ? /^[A-Za-z_]+/.exec(names[0])[0] : "r";
    const plain = names.filter(n => !new RegExp("^" + pre + "\\d+$").test(n));
    return `${pre}0-${pre}${isa.nreg - 1}` + (plain.length ? `, or ${plain.slice(0, 12).join(" ")}${plain.length > 12 ? " ..." : ""}` : "");
  };
  const form = e => e.op + (e.pattern.length ? " " : "") + e.pattern.map(o => o.name + (o.mem ? "(" + o.mem.name + ")" : "")).join(", ");

  /* which overload fits these arguments? the first whose shape fits and whose
   * `if` holds; the choice is made in pass 1 and kept, so addresses stay put */
  function pick(op, args, key, pcNow) {
    const list = isa.over.get(op);
    if (!list) fail(`there is no instruction called ${op}`);
    const at = frozen.get(key);
    const cands = at === undefined ? list.map((e, i) => i) : [at];
    let why = null;
    for (const i of cands) {
      const e = list[i];
      if (e.pattern.length !== args.length) continue;
      const bind = new Map(), text = new Map();
      let bad = null;
      e.pattern.forEach((o, j) => {
        if (bad) return;
        const a = args[j], ms = memSplit(a);
        if (o.mem) {
          if (!ms) { bad = `expected an address like 8(sp), got "${a}"`; return; }
          text.set(o.name, ms[0] || "0"); text.set(o.mem.name, ms[1]);
          bind.set(o.mem.name, regOf(ms[1]));
          bind.set(o.name, { src: ms[0] || "0" });
        } else if (o.kind.type === "reg") {
          if (regOf(a) === undefined) { bad = `"${a}" is not a register (${regHelp()})`; return; }
          text.set(o.name, a); bind.set(o.name, regOf(a));
        } else if (o.kind.type === "any") {
          text.set(o.name, a); bind.set(o.name, { src: a });
        } else {
          if (ms && ms[0] && !/^%/.test(a)) { bad = `${a} is not a value; write the address as ${form(e)}`; return; }
          text.set(o.name, a); bind.set(o.name, { src: a });
        }
      });
      if (bad) { why = why || bad; continue; }
      const env = new Map([["pc", pcNow]]);
      for (const o of e.pattern) {
        const b = bind.get(o.name);
        if (b && b.src !== undefined && o.kind.type !== "any") {
          try { env.set(o.name, val(b.src, true)); } catch (x) { if (!(x instanceof Unknown)) throw x; env.set(o.name, undefined); }
        }
      }
      if (at === undefined && e.guard && !guardHolds(e.guard, env)) { why = why || `${op}: no form of it fits these operands`; continue; }
      if (at === undefined) frozen.set(key, i);
      return { e, bind, text };
    }
    if (why) fail(why);
    fail(`expected ${[...new Set(list.map(form))].join("  or  ")}`);
  }
  function guardHolds(g, env) {
    try {
      const a = evalE(g.a, env, true);
      if (g.fits) return fits(a, g.fits);
      if (g.cmp) return !!evalE({ k: "b", op: g.cmp, a: g.a, b: g.b }, env, true);
      return !!a;
    } catch (x) { if (x instanceof Unknown) return false; throw x; }
  }

  /* substitute operand names in a pseudo's template; returns source text */
  const sub = (e, text, pcNow) => {
    const go = n => {
      switch (n.k) {
      case "n": return String(n.v);
      case "s": return n.name === "pc" ? String(pcNow) : text.has(n.name) ? paren(text.get(n.name)) : n.name;
      case "u": return n.op + "(" + go(n.a) + ")";
      case "b": return "(" + go(n.a) + n.op + go(n.b) + ")";
      case "f": return "%" + n.name + "(" + n.args.map(go).join(",") + ")";
      }
    };
    return go(e);
  };
  const paren = t => /^[\w.$]+$/.test(t) ? t : "(" + t + ")";

  /* one instruction or pseudo-instruction -> its words */
  function instr(op, args, key, depth) {
    if (depth > 4) fail("pseudo-instructions nest too deeply");
    const { e, bind, text } = pick(op, args, key, pc);
    if (e.pseudo) {
      const words = [];
      e.body.forEach((b, i) => {
        const a = b.args.map(x => sub(x.e, text, pc) + (x.mem ? "(" + sub(x.mem, text, pc) + ")" : ""));
        const saved = pc;
        words.push(...instr(b.op, a, key + "/" + i, depth + 1));
        pc = saved;
      });
      return words;
    }
    return [encode(e, bind, args)];
  }

  function encode(e, bind, args) {
    const f = isa.formats.get(e.format), vals = new Map();
    for (const o of e.pattern) {
      const put = (name, kind, b) => {
        if (kind.type === "any") return;
        if (kind.type === "reg") { vals.set(name, b); return; }
        let v = val(b.src);
        if (kind.type === "rel") {
          v -= pc;
          if (pass === 2) {
            if (v % kind.align) fail(kind.align === 2 ? "a jump target must be at an even address" : `a jump target must be at an address that is a multiple of ${kind.align}`);
            const lo = -(2 ** (kind.bits - 1)), hi = 2 ** (kind.bits - 1) - kind.align;
            if (v < lo || v > hi) fail(`${b.src} is ${v} ${isa.unit === 8 ? "bytes " : ""}away, too far (it reaches ${lo} to ${hi})`);
          }
        } else if (pass === 2 && !fits(v, kind)) {
          const what = o.mem && name === o.name ? "the offset" : name === "imm" ? "the immediate" : `the ${name}`;
          const lo = kind.type === "s" ? -(2 ** (kind.bits - 1)) : kind.type === "u" ? 0 : -(2 ** (kind.bits - 1));
          const hi = kind.type === "s" ? 2 ** (kind.bits - 1) - 1 : kind.type === "u" ? 2 ** kind.bits - 1 : 2 ** kind.bits - 1;
          fail(`${what} ${v} does not fit in ${kind.bits} bits (${lo} to ${hi})`);
        }
        vals.set(name, v);
      };
      put(o.name, o.kind, bind.get(o.name));
      if (o.mem) put(o.mem.name, o.mem.kind, bind.get(o.mem.name));
    }
    for (const a of e.assigns) vals.set(a.name, evalE(a.e));
    let word = 0n;
    for (const fl of f.fields) {
      const v = BigInt.asUintN(f.spans.get(fl.name) || fl.width, BigInt(Math.trunc(vals.get(fl.name))));
      let part;
      if (!fl.slices) part = v;
      else { part = 0n; for (const [hi, lo] of fl.slices) part = (part << BigInt(hi - lo + 1)) | ((v >> BigInt(lo)) & ((1n << BigInt(hi - lo + 1)) - 1n)); }
      word = (word << BigInt(fl.width)) | part;
    }
    return Number(word);
  }

  const emitData = (bytes, ln, srcText) => {
    if (pass === 2) {
      listing.push({ line: ln, sec: "data", addr: pc, hex: bytes.slice(0, 8).map(b => (b & 255).toString(16).padStart(2, "0")).join(" ") + (bytes.length > 8 ? " …" : ""), src: srcText });
      bytes.forEach((b, i) => { data[pc - dataBase + i] = b & 255; });
    }
    pc += bytes.length;
  };
  const hexw = w => (w >>> 0).toString(16).padStart(isa.width / 4, "0");
  let padWord = 0;
  try { pass = 2; pc = 0; symbols.clear(); padWord = instr("nop", [], "nop", 0)[0] >>> 0; } catch (x) { padWord = 0; }

  for (pass = 1; pass <= 2; pass++) {
    sec = "text"; text = []; data = []; listing = [];
    const pcs = { text: textBase, data: dataBase };
    pc = textBase;
    const swap = s => { pcs[sec] = pc; sec = s; pc = pcs[s]; };
    for (let ln = 1; ln <= lines.length; ln++) {
      const srcText = lines[ln - 1]; curLn = ln;
      let l = stripComment(srcText).trim();
      try {
        let m;
        while ((m = /^([A-Za-z_.$][\w.$]*)\s*:\s*/.exec(l))) {
          if (pass === 1) { if (symbols.has(m[1])) fail(`the label ${m[1]} is defined twice`); symbols.set(m[1], pc); }
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
            if (pass === 1) symbols.set(a[0], val(a[1], true)); break;
          case ".align": {
            const n = 2 ** val(a[0], true), pad = (n - (pc % n)) % n;
            if (sec === "text") {
              if (pad % U) fail(`.align in .text must keep instructions on whole words`);
              for (let i = 0; i < pad / U; i++) { if (pass === 2) text[(pc - textBase) / U] = padWord; pc += U; }
            } else emitData(new Array(pad).fill(0), ln, srcText);
            break;
          }
          case ".ascii": case ".asciz": case ".string": {
            if (sec === "text") fail("strings go in .data");
            const m2 = /^"((?:\\.|[^"\\])*)"$/.exec(rest);
            if (!m2) fail(`expected ${op} "text"`);
            const bytes = [...unescape(m2[1])].map(c => c.charCodeAt(0) & 255);
            if (op !== ".ascii") bytes.push(0);
            emitData(bytes, ln, srcText); break;
          }
          case ".space": case ".zero": {
            const n = val(a[0], true);
            if (sec === "text") fail(`${op} goes in .data`);
            emitData(new Array(n).fill(0), ln, srcText); break;
          }
          default: {
            if (!isa.data.has(op)) fail(`there is no directive called ${op}`);
            const size = isa.data.get(op), bytes = [];
            a.forEach(x => { const v = val(x); for (let i = 0; i < size; i++) bytes.push(isa.endian === "big" ? (v >>> (8 * (size - 1 - i))) & 255 : (v >>> (8 * i)) & 255); });
            if (sec === "text") {
              if (size !== U) fail(`${op} goes in .data; .text holds words`);
              a.forEach(x => { const v = val(x) >>> 0; if (pass === 2) { text[(pc - textBase) / U] = v; listing.push({ line: ln, sec: "text", addr: pc, hex: hexw(v), src: srcText }); } pc += U; });
            } else emitData(bytes, ln, srcText);
          }
          }
          continue;
        }
        if (sec !== "text") fail("instructions go in .text");
        const words = instr(op, a, String(ln), 0);
        if (pass === 2) words.forEach((w, i) => {
          text[(pc - textBase) / U + i] = w >>> 0;
          listing.push({ line: ln, sec: "text", addr: pc + U * i, hex: hexw(w), src: i ? "" : srcText });
        });
        pc += U * words.length;
      } catch (e) {
        if (!(e instanceof AsmError)) throw e;
        if (!errors.some(x => x.line === ln)) errors.push({ line: ln, msg: e.message });
      }
    }
  }
  for (let i = 0; i < text.length; i++) if (text[i] === undefined) text[i] = 0;
  for (let i = 0; i < data.length; i++) if (data[i] === undefined) data[i] = 0;
  return { text, data, symbols, listing, errors: errors.sort((x, y) => x.line - y.line) };
}

/* "x1 ra": every name register i goes by, for a register list */
function regLabel(isa, i) {
  const names = [];
  for (const [n, k] of isa.regs) if (k === i) names.push(n);
  return names.join(" ");
}
/* every way to write an instruction: [{ op, text, pseudo }] */
function forms(isa) {
  const out = [];
  for (const [op, list] of isa.over) for (const e of list)
    out.push({ op, pseudo: e.pseudo, text: op + (e.pattern.length ? " " : "") + e.pattern.map(o => o.name + (o.mem ? "(" + o.mem.name + ")" : "")).join(", ") });
  return out;
}

return { load, assemble: (isa, src, opts) => assemble(isa, src, opts), regLabel, forms };
})();
if (typeof module !== "undefined") module.exports = WireAsm;
