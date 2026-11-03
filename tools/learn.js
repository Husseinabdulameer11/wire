#!/usr/bin/env node
/* learn.js - makes the learning page: lessons/ in order, each a short text,
 * a task, a drawing to start from, and how Wire knows it is done. The page
 * is the playground's drawing with a lesson beside it (learn.html is
 * playground.html with the lessons put in, so make it after the playground).
 *
 *   node tools/learn.js build/wire build/site        writes learn.html (make site)
 *   node tools/learn.js build/wire --check           only checks the lessons (make test)
 *
 * A lesson (lessons/<nn>_<name>.lesson) is a header and sections:
 *   title: ...
 *   check: on y           done when the light y is on (the learner clicks the inputs)
 *   check: behaves        done when the circuit does what ---- reference does, for every input
 *   parts: and or not chip:my_xor      what the parts list offers (nothing: no list)
 *   keep: my_xor          the learner's circuit becomes this chip, for later lessons
 *   needs: my_xor         the chips (kept by earlier lessons) this one offers
 *   ---- intro, ---- task, ---- hint1, ---- hint2, ---- solution, ---- done    text
 *   ---- start            the drawing it starts with
 *   ---- answer           a drawing that is right (Show me the solution loads it)
 *   ---- reference        Wire that does what the circuit must do (check: behaves)
 * A drawing is a part a line, "kind name x y" ("chip name comp x y"), and
 * wires, "from[.pin] -> to[.pin]" (an output is "out", a light's input "in",
 * a gate's inputs 0, 1 ...). The checks: the answer passes, the start does not. */
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
const out = check ? null : args[1];
if (!wire || (!check && !out)) { console.error("usage: node tools/learn.js build/wire build/site  |  node tools/learn.js build/wire --check"); process.exit(2); }

const GATES = new Set(["and", "or", "nand", "nor", "xor", "xnor"]);
const KINDS = new Set(["input", "led", "output", "not", "buf", "chip", ...GATES]);

function parse(file) {
  const text = fs.readFileSync(file, "utf8").replace(/\r\n/g, "\n");
  const parts = text.split(/^---- (\w+)\n/m), l = { header: {}, s: {} };
  parts[0].split("\n").forEach(x => { const m = /^(\w+):\s*(.*)$/.exec(x.trim()); if (m) l.header[m[1]] = m[2].trim(); });
  for (let i = 1; i < parts.length; i += 2) l.s[parts[i]] = parts[i + 1].replace(/\s+$/, "");
  return l;
}
/* a drawing, as the playground keeps it: { next, parts, links } */
function drawing(text, chips, where) {
  const m = { title: "Lesson", next: 1, parts: [], links: [] }, byName = new Map();
  const pinsOf = c => { const d = chips[c]; if (!d) throw new Error(`${where}: there is no chip ${c} (keep: it in an earlier lesson)`);
    return { ins: d.parts.filter(p => p.kind === "input").map(p => [p.name, 1]), outs: d.parts.filter(p => p.kind === "output").map(p => [p.name, 1]) }; };
  for (const raw of text.split("\n")) {
    const line = raw.replace(/#.*$/, "").trim();
    if (!line) continue;
    let w = /^(\w+)(?:\.(\w+))?\s*->\s*(\w+)(?:\.(\w+))?$/.exec(line);
    if (w) {
      const f = byName.get(w[1]), t = byName.get(w[3]);
      if (!f || !t) throw new Error(`${where}: "${line}": there is no part ${!f ? w[1] : w[3]}`);
      m.links.push({ from: { id: f.id, pin: w[2] || (f.kind === "chip" ? f.outs[0][0] : "out") }, to: { id: t.id, pin: w[4] || "in" } });
      continue;
    }
    w = /^(\w+)\s+(\w+)(?:\s+(\w+))?\s+(-?\d+)\s+(-?\d+)$/.exec(line);
    if (!w || !KINDS.has(w[1])) throw new Error(`${where}: cannot read "${line}"`);
    const p = { id: m.next++, kind: w[1], name: w[2], x: +w[4], y: +w[5] };
    if (p.kind === "input" || p.kind === "output") p.bits = 1;
    if (GATES.has(p.kind)) p.n = 2;
    if (p.kind === "chip") { p.comp = w[3]; Object.assign(p, pinsOf(w[3])); }
    m.parts.push(p); byName.set(p.name, p);
  }
  return m;
}
/* a lesson's drawing kept as a chip: its lights become the chip's outputs */
const asChip = d => ({ next: d.next, parts: d.parts.map(p => p.kind === "led" ? { ...p, kind: "output", bits: 1 } : p), links: d.links });

/* the drawing as Wire, as the playground writes it (for the checks here) */
function wireText(m, chips) {
  const lines = (sh) => {
    const byId = new Map(sh.parts.map(p => [p.id, p])), into = new Map(sh.links.map(l => [l.to.id + ":" + l.to.pin, l]));
    const from = (p, pin) => {
      const l = into.get(p.id + ":" + pin), f = l && byId.get(l.from.id);
      if (!f) return "0";
      return f.kind === "chip" ? `${f.name}.${l.from.pin}` : f.name;
    };
    const body = [];
    for (const p of sh.parts) {
      if (GATES.has(p.kind)) body.push(`${p.name} = ${p.kind}(${from(p, "0")}, ${from(p, "1")})`);
      else if (p.kind === "not" || p.kind === "buf") body.push(`${p.name} = ${p.kind}(${from(p, "in")})`);
      else if (p.kind === "chip") body.push(`${p.name} = ${p.comp}(${p.ins.map(([n]) => `${n} = ${from(p, n)}`).join(", ")})`);
      else if (p.kind === "led" || p.kind === "output") body.push(`${p.name} = ${from(p, "in")}`);
    }
    return body;
  };
  const used = [...new Set(m.parts.filter(p => p.kind === "chip").map(p => p.comp))];
  const t = [`circuit "lesson"`];
  for (const c of used) {
    const d = chips[c], ins = d.parts.filter(p => p.kind === "input").map(p => p.name), outs = d.parts.filter(p => p.kind === "output").map(p => p.name);
    t.push(`component ${c}(${ins.join(", ")}) -> (${outs.join(", ")}) {`, ...lines(d).map(l => "    " + l), "}");
  }
  const decl = k => { const ps = m.parts.filter(p => p.kind === k); if (ps.length) t.push(`${k} ${ps.map(p => p.name).join(", ")}`); };
  decl("input"); decl("led"); decl("output");
  t.push(...lines(m));
  return t.join("\n") + "\n";
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "learn-"));
const netlist = (text, name) => {
  const f = path.join(tmp, name + ".wire");
  fs.writeFileSync(f, text);
  try { return JSON.parse(execFileSync(wire, ["--json", f], { stdio: ["ignore", "pipe", "pipe"], maxBuffer: 1 << 26 }).toString()); }
  catch (e) { throw new Error(`${name}: wire could not read it:\n${(e.stderr || "").toString()}\n${text}`); }
};

let failures = 0;
const fail = m => { failures++; console.error("FAIL " + m); };
const chips = {}, lessons = [];
const dir = path.join(root, "lessons");
for (const f of fs.readdirSync(dir).filter(n => n.endsWith(".lesson")).sort()) {
  const id = f.replace(/\.lesson$/, "");
  try {
    const l = parse(path.join(dir, f));
    for (const s of ["intro", "task", "start", "answer", "solution", "done"]) if (!l.s[s]) throw new Error(`${id}: there is no ---- ${s}`);
    const needs = (l.header.needs || "").split(/\s+/).filter(Boolean);
    const lessonChips = Object.fromEntries(needs.map(c => { if (!chips[c]) throw new Error(`${id}: needs ${c}, which no earlier lesson keeps`); return [c, chips[c]]; }));
    const start = drawing(l.s.start, lessonChips, id + " start"), answer = drawing(l.s.answer, lessonChips, id + " answer");
    const lesson = {
      id, title: l.header.title, check: l.header.check || "", parts: (l.header.parts || "").split(/\s+/).filter(Boolean),
      keep: l.header.keep || null, needs, intro: l.s.intro, task: l.s.task, hints: [l.s.hint1, l.s.hint2, l.s.hint3].filter(Boolean),
      solution: l.s.solution, done: l.s.done, start, answer
    };
    const allowed = new Set(["input", "led", ...lesson.parts.map(k => k.startsWith("chip:") ? "chip" : k)]);
    for (const p of answer.parts) if (!allowed.has(p.kind)) fail(`${id}: the answer uses a ${p.kind}, which parts: does not offer`);
    const on = /^on\s+(\w+)$/.exec(lesson.check);
    if (on) {
      lesson.light = on[1];
      if (!answer.parts.some(p => p.kind === "led" && p.name === on[1])) fail(`${id}: there is no light ${on[1]}`);
    } else if (lesson.check === "behaves") {
      if (!l.s.reference) throw new Error(`${id}: check: behaves needs a ---- reference`);
      const D = netlist(`circuit "reference"\n${l.s.reference}\n`, id + "_ref"), P = WireExam.ports(D);
      lesson.inputs = P.ins.map(({ name, w }) => ({ name, w }));
      lesson.outputs = P.outs.map(({ name, w }) => ({ name, w }));
      lesson.clock = null;
      lesson.tests = WireExam.makeTests(lesson, WireExam.rng(1));
      lesson.tests.expect = WireExam.run(D, WireSim.createSync(D), lesson, lesson.tests.steps).got;
      const grade = (d, what) => { const N = netlist(wireText(d, lessonChips), id + "_" + what); return WireExam.grade(N, WireSim.createSync(N), lesson); };
      const ga = grade(answer, "answer");
      if (!ga.ok) fail(`${id}: the answer does not pass: ${ga.text}`);
      if (grade(start, "start").ok) fail(`${id}: the drawing it starts with is already right`);
    } else throw new Error(`${id}: check: is "on <light>" or "behaves"`);
    if (lesson.keep) chips[lesson.keep] = asChip(answer);   /* for the checks; the page keeps the learner's own */
    lesson.chipAnswers = Object.fromEntries(needs.map(c => [c, chips[c]]));
    lessons.push(lesson);
  } catch (e) { fail(e.message); }
}
fs.rmSync(tmp, { recursive: true, force: true });
if (failures) { console.error(`learn: ${failures} problem${failures === 1 ? "" : "s"} in the lessons`); process.exit(1); }
if (check) { console.log(`learn: all ${lessons.length} lessons check out (every answer passes, no lesson starts done)`); process.exit(0); }

/* ---- learn.html: the playground, with the lessons and the checker put in */
const pg = path.join(out, "playground.html");
if (!fs.existsSync(pg)) { console.error(`learn: no ${pg}; make the playground first (tools/playground.js)`); process.exit(1); }
const js = f => fs.readFileSync(path.join(root, "src", f), "utf8").replace(/<\/script/gi, "<\\/script");
let page = fs.readFileSync(pg, "utf8");
const data = JSON.stringify({ lessons }).replace(/</g, "\\u003c");
page = page.replace(/<title>[^<]*<\/title>/, "<title>Learn digital circuits - Wire</title>")
           .replace(/<meta name="description" content="[^"]*">/, '<meta name="description" content="Learn how digital circuits work, from a switch and a light to a circuit that adds, by building them in your browser.">')
           .replace('<script src="wire.js"></script>', () => `<script>window.WIRE_LEARN = ${data};</script>\n<script src="wire.js"></script>`);
const end = page.lastIndexOf("</body>");
page = page.slice(0, end) + `<script>\n${js("wire_sim.js")}\n</script>\n<script>\n${js("wire_exam.js")}\n</script>\n<script>\n${js("wire_learn.js")}\n</script>\n` + page.slice(end);
fs.writeFileSync(path.join(out, "learn.html"), page);
console.log(`wrote learn.html to ${out} (${lessons.length} lessons)`);
