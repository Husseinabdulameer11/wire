/* ref_sim.js - the original JavaScript simulator from wire_view.html, kept
 * as the reference that tests/sim/simtest.js checks the WebAssembly
 * simulator against. Do not "fix" it here: if it is wrong, fix both and
 * say why.
 *
 * Added since, written plainly here and separately in wire_sim.js, so the
 * two can check each other (marked "added"):
 *   - floating values (z: known bit 0, value bit 1), passed on by pins,
 *     buf, split, merge and bus; other parts read z as unknown
 *   - tri, bus, rom, ram, keypad, and counter load / d / down
 *   - which bus has two drivers at once, after each settle */
"use strict";
module.exports = function refSim(D) {
const nets = D.nets.map((n, i) => ({ id: i, w: n.w, name: n.name, v: 0, k: 0, driver: null, readers: [] }));
const nodes = D.nodes.map((n, i) => Object.assign({ id: i, x: 0, y: 0, state: 0, lastClk: 0 }, n));
const SOURCES = new Set(["input", "button", "clock"]);
const SEQ = new Set(["dff", "tff", "jkff", "counter"]);
nodes.forEach(n => {
  n.out.forEach((net, p) => { nets[net].driver = { node: n.id, port: p }; });
  n.in.forEach((net, p) => nets[net].readers.push({ node: n.id, port: p }));
});
const mask = w => w >= 32 ? 0xFFFFFFFF : ((1 << w) >>> 0) - 1;
const u32 = x => x >>> 0;
const redraw = () => {};
function rd(n, p) { const net = nets[n.in[p]]; return net; }
function inByName(n, name) { const i = n.inNames.indexOf(name); return i < 0 ? null : nets[n.in[i]]; }
function setOut(n, p, v, k, keepZ) {
  const net = nets[n.out[p]], m = mask(net.w);
  k = u32(k & m); v = keepZ ? u32(v & m) : u32(v & k);   /* added: keepZ */
  if (net.v !== v || net.k !== k) { net.v = v; net.k = k; return true; }
  return false;
}
const known = net => net.k === mask(net.w);

function evalNode(n) {
  const k = n.kind;
  let v, kn, ch = false;
  switch (k) {
  case "input": case "button": case "clock":
    return setOut(n, 0, n.state, 0xFFFFFFFF);
  case "const":
    return setOut(n, 0, n.value, 0xFFFFFFFF);
  case "and": case "nand": case "or": case "nor": case "xor": case "xnor": {
    const w = nets[n.out[0]].w, m = mask(w);
    let a = rd(n, 0); v = a.v; kn = a.k;
    for (let i = 1; i < n.in.length; i++) {
      const b = rd(n, i);
      if (k === "and" || k === "nand") { const nk = (kn & b.k) | (kn & ~v) | (b.k & ~b.v); v = v & b.v & nk; kn = nk; }
      else if (k === "or" || k === "nor") { const nk = (kn & b.k) | (kn & v) | (b.k & b.v); v = (v | b.v) & nk; kn = nk; }
      else { kn = kn & b.k; v = (v ^ b.v) & kn; }
    }
    if (k === "nand" || k === "nor" || k === "xnor") v = ~v & kn & m;
    return setOut(n, 0, v, kn);
  }
  case "not": { const a = rd(n, 0); return setOut(n, 0, ~a.v & a.k, a.k); }
  case "pin_in": case "pin_out": { const a = rd(n, 0); return setOut(n, 0, a.v, a.k, true); }
  case "buf": { const a = rd(n, 0); return setOut(n, 0, a.v, a.k, true); }
  /* added */
  case "keypad": return setOut(n, 0, n.state, 0xFFFFFFFF);
  case "tri": {
    const x = rd(n, 0), en = rd(n, 1), w = nets[n.out[0]].w;
    if (!known(en)) return setOut(n, 0, 0, 0);
    if (en.v) return setOut(n, 0, x.v & x.k, x.k, true);
    return setOut(n, 0, mask(w), 0, true);              /* floating */
  }
  case "bus": {
    const w = nets[n.out[0]].w;
    let talking = [], bits = [];
    for (let b = 0; b < w; b++) {
      /* the inputs that are not floating on this bit */
      const on = n.in.map(i => nets[i]).filter(x => !(((x.k >>> b) & 1) === 0 && ((x.v >>> b) & 1) === 1));
      if (on.length === 0) bits.push([1, 0]);           /* nobody: floating */
      else if (on.length > 1) bits.push([0, 0]);        /* two at once: unknown */
      else bits.push([(on[0].v >>> b) & (on[0].k >>> b) & 1, (on[0].k >>> b) & 1]);
    }
    v = 0; kn = 0;
    bits.forEach(([bv, bk], b) => { v |= bv << b; kn |= bk << b; });
    return setOut(n, 0, v, kn, true);
  }
  case "rom": case "ram": {
    const a = inByName(n, "addr");
    if (!known(a)) return setOut(n, 0, 0, 0);
    return setOut(n, 0, n.mem[a.v], 0xFFFFFFFF);
  }
  case "mux": {
    const sel = nets[n.in[n.in.length - 1]];
    if (!known(sel)) return setOut(n, 0, 0, 0);
    const d = rd(n, sel.v); return setOut(n, 0, d.v, d.k);
  }
  case "demux": {
    const d = rd(n, 0), sel = rd(n, 1);
    for (let i = 0; i < n.out.length; i++)
      ch = (known(sel) ? (i === sel.v ? setOut(n, i, d.v, d.k) : setOut(n, i, 0, 0xFFFFFFFF)) : setOut(n, i, 0, 0)) || ch;
    return ch;
  }
  case "decoder": {
    const sel = rd(n, 0), en = inByName(n, "en");
    if (en && known(en) && !en.v) return setOut(n, 0, 0, 0xFFFFFFFF);
    if (!known(sel) || (en && !known(en))) return setOut(n, 0, 0, 0);
    return setOut(n, 0, u32(1 << sel.v), 0xFFFFFFFF);
  }
  case "encoder": {
    const a = rd(n, 0);
    if (!known(a)) return setOut(n, 0, 0, 0) | setOut(n, 1, 0, 0);
    let hi = 0; for (let b = 0; b < a.w; b++) if ((a.v >>> b) & 1) hi = b;
    return setOut(n, 0, hi, 0xFFFFFFFF) | setOut(n, 1, a.v ? 1 : 0, 1);
  }
  case "add": {
    const a = rd(n, 0), b = rd(n, 1), c = inByName(n, "cin"), w = a.w;
    if (!known(a) || !known(b) || (c && !known(c))) return setOut(n, 0, 0, 0) | setOut(n, 1, 0, 0);
    const s = a.v + b.v + (c ? c.v : 0);
    return setOut(n, 0, s, 0xFFFFFFFF) | setOut(n, 1, w >= 32 ? (s > 0xFFFFFFFF ? 1 : 0) : (s >>> w) & 1, 1);
  }
  case "compare": {
    const a = rd(n, 0), b = rd(n, 1);
    if (!known(a) || !known(b)) return setOut(n, 0, 0, 0) | setOut(n, 1, 0, 0) | setOut(n, 2, 0, 0);
    return setOut(n, 0, a.v < b.v ? 1 : 0, 1) | setOut(n, 1, a.v === b.v ? 1 : 0, 1) | setOut(n, 2, a.v > b.v ? 1 : 0, 1);
  }
  case "dff": case "tff": case "jkff": case "counter": {
    const rst = inByName(n, "rst");
    if (rst && known(rst) && rst.v) n.state = 0;
    const w = nets[n.out[0]].w;
    ch = setOut(n, 0, n.state, 0xFFFFFFFF);
    if (k === "counter") {
      const en = inByName(n, "en"), on = !en || (known(en) && en.v);
      const dn = inByName(n, "down"), down = dn && known(dn) && dn.v;   /* added: down */
      ch = setOut(n, 1, on && n.state === (down ? 0 : mask(w)) ? 1 : 0, 1) || ch;
    } else ch = setOut(n, 1, ~n.state & mask(w), 0xFFFFFFFF) || ch;
    return ch;
  }
  case "split": {
    const a = rd(n, 0);
    n.ranges.forEach((r, i) => {
      const m = mask(r[0] - r[1] + 1);
      ch = setOut(n, i, (a.v >>> r[1]) & m, (a.k >>> r[1]) & m, true) || ch;
    });
    return ch;
  }
  case "merge": {
    v = 0; kn = 0;
    n.ranges.forEach((r, i) => { const a = rd(n, i); v |= a.v << r[1]; kn |= a.k << r[1]; });
    return setOut(n, 0, v, kn, true);
  }
  }
  return false;
}
/* the next state of a flip-flop on a rising clock edge */
function nextState(n) {
  const w = nets[n.out[0]].w, en = inByName(n, "en");
  /* added: a counter's load wins, even without en */
  const ld = n.kind === "counter" && inByName(n, "load");
  if (ld && known(ld) && ld.v) { const d = inByName(n, "d"); return (d.v & d.k) & mask(w); }
  if (en && !(known(en) && en.v)) return n.state;
  const val = name => { const x = inByName(n, name); return x ? x.v : 0; };
  switch (n.kind) {
  case "dff": return rd(n, 0).v & rd(n, 0).k;   /* added: & k, so z is stored as 0 */
  case "tff": return val("t") ? n.state ^ 1 : n.state;
  case "jkff": { const j = val("j"), kk = val("k"); return j && kk ? n.state ^ 1 : j ? 1 : kk ? 0 : n.state; }
  case "counter": {   /* added: down */
    const dn = inByName(n, "down");
    return (n.state + (dn && known(dn) && dn.v ? -1 : 1)) & mask(w);
  }
  }
  return n.state;
}

let order = [];        /* evaluation order (sources first, then topological) */
function computeOrder() {
  nodes.forEach((n, i) => { n.vi = i; });
  const drv = new Map();
  nets.forEach((net, i) => { if (net.driver) drv.set(i, net.driver); });
  order = graphOf(nodes, drv).topo.map(i => nodes[i]);
}
const seqNodes = nodes.filter(n => SEQ.has(n.kind) || n.kind === "ram");   /* added: ram */
/* added: rom and ram contents */
function resetMem() {
  nodes.forEach(n => {
    if (n.kind !== "rom" && n.kind !== "ram") return;
    n.mem = new Uint32Array(1 << n.abits);
    (n.init || []).forEach(([start, words]) => words.forEach((x, i) => { n.mem[start + i] = x; }));
  });
}
resetMem();
let unstable = false, conflict = -1;
function settle() {
  const done = ok => {
    /* added: the first bus where two parts talk at once */
    conflict = -1;
    for (const n of nodes) {
      if (n.kind !== "bus" || conflict >= 0) continue;
      const w = nets[n.out[0]].w;
      for (let b = 0; b < w; b++) {
        const on = n.in.map(i => nets[i]).filter(x => !(((x.k >>> b) & 1) === 0 && ((x.v >>> b) & 1) === 1));
        if (on.length > 1) { conflict = n.id; break; }
      }
    }
    return ok;
  };
  for (let pass = 0; pass < 200; pass++) {
    let ch = false;
    for (const n of order) if (evalNode(n)) ch = true;
    if (!ch) { unstable = false; return done(true); }
  }
  unstable = true;
  return done(false);
}
/* settle, then clock every flip-flop that saw a rising edge; repeat for
 * ripple counters, where one flip-flop clocks the next */
function simulate() {
  settle();
  for (let round = 0; round < 64; round++) {
    const fire = [];
    for (const n of seqNodes) {
      const c = inByName(n, "clk"), now = c && known(c) ? c.v : 0;
      if (!n.lastClk && now) fire.push(n);
      n.lastClk = now;
    }
    if (!fire.length) break;
    /* added: a ram writes din to addr when we is 1 */
    const ramW = n => {
      const we = inByName(n, "we"), a = inByName(n, "addr"), d = inByName(n, "din");
      return known(we) && we.v && known(a) ? [a.v, d.v & d.k] : null;
    };
    const next = fire.map(n => n.kind === "ram" ? ramW(n) : nextState(n));
    fire.forEach((n, i) => {
      if (n.kind !== "ram") n.state = next[i];
      else if (next[i]) n.mem[next[i][0]] = next[i][1];
    });
    settle();
  }
  redraw();
}

function graphOf(items, drv) {
  const N = items.length, edges = [];
  items.forEach(v => v.in.forEach((net, p) => {
    const d = drv.get(net); if (d) edges.push({ u: d.node, v: v.vi, net, p });
  }));
  const outE = items.map(() => []), inE = items.map(() => []);
  edges.forEach((e, i) => { outE[e.u].push(i); inE[e.v].push(i); });
  /* depth-first search from the inputs marks the edges that close a loop */
  const back = new Set(), state = new Array(N).fill(0);
  const visit = s => {
    const stack = [[s, 0]]; state[s] = 1;
    while (stack.length) {
      const top = stack[stack.length - 1], u = top[0];
      if (top[1] < outE[u].length) {
        const ei = outE[u][top[1]++], v = edges[ei].v;
        if (state[v] === 1) back.add(ei);
        else if (state[v] === 0) { state[v] = 1; stack.push([v, 0]); }
      } else { state[u] = 2; stack.pop(); }
    }
  };
  items.filter(n => isSrc(n)).forEach(n => { if (!state[n.vi]) visit(n.vi); });
  items.filter(n => n.kind === "const").forEach(n => { if (!state[n.vi]) visit(n.vi); });
  items.forEach(n => { if (!state[n.vi]) visit(n.vi); });
  const indeg = new Array(N).fill(0);
  edges.forEach((e, i) => { if (!back.has(i)) indeg[e.v]++; });
  const topo = [], q = items.filter(n => !indeg[n.vi]).map(n => n.vi);
  while (q.length) {
    const u = q.shift(); topo.push(u);
    outE[u].forEach(ei => { if (!back.has(ei) && --indeg[edges[ei].v] === 0) q.push(edges[ei].v); });
  }
  return { edges, outE, inE, back, topo };
}
const isSrc = n => SOURCES.has(n.kind) || n.kind === "pin_in";
const isSink = n => SINKS.has(n.kind) || n.kind === "pin_out";
computeOrder();
nets.forEach(n => { n.v = 0; n.k = mask(n.w); });
return { nets, nodes, settle, simulate, resetMem, unstable: () => unstable, conflict: () => conflict };
};
