/* wire_exam.js - checks a circuit against an exercise (exercises/, tools/exam.js).
 *
 * An exercise says which ports the circuit has and how it must behave: the
 * values to put on its inputs, step by step, and the outputs it must give.
 * Those come from the exercise's own solution, simulated when the site is
 * made; the solution itself is not in the page.
 *
 *   WireExam.ports(D)                     the ports of a netlist (wire --json)
 *   WireExam.makeTests(ex, rng)           the steps to try (tools/exam.js)
 *   WireExam.run(D, sim, ex, steps)       what a circuit's outputs are at each step
 *   WireExam.grade(D, sim, ex)            { ok, title, text, rows, columns }
 *
 * A step sets the inputs; for a circuit with a clock it is then one clock
 * cycle (the clock goes to 1 and back to 0), and the outputs are read after
 * it. A circuit without a clock is read once its inputs are set. */
"use strict";
const WireExam = (() => {

const SOURCES = new Set(["input", "button", "clock", "keypad"]);
const mask = w => w >= 32 ? 0xFFFFFFFF : ((1 << w) >>> 0) - 1;

/* the ports at the top of a circuit: [{ name, w, id, clock }] in and out */
function ports(D) {
  const ins = [], outs = [];
  D.nodes.forEach((n, j) => {
    if (n.inst !== 0 || !n.name) return;
    if (SOURCES.has(n.kind)) ins.push({ name: n.name.toLowerCase(), w: D.nets[n.out[0]].w, id: j, clock: n.kind === "clock" });
    else if (n.kind === "output") outs.push({ name: n.name.toLowerCase(), w: D.nets[n.in[0]].w, id: j, net: n.in[0] });
  });
  return { ins, outs };
}

/* the steps: every input for a circuit without a clock (when there are not too many),
 * else random ones; a circuit with a clock starts with its reset held */
function makeTests(ex, rng) {
  const data = ex.inputs.filter(p => p.name !== ex.clock);
  const bits = data.reduce((t, p) => t + p.w, 0);
  const rand = w => w >= 32 ? (rng() * 4294967296) >>> 0 : Math.floor(rng() * 2 ** w);
  const steps = [];
  let checkFrom = 0;
  if (!ex.clock) {
    if (bits <= 12) {
      for (let k = 0; k < 2 ** bits; k++) {   /* the first input is the most significant bits */
        let rest = k;
        const s = [];
        for (let i = data.length - 1; i >= 0; i--) { s[i] = rest % 2 ** data[i].w; rest = Math.floor(rest / 2 ** data[i].w); }
        steps.push(s);
      }
    } else {
      steps.push(data.map(() => 0), data.map(p => mask(p.w)));
      for (let k = 0; k < (ex.count || 1500); k++) steps.push(data.map(p => rand(p.w)));
    }
    return { steps, checkFrom };
  }
  const prob = ex.random || {};
  const reset = data.find(p => /^(rst|reset|clr|clear)$/.test(p.name));
  if (reset) {   /* two cycles of reset first; they are not checked */
    for (let k = 0; k < 2; k++) steps.push(data.map(p => p === reset ? 1 : 0));
    checkFrom = 2;
  } else checkFrom = ex.settle || 1;
  const n = ex.cycles || 200;
  while (steps.length < n) steps.push(data.map(p => {
    if (p.w === 1) return rng() < (p.name in prob ? prob[p.name] : p === reset ? 0.03 : 0.5) ? 1 : 0;
    return rand(p.w);
  }));
  return { steps, checkFrom };
}

/* the outputs of a circuit at each step: [[value or null (unknown)]] */
function run(D, sim, ex, steps) {
  const P = ports(D), byName = new Map(P.ins.map(p => [p.name, p]));
  const data = ex.inputs.filter(p => p.name !== ex.clock).map(p => byName.get(p.name));
  const clk = ex.clock ? byName.get(ex.clock) : null;
  const outs = ex.outputs.map(o => P.outs.find(p => p.name === o.name));
  D.nets.forEach((n, i) => { sim.setV(i, 0); sim.setK(i, mask(n.w)); });
  P.ins.forEach(p => sim.setState(p.id, 0));
  sim.simulate();
  const got = [];
  for (let s = 0; s < steps.length; s++) {
    data.forEach((p, i) => sim.setState(p.id, steps[s][i] & mask(p.w)));
    sim.simulate();
    if (clk) { sim.setState(clk.id, 1); sim.simulate(); sim.setState(clk.id, 0); sim.simulate(); }
    if (sim.unstable()) return { got, unstable: s };
    got.push(outs.map(o => sim.k(o.net) === mask(o.w) ? sim.v(o.net) >>> 0 : null));
  }
  return { got };
}

const bin = (v, w) => v === null ? "X".repeat(w) : w <= 8 ? (v >>> 0).toString(2).padStart(w, "0") : "0x" + (v >>> 0).toString(16).toUpperCase().padStart(Math.ceil(w / 4), "0");
const show = (v, w) => v === null ? "X" : w === 1 ? String(v) : bin(v, w);

/* does the circuit do what the exercise asks? */
function grade(D, sim, ex) {
  const P = ports(D);
  /* the ports first: the names, which way they go, and how wide */
  const problems = [];
  for (const p of ex.inputs) {
    const s = P.ins.find(q => q.name === p.name);
    if (!s) problems.push(P.outs.some(q => q.name === p.name) ? `${p.name} must be an input (in), not an output` : `the input ${p.name} is missing`);
    else if (s.w !== p.w) problems.push(`${p.name} must be ${p.w} bit${p.w === 1 ? "" : "s"} wide${p.w === 1 ? " (std_logic)" : ` (std_logic_vector(${p.w - 1} downto 0))`}, yours is ${s.w}`);
  }
  for (const p of ex.outputs) {
    const s = P.outs.find(q => q.name === p.name);
    if (!s) problems.push(P.ins.some(q => q.name === p.name) ? `${p.name} must be an output (out), not an input` : `the output ${p.name} is missing`);
    else if (s.w !== p.w) problems.push(`${p.name} must be ${p.w} bit${p.w === 1 ? "" : "s"} wide${p.w === 1 ? " (std_logic)" : ` (std_logic_vector(${p.w - 1} downto 0))`}, yours is ${s.w}`);
  }
  for (const q of P.ins) if (!ex.inputs.some(p => p.name === q.name)) problems.push(`the exercise has no input called ${q.name}`);
  for (const q of P.outs) if (!ex.outputs.some(p => p.name === q.name)) problems.push(`the exercise has no output called ${q.name}`);
  if (problems.length) return { ok: false, title: "The ports are not the ones the exercise asks for", text: problems.join("; ") + ". Keep the entity as the exercise gives it." };

  const { got, unstable } = run(D, sim, ex, ex.tests.steps);
  const data = ex.inputs.filter(p => p.name !== ex.clock);
  const columns = [...(ex.clock ? ["cycle"] : []), ...data.map(p => p.name), ...ex.outputs.map(o => o.name)];
  if (unstable !== undefined) return { ok: false, title: "Your circuit never settles", text: "Some signal depends on itself without a clock in between (a loop), so it keeps changing. Every feedback path needs to go through a register (a process with rising_edge)." };
  for (let s = ex.tests.checkFrom; s < ex.tests.steps.length; s++) {
    const exp = ex.tests.expect[s];
    const bad = ex.outputs.findIndex((o, i) => got[s][i] !== exp[i]);
    if (bad < 0) continue;
    const o = ex.outputs[bad], inputsText = data.map((p, i) => `${p.name} = ${show(ex.tests.steps[s][i], p.w)}`).join(", ");
    const yours = got[s][bad];
    const text = ex.clock
      ? `After clock cycle ${s + 1}, ${o.name} should be ${show(exp[bad], o.w)}, yours is ${show(yours, o.w)}.`
      : `When ${inputsText}: ${o.name} should be ${show(exp[bad], o.w)}, yours is ${show(yours, o.w)}.`;
    const why = yours === null ? " X means it has no value: it is not assigned, or it depends on a signal that never gets one." : "";
    /* the rows to show: for a clock, the cycles leading up to it */
    const from = ex.clock ? Math.max(0, s - 7) : s, rows = [];
    for (let r = from; r <= s; r++) rows.push({
      cells: [...(ex.clock ? [String(r + 1)] : []), ...data.map((p, i) => show(ex.tests.steps[r][i], p.w)),
              ...ex.outputs.map((q, i) => ({ want: show(ex.tests.expect[r][i], q.w), got: show(got[r][i], q.w), bad: got[r][i] !== ex.tests.expect[r][i] && r >= ex.tests.checkFrom }))],
      reset: r < ex.tests.checkFrom
    });
    return { ok: false, title: "Not yet", text: text + why, rows, columns, step: s };
  }
  const n = ex.tests.steps.length - ex.tests.checkFrom;
  return { ok: true, title: "Correct!", text: ex.clock ? `Your circuit gave the right outputs in all ${n} clock cycles it was tried for.` : n === 2 ** data.reduce((t, p) => t + p.w, 0) ? `Your circuit is right for every one of the ${n} possible inputs.` : `Your circuit gave the right outputs for all ${n} inputs it was tried with.` };
}

/* a small seeded random generator, so the same exercise always gets the same tests */
function rng(seed) {
  let a = seed >>> 0;
  return () => { a = (a + 0x6D2B79F5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

return { ports, makeTests, run, grade, rng, show };
})();
if (typeof module !== "undefined") module.exports = WireExam;
