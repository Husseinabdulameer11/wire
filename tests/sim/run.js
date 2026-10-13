/* run.js - run a circuit's clock in Node and print outputs.
 *   node tests/sim/run.js build/wire file.wire [cycles] [output names...]
 * Stops early when an output called `halted` turns 1. */
"use strict";
const { execFileSync } = require("child_process");
const WireSim = require("../../src/wire_sim.js");
const [wire, file, cyc = "1000", ...names] = process.argv.slice(2);
const D = JSON.parse(execFileSync(wire, ["--json", file], { stdio: ["ignore", "pipe", "ignore"] }).toString());
const sim = WireSim.createSync(D);
const mask = w => w >= 32 ? 0xFFFFFFFF : ((1 << w) >>> 0) - 1;
D.nets.forEach((n, i) => { sim.setV(i, 0); sim.setK(i, mask(n.w)); });
const outs = D.nodes.filter(n => ["output", "led", "hex"].includes(n.kind) && (!names.length || names.includes(n.name)));
const halted = D.nodes.find(n => n.name === "halted");
sim.simulate();
let c = 0;
const t0 = process.hrtime.bigint();
for (; c < +cyc; c++) {
  sim.toggle(2);
  if (halted && sim.v(halted.in[0]) === 1 && sim.k(halted.in[0]) === 1) { c++; break; }
}
const ms = Number(process.hrtime.bigint() - t0) / 1e6;
console.log(`${c} cycles in ${ms.toFixed(1)} ms` + (sim.conflict() >= 0 ? `  BUS CONFLICT at ${D.nodes[sim.conflict()].name}` : ""));
outs.forEach(n => console.log(`  ${n.name} = ${sim.k(n.in[0]) === mask(D.nets[n.in[0]].w) ? sim.v(n.in[0]) : "x"}`));
