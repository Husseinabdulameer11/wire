/* screen.js - run a circuit until its `halted` output is 1 (or for so many
 * cycles) and print its screen, one letter per pixel:
 *   node tests/sim/screen.js build/wire file.wire [cycles]
 * 1-bit pixels are . and #; 3-bit ones . B G C R M Y W (black, blue, green,
 * cyan, red, magenta, yellow, white). make test compares the picture of
 * examples/screen/computer with tests/sim/computer.screen. */
"use strict";
const { execFileSync } = require("child_process");
const WireSim = require("../../src/wire_sim.js");
const [wire, file, max = "100000"] = process.argv.slice(2);
const D = JSON.parse(execFileSync(wire, ["--json", file], { stdio: ["ignore", "pipe", "ignore"] }).toString());
const sim = WireSim.createSync(D), mask = w => w >= 32 ? 0xFFFFFFFF : 2 ** w - 1;
D.nets.forEach((n, i) => { sim.setV(i, 0); sim.setK(i, mask(n.w)); });
sim.simulate();
const halted = D.nodes.find(n => n.name === "halted" && n.inst === 0);
let c = 0;
for (; c < +max; c++) {
  sim.toggle(2);
  if (halted && sim.v(halted.in[0]) === 1 && sim.k(halted.in[0]) === 1) { c++; break; }
}
console.log(`${c} cycles${halted && sim.v(halted.in[0]) === 1 ? ", halted" : ""}`);
D.nodes.forEach((n, j) => {
  if (!n.screen) return;
  const px = sim.words(j), w = n.screen, h = px.length / w;
  const ch = n.bits === 1 ? ".#" : n.bits === 3 ? ".BGCRMYW" : null;
  console.log(`${n.name || "screen"} ${w} x ${h}:`);
  for (let y = 0; y < h; y++)
    console.log(Array.from({ length: w }, (_, x) => ch ? ch[px[y * w + x]] : px[y * w + x].toString(16) + " ").join(""));
});
