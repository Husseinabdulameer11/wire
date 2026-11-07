/* wire_learn.js - the lessons of learn.html (tools/learn.js puts them in).
 *
 * learn.html is the playground's drawing with a lesson beside it. This
 * script runs after the playground's own and works through it: it sets the
 * drawing each lesson starts with, offers only the parts the lesson uses,
 * and knows when the lesson is done, either a light is on (the learner
 * clicked the switches) or the circuit does what the lesson asks for every
 * input (src/wire_exam.js, as the exercises check). A failed Check gives a
 * hint, then a bigger one; after three, the solution. A lesson can keep the
 * learner's circuit as a chip for the lessons after it.
 *
 * There are two courses (tracks), each easy to hard on its own: Drawing
 * (drag and drop, no code) and VHDL (written only). The first visit asks
 * which one; the bar above the lesson switches between them later. */
(function () {
"use strict";
const DATA = window.WIRE_LEARN;
if (!DATA) return;
const LESSONS = DATA.lessons, TRACKS = DATA.tracks || [{ dir: "circuits", name: "Circuits" }];
const KEY = "wire-learn-1";
let st = { models: {}, code: {}, chips: {}, done: {}, fails: {}, hints: {}, last: null, track: null };
try { const s = JSON.parse(localStorage.getItem(KEY) || "null"); if (s && typeof s === "object") st = Object.assign(st, s); } catch (e) {}
const save = () => { try { localStorage.setItem(KEY, JSON.stringify(st)); } catch (e) {} };
/* the lessons were once one course, mixed: what was done then counts in the two courses */
const OLD = { "01_switch": "draw/switch", "02_not": "draw/not", "03_and": "draw/and", "04_or": "draw/or", "05_wire": "vhdl/first",
  "06_gates": "vhdl/and", "07_nand": "draw/nand", "08_nor": "vhdl/nor", "09_xor": "draw/xor", "10_signals": "vhdl/signals",
  "11_majority": "draw/majority", "12_parity": "vhdl/parity", "13_half_adder": "draw/half_adder", "14_two_outputs": "vhdl/half_adder",
  "15_full_adder": "draw/full_adder", "16_full_adder_vhdl": "vhdl/full_adder", "17_mux_gates": "draw/mux", "18_choose": "vhdl/choose",
  "19_buses": "vhdl/buses", "20_slices": "vhdl/slices", "21_adder": "vhdl/adder", "22_decoder": "vhdl/decoder",
  "23_dff_draw": "draw/flipflop", "24_flipflop": "vhdl/flipflop", "25_counter": "vhdl/counter" };
/* ...and for a while were numbered, draw/08_xor: now draw/xor */
const renamed = k => OLD[k] || (/^(\w+)\/\d+_(\w+)$/.test(k) ? k.replace(/\/\d+_/, "/") : null);
(function migrate() {
  let moved = false;
  for (const k of ["models", "code", "done", "fails", "hints", "solutionShown"]) {
    const o = st[k];
    if (!o) continue;
    for (const old of Object.keys(o)) { const nw = renamed(old); if (nw) { if (!(nw in o)) o[nw] = o[old]; delete o[old]; moved = true; } }
  }
  if (renamed(st.last)) { st.last = renamed(st.last); st.track = st.last.split("/")[0]; moved = true; }   /* back where they were, no picker */
  if (moved) save();
})();
const trackOf = d => TRACKS.find(t => t.dir === d) || TRACKS[0];
const inTrackOf = d => LESSONS.map((l, i) => [l, i]).filter(([l]) => l.track === d);
const firstOpen = d => { const ls = inTrackOf(d); return (ls.find(([l]) => !st.done[l.id]) || ls[0] || [null, 0])[1]; };
const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const copy = x => JSON.parse(JSON.stringify(x));

/* ---- the text: paragraphs, - lists, | tables, `code`, **bold** */
const inline = s => esc(s).replace(/`([^`]+)`/g, "<code>$1</code>").replace(/\*\*([^*]+)\*\*/g, "<b>$1</b>");
function md(text) {
  const lines = (text || "").split("\n"), out = [];
  for (let i = 0; i < lines.length; ) {
    const l = lines[i];
    if (!l.trim()) { i++; continue; }
    if (/^```/.test(l)) { const c = []; for (i++; i < lines.length && !/^```/.test(lines[i]); i++) c.push(lines[i]); i++; out.push(`<pre>${esc(c.join("\n"))}</pre>`); continue; }
    if (/^- /.test(l)) { const it = []; for (; i < lines.length && /^- /.test(lines[i]); i++) it.push(`<li>${inline(lines[i].slice(2))}</li>`); out.push(`<ul>${it.join("")}</ul>`); continue; }
    if (/^\|/.test(l)) {
      const rows = [];
      for (; i < lines.length && /^\|/.test(lines[i]); i++) if (!/^\|[\s|:-]+\|$/.test(lines[i])) rows.push(lines[i].replace(/^\||\|$/g, "").split("|").map(c => c.trim()));
      out.push(`<table class="ltable"><tr>${rows[0].map(c => `<th>${inline(c)}</th>`).join("")}</tr>${rows.slice(1).map(r => `<tr>${r.map(c => `<td>${inline(c)}</td>`).join("")}</tr>`).join("")}</table>`);
      continue;
    }
    const p = [];
    for (; i < lines.length && lines[i].trim() && !/^(- |\||```)/.test(lines[i]); i++) p.push(lines[i]);
    out.push(`<p>${inline(p.join(" "))}</p>`);
  }
  return out.join("");
}

/* ---- the playground, made into a lesson: less of it, and the lesson on top */
document.body.classList.add("learn");
document.title = "Learn digital circuits - Wire";
const hide = el => { if (el) el.hidden = true; };
const h1 = document.querySelector("header h1 span");
if (h1) h1.textContent = "learn";
hide(document.querySelector("header .group.mode"));
hide($("langbox"));
document.querySelectorAll("header .file").forEach(hide);
hide(document.querySelector("header .actions"));
hide($("how"));
const h2 = text => [...document.querySelectorAll("#draw h2")].find(h => h.textContent.trim() === text);
[h2("Circuit and chips"), $("sheets"), $("crumbs"), h2("Selected part"), $("props"), h2("The Wire it makes"), $("gen"), $("tocode") && $("tocode").closest(".tools")].forEach(hide);
const css = document.createElement("style");
css.textContent = `
#lesson { margin: -2px 0 14px; }
#lesson .lnav { display: flex; gap: 4px 12px; flex-wrap: wrap; margin-bottom: 10px; }
#lesson .lnav .lgroup { display: inline-flex; gap: 3px; flex-wrap: wrap; }
#lesson .lnav button { min-height: 26px; min-width: 26px; padding: 0 5px; border: 1px solid var(--line); border-radius: 15px; background: var(--paper); font-size: 13px; color: var(--ink); cursor: pointer; }
#lesson .lnav button.done { background: var(--on); border-color: var(--on); color: #fff; }
#lesson .lnav button[aria-current="true"] { box-shadow: 0 0 0 2px var(--hi); font-weight: 600; }
#lesson .lkick { font-size: 13px; color: var(--ink-soft); }
#lesson h2.ltitle { font-size: 22px; color: var(--ink); margin: 0 0 8px; font-weight: 600; }
#lesson p { margin: 0 0 9px; line-height: 1.5; }
#lesson ul { margin: 0 0 9px; padding-left: 20px; }
#lesson code { background: var(--hi-soft); padding: 0 4px; border-radius: 3px; font: 13.5px "IBM Plex Mono", ui-monospace, monospace; }
#lesson .ltable { border-collapse: collapse; margin: 2px 0 12px; font: 13.5px "IBM Plex Mono", ui-monospace, monospace; }
#lesson .ltable th, #lesson .ltable td { border: 1px solid var(--line); padding: 2px 12px; text-align: center; }
#lesson .ltable th { background: var(--paper); font-family: inherit; }
#lesson .ltask { border: 1px solid var(--hi); background: var(--hi-soft); border-radius: 8px; padding: 10px 12px; margin: 10px 0; }
#lesson .ltask .lt { font-weight: 600; font-size: 13px; text-transform: uppercase; letter-spacing: .04em; color: var(--ink-soft); margin-bottom: 2px; }
#lesson .ltask p:last-child { margin-bottom: 0; }
#lesson .lbar { display: flex; gap: 6px; flex-wrap: wrap; margin: 10px 0; }
#lesson .lbar button { min-height: 34px; padding: 0 12px; border: 1px solid var(--line); border-radius: 6px; background: var(--panel); color: var(--ink); font: inherit; cursor: pointer; }
#lesson .lbar button.primary { background: var(--ink); color: var(--paper); border-color: var(--ink); font-weight: 600; }
#lesson .lbar button:hover { background: var(--hi-soft); }
#lesson .lbar button.primary:hover { background: var(--ink); box-shadow: 0 0 0 3px var(--hi-soft); }
#lesson .lnote { border-left: 3px solid var(--line); padding: 6px 10px; margin: 8px 0; border-radius: 0 6px 6px 0; background: var(--paper); }
#lesson .lnote p:last-child { margin-bottom: 0; }
#lesson .lnote.hint { border-left-color: var(--hi); }
#lesson .lnote.bad { border-left-color: var(--bad); }
#lesson .lnote.sol { border-left-color: var(--part, var(--ink)); }
#lesson .lnote b.k { display: block; font-size: 13px; color: var(--ink-soft); }
#lesson .ldone { border: 1px solid var(--on); background: color-mix(in srgb, var(--on) 12%, transparent); border-radius: 8px; padding: 10px 12px; margin: 10px 0; }
#lesson .ldone h3 { margin: 0 0 4px; font-size: 17px; color: var(--on); }
#lesson .ldone p:last-child { margin-bottom: 0; }
#lesson pre { background: var(--paper); border: 1px solid var(--line); border-radius: 6px; padding: 8px 10px; margin: 0 0 10px; overflow-x: auto; font: 13px/1.45 "IBM Plex Mono", ui-monospace, monospace; }
#lesson .ltracks { display: flex; gap: 0; margin-bottom: 10px; border: 1px solid var(--line); border-radius: 8px; overflow: hidden; }
#lesson .ltracks button { flex: 1; min-height: 34px; border: 0; background: var(--paper); color: var(--ink-soft); font: inherit; font-weight: 600; cursor: pointer; }
#lesson .ltracks button + button { border-left: 1px solid var(--line); }
#lesson .ltracks button[aria-pressed="true"] { background: var(--panel); color: var(--ink); box-shadow: inset 0 -3px var(--hi); }
#lesson .ltracks small { font-weight: 400; color: var(--ink-soft); }
#lesson .lnote a.line { cursor: pointer; text-decoration: underline; }
body.learn-vhdl #side { grid-template-rows: auto 1fr auto; }
body.learn-vhdl #lesson { max-height: 48vh; overflow: auto; margin: 0; padding: 14px 16px 6px; border-bottom: 1px solid var(--line); }
#lpick { position: fixed; inset: 0; z-index: 50; overflow: auto; background: var(--paper); color: var(--ink); display: flex; align-items: flex-start; justify-content: center; padding: 48px 16px 32px; }
#lpick .lpin { width: 100%; max-width: 760px; }
#lpick h2 { font-size: 30px; margin: 0 0 8px; font-weight: 600; }
#lpick .lsub { color: var(--ink-soft); margin: 0 0 26px; font-size: 16px; line-height: 1.5; }
#lpick .lcards { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 16px; }
#lpick .lcard { text-align: left; white-space: normal; align-items: stretch; height: auto; min-width: 0; display: flex; flex-direction: column; gap: 8px; padding: 18px 18px 16px; border: 1px solid var(--line); border-radius: 12px; background: var(--panel); color: var(--ink); font: inherit; cursor: pointer; }
#lpick .lcard:hover, #lpick .lcard:focus-visible { border-color: var(--hi); box-shadow: 0 0 0 3px var(--hi-soft); outline: none; }
#lpick .lcard > * { display: block; white-space: normal; text-align: left; }
#lpick .lcard .lgo { display: flex; }
#lpick .lcard .ln { font-size: 21px; font-weight: 600; }
#lpick .lcard .la { color: var(--ink-soft); line-height: 1.45; }
#lpick .lcard ul { margin: 2px 0 4px; padding-left: 18px; line-height: 1.5; }
#lpick .lcard .lgo { margin-top: auto; display: flex; justify-content: space-between; align-items: center; gap: 8px; padding-top: 6px; font-size: 14px; color: var(--ink-soft); }
#lpick .lcard .lgo b { color: var(--paper); background: var(--ink); border-radius: 6px; padding: 7px 12px; font-size: 15px; }
#lpick .lfoot { margin-top: 22px; font-size: 14px; color: var(--ink-soft); }
#lesson .lswitch { background: none; border: 0; padding: 0; color: var(--ink-soft); font: inherit; font-size: 13px; text-decoration: underline; cursor: pointer; }
#lesson .ldone button { margin-top: 8px; min-height: 36px; padding: 0 14px; border-radius: 6px; border: 1px solid var(--ink); background: var(--ink); color: var(--paper); font: inherit; font-weight: 600; cursor: pointer; }
`;
document.head.append(css);
const panel = document.createElement("div");
panel.id = "lesson";
$("draw").prepend(panel);

/* ---- what each lesson offers: the parts list, and the keys and drops that place parts */
let cur = 0, lesson = LESSONS[0], solved = false, started = false;   /* started: a lesson is open (not the course picker) */
const allowed = kind => kind === "input" || kind === "led" ? false : lesson.parts.includes(kind);
function applyPalette() {
  document.querySelectorAll("#draw .parts").forEach(box => {
    let any = false;
    box.querySelectorAll("button").forEach(b => { const ok = allowed(b.dataset.kind); b.hidden = !ok; any = any || ok; });
    box.hidden = !any;
    const h = box.previousElementSibling;
    if (h && h.tagName === "H2") h.hidden = !any;
  });
}
const showGen0 = showGen, showSheets0 = showSheets;
showGen = function () { showGen0(); applyPalette(); };
showSheets = function () { showSheets0(); applyPalette(); };   /* it shows the in and out parts again */
const add0 = wireCanvas.add, arm0 = wireCanvas.arm;
const notHere = () => { msg.className = "err"; msg.textContent = "That part is not used in this lesson: the parts on the left are the ones you need."; };
wireCanvas.add = function (kind, x, y) { if (!allowed(kind)) { notHere(); return; } return add0.call(this, kind, x, y); };
wireCanvas.arm = function (kind) { if (!allowed(kind)) { notHere(); return; } return arm0.call(this, kind); };
wireCanvas.openChip = () => false;   /* your chip opens inside the circuit, to look at, not as a sheet to change */
/* every signal in a lesson is one bit; a flip-flop wired in a loop (its q
 * back into its d) has no width Wire can work out, so say it is 1 */
/* a lesson just opened is shown fresh: fitted on screen, every switch at 0,
 * not with the zoom and switches of the lesson before it */
let fresh = false;
const show0 = show;
show = function (html) {
  if (!fresh || mode !== "draw") return show0(html);
  fresh = false;
  let w = null, f = null;
  try { w = frames[front].contentWindow; f = w && w.wireEditState; if (f) w.wireEditState = () => ({}); } catch (e) { w = null; }
  try { return show0(html); } finally { if (w && f) w.wireEditState = f; }
};
const sheetLines0 = sheetLines;
sheetLines = function (m, sh) { return sheetLines0(m, Object.assign({}, sh, { parts: sh.parts.map(p => p.kind === "dff" && !p.w ? Object.assign({}, p, { w: 1 }) : p) })); };

/* ---- a lesson */
const id = () => lesson.id;
const chipFor = c => st.chips[c] || lesson.chipAnswers[c];
function setDrawing(d) {
  const m = copy(d);
  m.title = "Lesson";
  m.next = m.next || Math.max(0, ...m.parts.map(p => p.id)) + 1;
  m.chips = {};
  for (const c of lesson.needs) m.chips[c] = copy(chipFor(c));
  m.at = null; m.path = [];
  return m;
}
function open(i) {
  started = true; fresh = true;
  cur = Math.max(0, Math.min(LESSONS.length - 1, i));
  lesson = LESSONS[cur];
  solved = !!st.done[id()];
  st.last = id(); st.track = lesson.track; save();
  if (lesson.kind === "vhdl") { openCode(); return; }
  document.body.classList.remove("learn-vhdl");
  $("draw").prepend(panel);
  model = setDrawing(st.models[id()] || lesson.start);
  if (mode !== "draw") { selectedName = null; setMode("draw"); undoStack = []; redoStack = []; render(); fixTab(); if (history.replaceState) history.replaceState(null, "", "#" + id()); return; }
  undoStack = []; redoStack = []; selectedName = null; selectedNames = [];
  disarm();
  $("name").value = "lesson" + (cur + 1);
  showGen(); showProps(); compile();
  render();
  if (history.replaceState) history.replaceState(null, "", "#" + id());
}
/* a lesson in VHDL: the code editor, with the lesson above it */
function openCode() {
  document.body.classList.add("learn-vhdl");
  $("side").insertBefore(panel, $("editor"));
  disarm();
  lang = "vhdl"; dir = "";
  $("name").value = "lesson";
  src.value = st.code[id()] !== undefined ? st.code[id()] : lesson.start;
  if (mode !== "write") setMode("write"); else { paint(); persist(); schedule(0); }
  $("langbox").hidden = true;
  fixTab();
  render();
  if (history.replaceState) history.replaceState(null, "", "#" + id());
}
function setCode(text) { src.value = text; edited(); paint(); persist(); schedule(0); }
function fixTab() { const t = $("tab-side"); if (t) t.textContent = "Lesson"; }
function render(extra) {
  const inTrack = LESSONS.map((l, i) => [l, i]).filter(([l]) => l.track === lesson.track);
  /* the lessons, a dot each, grouped by chapter */
  const chapters = [];
  inTrack.forEach(([l, i], k) => { const last = chapters[chapters.length - 1]; if (!last || last.name !== (l.chapter || "")) chapters.push({ name: l.chapter || "", dots: [] });
    chapters[chapters.length - 1].dots.push(`<button type="button" data-i="${i}" class="${st.done[l.id] ? "done" : ""}" aria-current="${i === cur}" title="${esc(l.title)}">${k + 1}</button>`); });
  const nav = chapters.map(c => `<span class="lgroup" title="${esc(c.name)}">${c.dots.join("")}</span>`).join("");
  const tracks = TRACKS.map(t => { const ls = LESSONS.filter(l => l.track === t.dir), d = ls.filter(l => st.done[l.id]).length;
    return `<button type="button" data-t="${t.dir}" aria-pressed="${t.dir === lesson.track}" title="${esc(t.about || "")}">${esc(t.name)} <small>${d}/${ls.length}</small></button>`; }).join("");
  const pos = inTrack.findIndex(([l]) => l === lesson);
  const fails = st.fails[id()] || 0, shown = st.hints[id()] || 0;
  panel.innerHTML = `
${TRACKS.length > 1 ? `<div class="ltracks" role="group" aria-label="Course">${tracks}</div>` : ""}
<nav class="lnav" aria-label="Lessons">${nav}</nav>
<div class="lkick">Lesson ${pos + 1} of ${inTrack.length}${lesson.chapter ? ` · ${esc(lesson.chapter)}` : ""}</div>
<h2 class="ltitle">${esc(lesson.title)}</h2>
${md(lesson.intro)}
<div class="ltask"><div class="lt">Your task</div>${md(lesson.task)}</div>
<div class="lbar">
  ${lesson.check === "behaves" ? `<button type="button" class="primary" data-a="check">Check my circuit</button>` : ""}
  ${shown < lesson.hints.length ? `<button type="button" data-a="hint">${shown ? "Another hint" : "Hint"}</button>` : ""}
  ${fails >= 3 || shown >= lesson.hints.length ? `<button type="button" data-a="solution">Show me the solution</button>` : ""}
  <button type="button" data-a="reset" title="Back to the drawing this lesson starts with">Start again</button>
</div>
<div class="lfeed">${extra || ""}</div>
${lesson.hints.slice(0, shown).map((h, k) => `<div class="lnote hint"><b class="k">Hint ${k + 1}</b>${md(h)}</div>`).join("")}
${st.solutionShown && st.solutionShown[id()] ? `<div class="lnote sol"><b class="k">The solution</b>${md(lesson.solution)}</div>` : ""}
${solved ? doneBox() : ""}`;
  panel.querySelectorAll(".lnav button").forEach(b => { b.onclick = () => open(+b.dataset.i); });
  panel.querySelectorAll(".ltracks button").forEach(b => { b.onclick = () => {
    if (b.dataset.t !== lesson.track) choose(b.dataset.t);
  }; });
  panel.querySelectorAll("a.line").forEach(a => { a.onclick = () => goLine(+a.dataset.line, 1); });
  panel.querySelectorAll("[data-a]").forEach(b => { b.onclick = () => act(b.dataset.a); });
  const nx = panel.querySelector("[data-next]");
  if (nx) nx.onclick = () => { open(cur + 1); $("draw").scrollTop = 0; };
  const ot = panel.querySelector("[data-other]");
  if (ot) ot.onclick = () => choose(ot.dataset.other);
}
function doneBox() {
  const nextL = LESSONS[cur + 1];
  if (nextL && nextL.track === lesson.track)
    return `<div class="ldone" role="status"><h3>✓ Well done!</h3>${md(lesson.done)}<button type="button" data-next>Next lesson: ${esc(nextL.title)} →</button></div>`;
  /* the end of a course: the other one, if there is more to do there, and the playground */
  const other = TRACKS.find(t => t.dir !== lesson.track && inTrackOf(t.dir).some(([l]) => !st.done[l.id]));
  return `<div class="ldone" role="status"><h3>✓ Well done!</h3>${md(lesson.done)}<p>That is the end of the ${esc(trackOf(lesson.track).name)} course. Build anything you like in the <a href="playground.html">playground</a>${other ? `, or learn the same circuits in the ${esc(other.name)} course` : ""}.</p>${other
    ? `<button type="button" data-other="${other.dir}">Start the ${esc(other.name)} course →</button>` : ""}</div>`;
}
function act(a) {
  if (a === "check") check(false);
  else if (a === "hint") { st.hints[id()] = (st.hints[id()] || 0) + 1; save(); render(); }
  else if (a === "reset" && lesson.kind === "vhdl") {
    if (src.value !== lesson.start && !confirm("Start this lesson's code again? What you wrote is lost.")) return;
    setCode(lesson.start); render();
  } else if (a === "solution" && lesson.kind === "vhdl") {
    st.solutionShown = st.solutionShown || {};
    st.solutionShown[id()] = true; save();
    setCode(lesson.answer); render();
  } else if (a === "reset") {
    if (!confirm("Start this lesson's circuit again? What you drew is lost (Undo brings it back).")) return;
    change(m => { const d = setDrawing(lesson.start); Object.assign(m, { parts: d.parts, links: d.links, next: d.next, chips: d.chips }); });
    render();
  } else if (a === "solution") {
    st.solutionShown = st.solutionShown || {};
    st.solutionShown[id()] = true; save();
    change(m => { const d = setDrawing(lesson.answer); Object.assign(m, { parts: d.parts, links: d.links, next: d.next, chips: d.chips }); });
    render();
  }
}

/* ---- is it done? */
function succeed() {
  if (solved) return;
  solved = true;
  st.done[id()] = true;
  if (lesson.keep) {   /* the circuit you made, as a chip: its lights are its outputs */
    const sh = sheet(model);
    st.chips[lesson.keep] = { next: sh.next, parts: copy(sh.parts).map(p => p.kind === "led" ? Object.assign(p, { kind: "output", bits: 1 }) : p), links: copy(sh.links) };
  }
  save();
  render();
  const box = panel.querySelector(".ldone");
  if (box) box.scrollIntoView({ block: "nearest", behavior: "smooth" });
}
const onOff = v => v === "1" ? "on" : v === "0" ? "off" : "not connected";
let checking = 0;
async function check(quiet) {
  if (lesson.check !== "behaves") {
    if (!quiet) { const v = lightNow(); if (v === 1) succeed(); else feedback(`<div class="lnote bad">The light <code>${esc(lesson.light)}</code> is still off. Click the switches in the circuit on the right.</div>`); }
    return;
  }
  const seq = ++checking, here = cur;
  const code = lesson.kind === "vhdl";
  const path = mainPath(), r = await wire(["--json", path], path, code ? src.value : genCode(model));
  if (seq !== checking || here !== cur) return;
  if (code && r.code !== 0) { if (!quiet) failedCode(r.err, path); return; }
  if (r.code !== 0) { if (!quiet) failed("Your circuit is not finished yet: " + (r.err.split("\n").find(l => /error/.test(l)) || "").replace(/^.*?error:\s*/, "") + "."); return; }
  const used = code ? [] : sheet(model).parts.filter(p => !["input", "clock", "led", "output"].includes(p.kind) && !allowed(p.kind === "chip" ? "chip:" + p.comp : p.kind));
  if (used.length) { if (!quiet) failed(`This time, use only the parts on the left: take out ${used.map(p => p.name).join(", ")}.`); return; }
  const D = JSON.parse(r.out), sim = await WireSim.create(D);
  if (seq !== checking || here !== cur) return;
  const g = WireExam.grade(D, sim, lesson);
  if (g.ok) { succeed(); return; }
  if (quiet) return;
  if (!g.rows) { failed(code ? g.title + ": " + g.text : /missing/.test(g.text) ? "A switch or a light the lesson needs is gone. Press Start again to get it back." : g.text); return; }
  if (code) { failed(`Not yet. ${esc(g.text)}`, true); return; }
  /* in words: with these switches, this light should be on but it is off */
  const row = g.rows[g.rows.length - 1].cells, nIn = lesson.inputs.length;
  const ins = lesson.inputs.map((p, i) => `<code>${esc(p.name)}</code> ${row[i] === "1" ? "on" : "off"}`).join(" and ");
  const k = row.slice(nIn).findIndex(c => c.bad), o = lesson.outputs[k], c = row[nIn + k];
  failed(`Not yet. With ${ins}, the light <code>${esc(o.name)}</code> should be <b>${onOff(c.want)}</b>, but yours is <b>${onOff(c.got)}</b>. Set the switches that way and look.`, true);
}
/* what Wire says about the VHDL, for someone learning it */
function explain(msg) {
  if (/syntax error/.test(msg)) return "A syntax error is most often a missing semicolon (;) at the end of the line before, or a missing end.";
  if (/is never given a value/.test(msg)) return "Every output needs a line that gives it a value, like y <= ...;";
  if (/is one bit \(std_logic\), but the value/.test(msg)) return "One bit is written with single quotes: '1' or '0'. Double quotes are for buses: \"0101\".";
  if (/no signal, port or constant called/.test(msg)) return "Check the spelling: you can use the entity's ports, and signals declared between architecture and begin.";
  return "";
}
function failedCode(err, path) {
  const line = err.split("\n").find(l => /error/.test(l)) || err.split("\n")[0] || "";
  const m = /:(\d+):\d+:\s*(?:syntax )?(?:error:\s*)?(.*)$/.exec(line), msgText = m ? m[2] : line;
  const where = m ? `<a class="line" data-line="${m[1]}">Line ${m[1]}</a>: ` : "";
  const why = explain(line);
  failed(`Wire cannot read this yet. ${where}${esc(msgText)}${why ? `<br>${esc(why)}` : ""}`, true);
}
function failed(html, isHtml) {
  st.fails[id()] = (st.fails[id()] || 0) + 1;
  /* a hint comes with the first and the second miss */
  const f = st.fails[id()];
  if (f <= lesson.hints.length && (st.hints[id()] || 0) < f) st.hints[id()] = f;
  save();
  render(`<div class="lnote bad">${isHtml ? html : esc(html)}</div>`);
}
const feedback = html => { const f = panel.querySelector(".lfeed"); if (f) f.innerHTML = html; };
function lightNow() {
  try {
    const w = frames[front].contentWindow.wirePage, n = w.nodes.find(x => x.kind === "led" && x.name === lesson.light);
    return n ? w.nets[n.in[0]].v : 0;
  } catch (e) { return 0; }
}

/* the playground says so: a drawing was kept, a page was shown */
window.wireLearn = {
  changed(m) {
    if (!started) return;
    if (lesson.kind === "vhdl") { if (mode === "write") { st.code[id()] = src.value; save(); } return; }
    if (mode !== "draw") return;
    const sh = sheet(m);
    st.models[id()] = { next: sh.next, parts: copy(sh.parts), links: copy(sh.links) };
    save();
  },
  shown() { if (started && lesson.check === "behaves" && !solved) check(true); }
};
/* a lesson done by clicking: watch the light */
setInterval(() => { if (started && lesson.check !== "behaves" && !solved && lightNow() === 1) succeed(); }, 300);

/* ---- the courses: picked once, before the first lesson */
function choose(d) {
  const pick = $("lpick");
  if (pick) pick.remove();
  st.track = d; save();
  open(firstOpen(d));
}
const WHAT = {
  draw: ["Drag gates onto the circuit and wire them", "Build your own chips and reuse them", "From a switch to an adder, a decoder and a counter"],
  vhdl: ["Type the circuit; Wire draws it as you write", "Gates, signals, buses and numbers", "From one line to registers and a counter"]
};
function picker() {
  const box = document.createElement("div");
  box.id = "lpick";
  box.innerHTML = `<div class="lpin">
<h2>How do you want to learn?</h2>
<p class="lsub">Both courses teach digital circuits from the very start, with no knowledge needed, and get harder one small step at a time. Pick the one that suits you; you can switch at any time.</p>
<div class="lcards">${TRACKS.map(t => { const ls = inTrackOf(t.dir), d = ls.filter(([l]) => st.done[l.id]).length;
    return `<button type="button" class="lcard" data-t="${t.dir}"><span class="ln">${esc(t.name)}</span><span class="la">${esc(t.about || "")}</span>
<ul>${(WHAT[t.dir] || []).map(w => `<li>${esc(w)}</li>`).join("")}</ul>
<span class="lgo"><span>${ls.length} lessons${d ? ` · ${d} done` : ""}</span><b>${d ? "Continue" : "Start"} →</b></span></button>`; }).join("")}</div>
<p class="lfoot">Already know your way around? The <a href="playground.html">playground</a> has every part, with no lessons.</p>
</div>`;
  document.body.append(box);
  box.querySelectorAll(".lcard").forEach(b => { b.onclick = () => choose(b.dataset.t); });
  const first = box.querySelector(".lcard");
  if (first) first.focus();
}

/* ---- start: the lesson in the address, else the one you were on, else ask which course */
fixTab();
const find = h => { const i = LESSONS.findIndex(l => l.id === h); return i >= 0 ? i : LESSONS.findIndex(l => l.id === renamed(h)); };
const want = find(location.hash.slice(1)), last = LESSONS.findIndex(l => l.id === st.last);
if (want >= 0) open(want);
else if (last >= 0 && st.track) open(last);
else if (TRACKS.length > 1) picker();
else open(0);
addEventListener("hashchange", () => { const i = find(location.hash.slice(1)); if (i >= 0 && i !== cur) { const p = $("lpick"); if (p) p.remove(); open(i); } });
})();
