/* wire_sim.js - compiles a Wire netlist into a WebAssembly module.
 *
 * The page (and tests/simtest.js under Node) hand this the netlist that
 * wire.c writes, and get back a simulator whose whole state lives in one
 * WebAssembly.Memory:
 *
 *   word 0                      1 while the last settle did not converge
 *   word 1                      1 + the bus part where two drivers talk at once
 *   NET  + 8*i, NET + 8*i + 4   value and known-mask of net i
 *   NODE + 8*j, NODE + 8*j + 4  state and last clock level of node j
 *   TAB  ...                    address tables for mux inputs
 *   MEM  ...                    the words of every rom and ram
 *
 * Every net carries a value v and a known mask k, per bit:
 *   k = 1          known, the value is v
 *   k = 0, v = 0   unknown (x)
 *   k = 0, v = 1   floating (z): nothing drives it, like a bus nobody talks on
 * Pins, buf, split and merge pass z along; every other part reads it as x.
 *
 * The bytes are written directly; no toolchain is involved. Exports:
 *   settle()     evaluate all logic until it is stable; returns 1 if it is
 *   simulate()   settle, then clock every flip-flop that saw a rising edge,
 *                repeating for ripple counters (up to 64 rounds)
 *   toggle(n)    n times: flip every clock and simulate
 *   memory
 *
 * Scheduling: a flip-flop's outputs depend at once only on rst (and on en
 * for a counter's carry), so the logic between flip-flops is ordered once
 * and runs as straight-line code. Real loops made of gates (an SR latch)
 * are found as strongly connected components and only those are repeated
 * until nothing changes. */
"use strict";
const WireSim = (() => {

const SEQ = new Set(["dff", "tff", "jkff", "counter"]);
/* parts that pass a floating (z) value on unchanged */
const PASS_Z = new Set(["buf", "pin_in", "pin_out", "split", "merge", "bus"]);
const SOURCES = new Set(["input", "button", "clock"]);
const mask32 = w => (w >= 32 ? -1 : (2 ** w - 1) | 0);

/* ---------------------------------------------------------------- bytes */
class Bytes {
  constructor() { this.b = []; }
  byte(...xs) { for (const x of xs) this.b.push(x & 255); return this; }
  u(x) { x >>>= 0; do { let y = x & 127; x >>>= 7; if (x) y |= 128; this.b.push(y); } while (x); return this; }
  s(x) {
    x |= 0;
    for (;;) {
      const y = x & 127; x >>= 7;
      if ((x === 0 && !(y & 64)) || (x === -1 && (y & 64))) { this.b.push(y); return this; }
      this.b.push(y | 128);
    }
  }
  str(s) { const e = new TextEncoder().encode(s); this.u(e.length); return this.add(e); }
  vec(items) { this.u(items.length); items.forEach(it => this.add(it)); return this; }
  /* append without spreading: spreading a big array into push() overflows the stack */
  add(xs) { for (let i = 0; i < xs.length; i++) this.b.push(xs[i]); return this; }
  section(id, body) { this.byte(id).u(body.length); for (const x of body) this.b.push(x); return this; }
}

/* opcodes used */
const OP = {
  block: 0x02, loop: 0x03, if: 0x04, else: 0x05, end: 0x0b, br: 0x0c, br_if: 0x0d, call: 0x10,
  drop: 0x1a, select: 0x1b, get: 0x20, set: 0x21, tee: 0x22, load: 0x28, store: 0x36,
  i32: 0x41, eqz: 0x45, eq: 0x46, ne: 0x47, lt_u: 0x49, gt_u: 0x4b, clz: 0x67, add: 0x6a,
  sub: 0x6b, and: 0x71, or: 0x72, xor: 0x73, shl: 0x74, shr_u: 0x76,
  wrap: 0xa7, i64shr_u: 0x88, i64add: 0x7c, i64ext_u: 0xad, i64: 0x42
};
const VOID = 0x40, I32 = 0x7f, I64 = 0x7e;

/* a function body being written */
class Fn {
  constructor() { this.c = new Bytes(); }
  op(...xs) { this.c.byte(...xs); return this; }
  k(x) { this.c.byte(OP.i32).s(x); return this; }
  get(l) { this.c.byte(OP.get).u(l); return this; }
  set(l) { this.c.byte(OP.set).u(l); return this; }
  tee(l) { this.c.byte(OP.tee).u(l); return this; }
  /* load the word at a fixed address */
  ld(addr) { return this.k(0).op(OP.load, 2).u_(addr); }
  /* load the word at (top of stack) + off */
  ldAt(off) { return this.op(OP.load, 2).u_(off); }
  /* store: call pre() first to push the address base (i32 0), then the value */
  st(addr, pushValue) { this.k(0); pushValue(); return this.op(OP.store, 2).u_(addr); }
  u_(x) { this.c.u(x); return this; }
  if_(t = VOID) { return this.op(OP.if, t); }
  else_() { return this.op(OP.else); }
  end() { return this.op(OP.end); }
  call(f) { return this.op(OP.call).u_(f); }
  br(d) { return this.op(OP.br).u_(d); }
  brIf(d) { return this.op(OP.br_if).u_(d); }
}

/* --------------------------------------------------- the original order */
/* The evaluation order of the first simulator: depth-first from the
 * sources marks the edges that close loops, then a topological sort of the
 * rest. Returns each node's position. */
function classicOrder(nodes) {
  const N = nodes.length, drv = new Map(), edges = [];
  nodes.forEach((n, j) => n.out.forEach(net => drv.set(net, j)));
  nodes.forEach((n, j) => n.in.forEach(net => { if (drv.has(net)) edges.push([drv.get(net), j]); }));
  const outE = nodes.map(() => []);
  edges.forEach((e, i) => outE[e[0]].push(i));
  const back = new Set(), state = new Array(N).fill(0);
  const visit = s => {
    const stack = [[s, 0]]; state[s] = 1;
    while (stack.length) {
      const top = stack[stack.length - 1], u = top[0];
      if (top[1] < outE[u].length) {
        const ei = outE[u][top[1]++], v = edges[ei][1];
        if (state[v] === 1) back.add(ei);
        else if (state[v] === 0) { state[v] = 1; stack.push([v, 0]); }
      } else { state[u] = 2; stack.pop(); }
    }
  };
  const isSrc = n => SOURCES.has(n.kind) || n.kind === "pin_in";
  nodes.forEach((n, j) => { if (isSrc(n) && !state[j]) visit(j); });
  nodes.forEach((n, j) => { if (n.kind === "const" && !state[j]) visit(j); });
  nodes.forEach((n, j) => { if (!state[j]) visit(j); });
  const indeg = new Array(N).fill(0);
  edges.forEach((e, i) => { if (!back.has(i)) indeg[e[1]]++; });
  const rank = new Array(N).fill(0), q = [];
  nodes.forEach((n, j) => { if (!indeg[j]) q.push(j); });
  let pos = 0;
  while (q.length) {
    const u = q.shift(); rank[u] = pos++;
    outE[u].forEach(ei => { if (!back.has(ei) && --indeg[edges[ei][1]] === 0) q.push(edges[ei][1]); });
  }
  return rank;
}

/* ------------------------------------------------------------- compiler */
function compile(D) {
  const nets = D.nets, nodes = D.nodes;
  const NET = 16;
  /* A chip's pins only pass a value through, so both sides of a pin share
   * one slot in memory and the pin costs nothing at run time. */
  const rep = nets.map((_, i) => i);
  const root = i => { while (rep[i] !== i) i = rep[i] = rep[rep[i]]; return i; };
  const alias = new Set();                 /* pin nodes that became shared slots */
  nodes.forEach((n, j) => {
    if ((n.kind === "pin_in" || n.kind === "pin_out") && n.in.length === 1 && n.out.length === 1 &&
        nets[n.in[0]].w === nets[n.out[0]].w) {
      rep[root(n.out[0])] = root(n.in[0]);
      alias.add(j);
    }
  });
  const slot = new Array(nets.length).fill(-1);
  let nslots = 0;
  nets.forEach((_, i) => { const r = root(i); if (slot[r] < 0) slot[r] = nslots++; });
  const V = i => NET + 8 * slot[root(i)], K = i => V(i) + 4;
  const NODE = NET + 8 * nslots;
  const S = j => NODE + 8 * j, LC = j => NODE + 8 * j + 4;
  let TAB = NODE + 8 * nodes.length;
  const tabOf = new Map();                 /* mux node -> table address */
  nodes.forEach((n, j) => {
    if (n.kind !== "mux") return;
    tabOf.set(j, TAB);
    TAB += 4 * (n.in.length - 1);
  });
  /* rom and ram contents: one 32-bit word per address */
  const memOf = new Map();
  let MEM = TAB;
  nodes.forEach((n, j) => {
    if (n.kind !== "rom" && n.kind !== "ram") return;
    memOf.set(j, MEM);
    MEM += 4 * (1 << n.abits);
  });
  const bytes = MEM;
  const width = i => nets[i].w;
  const inBy = (n, name) => { const i = (n.inNames || []).indexOf(name); return i < 0 ? -1 : n.in[i]; };

  /* ---- which node drives each net */
  const driver = new Array(nets.length).fill(-1);
  nodes.forEach((n, j) => n.out.forEach(net => { driver[net] = j; }));

  /* ---- combinational inputs of each node */
  const combIn = n => {
    if (n.kind === "ram") return [inBy(n, "addr")];     /* reading; writing waits for the clock */
    if (!SEQ.has(n.kind)) return n.in;
    const r = [inBy(n, "rst")];
    if (n.kind === "counter") r.push(inBy(n, "en"), inBy(n, "down"));
    return r.filter(x => x >= 0);
  };

  /* ---- strongly connected components (Tarjan, iterative), in dependency order */
  const N = nodes.length;
  const succ = nodes.map(() => []);
  const selfLoop = new Array(N).fill(false);
  nodes.forEach((n, j) => combIn(n).forEach(net => {
    const d = driver[net];
    if (d < 0) return;
    succ[d].push(j);
    if (d === j) selfLoop[j] = true;
  }));
  const index = new Array(N).fill(-1), low = new Array(N).fill(0), onStack = new Array(N).fill(false);
  const stack = [], sccs = [];
  let counter = 0;
  for (let root = 0; root < N; root++) {
    if (index[root] >= 0) continue;
    const work = [[root, 0]];
    index[root] = low[root] = counter++; stack.push(root); onStack[root] = true;
    while (work.length) {
      const top = work[work.length - 1], u = top[0];
      if (top[1] < succ[u].length) {
        const v = succ[u][top[1]++];
        if (index[v] < 0) {
          index[v] = low[v] = counter++; stack.push(v); onStack[v] = true;
          work.push([v, 0]);
        } else if (onStack[v]) low[u] = Math.min(low[u], index[v]);
      } else {
        work.pop();
        if (work.length) { const p = work[work.length - 1][0]; low[p] = Math.min(low[p], low[u]); }
        if (low[u] === index[u]) {
          const comp = [];
          let v;
          do { v = stack.pop(); onStack[v] = false; comp.push(v); } while (v !== u);
          sccs.push(comp);
        }
      }
    }
  }
  sccs.reverse();                          /* Tarjan finds them sinks first */
  /* Inside a loop the order of evaluation decides how it powers up (which
   * side of an SR latch wins with both inputs 0). Use the order the
   * original simulator used, so circuits behave as they always have. */
  const rank = classicOrder(nodes);
  sccs.forEach(c => c.sort((a, b) => rank[a] - rank[b]));
  const loops = sccs.filter(c => c.length > 1 || selfLoop[c[0]]);

  /* ---- settle(): locals */
  const Lv = 0, Lk = 1, La = 2, Lb = 3, Lnk = 4, Lch = 5, Lit = 6, Lt = 7, Ls = 8 /* i64 */;
  const f = new Fn();
  let track = false;                       /* inside a loop: note changes in Lch */

  const known = net => f.ld(K(net)).k(mask32(width(net))).op(OP.eq);
  /* write locals v, k (or a constant k) to an output net; unknown bits
   * become 0, unless keepZ, when floating bits stay as they are */
  const out = (net, kc, keepZ) => {
    const m = mask32(width(net));
    if (kc !== undefined) {
      const k = kc & m;
      if (k !== -1) f.get(Lv).k(k).op(OP.and).set(Lv);
      f.k(k).set(Lk);
    } else if (keepZ) {
      if (m !== -1) f.get(Lk).k(m).op(OP.and).set(Lk).get(Lv).k(m).op(OP.and).set(Lv);
    } else {
      if (m !== -1) f.get(Lk).k(m).op(OP.and).set(Lk);
      f.get(Lv).get(Lk).op(OP.and).set(Lv);
    }
    if (track) {
      f.ld(V(net)).get(Lv).op(OP.ne).ld(K(net)).get(Lk).op(OP.ne).op(OP.or).if_();
      f.st(V(net), () => f.get(Lv)).st(K(net), () => f.get(Lk)).k(1).set(Lch);
      f.end();
    } else {
      f.st(V(net), () => f.get(Lv)).st(K(net), () => f.get(Lk));
    }
  };
  const zero = () => f.k(0).set(Lv).k(0).set(Lk);
  const copy = net => f.ld(V(net)).set(Lv).ld(K(net)).set(Lk);

  function emit(n, j) {
    const k = n.kind;
    if (alias.has(j)) return;
    switch (k) {
    case "input": case "button": case "clock": case "keypad":
      f.ld(S(j)).set(Lv); out(n.out[0], -1); return;
    case "const":
      f.k(n.value | 0).set(Lv); out(n.out[0], -1); return;
    case "and": case "nand": case "or": case "nor": case "xor": case "xnor": {
      const m = mask32(width(n.out[0]));
      copy(n.in[0]);
      for (let i = 1; i < n.in.length; i++) {
        f.ld(V(n.in[i])).set(La).ld(K(n.in[i])).set(Lb);
        if (k === "and" || k === "nand") {
          /* nk = (k & bk) | (k & ~v) | (bk & ~bv); v = v & bv & nk */
          f.get(Lk).get(Lb).op(OP.and)
           .get(Lk).get(Lv).k(-1).op(OP.xor).op(OP.and).op(OP.or)
           .get(Lb).get(La).k(-1).op(OP.xor).op(OP.and).op(OP.or).set(Lnk);
          f.get(Lv).get(La).op(OP.and).get(Lnk).op(OP.and).set(Lv).get(Lnk).set(Lk);
        } else if (k === "or" || k === "nor") {
          /* nk = (k & bk) | (k & v) | (bk & bv); v = (v | bv) & nk */
          f.get(Lk).get(Lb).op(OP.and)
           .get(Lk).get(Lv).op(OP.and).op(OP.or)
           .get(Lb).get(La).op(OP.and).op(OP.or).set(Lnk);
          f.get(Lv).get(La).op(OP.or).get(Lnk).op(OP.and).set(Lv).get(Lnk).set(Lk);
        } else {
          f.get(Lk).get(Lb).op(OP.and).set(Lk);
          f.get(Lv).get(La).op(OP.xor).get(Lk).op(OP.and).set(Lv);
        }
      }
      if (k === "nand" || k === "nor" || k === "xnor")
        f.get(Lv).k(-1).op(OP.xor).get(Lk).op(OP.and).k(m).op(OP.and).set(Lv);
      out(n.out[0]); return;
    }
    case "not":
      f.ld(V(n.in[0])).k(-1).op(OP.xor).ld(K(n.in[0])).op(OP.and).set(Lv).ld(K(n.in[0])).set(Lk);
      out(n.out[0]); return;
    case "buf": case "pin_in": case "pin_out":
      copy(n.in[0]); out(n.out[0], undefined, true); return;
    case "tri": {
      /* en 1: the value (z read as x); en 0: floating; en unknown: x */
      const x = n.in[0], en = n.in[1], m = mask32(width(n.out[0]));
      known(en).if_();
      f.ld(V(en)).if_();
      f.ld(V(x)).ld(K(x)).op(OP.and).set(Lv).ld(K(x)).set(Lk);
      f.else_().k(m).set(Lv).k(0).set(Lk).end();
      f.else_(); zero(); f.end();
      out(n.out[0], undefined, true); return;
    }
    case "bus": {
      /* per bit: nobody driving -> z; one -> its value; two or more -> x */
      const m = mask32(width(n.out[0]));
      const Ld1 = La, Ld2 = Lb, Lkd = Lnk;
      f.k(0).set(Ld1).k(0).set(Ld2).k(0).set(Lv).k(0).set(Lkd);
      n.in.forEach(i => {
        /* t = bits this input drives (not z) */
        f.ld(V(i)).ld(K(i)).k(-1).op(OP.xor).op(OP.and).k(-1).op(OP.xor).k(m).op(OP.and).set(Lt);
        f.get(Ld2).get(Ld1).get(Lt).op(OP.and).op(OP.or).set(Ld2);
        f.get(Ld1).get(Lt).op(OP.or).set(Ld1);
        f.get(Lv).ld(V(i)).ld(K(i)).op(OP.and).get(Lt).op(OP.and).op(OP.or).set(Lv);
        f.get(Lkd).ld(K(i)).get(Lt).op(OP.and).op(OP.or).set(Lkd);
      });
      /* k = known and driven by exactly one; v = that value, or 1 where floating */
      f.get(Lkd).get(Ld1).op(OP.and).get(Ld2).k(-1).op(OP.xor).op(OP.and).set(Lk);
      f.get(Lv).get(Lk).op(OP.and).get(Ld1).k(-1).op(OP.xor).k(m).op(OP.and).op(OP.or).set(Lv);
      out(n.out[0], undefined, true); return;
    }
    case "rom": case "ram": {
      const a = inBy(n, "addr");
      known(a).if_();
      f.ld(V(a)).k(2).op(OP.shl).ldAt(memOf.get(j)).set(Lv).k(-1).set(Lk);
      f.else_(); zero(); f.end();
      out(n.out[0]); return;
    }
    case "mux": {
      const sel = n.in[n.in.length - 1];
      known(sel).if_();
      f.ld(V(sel)).k(2).op(OP.shl).ldAt(tabOf.get(j)).tee(Lt).ldAt(0).set(Lv).get(Lt).ldAt(4).set(Lk);
      f.else_(); zero(); f.end();
      out(n.out[0]); return;
    }
    case "demux": {
      const d = n.in[0], sel = n.in[1];
      n.out.forEach((o, i) => {
        known(sel).if_();
        f.ld(V(sel)).k(i).op(OP.eq).if_();
        copy(d);
        f.else_().k(0).set(Lv).k(-1).set(Lk).end();
        f.else_(); zero(); f.end();
        out(o);
      });
      return;
    }
    case "decoder": {
      const sel = n.in[0], en = inBy(n, "en");
      /* if en known and 0: all known zeros; else if sel or en unknown: unknown;
       * else a single 1 at bit sel */
      if (en >= 0) {
        known(en).ld(V(en)).op(OP.eqz).op(OP.and).if_();
        f.k(0).set(Lv).k(-1).set(Lk);
        f.else_();
        known(sel).ld(K(en)).k(mask32(width(en))).op(OP.eq).op(OP.and).if_();
        f.k(1).ld(V(sel)).op(OP.shl).set(Lv).k(-1).set(Lk);
        f.else_(); zero(); f.end();
        f.end();
      } else {
        known(sel).if_();
        f.k(1).ld(V(sel)).op(OP.shl).set(Lv).k(-1).set(Lk);
        f.else_(); zero(); f.end();
      }
      out(n.out[0]); return;
    }
    case "encoder": {
      const a = n.in[0];
      known(a).if_();
      f.ld(V(a)).set(Lt);
      f.k(31).get(Lt).op(OP.clz).op(OP.sub).k(0).get(Lt).op(OP.select).set(Lv).k(-1).set(Lk);
      f.get(Lt).k(0).op(OP.ne).set(La).k(1).set(Lb);
      f.else_(); zero(); f.k(0).set(La).k(0).set(Lb); f.end();
      out(n.out[0]);
      f.get(La).set(Lv).get(Lb).set(Lk);
      out(n.out[1]); return;
    }
    case "add": {
      const a = n.in[0], b = n.in[1], c = inBy(n, "cin"), w = width(a);
      known(a); known(b).op(OP.and);
      if (c >= 0) known(c).op(OP.and);
      f.if_();
      f.ld(V(a)).op(OP.i64ext_u).ld(V(b)).op(OP.i64ext_u).op(OP.i64add);
      if (c >= 0) f.ld(V(c)).op(OP.i64ext_u).op(OP.i64add);
      f.tee(Ls).op(OP.wrap).set(Lv).k(-1).set(Lk);
      f.get(Ls).op(OP.i64).u_(w) /* w < 64: same byte as signed */.op(OP.i64shr_u).op(OP.wrap).k(1).op(OP.and).set(La).k(-1).set(Lb);
      f.else_(); zero(); f.k(0).set(La).k(0).set(Lb); f.end();
      out(n.out[0]);
      f.get(La).set(Lv).get(Lb).set(Lk);
      out(n.out[1]); return;
    }
    case "compare": {
      const a = n.in[0], b = n.in[1];
      [OP.lt_u, OP.eq, OP.gt_u].forEach((cmp, i) => {
        known(a); known(b).op(OP.and).if_();
        f.ld(V(a)).ld(V(b)).op(cmp).set(Lv).k(-1).set(Lk);
        f.else_(); zero(); f.end();
        out(n.out[i]);
      });
      return;
    }
    case "dff": case "tff": case "jkff": case "counter": {
      const rst = inBy(n, "rst"), w = width(n.out[0]);
      if (rst >= 0) {
        known(rst).ld(V(rst)).k(0).op(OP.ne).op(OP.and).if_();
        f.st(S(j), () => f.k(0));
        f.end();
      }
      f.ld(S(j)).set(Lv); out(n.out[0], -1);
      if (k === "counter") {
        const en = inBy(n, "en"), dn = inBy(n, "down");
        /* carry = (no en, or en known and 1) and state == all ones
         * (counting down: state == 0) */
        if (dn >= 0) {
          known(dn).ld(V(dn)).k(0).op(OP.ne).op(OP.and).if_(I32);
          f.ld(S(j)).op(OP.eqz);
          f.else_().ld(S(j)).k(mask32(w)).op(OP.eq).end();
        } else f.ld(S(j)).k(mask32(w)).op(OP.eq);
        if (en >= 0) known(en).ld(V(en)).k(0).op(OP.ne).op(OP.and).op(OP.and);
        f.set(Lv); out(n.out[1], -1);
      } else {
        f.ld(S(j)).k(-1).op(OP.xor).k(mask32(w)).op(OP.and).set(Lv); out(n.out[1], -1);
      }
      return;
    }
    case "split": {
      const a = n.in[0];
      n.ranges.forEach((r, i) => {
        const m = mask32(r[0] - r[1] + 1);
        f.ld(V(a)).k(r[1]).op(OP.shr_u).k(m).op(OP.and).set(Lv);
        f.ld(K(a)).k(r[1]).op(OP.shr_u).k(m).op(OP.and).set(Lk);
        out(n.out[i], undefined, true);
      });
      return;
    }
    case "merge": {
      f.k(0).set(Lv).k(0).set(Lk);
      n.ranges.forEach((r, i) => {
        f.get(Lv).ld(V(n.in[i])).k(r[1]).op(OP.shl).op(OP.or).set(Lv);
        f.get(Lk).ld(K(n.in[i])).k(r[1]).op(OP.shl).op(OP.or).set(Lk);
      });
      out(n.out[0], undefined, true); return;
    }
    }
    /* sinks (output, led, seg7, hex) drive nothing */
  }

  /* ---- settle body: components in order; loops repeat until no change */
  f.st(0, () => f.k(0));
  const isLoop = new Set(loops);
  for (const comp of sccs) {
    if (!isLoop.has(comp)) { emit(nodes[comp[0]], comp[0]); continue; }
    track = true;
    f.k(0).set(Lit);
    f.op(OP.block, VOID).op(OP.loop, VOID);
    f.k(0).set(Lch);
    comp.forEach(j => emit(nodes[j], j));
    /* stop when nothing changed */
    f.get(Lch).op(OP.eqz).brIf(1);
    /* or after 200 passes, marking the circuit unstable */
    f.get(Lit).k(1).op(OP.add).tee(Lit).k(200).op(OP.eq).if_();
    f.st(0, () => f.k(1)).br(2);
    f.end();
    f.br(0);
    f.end().end();
    track = false;
  }
  /* a bus where two parts talk at once: remember the first one */
  f.st(4, () => f.k(0));
  nodes.forEach((n, j) => {
    if (n.kind !== "bus") return;
    const m = mask32(width(n.out[0]));
    f.k(0).set(La).k(0).set(Lb);
    n.in.forEach(i => {
      f.ld(V(i)).ld(K(i)).k(-1).op(OP.xor).op(OP.and).k(-1).op(OP.xor).k(m).op(OP.and).set(Lt);
      f.get(Lb).get(La).get(Lt).op(OP.and).op(OP.or).set(Lb);
      f.get(La).get(Lt).op(OP.or).set(La);
    });
    f.get(Lb).k(0).op(OP.ne).ld(4).op(OP.eqz).op(OP.and).if_().st(4, () => f.k(j + 1)).end();
  });
  f.ld(0).op(OP.eqz).end();
  const settleBody = new Bytes().vec([[8, I32], [1, I64]].map(([n, t]) => new Bytes().u(n).byte(t).b));
  settleBody.add(f.c.b);

  /* ---- simulate(): settle, then clock the flip-flops that saw a rising edge */
  const seq = [];
  nodes.forEach((n, j) => { if (SEQ.has(n.kind) || n.kind === "ram") seq.push(j); });
  const g = new Fn();
  /* locals: then per clocked part i, fire = 4 + 3i, next (or the word to
   * write) = 5 + 3i, the address to write = 6 + 3i */
  const Gr = 0, Gfired = 1, Gc = 2, Gst = 3;
  const gKnown = net => g.ld(K(net)).k(mask32(width(net))).op(OP.eq);
  g.call(0).op(OP.drop);
  g.k(0).set(Gr);
  g.op(OP.block, VOID).op(OP.loop, VOID);
  g.k(0).set(Gfired);
  seq.forEach((j, i) => {
    const n = nodes[j], clk = inBy(n, "clk"), Lf = 4 + 3 * i, Ln = 5 + 3 * i, Laddr = 6 + 3 * i;
    const w = width(n.out[0]);
    if (clk >= 0) gKnown(clk).if_(I32).ld(V(clk)).else_().k(0).end();
    else g.k(0);
    g.set(Gc);
    /* rising = last == 0 and now != 0 */
    g.ld(LC(j)).op(OP.eqz).get(Gc).k(0).op(OP.ne).op(OP.and).set(Lf);
    g.st(LC(j), () => g.get(Gc));
    if (n.kind === "ram") {
      /* write if we is known and 1 and the address is known */
      const we = inBy(n, "we"), a = inBy(n, "addr"), din = inBy(n, "din");
      g.get(Lf).if_();
      gKnown(we).ld(V(we)).k(0).op(OP.ne).op(OP.and).ld(K(a)).k(mask32(width(a))).op(OP.eq).op(OP.and).set(Lf);
      g.get(Lf).if_();
      g.k(1).set(Gfired);
      g.ld(V(a)).k(2).op(OP.shl).k(memOf.get(j)).op(OP.add).set(Laddr);
      g.ld(V(din)).ld(K(din)).op(OP.and).set(Ln);
      g.end();
      g.end();
      return;
    }
    g.get(Lf).if_();
    g.k(1).set(Gfired);
    /* next state */
    g.ld(S(j)).set(Gst);
    const en = inBy(n, "en");
    const val = name => { const x = inBy(n, name); if (x >= 0) g.ld(V(x)); else g.k(0); };
    const body = () => {
      if (n.kind === "dff") g.ld(V(n.in[0])).ld(K(n.in[0])).op(OP.and).set(Ln);
      else if (n.kind === "tff") {
        g.get(Gst).k(1).op(OP.xor).get(Gst); val("t"); g.op(OP.select).set(Ln);
      } else if (n.kind === "jkff") {
        /* j&&k ? s^1 : j ? 1 : k ? 0 : s */
        g.get(Gst).k(1).op(OP.xor);
        g.k(1);
        g.k(0).get(Gst); val("k"); g.op(OP.select);
        val("j"); g.op(OP.select);
        val("j"); g.k(0).op(OP.ne); val("k"); g.k(0).op(OP.ne).op(OP.and);
        g.op(OP.select).set(Ln);
      } else {
        /* counter: one up, or one down while down is 1 */
        const dn = inBy(n, "down");
        if (dn >= 0) {
          g.get(Gst).k(-1).op(OP.add).get(Gst).k(1).op(OP.add);
          gKnown(dn).ld(V(dn)).k(0).op(OP.ne).op(OP.and).op(OP.select);
        } else g.get(Gst).k(1).op(OP.add);
        g.k(mask32(w)).op(OP.and).set(Ln);
      }
    };
    const counting = () => {
      if (en >= 0) {
        gKnown(en).ld(V(en)).k(0).op(OP.ne).op(OP.and).if_();
        body();
        g.else_().get(Gst).set(Ln).end();
      } else body();
    };
    const load = inBy(n, "load");
    if (n.kind === "counter" && load >= 0) {
      /* load wins over counting, and does not need en */
      const d = inBy(n, "d");
      gKnown(load).ld(V(load)).k(0).op(OP.ne).op(OP.and).if_();
      g.ld(V(d)).ld(K(d)).op(OP.and).k(mask32(w)).op(OP.and).set(Ln);
      g.else_(); counting(); g.end();
    } else counting();
    g.end();
  });
  g.get(Gfired).op(OP.eqz).brIf(1);
  seq.forEach((j, i) => {
    g.get(4 + 3 * i).if_();
    if (nodes[j].kind === "ram") g.get(6 + 3 * i).get(5 + 3 * i).op(OP.store, 2).u_(0);
    else g.st(S(j), () => g.get(5 + 3 * i));
    g.end();
  });
  g.call(0).op(OP.drop);
  g.get(Gr).k(1).op(OP.add).tee(Gr).k(64).op(OP.lt_u).brIf(0);
  g.end().end();
  g.ld(0).op(OP.eqz).end();
  const simBody = new Bytes().vec([[4 + 3 * seq.length, I32]].map(([n, t]) => new Bytes().u(n).byte(t).b));
  simBody.add(g.c.b);

  /* ---- toggle(n): flip every clock and simulate, n times */
  const clocks = [];
  nodes.forEach((n, j) => { if (n.kind === "clock") clocks.push(j); });
  const t = new Fn();
  t.op(OP.block, VOID).op(OP.loop, VOID);
  t.get(0).op(OP.eqz).brIf(1);
  clocks.forEach(j => t.st(S(j), () => t.ld(S(j)).k(1).op(OP.xor)));
  t.call(1).op(OP.drop);
  t.get(0).k(1).op(OP.sub).set(0).br(0);
  t.end().end();
  t.ld(0).op(OP.eqz).end();
  const togBody = new Bytes().vec([]);
  togBody.add(t.c.b);

  /* ---- the module */
  const m = new Bytes();
  m.byte(0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00);
  m.section(1, new Bytes().vec([
    [0x60, 0, 1, I32],                     /* () -> i32 */
    [0x60, 1, I32, 1, I32]                 /* (i32) -> i32 */
  ]).b);
  m.section(3, new Bytes().vec([[0], [0], [1]]).b);
  const pages = Math.max(1, Math.ceil(bytes / 65536));
  m.section(5, new Bytes().u(1).byte(0x00).u(pages).b);
  m.section(7, new Bytes().vec([
    [...new Bytes().str("settle").byte(0).u(0).b],
    [...new Bytes().str("simulate").byte(0).u(1).b],
    [...new Bytes().str("toggle").byte(0).u(2).b],
    [...new Bytes().str("memory").byte(2).u(0).b]
  ]).b);
  const code = [settleBody, simBody, togBody].map(body => new Bytes().u(body.b.length).add(body.b).b);
  m.section(10, new Bytes().vec(code).b);

  /* mux tables are data the code reads; fill them after instantiation */
  const init = mem => {
    tabOf.forEach((addr, j) => {
      const n = nodes[j];
      for (let i = 0; i < n.in.length - 1; i++) mem[(addr >> 2) + i] = V(n.in[i]);
    });
    resetMem(mem);
  };
  /* rom and ram back to the contents written in the circuit */
  const resetMem = mem => {
    memOf.forEach((base, j) => {
      const n = nodes[j], at = base >> 2;
      mem.fill(0, at, at + (1 << n.abits));
      (n.init || []).forEach(([start, words]) => { for (let i = 0; i < words.length; i++) mem[at + start + i] = words[i]; });
    });
  };
  return {
    bytes: new Uint8Array(m.b), init, resetMem, memOf, V, K, S, LC, loops, sccs, seq, clocks,
    layoutText() {
      const lines = [`memory: ${bytes} bytes, ${pages} page(s)`, `nets at ${NET} (${nslots} slots for ${nets.length} nets; pins share), nodes at ${NODE}`];
      nets.forEach((n, i) => lines.push(`  net ${i} ${n.name || ""} [${n.w}]  v@${V(i)} k@${K(i)}`));
      lines.push("order:");
      sccs.forEach(c => lines.push((isLoop.has(c) ? "  loop " : "  ") + c.map(j => `${j}:${nodes[j].kind}${nodes[j].name ? " " + nodes[j].name : ""}`).join(", ")));
      return lines.join("\n");
    }
  };
}

/* wrap an instance: plain helpers over the shared memory */
function wrap(c, instance, D) {
  const ex = instance.exports, mem = new Int32Array(ex.memory.buffer);
  c.init(mem);
  return {
    exports: ex, mem, compiled: c,
    v: i => mem[c.V(i) >> 2] >>> 0,
    k: i => mem[c.K(i) >> 2] >>> 0,
    setV: (i, x) => { mem[c.V(i) >> 2] = x; },
    setK: (i, x) => { mem[c.K(i) >> 2] = x; },
    state: j => mem[c.S(j) >> 2] >>> 0,
    setState: (j, x) => { mem[c.S(j) >> 2] = x; },
    lastClk: j => mem[c.LC(j) >> 2],
    setLastClk: (j, x) => { mem[c.LC(j) >> 2] = x; },
    settle: () => ex.settle() === 1,
    simulate: () => ex.simulate() === 1,
    toggle: n => ex.toggle(n) === 1,
    unstable: () => mem[0] !== 0,
    /* the bus part (node index) where two drivers talk at once, or -1 */
    conflict: () => mem[1] - 1,
    /* the words of a rom or ram, as a view into the memory */
    words: j => { const b = c.memOf.get(j); return b === undefined ? null : new Uint32Array(mem.buffer, b, 1 << D.nodes[j].abits); },
    resetMem: () => c.resetMem(mem),
    dump: () => c.layoutText()
  };
}

/* async: browsers limit synchronous compilation on the page's main thread */
async function create(D) {
  const c = compile(D);
  const { instance } = await WebAssembly.instantiate(c.bytes);
  return wrap(c, instance, D);
}
function createSync(D) {
  const c = compile(D);
  return wrap(c, new WebAssembly.Instance(new WebAssembly.Module(c.bytes)), D);
}

return { compile, create, createSync };
})();
if (typeof module !== "undefined") module.exports = WireSim;
