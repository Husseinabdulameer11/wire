/* wire_isaparse.js - the .isa language (src/isa.gram), read in the page.
 *
 * wire reads a .isa file when a circuit has a cpu statement and puts its
 * syntax tree in the page. An instruction set written in the page itself (a
 * CPU found without a cpu statement, or one being changed) is read here into
 * the same tree, node for node, so WireAsm.load takes either:
 *
 *   WireIsaParse.parse(text) -> { t: "isa", c: [...] }
 *   throws an Error with .line and .col on a mistake
 *
 * It follows isa.gram rule by rule (a PEG): `<-` rules make a node, `<^`
 * rules pass a single child up, `<=` rules are tokens (leaves), `~` drops.
 * tests/asm checks it against wire's own parser on every .isa there is. */
"use strict";
const WireIsaParse = (() => {

const KEYWORDS = ["isa", "reg", "names", "alias", "format", "data", "func", "pseudo", "if", "fits"];
const WORD = /[A-Za-z0-9_]/;

function parse(src) {
  let p = 0, far = 0, farAt = 0, farWant = new Set(), pre = 0;
  const lineStarts = [0];
  for (let i = 0; i < src.length; i++) if (src[i] === "\n") lineStarts.push(i + 1);
  const lineOf = at => { let lo = 0, hi = lineStarts.length - 1; while (lo < hi) { const m = (lo + hi + 1) >> 1; if (lineStarts[m] <= at) lo = m; else hi = m - 1; } return lo + 1; };

  /* the furthest place a token was looked for; the message points just after the text before it */
  const want = w => { if (p > far) { far = p; farAt = pre; farWant = new Set(); } if (p === far) farWant.add(w); };
  function skip() {
    pre = p;
    for (;;) {
      if (p < src.length && /[ \t\r\n]/.test(src[p])) { p++; continue; }
      if (src[p] === "#") { while (p < src.length && src[p] !== "\n") p++; continue; }
      return;
    }
  }
  const leaf = (t, a, b) => ({ t, x: src.slice(a, b) });
  /* a quoted literal: skip, then the exact text */
  function lit(s) {
    skip();
    if (src.startsWith(s, p)) { p += s.length; return true; }
    want(`"${s}"`); return false;
  }
  const isKeyword = at => KEYWORDS.some(k => src.startsWith(k, at) && !WORD.test(src[at + k.length] || ""));
  function kw(k) {
    skip();
    if (src.startsWith(k, p) && !WORD.test(src[p + k.length] || "")) { p += k.length; return true; }
    want(k); return false;
  }
  /* tokens */
  function ident() {
    skip();
    const a = p;
    if (!isKeyword(p) && /[A-Za-z_]/.test(src[p] || "")) {
      p++;
      while (p < src.length && /[A-Za-z0-9_.]/.test(src[p])) p++;
      return leaf("ident", a, p);
    }
    want("a name"); return null;
  }
  function number() {
    skip();
    const a = p;
    let m;
    if (src.startsWith("0b", p) && (m = /^[01_]+/.exec(src.slice(p + 2)))) p += 2 + m[0].length;
    else if (src.startsWith("0x", p) && (m = /^[0-9a-fA-F_]+/.exec(src.slice(p + 2)))) p += 2 + m[0].length;
    else if ((m = /^[0-9]+/.exec(src.slice(p)))) p += m[0].length;
    else { want("a number"); return null; }
    return leaf("number", a, p);
  }
  function string() {
    skip();
    const m = /^"[^"\n]*"/.exec(src.slice(p));
    if (!m) { want("a name in quotes"); return null; }
    const a = p; p += m[0].length; return leaf("string", a, p);
  }
  function char() {
    skip();
    const m = /^'(\\.|[^\\'])'/.exec(src.slice(p));
    if (!m) { want("a character"); return null; }
    const a = p; p += m[0].length; return leaf("char", a, p);
  }
  function fname() {
    skip();
    const m = /^%[A-Za-z_][A-Za-z0-9_]*/.exec(src.slice(p));
    if (!m) { want("%name"); return null; }
    const a = p; p += m[0].length; return leaf("fname", a, p);
  }
  function kind() {
    skip();
    const m = /^(reg|any|rel[0-9]+(@[0-9]+)?|[sub][0-9]+)(?![A-Za-z0-9_])/.exec(src.slice(p));
    if (!m) { want("a kind (reg, sN, uN, bN, relN, any)"); return null; }
    const a = p; p += m[0].length; return leaf("kind", a, p);
  }
  function cmpop() {
    skip();
    for (const o of ["==", "!=", "<=", ">=", "<", ">"]) if (src.startsWith(o, p)) { const a = p; p += o.length; return leaf("cmpop", a, p); }
    want("a comparison"); return null;
  }
  /* try a rule; put the position back if it fails */
  function attempt(f) { const at = p; const r = f(); if (r === null || r === undefined || r === false) { p = at; return null; } return r; }
  const node = (t, c) => ({ t, c });
  /* a <^ rule: one child passes up */
  const up = (t, c) => c.length === 1 ? c[0] : node(t, c);

  /* ---- expressions */
  function expr() { return orx(); }
  function chain(t, sub, sep) {
    return attempt(() => {
      const first = sub();
      if (!first) return null;
      const c = [first];
      for (;;) {
        const r = attempt(() => { if (!lit(sep)) return null; return sub(); });
        if (!r) break;
        c.push(r);
      }
      return up(t, c);
    });
  }
  function orx() { return chain("orx", xorx, "|"); }
  function xorx() { return chain("xorx", andx, "^"); }
  function andx() { return chain("andx", shiftx, "&"); }
  function opchain(t, sub, ops) {
    return attempt(() => {
      const first = sub();
      if (!first) return null;
      const c = [first];
      for (;;) {
        const r = attempt(() => {
          skip();
          const o = ops.find(s => src.startsWith(s, p));
          if (!o) { ops.forEach(s => want(`"${s}"`)); return null; }
          const a = p; p += o.length;
          const b = sub();
          return b ? [{ t: "", x: src.slice(a, a + o.length) }, b] : null;
        });
        if (!r) break;
        c.push(...r);
      }
      return up(t, c);
    });
  }
  function shiftx() { return opchain("shiftx", sumx, ["<<", ">>"]); }
  function sumx() { return opchain("sumx", prodx, ["+", "-"]); }
  function prodx() { return chain("prodx", unary, "*"); }
  function unary() {
    return attempt(() => { if (!lit("-")) return null; const u = unary(); return u ? node("neg", [u]) : null; })
        || attempt(() => { if (!lit("~")) return null; const u = unary(); return u ? node("inv", [u]) : null; })
        || primary();
  }
  function primary() {
    return attempt(call) || attempt(char) || attempt(number) || attempt(ident)
        || attempt(() => { if (!lit("(")) return null; const e = expr(); if (!e || !lit(")")) return null; return e; });
  }
  function call() {
    const f = fname();
    if (!f || !lit("(")) return null;
    const c = [f];
    const first = attempt(expr);
    if (first) {
      c.push(first);
      for (;;) { const r = attempt(() => lit(",") ? expr() : null); if (!r) break; c.push(r); }
    }
    if (!lit(")")) return null;
    return node("call", c);
  }

  /* ---- statements */
  function header() {
    return attempt(() => {
      if (!kw("isa")) return null;
      const s = string();
      if (!s) return null;
      const c = [s];
      const o = attempt(opts);
      if (o) c.push(o);
      return node("header", c);
    });
  }
  function opts() {
    if (!lit("(")) return null;
    const first = opt();
    if (!first) return null;
    const c = [first];
    for (;;) { const r = attempt(() => lit(",") ? opt() : null); if (!r) break; c.push(r); }
    if (!lit(")")) return null;
    return node("opts", c);
  }
  function opt() {
    return attempt(() => {
      const k = ident();
      if (!k || !lit("=")) return null;
      const v = attempt(number) || attempt(ident) || attempt(string);
      return v ? node("opt", [k, v]) : null;
    });
  }
  function regs() {
    if (!kw("reg")) return null;
    const n = ident(); if (!n || !lit("[")) return null;
    const a = number(); if (!a || !lit("..")) return null;
    const b = number(); if (!b || !lit("]")) return null;
    return node("regs", [n, a, b]);
  }
  function names() {
    if (!kw("names")) return null;
    const first = ident(); if (!first) return null;
    const c = [first];
    for (;;) { const r = attempt(() => lit(",") ? ident() : null); if (!r) break; c.push(r); }
    return node("names", c);
  }
  function alias() {
    if (!kw("alias")) return null;
    const n = ident(); if (!n || !lit("=")) return null;
    const v = attempt(ident) || attempt(number);
    return v ? node("alias", [n, v]) : null;
  }
  function format() {
    if (!kw("format")) return null;
    const n = ident(); if (!n || !lit("=")) return null;
    const c = [n];
    for (;;) { const f = attempt(field); if (!f) break; c.push(f); }
    return c.length > 1 ? node("format", c) : null;
  }
  function field() {
    return attempt(() => {   /* slicefield */
      const n = ident(); if (!n || !lit("[")) return null;
      const first = slice(); if (!first) return null;
      const c = [n, first];
      for (;;) { const r = attempt(() => lit("|") ? slice() : null); if (!r) break; c.push(r); }
      if (!lit("]")) return null;
      return node("slicefield", c);
    }) || attempt(() => {    /* widthfield */
      const n = ident(); if (!n || !lit(":")) return null;
      const w = number(); return w ? node("widthfield", [n, w]) : null;
    });
  }
  function slice() {
    return attempt(() => {
      const a = number(); if (!a) return null;
      const b = attempt(() => lit(":") ? number() : null);
      return node("slice", b ? [a, b] : [a]);
    });
  }
  function datadef() {
    if (!kw("data")) return null;
    const first = dentry(); if (!first) return null;
    const c = [first];
    for (;;) { const r = attempt(() => lit(",") ? dentry() : null); if (!r) break; c.push(r); }
    return node("datadef", c);
  }
  function dentry() {
    return attempt(() => { const n = ident(); if (!n) return null; const v = number(); return v ? node("dentry", [n, v]) : null; });
  }
  function func() {
    if (!kw("func")) return null;
    const f = fname(); if (!f || !lit("(")) return null;
    const a = ident(); if (!a || !lit(")") || !lit("=")) return null;
    const e = expr(); return e ? node("func", [f, a, e]) : null;
  }
  function pattern() {
    return attempt(() => {
      const first = operand(); if (!first) return null;
      const c = [first];
      for (;;) { const r = attempt(() => lit(",") ? operand() : null); if (!r) break; c.push(r); }
      return node("pattern", c);
    });
  }
  function operand() {
    return attempt(() => {
      const n = ident(); if (!n || !lit(":")) return null;
      const k = kind(); if (!k) return null;
      const c = [n, k];
      const m = attempt(() => {
        if (!lit("(")) return null;
        const a = ident(); if (!a || !lit(":")) return null;
        const b = kind(); if (!b || !lit(")")) return null;
        return [a, b];
      });
      if (m) c.push(...m);
      return node("operand", c);
    });
  }
  function assign() {
    return attempt(() => { const n = ident(); if (!n || !lit("=")) return null; const e = expr(); return e ? node("assign", [n, e]) : null; });
  }
  function guard() {
    return attempt(() => {
      if (!kw("if")) return null;
      const e = expr(); if (!e) return null;
      const c = [e];
      const r = attempt(() => { const o = cmpop(); if (!o) return null; const b = expr(); return b ? [o, b] : null; })
             || attempt(() => { if (!kw("fits")) return null; const k = kind(); return k ? [k] : null; });
      if (r) c.push(...r);
      return node("guard", [node("cond", c)]);
    });
  }
  function instr() {
    const n = ident(); if (!n) return null;
    const c = [n];
    const pt = pattern(); if (pt) c.push(pt);
    if (!lit("=")) return null;
    const f = ident(); if (!f || !lit("(")) return null;
    c.push(f);
    const first = assign(); if (!first) return null;
    c.push(first);
    for (;;) { const r = attempt(() => lit(",") ? assign() : null); if (!r) break; c.push(r); }
    if (!lit(")")) return null;
    const g = guard(); if (g) c.push(g);
    return node("instr", c);
  }
  function pseudo() {
    if (!kw("pseudo")) return null;
    const n = ident(); if (!n) return null;
    const c = [n];
    const pt = pattern(); if (pt) c.push(pt);
    if (!lit("=")) return null;
    const first = asmop(); if (!first) return null;
    c.push(first);
    for (;;) { const r = attempt(() => lit(";") ? asmop() : null); if (!r) break; c.push(r); }
    const g = guard(); if (g) c.push(g);
    return node("pseudo", c);
  }
  function asmop() {
    return attempt(() => {
      const n = ident(); if (!n) return null;
      const c = [n];
      const args = attempt(() => {
        const first = arg(); if (!first) return null;
        const a = [first];
        for (;;) { const r = attempt(() => lit(",") ? arg() : null); if (!r) break; a.push(r); }
        return a;
      });
      if (args) c.push(...args);
      return node("asmop", c);
    });
  }
  function arg() {
    return attempt(() => {
      const e = expr(); if (!e) return null;
      const m = attempt(() => { if (!lit("(")) return null; const x = expr(); return x && lit(")") ? x : null; });
      return node("arg", m ? [e, m] : [e]);
    });
  }
  function stmt() {
    for (const f of [regs, names, alias, format, datadef, func, pseudo, instr]) {
      skip();
      const at = p, r = attempt(f);
      if (r) { r.l = lineOf(at); return r; }
    }
    return null;
  }

  skip();
  const h0 = p, h = header();
  if (!h) return fail();
  h.l = lineOf(h0);
  const tree = { t: "isa", c: [h] };
  for (;;) { const s = stmt(); if (!s) break; tree.c.push(s); }
  skip();
  if (p < src.length) { want("a statement"); return fail(); }
  /* the tree as wire writes it: the line on each statement only, first in the object */
  tree.c = tree.c.map(s => ({ t: s.t, l: s.l, c: s.c }));
  return tree;

  function fail() {
    /* at the token that is wrong, unless it is on a later line: then where the line before ended */
    const at = lineOf(far) > lineOf(farAt) ? farAt : far;
    const line = lineOf(at), col = at - lineStarts[line - 1] + 1;
    const got = far >= src.length ? "the end" : JSON.stringify(src.slice(far).split(/\s/)[0].slice(0, 20) || src[far]);
    const w = [...farWant].slice(0, 6);
    const e = new Error(`line ${line}:${col}: expected ${w.join(" or ") || "something else"}, got ${got}`);
    e.line = line; e.col = col;
    throw e;
  }
}

return { parse };
})();
if (typeof module !== "undefined") module.exports = WireIsaParse;
