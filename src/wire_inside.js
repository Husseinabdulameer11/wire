/* wire_inside.js - what is inside the built-in parts.
 *
 * A counter, an adder, a mux... are single parts to the simulator. To look
 * inside one, the page asks expand(D, j) to add, next to part j, a chip
 * made of simpler parts that does the same: a counter is a register, an
 * adder (+1) and muxes; an adder is a row of full adders; a full adder,
 * a mux of two, a decoder are gates; a D flip-flop is two latches of NAND
 * gates. The simpler parts are built-in parts too, so they open as well,
 * down to the gates.
 *
 * The chip reads the same wires as the part and drives wires of its own
 * (nothing else reads them), so it is a second copy of the part that the
 * page simulates beside it. Its flip-flops are set to the part's state
 * when it is made (init), so the inside shows what the part holds.
 *
 * The added instance has builtin: j, so the circuit's view does not draw
 * it as a chip; the part itself is what opens it. tests/sim/inside.js
 * checks every inside against its part. */
"use strict";
const WireInside = (() => {

const OPENS = new Set(["mux", "demux", "decoder", "encoder", "add", "compare", "dff", "tff", "jkff", "counter"]);
const mask = w => w >= 32 ? 0xFFFFFFFF : 2 ** w - 1;
const inBy = (n, name) => { const i = (n.inNames || []).indexOf(name); return i < 0 ? -1 : n.in[i]; };

function canOpen(n) { return !!n && OPENS.has(n.kind); }

/* what the inside of part n is, in a few words */
function about(n, D) {
  const w = i => D.nets[i].w, k = n.kind;
  if (k === "add") return `${w(n.out[0])} full adders in a row, each carry into the next`;
  if (k === "compare") return "a subtractor (a + not b + 1): no borrow means a ≥ b; equal when every bit is";
  if (k === "mux") return n.in.length > 3 ? `a tree of 2-input muxes, one level per bit of sel` : w(n.out[0]) > 1 ? "a 2-input mux per bit" : "gates: d0 and not sel, or d1 and sel";
  if (k === "demux") return "a decoder picks the output; each output is d and its line";
  if (k === "decoder") return "one AND gate per output, of each sel bit or its inverse";
  if (k === "encoder") return "each input wins when no higher input is 1; the winners make the bits of the index";
  if (k === "counter") return "a register holding the count, an adder for the next value, and muxes for en, down and load";
  if (k === "tff") return "a D flip-flop that takes q xor t";
  if (k === "jkff") return "a D flip-flop that takes (j and not q) or (not k and q)";
  if (k === "dff") {
    if (w(n.out[0]) > 1) return `${w(n.out[0])} one-bit D flip-flops sharing the clock`;
    if (inBy(n, "en") >= 0) return "a D flip-flop that takes its own q back while en is 0";
    return "two latches of NAND gates: the master follows d while the clock is 0, the slave shows it once the clock rises";
  }
  return "";
}

/* add, beside part j, the chip that is its inside. Returns { inst, init },
 * where init(state, lastClk) lists what to set so the inside holds what
 * the part holds: ["state", node, value], ["lastClk", node, value],
 * ["net", net, value] (a wire in a loop of gates). */
function expand(D, j) {
  const n = D.nodes[j];
  if (!canOpen(n)) return null;
  const W = i => D.nets[i].w;
  const inits = [];               /* (state, lastClk) => actions */
  const sig = [n.kind, ...n.in.map(W), ...n.inNames, ...n.out.map(W)].join(",");
  const top = D.insts.length;
  D.insts.push({ comp: n.kind === "dff" && n.name === "" ? "dff" : n.kind, name: n.name || "", parent: n.inst,
                 line: n.line, file: n.file, defFile: n.file, defLine: n.line, builtin: j, key: "builtin:" + sig,
                 about: about(n, D) });

  /* ---- a sheet to put parts on (the inside, or a chip inside it) */
  const sheet = inst => {
    const net = (w, name = "") => { D.nets.push({ w, name }); return D.nets.length - 1; };
    const node = (kind, ins, outs, extra) => {
      /* ins: [[name, net]], outs: [[name, net or width]] -> the out nets */
      const o = outs.map(([nm, x], i) => typeof x === "object" ? net(x.w, x.name || "") : x);
      D.nodes.push(Object.assign({ kind, name: "", line: n.line, file: n.file, inst,
        in: ins.map(x => x[1]), inNames: ins.map(x => x[0]), out: o, outNames: outs.map(x => x[0]) }, extra || {}));
      return o;
    };
    const S = {
      net, node,
      last: () => D.nodes.length - 1,
      gate: (kind, xs, name) => node(kind, xs.map(x => ["", x]), [["out", { w: W(xs[0]), name }]])[0],
      not: (a, name) => node("not", [["in", a]], [["out", { w: W(a), name }]])[0],
      konst: (v, w) => node("const", [], [["out", { w }]], { value: v >>> 0 })[0],
      bits: a => {
        if (W(a) === 1) return [a];
        return node("split", [["in", a]], Array.from({ length: W(a) }, (_, k) => [String(k), { w: 1 }]),
                    { ranges: Array.from({ length: W(a) }, (_, k) => [k, k]) });
      },
      merge: (xs, name) => {
        if (xs.length === 1) return xs[0];
        let lo = 0;
        const ranges = xs.map(x => { const r = [lo + W(x) - 1, lo]; lo += W(x); return r; });
        return node("merge", xs.map((x, i) => [String(i), x]), [["out", { w: lo, name }]], { ranges })[0];
      },
      /* an OR or AND of any number of 1-bit wires (a gate takes up to 16) */
      all: (kind, xs, name) => {
        if (xs.length === 1) return xs[0];
        if (xs.length <= 16) return S.gate(kind, xs, name);
        return S.gate(kind, [S.all(kind, xs.slice(0, 16)), S.all(kind, xs.slice(16))], name);
      },
      mux2: (d0, d1, s, name) => node("mux", [["d0", d0], ["d1", d1], ["sel", s]], [["out", { w: W(d0), name }]])[0],
      /* a chip: pins in, a body on its own sheet, pins out */
      chip: (comp, name, ins, outs, body) => {
        const c = D.insts.length;
        D.insts.push({ comp, name, parent: inst, line: n.line, file: n.file, defFile: n.file, defLine: n.line, key: "builtin:" + comp });
        const C = sheet(c), pins = {};
        ins.forEach(([nm, x]) => { pins[nm] = C.node("pin_in", [["", x]], [["", { w: W(x), name: nm }]], { name: nm })[0]; });
        const res = body(C, pins);
        return outs.map(nm => C.node("pin_out", [["", res[nm]]], [["", { w: W(res[nm]), name: name + "." + nm }]], { name: nm })[0]);
      }
    };
    return S;
  };

  const T = sheet(top);
  const pin = {};
  n.in.forEach((x, i) => {
    const nm = n.inNames[i] || "in" + i;
    pin[nm] = T.node("pin_in", [["", x]], [["", { w: W(x), name: nm }]], { name: nm })[0];
  });
  const outs = build(T, n, pin, W, inits);
  n.outNames.forEach((nm, i) => {
    T.node("pin_out", [["", outs[nm]]], [["", { w: W(outs[nm]), name: "" }]], { name: nm });
  });
  return { inst: top, init: (state, lastClk) => inits.flatMap(f => f(state >>> 0, lastClk)) };
}

/* ---- the insides, part by part: returns { outName: net } */
function build(T, n, p, W, inits) {
  const k = n.kind, w = W(n.out[0]);
  const flip = (S, d, clk, opts, q, nq) => {
    /* a 1-bit D flip-flop part on sheet S, its q and nq given (for loops) */
    const ins = [["d", d], ["clk", clk]];
    if (opts.en != null) ins.push(["en", opts.en]);
    if (opts.rst != null) ins.push(["rst", opts.rst]);
    S.node("dff", ins, [["q", q], ["nq", nq]], { name: opts.name || "" });
    return S.last();
  };
  const holds = (j, bit) => inits.push((s, lc) => [["state", j, bit == null ? s : (s >>> bit) & 1], ["lastClk", j, lc]]);
  const rst = p.rst, en = p.en;

  switch (k) {
  case "add": {
    const a = T.bits(p.a), b = T.bits(p.b), s = [];
    let c = p.cin != null ? p.cin : T.konst(0, 1);
    for (let i = 0; i < w; i++) {
      const [si, ci] = T.chip("full_adder", "fa" + i, [["a", a[i]], ["b", b[i]], ["cin", c]], ["s", "cout"], (C, q) => {
        const x = C.gate("xor", [q.a, q.b], "a^b");
        return { s: C.gate("xor", [x, q.cin], "s"), cout: C.gate("or", [C.gate("and", [q.a, q.b]), C.gate("and", [q.cin, x])], "cout") };
      });
      s.push(si); c = ci;
    }
    return { sum: T.merge(s, "sum"), cout: c };
  }
  case "compare": {
    const aw = W(p.a);
    const nb = T.not(p.b, "not b");
    const sub = T.node("add", [["a", p.a], ["b", nb], ["cin", T.konst(1, 1)]], [["sum", { w: aw, name: "a-b" }], ["cout", { w: 1, name: "a>=b" }]], { name: "sub" });
    const lt = T.not(sub[1], "lt");
    const x = T.gate("xnor", [p.a, p.b], "same bits");
    const eq = T.all("and", T.bits(x), "eq");
    const gt = T.gate("nor", [lt, eq], "gt");
    return { lt, eq, gt };
  }
  case "mux": {
    const nd = n.in.length - 1, ds = n.inNames.slice(0, nd).map(nm => p[nm]);
    if (nd === 2 && w === 1) {
      const ns = T.not(p.sel, "not sel");
      return { out: T.gate("or", [T.gate("and", [ds[0], ns]), T.gate("and", [ds[1], p.sel])], "out") };
    }
    if (nd === 2) {
      const b0 = T.bits(ds[0]), b1 = T.bits(ds[1]);
      return { out: T.merge(b0.map((x, i) => T.mux2(x, b1[i], p.sel)), "out") };
    }
    const sb = T.bits(p.sel);
    let cur = ds, lvl = 0;
    while (cur.length > 1) {
      const nx = [];
      for (let i = 0; i < cur.length; i += 2) nx.push(T.mux2(cur[i], cur[i + 1], sb[lvl]));
      cur = nx; lvl++;
    }
    return { out: cur[0] };
  }
  case "demux": {
    const dec = T.bits(T.node("decoder", [["sel", p.sel]], [["out", { w: 2 ** W(p.sel), name: "line" }]])[0]);
    const dw = W(p.d), r = {};
    n.outNames.forEach((nm, i) => {
      const line = dw === 1 ? dec[i] : T.merge(Array(dw).fill(dec[i]));
      r[nm] = T.gate("and", [p.d, line], nm);
    });
    return r;
  }
  case "decoder": {
    const sb = T.bits(p.sel), ns = sb.map((b, i) => T.not(b, "not s" + i)), lines = [];
    for (let i = 0; i < w; i++) {
      const terms = sb.map((b, kk) => (i >> kk) & 1 ? b : ns[kk]);
      if (en != null) terms.push(en);
      lines.push(T.all("and", terms, "line" + i));
    }
    return { out: T.merge(lines, "out") };
  }
  case "encoder": {
    const xb = T.bits(p.in), nx = xb.length;
    /* above[i]: some input higher than i is 1 */
    const above = new Array(nx).fill(null);
    for (let i = nx - 2; i >= 0; i--) above[i] = i === nx - 2 ? xb[nx - 1] : T.gate("or", [xb[i + 1], above[i + 1]]);
    const wins = xb.map((x, i) => above[i] == null ? x : T.gate("and", [x, T.not(above[i])], "wins" + i));
    const ob = [];
    for (let b = 0; b < w; b++) ob.push(T.all("or", wins.filter((_, i) => (i >> b) & 1), "bit" + b));
    return { out: T.merge(ob, "out"), valid: T.all("or", xb, "valid") };
  }
  case "dff": {
    if (w > 1) {
      const d = T.bits(p.d), q = [], nq = [];
      for (let i = 0; i < w; i++) {
        const qi = T.net(1, "q" + i), nqi = T.net(1);
        holds(flip(T, d[i], p.clk, { en, rst, name: "ff" + i }, qi, nqi), i);
        q.push(qi); nq.push(nqi);
      }
      return { q: T.merge(q, "q"), nq: T.merge(nq, "nq") };
    }
    if (en != null) {
      const q = T.net(1, "q"), nq = T.net(1, "nq");
      const d = T.mux2(q, p.d, en, "d or q");
      holds(flip(T, d, p.clk, { rst, name: "ff" }, q, nq));
      return { q, nq };
    }
    /* master-slave, of NAND gates; rst (async) clears both latches */
    const nr = rst != null ? T.not(rst, "not rst") : null, r = xs => nr != null ? [...xs, nr] : xs;
    const nclk = T.not(p.clk, "not clk"), nd = T.not(p.d, "not d");
    const m = T.net(1, "master"), mn = T.net(1, "not master"), q = T.net(1, "q"), nq = T.net(1, "nq");
    const s1 = T.gate("nand", r([p.d, nclk])), r1 = T.gate("nand", [nd, nclk]);
    T.node("nand", [["", s1], ["", mn]], [["out", m]]);
    T.node("nand", r([r1, m]).map(x => ["", x]), [["out", mn]]);
    const s2 = T.gate("nand", r([m, p.clk])), r2 = T.gate("nand", [mn, p.clk]);
    T.node("nand", [["", s2], ["", nq]], [["out", q]]);
    T.node("nand", r([r2, q]).map(x => ["", x]), [["out", nq]]);
    inits.push(s => [["net", m, s & 1], ["net", mn, ~s & 1], ["net", q, s & 1], ["net", nq, ~s & 1]]);
    return { q, nq };
  }
  case "tff": case "jkff": {
    const q = T.net(1, "q"), nq = T.net(1, "nq");
    const d = k === "tff" ? T.gate("xor", [p.t, q], "d")
                          : T.gate("or", [T.gate("and", [p.j, nq]), T.gate("and", [T.not(p.k), q])], "d");
    holds(flip(T, d, p.clk, { rst, name: "ff" }, q, nq));
    return { q, nq };
  }
  case "counter": {
    const q = T.net(w, "count"), nq = T.net(w);
    let next = T.node("add", [["a", q], ["b", T.konst(1, w)]], [["sum", { w, name: "count+1" }], ["cout", { w: 1 }]], { name: "plus1" })[0];
    if (p.down != null) {
      const dec = T.node("add", [["a", q], ["b", T.konst(mask(w), w)]], [["sum", { w, name: "count-1" }], ["cout", { w: 1 }]], { name: "minus1" })[0];
      next = T.mux2(next, dec, p.down, "step");
    }
    if (en != null) next = T.mux2(q, next, en, "next");
    if (p.load != null) next = T.mux2(next, p.d, p.load, "next");
    const ins = [["d", next], ["clk", p.clk]];
    if (rst != null) ins.push(["rst", rst]);
    T.node("dff", ins, [["q", q], ["nq", nq]], { name: "reg" });
    holds(T.last());
    const last = p.down != null ? T.mux2(T.konst(mask(w), w), T.konst(0, w), p.down, "last") : T.konst(mask(w), w);
    const at = T.node("compare", [["a", q], ["b", last]], [["lt", { w: 1 }], ["eq", { w: 1, name: "at last" }], ["gt", { w: 1 }]])[1];
    return { q, carry: en != null ? T.gate("and", [at, en], "carry") : at };
  }
  }
  return {};
}

return { canOpen, expand, about, OPENS };
})();
if (typeof module !== "undefined") module.exports = WireInside;
