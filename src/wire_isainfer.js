/* wire_isainfer.js - an instruction set read from the hardware of a CPU.
 *
 * For a CPU written in VHDL that comes with no instruction set, the page
 * works out what it can from the circuit itself, in the .isa language:
 *
 *   the fields       the bit slices the circuit takes of the instruction
 *   the opcodes      the values the opcode slice is compared with (or a
 *                    `with ... select` picks on)
 *   register fields  slices that pick a register (the select of a mux of
 *                    flip-flops)
 *   signed fields    slices that are sign-extended
 *   pc-relative      slices added to the program counter
 *
 * Which fields each opcode uses is not in any one place, so it is found by
 * trying it: the instruction runs for one instruction's worth of clocks from
 * a few random register and memory contents, once as it is and once with
 * each bit flipped; a bit that changes nothing is padding for that opcode.
 * The names are not in the hardware at all: instructions are op0, op1 ...
 * and the operands are named after the circuit's signals, to rename.
 *
 *   WireIsaInfer.infer(D, info, sim) -> .isa text, or null when the
 *   circuit does not decode its instructions in a way this can follow.
 *   info is WireCpu.detect(D); sim a WireSim instance of D (its own). */
"use strict";
const WireIsaInfer = (() => {

const SEQ = new Set(["dff", "tff", "jkff", "counter"]);
const PASS = new Set(["pin_in", "pin_out", "buf", "bus"]);
const mask = w => w >= 32 ? 0xFFFFFFFF : ((1 << w) >>> 0) - 1;
const ident = s => /^[A-Za-z_][A-Za-z0-9_]*$/.test(s || "") && !/^(isa|reg|names|alias|format|data|func|pseudo|if|fits|pc|r\d+)$/.test(s);

function infer(D, info, sim) {
  if (!info || info.text === undefined) return null;
  const nodes = D.nodes, nets = D.nets;
  const text = nodes[info.text], W = text.bits;
  if (W > 32) return null;
  const driver = new Array(nets.length).fill(-1);
  const readers = nets.map(() => []);
  nodes.forEach((n, j) => {
    (n.out || []).forEach(x => { driver[x] = j; });
    (n.in || []).forEach((x, k) => readers[x].push([j, k]));
  });
  const inBy = (n, name) => { const i = (n.inNames || []).indexOf(name); return i < 0 ? -1 : n.in[i]; };
  /* through pins and buffers, to where a value comes from */
  const base = x => { for (let k = 0; k < 64 && x >= 0; k++) { const d = driver[x]; if (d < 0 || !PASS.has(nodes[d].kind)) return x; x = nodes[d].in[0]; } return x; };
  /* every net that is the same value as x (x itself, and what pins pass it on to) */
  const same = x => {
    const out = [x], seen = new Set(out);
    for (let i = 0; i < out.length; i++) readers[out[i]].forEach(([j]) => {
      if (!PASS.has(nodes[j].kind)) return;
      nodes[j].out.forEach(o => { if (!seen.has(o)) { seen.add(o); out.push(o); } });
    });
    return out;
  };
  const uses = x => same(x).flatMap(y => readers[y]);
  const nameOf = x => same(x).map(y => nets[y].name).find(ident) || "";

  /* ---- the instruction: what the program memory gives, or the register a
   * CPU of several clocks an instruction copies it into (an IR) */
  const romOut = base(text.out[0]);
  const loadedFromRom = j => {   /* the logic in front of a register reaches the program memory */
    const seen = new Set(), stack = [nodes[j].in[0]];
    for (let k = 0; stack.length && k < 400; k++) {
      const x = base(stack.pop());
      if (x === romOut) return true;
      if (seen.has(x)) continue;
      seen.add(x);
      const d = driver[x];
      /* the whole word, copied: through muxes and a bus (and-or), never a slice or arithmetic */
      if (d < 0 || !["mux", "and", "or", "bus"].includes(nodes[d].kind)) continue;
      nodes[d].in.forEach((y, i) => { if (nodes[d].inNames[i] !== "sel" && nets[y].w === W) stack.push(y); });
    }
    return false;
  };
  const irNodes = nodes.map((n, j) => j).filter(j => nodes[j].kind === "dff" && nets[nodes[j].out[0]].w === W && loadedFromRom(j));
  let slices = [];
  [text.out[0], ...irNodes.map(j => nodes[j].out[0])].forEach(src => same(src).forEach(x => readers[x].forEach(([j]) => {
    const n = nodes[j];
    if (n.kind !== "split" || !n.ranges) return;
    n.out.forEach((o, i) => { const [hi, lo] = n.ranges[i]; slices.push({ net: o, hi, lo, w: hi - lo + 1, src }); });
  })));
  if (!slices.length) return null;
  const constOf = x => { const d = driver[base(x)]; return d >= 0 && nodes[d].kind === "const" ? nodes[d].value >>> 0 : null; };
  const isStateNet = x => { const d = driver[base(x)]; return d >= 0 && SEQ.has(nodes[d].kind); };

  /* a register field: the select of a mux whose inputs are all flip-flops */
  const regMuxes = new Map();   /* slice -> the flip-flops of the register file */
  slices.forEach(s => uses(s.net).forEach(([j, k]) => {
    const n = nodes[j];
    if (n.kind !== "mux" || n.inNames[k] !== "sel") return;
    const data = n.in.filter((x, i) => n.inNames[i] !== "sel");
    if (data.length >= 2 && data.every(isStateNet)) regMuxes.set(s, data.map(x => driver[base(x)]));
  }));
  const regFile = [...new Set([...regMuxes.values()].flat())];
  const nreg = Math.max(0, ...[...regMuxes.values()].map(d => d.length));
  slices.forEach(s => { s.reg = regMuxes.has(s); });

  /* the values a slice is decoded into by gates: VHDL read as Logisim draws
   * it (op = "0101" is an AND of the bits and their inverses, a with/select
   * table of constants an OR of ANDs per bit). Every gate whose inputs are
   * only bits of the slice is worked out for each value the slice can have;
   * the gates whose outputs go on to other logic tell the values apart, and
   * a value that makes them give something else than most values do is one
   * the hardware decodes. */
  const GATE1 = new Set(["not", "buf", "and", "or", "xor", "nand", "nor", "xnor"]);
  const gateValues = (s, vals) => {
    if (s.w > 8) return;
    const bitOf = new Map();
    same(s.net).forEach(x => readers[x].forEach(([j]) => {
      const n = nodes[j];
      if (n.kind === "split" && n.ranges) n.out.forEach((o, i) => { if (n.ranges[i][0] === n.ranges[i][1]) bitOf.set(base(o), n.ranges[i][0]); });
    }));
    slices.forEach(t => { if (t.src === s.src && t.w === 1 && t.hi >= s.lo && t.hi <= s.hi) bitOf.set(base(t.net), t.hi - s.lo); });
    if (!bitOf.size) return;
    const pureGate = j => GATE1.has(nodes[j].kind) && nets[nodes[j].out[0]].w === 1;
    /* the gates fed only by bits of the slice (and by such gates) */
    const pure = new Set(), seen = new Set();
    const isPureNet = x => bitOf.has(base(x)) || (driver[base(x)] >= 0 && pure.has(driver[base(x)]));
    let grew = true;
    const cand = new Set();
    bitOf.forEach((_, x) => uses(x).forEach(([j]) => { if (pureGate(j)) cand.add(j); }));
    while (grew) {
      grew = false;
      for (const j of cand) {
        if (pure.has(j) || !nodes[j].in.every(isPureNet)) continue;
        pure.add(j); grew = true;
        uses(nodes[j].out[0]).forEach(([k]) => { if (pureGate(k) && !seen.has(k)) { seen.add(k); cand.add(k); } });
      }
    }
    const front = [...pure].filter(j => uses(nodes[j].out[0]).some(([k]) => !pure.has(k)));
    if (!front.length) return;
    const ev = (x, v, memo) => {
      const b = base(x);
      if (bitOf.has(b)) return (v >> bitOf.get(b)) & 1;
      if (memo.has(b)) return memo.get(b);
      const n = nodes[driver[b]], a = n.in.map(y => ev(y, v, memo));
      const k = n.kind, all = a.every(Boolean), any = a.some(Boolean), par = a.reduce((p, q) => p ^ q, 0);
      const r = k === "not" ? 1 - a[0] : k === "buf" ? a[0] : k === "and" ? +all : k === "nand" ? +!all
              : k === "or" ? +any : k === "nor" ? +!any : k === "xor" ? par : 1 - par;
      memo.set(b, r);
      return r;
    };
    const sigs = [];
    for (let v = 0; v < 2 ** s.w; v++) { const memo = new Map(); sigs.push([...pure].map(j => ev(nodes[j].out[0], v, memo))); }
    /* a gate that is 1 for one value only is a comparison with it (op = "0101") */
    const one = [...pure].map((j, i) => sigs.map(x => x[i])).filter(col => col.reduce((p, q) => p + q, 0) === 1);
    one.forEach(col => vals.add(col.indexOf(1)));
    if (one.length) return;
    /* no such gate: a table (with ... select of constants); the values whose
     * outputs differ from what most values give */
    const fi = front.map(j => [...pure].indexOf(j));
    const sig = sigs.map(x => fi.map(i => x[i]).join(""));
    const count = new Map();
    sig.forEach(x => count.set(x, (count.get(x) || 0) + 1));
    const top = [...count].sort((a, b) => b[1] - a[1]);
    if (top.length > 1 && top[0][1] === top[1][1]) return;   /* no one value most of them give */
    sig.forEach((x, v) => { if (x !== top[0][0]) vals.add(v); });
  };

  /* the opcode: the slice compared with the most constants (and picked on by with/select) */
  const valuesOf = s => {
    const vals = new Set();
    gateValues(s, vals);
    uses(s.net).forEach(([j, k]) => {
      const n = nodes[j];
      if (n.kind === "compare") { const c = constOf(n.in[1 - k]); if (c !== null) vals.add(c); }
      if (n.kind === "mux" && n.inNames[k] === "sel") {
        const data = n.in.filter((x, i) => n.inNames[i] !== "sel");
        if (data.length === 2 ** s.w) {
          const count = new Map();
          data.forEach(x => count.set(base(x), (count.get(base(x)) || 0) + 1));
          const common = [...count].sort((a, b) => b[1] - a[1])[0][0];
          data.forEach((x, i) => { if (base(x) !== common) vals.add(i); });
        }
      }
    });
    return vals;
  };
  let op = null, opVals = null;
  slices.filter(s => !s.reg && s.w >= 2).forEach(s => {
    const v = valuesOf(s);
    /* compared with every value it can have, and as wide as a register's number: which register is written */
    if (nreg >= 2 && s.w === Math.ceil(Math.log2(nreg)) && v.size === 2 ** s.w) { s.reg = true; return; }
    if (v.size >= 2 && (!op || v.size > opVals.size || (v.size === opVals.size && s.hi > op.hi))) { op = s; opVals = v; }
  });
  if (!op) return null;
  /* the instruction is what the opcode is sliced from: that register (or the program memory);
   * other registers loaded over the same bus (an accumulator) are not it */
  slices = slices.filter(s => s.src === op.src);
  const ir = irNodes.filter(j => nodes[j].out[0] === op.src);
  irNodes.length = 0; irNodes.push(...ir);
  op.name = "op";
  slices.forEach(s => { if (s !== op) s.name = nameOf(s.net); });

  /* signed: a slice whose top bit is copied above it (VHDL: (others => x(n)) & x(n downto m)) */
  const bitSlice = b => slices.filter(s => s.hi === b && s.lo === b).map(s => base(s.net));
  const copiesBit = (x, b) => {
    const d = driver[base(x)];
    if (d < 0) return false;
    if (bitSlice(b).includes(base(x))) return true;
    const n = nodes[d];
    return n.kind === "merge" && n.in.every(y => copiesBit(y, b));
  };
  slices.forEach(s => {
    uses(s.net).forEach(([j, k]) => {
      const n = nodes[j];
      if (n.kind !== "merge" || k === 0) return;
      if (n.in.slice(0, k).every(y => copiesBit(y, s.hi))) {
        s.signed = true;
        if (!s.name) s.name = nameOf(n.out[0]);
        s.ext = n.out[0];
      }
    });
  });

  /* pc-relative: added to the program counter (the register the program is read at) */
  const pcNet = base(inBy(text, "addr"));
  const pcRegs = new Set();
  (function back(x, depth) {   /* the address may be a slice of the pc */
    if (depth > 8 || x < 0) return;
    const d = driver[base(x)];
    if (d < 0) return;
    if (SEQ.has(nodes[d].kind)) { pcRegs.add(base(x)); return; }
    if (nodes[d].kind === "split" || nodes[d].kind === "merge") nodes[d].in.forEach(y => back(y, depth + 1));
  })(pcNet, 0);
  const fromPc = x => {   /* the pc, or the pc plus a constant (the next instruction) */
    let b = base(x);
    for (let k = 0; k < 6; k++) {
      if (pcRegs.has(b)) return "this";
      const d = driver[b];
      if (d < 0) return null;
      const n = nodes[d];
      if (n.kind === "split" || n.kind === "merge") { b = base(n.in[0]); continue; }
      if (n.kind === "add") {
        const c = n.in.map(constOf), other = c[0] !== null ? n.in[1] : c[1] !== null ? n.in[0] : -1;
        if (other >= 0 && pcRegs.has(base(other)) && [0, 1].some(i => c[i] !== null && c[i] > 0)) return "next";
        return null;
      }
      return null;
    }
    return null;
  };
  slices.forEach(s => {
    const starts = [s.net, s.ext].filter(x => x !== undefined);
    const seen = new Set();
    const walk = (x, depth) => {
      if (depth > 6 || seen.has(x)) return;
      seen.add(x);
      uses(x).forEach(([j, k]) => {
        const n = nodes[j];
        if (n.kind === "add") { const r = fromPc(n.in[1 - k]); if (r) s.rel = s.rel || r; }
        if (n.kind === "split" || n.kind === "merge") n.out.forEach(o => walk(o, depth + 1));
      });
    };
    starts.forEach(x => walk(x, 0));
  });

  /* ---- trying instructions: what each bit does, for each opcode */
  const seqNodes = nodes.map((n, j) => j).filter(j => SEQ.has(nodes[j].kind));
  const rams = nodes.map((n, j) => j).filter(j => nodes[j].kind === "ram");
  const inputs = nodes.map((n, j) => j).filter(j => ["input", "button", "keypad"].includes(nodes[j].kind));
  const pcNode = pcRegs.size ? driver[[...pcRegs][0]] : -1;
  let rnd = 12345;
  const rand = () => { rnd = (Math.imul(rnd, 1103515245) + 12345) >>> 0; return (rnd ^ (rnd >>> 15)) >>> 0; };
  const romWords = sim.words(info.text);
  let romFill = null, flagVals = null;
  function start(word, regVals, ramVals) {
    D.nets.forEach((n, i) => { sim.setV(i, 0); sim.setK(i, mask(n.w)); });
    seqNodes.forEach(j => { sim.setState(j, nodes[j].start || 0); sim.setLastClk(j, 0); });
    rams.forEach(j => sim.setLastClk(j, 0));
    inputs.forEach(j => sim.setState(j, /(_n|_b|n)$/i.test(nodes[j].name || "") && /rst|reset|clr/i.test(nodes[j].name || "") ? 1 : 0));
    /* the instruction at the address the CPU starts at, other words random (a CPU with one memory reads its data there) */
    if (romFill) { for (let a = 0; a < romWords.length; a++) romWords[a] = romFill[a % romFill.length] & mask(W); romWords[0] = word >>> 0; }
    else romWords.fill(word >>> 0);
    regFile.forEach((j, i) => sim.setState(j, regVals[i] & mask(nets[nodes[j].out[0]].w)));
    if (flagVals) flags.forEach((j, i) => sim.setState(j, flagVals[i]));
    rams.forEach((j, i) => { const w = sim.words(j); for (let a = 0; a < w.length; a++) w[a] = ramVals[i][a % ramVals[i].length]; });
    sim.simulate();
  }
  /* one instruction: until the pc moves (a CPU that takes several clocks), at most 16 */
  let cycles = 1;
  const irSet = new Set(irNodes);
  function after(word, regVals, ramVals, n) {
    start(word, regVals, ramVals);
    for (let c = 0; c < n; c++) sim.toggle(2);
    const st = seqNodes.filter(j => !irSet.has(j)).map(j => sim.state(j));   /* not the IR: the next instruction is in it */
    rams.forEach(j => { const w = sim.words(j); for (let a = 0; a < w.length; a++) st.push(w[a]); });
    lastPc = pcNode >= 0 ? sim.state(pcNode) : 0;
    return st.join(",");
  }
  let lastPc = 0;
  {   /* how many clocks one instruction takes: the pc moves after them */
    const nop = ([...opVals][0] << op.lo) >>> 0, regs = regFile.map(() => 0), mem = rams.map(() => [0]);
    if (pcNode >= 0) {
      /* the pc moves at the fetch: the clocks between two moves are one instruction, and
       * it is done just before the second (a CPU of one clock an instruction: 1) */
      start(nop, regs, mem);
      let last = sim.state(pcNode);
      const moves = [];
      for (let c = 1; c <= 40 && moves.length < 2; c++) { sim.toggle(2); const now = sim.state(pcNode); if (now !== last) { moves.push(c); last = now; } }
      cycles = moves.length === 2 ? moves[1] - 1 : moves.length ? moves[0] : 1;
      if (cycles < 1) cycles = 1;
    }
  }
  const below = [];
  for (let b = W - 1; b >= 0; b--) if (b > op.hi || b < op.lo) below.push(b);
  const TRIALS = 6, budget = 6000;
  if (opVals.size * below.length * TRIALS * 2 * cycles > budget * 4) return null;   /* too big to try in the page */
  const flags = seqNodes.filter(j => nets[nodes[j].out[0]].w === 1 && !regFile.includes(j) && j !== pcNode && !irSet.has(j));
  /* trials: half with different register contents, half with all registers the same */
  /* a CPU with an instruction register takes several clocks, not always as many: it runs the
   * instruction over and over (all of the program memory is it) for long enough to do it
   * a few times. One that does an instruction a clock runs it once, the rest of the program
   * memory random (where a CPU with one memory reads its data) */
  if (irNodes.length) cycles = Math.max(24, 3 * cycles);
  else romFill = Array.from({ length: 256 }, rand);
  const trials = [];
  for (let t = 0; t < TRIALS; t++) {
    const eq = t >= TRIALS / 2, v = rand();
    trials.push({ regs: regFile.map(() => eq ? v : rand()), mem: rams.map(() => Array.from({ length: 64 }, rand)), eq });
  }
  /* and registers equal in two classes, by one bit of their number: then a branch that
   * compares two registers is taken or not as that bit of either number changes */
  for (let j = 0; (1 << j) < regFile.length; j++) {
    const a = rand(), b2 = rand();
    trials.push({ regs: regFile.map((x, i) => (i >> j) & 1 ? b2 : a), mem: rams.map(() => Array.from({ length: 64 }, rand)), eq: false });
  }
  /* flags (the zero and carry of a jz or jc) set at random in some trials, so a jump is taken */
  trials.forEach((t, i) => { if (i % 2) t.flags = flags.map(() => rand() & 1); });
  const fill = v => { const w = rand() & mask(W); return ((w & ~(mask(op.w) << op.lo)) | (v << op.lo)) >>> 0; };
  /* the bits that change what it does, and those that change where it goes next (a jump) */
  const matters = new Map(), jumps = new Map();
  for (const v of opVals) {
    const m = new Set(), jm = new Set();
    for (const t of trials) {
      flagVals = t.flags || null;
      const w = fill(v), ref = after(w, t.regs, t.mem, cycles), refPc = lastPc;
      for (const b of below) if (after(w ^ (1 << b), t.regs, t.mem, cycles) !== ref) { m.add(b); if (lastPc !== refPc) jm.add(b); }
    }
    matters.set(v, m); jumps.set(v, jm);
  }
  /* a register field, or an immediate that shares its bits? With every pair of
   * registers that differ in that bit holding the same value, a register
   * field's bit changes nothing */
  const regMemo = new Map();
  function actsAsRegister(v, s) {
    const key = v + ":" + s.hi + ":" + s.lo;
    if (!regMemo.has(key)) { flagVals = null; regMemo.set(key, actsAsRegister1(v, s)); }
    return regMemo.get(key);
  }
  function actsAsRegister1(v, s) {
    for (let b = s.lo; b <= s.hi; b++) {
      const j = b - s.lo;
      for (let t = 0; t < 3; t++) {
        const vals = regFile.map(() => rand()), regs = vals.map((x, i) => vals[i & ~(1 << j)]);
        const mem = rams.map(() => Array.from({ length: 64 }, rand)), w = fill(v);
        if (after(w, regs, mem, cycles) !== after(w ^ (1 << b), regs, mem, cycles)) return false;
      }
    }
    return true;
  }

  /* ---- each opcode's fields: the slices that cover the bits that matter */
  const inside = (s, m) => { for (let b = s.lo; b <= s.hi; b++) if (!m.has(b)) return false; return true; };
  const others = slices.filter(s => s !== op && !(s.w === 1 && slices.some(t => t !== s && t.w > 1 && t.lo <= s.lo && t.hi >= s.hi)));
  const instrs = [];
  for (const v of [...opVals].sort((a, b) => a - b)) {
    const m = matters.get(v), fields = [];
    for (let b = W - 1; b >= 0; ) {
      if (b <= op.hi && b >= op.lo) { fields.push({ name: "op", hi: op.hi, lo: op.lo, fixed: v }); b = op.lo - 1; continue; }
      if (!m.has(b)) {   /* padding, down to the next bit that matters */
        let lo = b;
        while (lo - 1 >= 0 && !m.has(lo - 1) && !(lo - 1 <= op.hi && lo - 1 >= op.lo)) lo--;
        fields.push({ name: "pad", hi: b, lo, fixed: 0 }); b = lo - 1; continue;
      }
      const cands = others.filter(s => s.hi === b && inside(s, m));
      const isReg = r => r.reg && regFile.length && actsAsRegister(v, r);
      const regs = cands.filter(isReg);
      /* an immediate may not take in a register field this instruction uses */
      const imms = cands.filter(s => !regs.includes(s) && !others.some(r => r !== s && r.reg && r.lo >= s.lo && r.hi <= s.hi && r.w < s.w && inside(r, m) && isReg(r)))
                        .sort((x, y) => y.w - x.w);
      const pick0 = regs[0] || imms[0] || cands.find(s => s.reg) || null;
      let pick = pick0;
      if (!pick) {   /* bits no slice of the circuit names */
        let lo = b;
        /* down to where a slice of the circuit starts */
        while (lo - 1 >= 0 && m.has(lo - 1) && !(lo - 1 <= op.hi && lo - 1 >= op.lo) && !others.some(s => s.hi === lo - 1 && inside(s, m))) lo--;
        pick = { name: `b${b}_${lo}`, hi: b, lo, w: b - lo + 1 };
      }
      /* a pc-relative slice is a jump only in the instructions where it moves the pc */
      let jump = false;
      for (let x = pick.lo; x <= pick.hi; x++) if (jumps.get(v).has(x)) jump = true;
      fields.push({ name: pick.name || `b${pick.hi}_${pick.lo}`, hi: pick.hi, lo: pick.lo, slice: pick, rel: !!(pick.rel && jump) });
      b = pick.lo - 1;
    }
    /* operand names, once each in an instruction */
    const used = new Map();
    fields.filter(f => f.name !== "op").forEach(f => {
      let n = f.name, k = 2;
      while (used.has(n)) n = f.name + k++;
      used.set(n, 1); f.name = n;
    });
    instrs.push({ v, fields });
  }

  /* ---- the .isa */
  const hexd = Math.ceil(op.w / 4);
  const fmtOf = new Map(), fmts = [];
  const layout = f => f.map(x => `${x.name}:${x.hi - x.lo + 1}`).join(" ");
  instrs.forEach(i => {
    const key = layout(i.fields);
    if (!fmtOf.has(key)) { fmtOf.set(key, "F" + (fmts.length + 1)); fmts.push(key); }
    i.fmt = fmtOf.get(key);
  });
  const kindOf = f => {
    const s = f.slice, w = f.hi - f.lo + 1;
    if (s && s.reg) return "reg";
    if (f.rel) return `rel${w}`;
    return (s && s.signed ? "s" : "u") + w;
  };
  const anyRel = instrs.some(i => i.fields.some(f => f.rel)), relNext = instrs.some(i => i.fields.some(f => f.rel && f.slice.rel === "next"));
  const out = [];
  const title = (D.title || "cpu").replace(/[^A-Za-z0-9_]/g, "_").toLowerCase();
  out.push(`# The instructions of ${D.title || "this CPU"}, read from its hardware: the fields are`);
  out.push(`# the slices the circuit takes of an instruction, the opcodes the values it`);
  out.push(`# compares them with, and each instruction's operands the bits that change`);
  out.push(`# what it does. The hardware has no names for them: rename op${(0).toString(16)}, op1 ...`);
  out.push(`# (and the operands, named after the signals) to what they do, and put them`);
  out.push(`# in the order you want to write them.`);
  out.push(``);
  out.push(`isa "${title}" (width = ${W}${relNext ? ", rel = next" : ""})`);
  if (nreg >= 2) out.push(`reg r[0..${nreg - 1}]`);
  out.push(`data word 1`);
  out.push(``);
  fmts.forEach((f, i) => out.push(`format F${i + 1} = ${f}`));
  out.push(``);
  const width = Math.max(...instrs.map(i => i.fields.filter(f => f.fixed === undefined).map(f => `${f.name}:${kindOf(f)}`).join(", ").length)) + 6;
  instrs.forEach(i => {
    const ops = i.fields.filter(f => f.fixed === undefined).map(f => `${f.name}:${kindOf(f)}`).join(", ");
    const fixed = i.fields.filter(f => f.fixed !== undefined).map(f => `${f.name} = ${f.name === "op" ? "0x" + f.fixed.toString(16).toUpperCase().padStart(hexd, "0") : f.fixed}`);
    out.push(`${("op" + i.v.toString(16).toUpperCase() + " " + ops).padEnd(width)} = ${i.fmt}(${fixed.join(", ")})`);
  });
  if (anyRel) { out.push(``); out.push(`# rel operands are labels: the distance from ${relNext ? "the instruction after it" : "the instruction"}.`); }
  out.push(``);
  return out.join("\n");
}

return { infer };
})();
if (typeof module !== "undefined") module.exports = WireIsaInfer;
