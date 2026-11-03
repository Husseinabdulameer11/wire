/* wire_learn.js - the lessons of learn.html (tools/learn.js puts them in).
 *
 * learn.html is the playground's drawing with a lesson beside it. This
 * script runs after the playground's own and works through it: it sets the
 * drawing each lesson starts with, offers only the parts the lesson uses,
 * and knows when the lesson is done, either a light is on (the learner
 * clicked the switches) or the circuit does what the lesson asks for every
 * input (src/wire_exam.js, as the exercises check). A failed Check gives a
 * hint, then a bigger one; after three, the solution. A lesson can keep the
 * learner's circuit as a chip for the lessons after it. */
(function () {
"use strict";
const DATA = window.WIRE_LEARN;
if (!DATA) return;
const LESSONS = DATA.lessons;
const KEY = "wire-learn-1";
let st = { models: {}, chips: {}, done: {}, fails: {}, hints: {}, last: null };
try { const s = JSON.parse(localStorage.getItem(KEY) || "null"); if (s && typeof s === "object") st = Object.assign(st, s); } catch (e) {}
const save = () => { try { localStorage.setItem(KEY, JSON.stringify(st)); } catch (e) {} };
const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const copy = x => JSON.parse(JSON.stringify(x));

/* ---- the text: paragraphs, - lists, | tables, `code`, **bold** */
const inline = s => esc(s).replace(/`([^`]+)`/g, "<code>$1</code>").replace(/\*\*([^*]+)\*\*/g, "<b>$1</b>");
function md(text) {
  const lines = (text || "").split("\n"), out = [];
  for (let i = 0; i < lines.length; ) {
    const l = lines[i];
    if (!l.trim()) { i++; continue; }
    if (/^- /.test(l)) { const it = []; for (; i < lines.length && /^- /.test(lines[i]); i++) it.push(`<li>${inline(lines[i].slice(2))}</li>`); out.push(`<ul>${it.join("")}</ul>`); continue; }
    if (/^\|/.test(l)) {
      const rows = [];
      for (; i < lines.length && /^\|/.test(lines[i]); i++) if (!/^\|[\s|:-]+\|$/.test(lines[i])) rows.push(lines[i].replace(/^\||\|$/g, "").split("|").map(c => c.trim()));
      out.push(`<table class="ltable"><tr>${rows[0].map(c => `<th>${inline(c)}</th>`).join("")}</tr>${rows.slice(1).map(r => `<tr>${r.map(c => `<td>${inline(c)}</td>`).join("")}</tr>`).join("")}</table>`);
      continue;
    }
    const p = [];
    for (; i < lines.length && lines[i].trim() && !/^(- |\|)/.test(lines[i]); i++) p.push(lines[i]);
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
#lesson .lnav { display: flex; gap: 6px; flex-wrap: wrap; margin-bottom: 10px; }
#lesson .lnav button { min-height: 30px; min-width: 30px; padding: 0 8px; border: 1px solid var(--line); border-radius: 15px; background: var(--paper); font-size: 13px; color: var(--ink); cursor: pointer; }
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
#lesson .ldone button { margin-top: 8px; min-height: 36px; padding: 0 14px; border-radius: 6px; border: 1px solid var(--ink); background: var(--ink); color: var(--paper); font: inherit; font-weight: 600; cursor: pointer; }
`;
document.head.append(css);
const panel = document.createElement("div");
panel.id = "lesson";
$("draw").prepend(panel);

/* ---- what each lesson offers: the parts list, and the keys and drops that place parts */
let cur = 0, lesson = LESSONS[0], solved = false;
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
  cur = Math.max(0, Math.min(LESSONS.length - 1, i));
  lesson = LESSONS[cur];
  solved = !!st.done[id()];
  st.last = id(); save();
  model = setDrawing(st.models[id()] || lesson.start);
  undoStack = []; redoStack = []; selectedName = null; selectedNames = [];
  disarm();
  $("name").value = "lesson" + (cur + 1);
  showGen(); showProps(); compile();
  render();
  if (history.replaceState) history.replaceState(null, "", "#" + id());
}
function render(extra) {
  const nav = LESSONS.map((l, i) => `<button type="button" data-i="${i}" class="${st.done[l.id] ? "done" : ""}" aria-current="${i === cur}" title="${esc(l.title)}">${i + 1}</button>`).join("");
  const fails = st.fails[id()] || 0, shown = st.hints[id()] || 0;
  panel.innerHTML = `
<nav class="lnav" aria-label="Lessons">${nav}</nav>
<div class="lkick">Lesson ${cur + 1} of ${LESSONS.length}</div>
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
  panel.querySelectorAll("[data-a]").forEach(b => { b.onclick = () => act(b.dataset.a); });
  const nx = panel.querySelector("[data-next]");
  if (nx) nx.onclick = () => { open(cur + 1); $("draw").scrollTop = 0; };
}
function doneBox() {
  const last = cur === LESSONS.length - 1;
  return `<div class="ldone" role="status"><h3>✓ Well done!</h3>${md(lesson.done)}${last
    ? `<p>That is the last lesson for now. Build anything you like in the <a href="playground.html">playground</a>.</p>`
    : `<button type="button" data-next>Next lesson: ${esc(LESSONS[cur + 1].title)} →</button>`}</div>`;
}
function act(a) {
  if (a === "check") check(false);
  else if (a === "hint") { st.hints[id()] = (st.hints[id()] || 0) + 1; save(); render(); }
  else if (a === "reset") {
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
  const path = mainPath(), r = await wire(["--json", path], path, genCode(model));
  if (seq !== checking || here !== cur) return;
  if (r.code !== 0) { if (!quiet) failed("Your circuit is not finished yet: " + (r.err.split("\n").find(l => /error/.test(l)) || "").replace(/^.*?error:\s*/, "") + "."); return; }
  const used = sheet(model).parts.filter(p => !["input", "led", "output"].includes(p.kind) && !allowed(p.kind === "chip" ? "chip:" + p.comp : p.kind));
  if (used.length) { if (!quiet) failed(`This time, use only the parts on the left: take out ${used.map(p => p.name).join(", ")}.`); return; }
  const D = JSON.parse(r.out), sim = await WireSim.create(D);
  if (seq !== checking || here !== cur) return;
  const g = WireExam.grade(D, sim, lesson);
  if (g.ok) { succeed(); return; }
  if (quiet) return;
  if (!g.rows) { failed(/missing/.test(g.text) ? "A switch or a light the lesson needs is gone. Press Start again to get it back." : g.text); return; }
  /* in words: with these switches, this light should be on but it is off */
  const row = g.rows[g.rows.length - 1].cells, nIn = lesson.inputs.length;
  const ins = lesson.inputs.map((p, i) => `<code>${esc(p.name)}</code> ${row[i] === "1" ? "on" : "off"}`).join(" and ");
  const k = row.slice(nIn).findIndex(c => c.bad), o = lesson.outputs[k], c = row[nIn + k];
  failed(`Not yet. With ${ins}, the light <code>${esc(o.name)}</code> should be <b>${onOff(c.want)}</b>, but yours is <b>${onOff(c.got)}</b>. Set the switches that way and look.`, true);
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
    const sh = sheet(m);
    st.models[id()] = { next: sh.next, parts: copy(sh.parts), links: copy(sh.links) };
    save();
  },
  shown() { if (lesson.check === "behaves" && !solved) check(true); }
};
/* a lesson done by clicking: watch the light */
setInterval(() => { if (lesson.check !== "behaves" && !solved && lightNow() === 1) succeed(); }, 300);

/* ---- start: the lesson in the address, else the one you were on, else the first */
const tab = $("tab-side");
if (tab) tab.textContent = "Lesson";
const want = location.hash.slice(1), at = LESSONS.findIndex(l => l.id === want);
open(at >= 0 ? at : Math.max(0, LESSONS.findIndex(l => l.id === st.last)));
addEventListener("hashchange", () => { const i = LESSONS.findIndex(l => l.id === location.hash.slice(1)); if (i >= 0 && i !== cur) open(i); });
})();
