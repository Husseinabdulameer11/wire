/* simtest.js - checks the WebAssembly simulator against the reference one.
 *
 *   node tests/sim/simtest.js build/wire [file.wire ...]
 *
 * With no files it runs every example (all of examples/ but lib folders) and
 * tests/sim/*.wire. Each circuit
 * goes through `wire --json`, then both simulators get the same random
 * clicks, clock ticks and resets, and after every action every net (value
 * and known mask), every flip-flop and input state, and the "unstable" flag
 * must agree. The first difference is printed with the recent actions and
 * where the WebAssembly code keeps that net. */
"use strict";
const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");
const WireSim = require("../../src/wire_sim.js");
const refSim = require("./ref_sim.js");

const wire = process.argv[2];
if (!wire) { console.error("usage: node simtest.js path/to/wire [file.wire ...]"); process.exit(2); }
let files = process.argv.slice(3);
if (!files.length) {
  const dir = d => fs.existsSync(d) ? fs.readdirSync(d, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name)).flatMap(e =>
    e.isDirectory() ? (e.name === "lib" || d === "tests/sim" ? [] : dir(path.join(d, e.name))) : e.name.endsWith(".wire") ? [path.join(d, e.name)] : []) : [];
  files = [...dir("examples"), ...dir("tests/sim")];
}
const STEPS = +(process.env.SIMTEST_STEPS || 400);
const mask = w => w >= 32 ? 0xFFFFFFFF : ((1 << w) >>> 0) - 1;

/* small seeded random numbers, so a failure repeats */
function rng(seed) {
  let s = seed >>> 0 || 1;
  return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s; };
}

let failed = 0;
for (const file of files) {
  const D = JSON.parse(execFileSync(wire, ["--json", file], { stdio: ["ignore", "pipe", "ignore"] }).toString());
  const ref = refSim(D);
  if (process.env.SIMTEST_VERBOSE) console.log(file, D.nodes.map(n => n.kind).join(" "));
  const sim = WireSim.createSync(D);
  D.nets.forEach((n, i) => { sim.setV(i, 0); sim.setK(i, mask(n.w)); });

  const SRC = ["input", "button", "clock"], SEQ = ["dff", "tff", "jkff", "counter"];
  const ins = [], clocks = [];
  D.nodes.forEach((n, j) => {
    if (n.kind === "input" || n.kind === "button" || n.kind === "keypad") ins.push(j);
    if (n.kind === "clock") clocks.push(j);
  });
  const both = (j, x) => { ref.nodes[j].state = x; sim.setState(j, x); };
  const both_sim = () => { ref.simulate(); sim.simulate(); };

  const rand = rng(0x9e3779b9 ^ file.length * 2654435761);
  const log = [];
  const t0 = process.hrtime.bigint();
  let bad = null;
  for (let step = 0; step < STEPS && !bad; step++) {
    const r = rand() % 100;
    if (step === 0) { log.push("start"); both_sim(); }
    else if (r < 35 && ins.length) {
      const j = ins[rand() % ins.length], w = D.nets[D.nodes[j].out[0]].w;
      const x = (rand() & mask(w)) >>> 0;
      log.push(`${D.nodes[j].name} = ${x}`);
      both(j, x); both_sim();
    } else if (r < 70 && clocks.length) {
      log.push("tick");
      clocks.forEach(j => both(j, ref.nodes[j].state ^ 1)); both_sim();
    } else if (r < 85 && clocks.length) {
      log.push("step");
      clocks.forEach(j => both(j, 1)); both_sim();
      clocks.forEach(j => both(j, 0)); both_sim();
    } else if (r < 92 && ins.length) {
      /* what the truth table does: change inputs and only settle */
      const j = ins[rand() % ins.length], w = D.nets[D.nodes[j].out[0]].w;
      const x = (rand() & mask(w)) >>> 0;
      log.push(`${D.nodes[j].name} = ${x} (settle only)`);
      both(j, x); ref.settle(); sim.settle();
    } else if (r < 95) {
      log.push("reset");
      ref.nodes.forEach((n, j) => {
        if (SRC.includes(n.kind) || SEQ.includes(n.kind)) both(j, 0);
        n.lastClk = 0; sim.setLastClk(j, 0);
      });
      ref.nets.forEach((n, i) => { n.v = 0; n.k = mask(n.w); sim.setV(i, 0); sim.setK(i, mask(n.w)); });
      ref.resetMem(); sim.resetMem();
      both_sim();
    } else {
      log.push("simulate");
      both_sim();
    }

    for (let i = 0; i < D.nets.length && !bad; i++) {
      const a = ref.nets[i], vv = sim.v(i), kk = sim.k(i);
      if (a.v >>> 0 !== vv || a.k >>> 0 !== kk)
        bad = `net ${i} "${a.name || ""}" [${a.w}]: reference v=${a.v >>> 0} k=${a.k >>> 0}, wasm v=${vv} k=${kk}`;
    }
    for (let j = 0; j < D.nodes.length && !bad; j++) {
      const n = ref.nodes[j];
      if (!SRC.includes(n.kind) && !SEQ.includes(n.kind)) continue;
      if (n.state >>> 0 !== sim.state(j)) bad = `node ${j} ${n.kind} "${n.name || ""}": reference state ${n.state >>> 0}, wasm ${sim.state(j)}`;
      else if ((n.lastClk | 0) !== sim.lastClk(j)) bad = `node ${j} ${n.kind}: reference lastClk ${n.lastClk}, wasm ${sim.lastClk(j)}`;
    }
    if (!bad && ref.unstable() !== sim.unstable())
      bad = `unstable: reference ${ref.unstable()}, wasm ${sim.unstable()}`;
    if (!bad && ref.conflict() !== sim.conflict())
      bad = `bus conflict: reference ${ref.conflict()}, wasm ${sim.conflict()}`;
    for (let j = 0; j < D.nodes.length && !bad; j++) {
      const n = ref.nodes[j];
      if (n.kind !== "ram") continue;
      const words = sim.words(j);
      for (let a = 0; a < n.mem.length && !bad; a++)
        if (n.mem[a] !== words[a]) bad = `ram ${j} "${n.name || ""}" word ${a}: reference ${n.mem[a]}, wasm ${words[a]}`;
    }
    if (bad) bad = `after step ${step}: ${bad}\n  last actions: ${log.slice(-8).join("; ")}`;
  }
  const ms = Number(process.hrtime.bigint() - t0) / 1e6;
  if (bad) {
    failed++;
    console.log(`FAIL ${file}\n  ${bad}`);
    if (process.env.SIMTEST_DUMP) console.log(sim.dump());
  } else {
    console.log(`ok   ${file}  (${D.nodes.length} parts, ${D.nets.length} nets, ${sim.compiled.loops.length} loops, ${sim.compiled.bytes.length} bytes of wasm, ${STEPS} steps in ${ms.toFixed(0)} ms)`);
  }
}
if (failed) { console.log(`${failed} of ${files.length} circuits differ`); process.exit(1); }
console.log(`all ${files.length} circuits agree`);
