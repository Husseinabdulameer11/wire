#!/usr/bin/env node
/* exam.js - makes the exercise page: exercises/ in levels, each a question,
 * the entity to fill in, and the tests a circuit must pass. The page compiles
 * what is written with Wire in WebAssembly (as the playground does) and
 * checks it (src/wire_exam.js). The solutions are not in the page: only the
 * inputs each test sets and the outputs the solution gives.
 *
 *   node tools/exam.js build/wire build/web build/site     writes exam.html (make site)
 *   node tools/exam.js build/wire --check                 only checks the exercises (make test)
 *
 * An exercise (exercises/<level>/<nn>_<name>.ex) is a header and sections:
 *   title: ...                        what the list shows
 *   table: yes                        show the solution's truth table in the question
 *   cycles: 300                       a circuit with a clock: how many cycles to try
 *   random: rst=0.02 en=0.8           how often a 1-bit input is 1 (else half the time)
 *   ---- question                     text: paragraphs, - lists, `code`, **bold**, ``` blocks, | tables
 *   ---- hint                         shown when asked for
 *   ---- starter                      the VHDL the page starts with (the entity is the exercise's)
 *   ---- solution                     a VHDL answer: the tests are what it does
 *   ---- wrong                        (optional) an answer that must fail, to check the tests
 * exercises/levels.txt names the levels, in order. */
"use strict";
const fs = require("fs");
const path = require("path");
const os = require("os");
const { execFileSync } = require("child_process");
const root = path.join(__dirname, "..");
const WireSim = require(path.join(root, "src", "wire_sim.js"));
const WireExam = require(path.join(root, "src", "wire_exam.js"));

const args = process.argv.slice(2);
const wire = args[0] && path.resolve(args[0]);
const check = args.includes("--check");
const [web, out] = check ? [] : [args[1], args[2]];
if (!wire || (!check && (!web || !out))) { console.error("usage: node tools/exam.js build/wire build/web build/site  |  node tools/exam.js build/wire --check"); process.exit(2); }

/* ---- reading the exercises */
function parse(file) {
  const text = fs.readFileSync(file, "utf8").replace(/\r\n/g, "\n");
  const parts = text.split(/^---- (\w+)\n/m);
  const ex = { header: {}, sections: {} };
  parts[0].split("\n").forEach(l => { const m = /^(\w+):\s*(.*)$/.exec(l.trim()); if (m) ex.header[m[1]] = m[2]; });
  for (let i = 1; i < parts.length; i += 2) ex.sections[parts[i]] = parts[i + 1].replace(/\s+$/, "") + "\n";
  for (const s of ["question", "starter", "solution"]) if (!ex.sections[s]) throw new Error(`${file}: there is no ---- ${s}`);
  if (!ex.header.title) throw new Error(`${file}: there is no title:`);
  return ex;
}
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "exam-"));
function netlist(vhdl, name) {
  const f = path.join(tmp, name + ".vhd");
  fs.writeFileSync(f, vhdl);
  try { return JSON.parse(execFileSync(wire, ["--json", f], { stdio: ["ignore", "pipe", "pipe"], maxBuffer: 1 << 28 }).toString()); }
  catch (e) { throw new Error(`${name}: wire could not read it:\n${(e.stderr || "").toString().split(tmp + path.sep).join("")}`); }
}
const hash = s => { let h = 2166136261; for (const c of s) h = Math.imul(h ^ c.charCodeAt(0), 16777619); return h >>> 0; };

const levels = fs.readFileSync(path.join(root, "exercises", "levels.txt"), "utf8").split(/\r?\n/)
  .map(l => l.replace(/#.*$/, "").trim()).filter(Boolean).map(l => { const [dir, name, about] = l.split("|").map(s => s.trim()); return { dir, name, about, exercises: [] }; });

let failures = 0;
const fail = msg => { failures++; console.error("FAIL " + msg); };
for (const lv of levels) {
  const dir = path.join(root, "exercises", lv.dir);
  for (const f of fs.readdirSync(dir).filter(n => n.endsWith(".ex")).sort()) {
    const id = lv.dir + "/" + f.replace(/\.ex$/, "");
    let ex;
    try { ex = parse(path.join(dir, f)); } catch (e) { fail(e.message); continue; }
    let D;
    try { D = netlist(ex.sections.solution, id.replace("/", "_")); } catch (e) { fail(e.message); continue; }
    const P = WireExam.ports(D);
    const clocks = P.ins.filter(p => p.clock);
    if (clocks.length > 1) { fail(`${id}: more than one clock`); continue; }
    const random = {};
    (ex.header.random || "").split(/\s+/).filter(Boolean).forEach(kv => { const [k, v] = kv.split("="); random[k.toLowerCase()] = +v; });
    const e = {
      id, level: lv.dir, title: ex.header.title, question: ex.sections.question, hint: ex.sections.hint || "", starter: ex.sections.starter,
      inputs: P.ins.map(({ name, w }) => ({ name, w })), outputs: P.outs.map(({ name, w }) => ({ name, w })),
      clock: clocks.length ? clocks[0].name : null, cycles: +ex.header.cycles || 0, random
    };
    e.tests = WireExam.makeTests(e, WireExam.rng(hash(id)));
    const ref = WireExam.run(D, WireSim.createSync(D), e, e.tests.steps);
    if (ref.unstable !== undefined) { fail(`${id}: the solution never settles`); continue; }
    e.tests.expect = ref.got;
    if (ref.got.some((r, s) => s >= e.tests.checkFrom && r.some(v => v === null))) fail(`${id}: the solution has outputs with no value (X) in the tests`);
    /* the solution passes, and a wrong answer does not */
    const g = WireExam.grade(D, WireSim.createSync(D), e);
    if (!g.ok) fail(`${id}: the solution does not pass its own tests: ${g.text}`);
    if (ex.sections.wrong) {
      try {
        const W = netlist(ex.sections.wrong, id.replace("/", "_") + "_wrong");
        const gw = WireExam.grade(W, WireSim.createSync(W), e);
        if (gw.ok) fail(`${id}: the wrong answer passes, so the tests miss it`);
      } catch (x) { fail(x.message); }
    }
    /* the starter: the same ports as the solution */
    const ents = /entity\s+(\w+)\s+is/i.exec(ex.sections.starter), ents2 = /entity\s+(\w+)\s+is/i.exec(ex.sections.solution);
    if (!ents || !ents2 || ents[1].toLowerCase() !== ents2[1].toLowerCase()) fail(`${id}: the starter's entity is not the solution's`);
    if (ex.header.table === "yes" && !e.clock) {
      const data = e.inputs;
      e.table = { columns: [...data.map(p => p.name), ...e.outputs.map(o => o.name)],
                  rows: e.tests.steps.map((s, i) => [...s.map((v, k) => WireExam.show(v, data[k].w)), ...e.tests.expect[i].map((v, k) => WireExam.show(v, e.outputs[k].w))]) };
      if (e.table.rows.length > 16) delete e.table;
    }
    lv.exercises.push(e);
  }
}
fs.rmSync(tmp, { recursive: true, force: true });
const n = levels.reduce((t, l) => t + l.exercises.length, 0);
if (failures) { console.error(`exam: ${failures} problem${failures === 1 ? "" : "s"} in the exercises`); process.exit(1); }
if (check) { console.log(`exam: all ${n} exercises pass their own tests, and every wrong answer fails`); process.exit(0); }

/* ---- the page: exam.html, next to the playground's wire.js and wire.wasm */
fs.mkdirSync(out, { recursive: true });
let page = fs.readFileSync(path.join(root, "src", "exam.html"), "utf8");
const json = JSON.stringify(levels).replace(/</g, "\\u003c");
const js = f => fs.readFileSync(path.join(root, "src", f), "utf8").replace(/<\/script/gi, "<\\/script");
page = page.replace("/*EXAM_DATA*/null", () => json)
           .replace("/*WIRE_SIM*/", () => js("wire_sim.js"))
           .replace("/*WIRE_EXAM*/", () => js("wire_exam.js"));
fs.writeFileSync(path.join(out, "exam.html"), page);
for (const f of ["wire.js", "wire.wasm"]) if (!fs.existsSync(path.join(out, f))) fs.copyFileSync(path.join(web, f), path.join(out, f));
console.log(`wrote exam.html to ${out} (${n} exercises in ${levels.length} levels, ${Math.round(page.length / 1024)} kB)`);
