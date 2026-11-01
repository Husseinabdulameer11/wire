/* wire_asmpanel.js - the Assembly drawer of a circuit that has a cpu statement.
 *
 * cpu name (isa = "x.isa", text = rom, data = ram, pc = pc, regs = "r%d") says
 * which part holds the program and which file describes the instructions.
 * This panel assembles with it (wire_asm.js): write a program, Assemble, and
 * its words go into the program memory (and .data into the data ram), the
 * circuit is reset, and Run starts the clock. The listing lights the
 * instruction at the pc; the registers tab shows the register wires.
 *
 * An example can add to it from its add-on script, through wirePage.asm:
 *   programs    { name: source }          programs to pick from
 *   speed       "max"                     the clock speed Run uses
 *   addTab({ id, title, build(pane, ctx), tick(ctx) })   a tab of its own
 * It works the page only through wirePage (see wire_view.html). */
(function () {
"use strict";
const asm = window.wirePage.asm = { programs: {}, speed: null, tabs: [], addTab(t) { this.tabs.push(t); } };
if (!D.cpu) return;
const cpu = D.cpu;

const store = {
  key: k => "wire-asm-" + (D.title || D.file || "") + "-" + cpu.name + "-" + k,
  get(k) { try { return localStorage.getItem(this.key(k)); } catch (e) { return null; } },
  set(k, v) { try { if (v === null) localStorage.removeItem(this.key(k)); else localStorage.setItem(this.key(k), v); } catch (e) {} }
};
const hex = (v, n) => (v >>> 0).toString(16).toUpperCase().padStart(n, "0");

window.wirePage.ready(page => {
  const el = (tag, props, ...kids) => { const e = Object.assign(document.createElement(tag), props || {}); e.append(...kids); return e; };
  let isa = null, isaError = "";
  try { isa = WireAsm.load(cpu.isa.tree); } catch (e) { isaError = `${cpu.isa.file}: ${e.message}`; }

  /* a memory named at the top of the circuit (parts inside a component can share its name) */
  const memory = (name, kinds) => page.nodes.find(n => n.name === name && n.inst === 0 && kinds.includes(n.kind)) || null;
  const textNode = memory(cpu.text, ["rom", "ram"]);
  const lanes = (cpu.data || []).map(n => memory(n, ["ram"]));
  const signal = name => {
    if (!name) return null;
    const n = page.nets.find(x => x.name === name);
    if (n) return n;
    const o = page.nodes.find(x => x.name === name && x.inst === 0 && x.kind === "output");
    return o ? page.nets[o.in[0]] : null;
  };
  const pcNet = signal(cpu.pc), haltNet = signal(cpu.halt);
  const unit = cpu.unit, width = cpu.width, U = width / unit;
  const textBase = cpu.textBase || 0, dataBase = cpu.dataBase || 0;
  const regNets = [];
  if (isa && cpu.regs) for (let i = 0; i < isa.nreg; i++) regNets.push(page.nets.find(n => n.name === cpu.regs.replace("%d", i)) || null);

  const css = el("style", { textContent: `
@media (min-width: 900px) { #asmdrawer { width: min(700px, 58vw); } }
#asmdrawer .scroll { display: flex; flex-direction: column; overflow: hidden; }
#asmdrawer .asm-bar, #asmdrawer .asm-status, #asmdrawer .asm-ed, #asmdrawer .asm-tabs { flex: none; }
.asm-bar { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; padding: 8px 12px; border-bottom: 1px solid var(--panel-line); }
.asm-bar select, .asm-bar button { font: inherit; font-size: 14px; color: var(--ink); background: var(--paper); border: 1px solid var(--panel-line); border-radius: 4px; min-height: 32px; padding: 0 10px; }
.asm-bar button.go { background: var(--ink); color: var(--paper); border-color: var(--ink); font-weight: 600; }
.asm-bar button:hover { background: var(--hi-soft); color: var(--ink); }
.asm-bar button.go:hover { background: var(--ink); color: var(--paper); box-shadow: 0 0 0 3px var(--hi-soft); }
.asm-bar button:disabled { opacity: .5; }
.asm-bar .edited { font-size: 12px; color: var(--ink-soft); }
.asm-status { padding: 6px 12px; font-size: 13px; color: var(--ink-soft); min-height: 1.6em; border-bottom: 1px solid var(--panel-line); }
.asm-status.bad { color: #c0392b; }
.asm-status a { color: inherit; cursor: pointer; text-decoration: underline; display: block; }
.asm-ed { display: flex; height: min(260px, 32vh); min-height: 120px; resize: vertical; overflow: hidden; border-bottom: 1px solid var(--panel-line); font: 13px/1.5 "IBM Plex Mono", ui-monospace, monospace; }
.asm-gut { margin: 0; padding: 6px 8px 6px 6px; text-align: right; color: var(--ink-soft); background: var(--paper); user-select: none; overflow: hidden; min-width: 3.2em; box-sizing: border-box; font: inherit; }
.asm-gut .err { color: #fff; background: #c0392b; border-radius: 2px; }
.asm-ed textarea { flex: 1; border: 0; margin: 0; padding: 6px 8px; resize: none; font: inherit; color: var(--ink); background: var(--panel); outline: none; white-space: pre; overflow: auto; tab-size: 8; }
.asm-tabs { display: flex; gap: 0; padding: 0 12px; border-bottom: 1px solid var(--panel-line); overflow-x: auto; }
.asm-tabs button { font: inherit; font-size: 14px; color: var(--ink-soft); background: none; border: 0; border-bottom: 2px solid transparent; padding: 8px 10px; min-height: 0; white-space: nowrap; }
.asm-tabs button[aria-selected="true"] { color: var(--ink); border-bottom-color: var(--hi); font-weight: 600; }
.asm-pane { padding: 10px 12px; flex: 1 1 auto; min-height: 0; overflow: auto; }
.asm-pane[hidden] { display: none; }
.asm-regs { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 2px 10px; font: 12.5px/1.6 "IBM Plex Mono", ui-monospace, monospace; }
.asm-regs div { display: flex; gap: 6px; padding: 0 4px; border-radius: 3px; }
.asm-regs .n { color: var(--ink-soft); width: 6.5em; }
.asm-regs .chg { background: var(--hi-soft); }
.asm-list, .asm-forms { font: 12.5px/1.55 "IBM Plex Mono", ui-monospace, monospace; }
.asm-list div { display: flex; gap: 12px; white-space: pre; padding: 0 6px; cursor: pointer; }
.asm-list div:hover { background: var(--hi-soft); }
.asm-list .a, .asm-list .w { color: var(--ink-soft); }
.asm-list .cur { background: var(--hi-soft); box-shadow: inset 3px 0 var(--hi); }
.asm-follow { font-size: 13px; color: var(--ink-soft); margin-bottom: 6px; display: block; }
.asm-forms div { white-space: pre; padding: 0 6px; }
.asm-forms .ps { color: var(--ink-soft); }
.asm-note { font-size: 13px; color: var(--ink-soft); margin: 0 0 8px; }
` });
  document.head.append(css);

  const d = page.addDrawer("asmdrawer", "Assembly", `Write a program for ${isa ? isa.name : "this circuit"}, assemble it and run it`);
  const body = d.body;
  body.innerHTML = `
<div class="asm-bar">
  <select aria-label="Program"></select><span class="edited"></span>
  <button class="go" title="Assemble the program, put it in the program memory, reset the circuit and run the clock">Assemble and run</button>
  <button class="asm" title="Assemble and load it, then reset; Step goes one clock at a time">Assemble</button>
  <button class="rev" title="Back to the program as it comes">Revert</button>
</div>
<div class="asm-status" role="status"></div>
<div class="asm-ed"><pre class="asm-gut" aria-hidden="true"></pre><textarea spellcheck="false" autocomplete="off" autocapitalize="off"></textarea></div>
<div class="asm-tabs" role="tablist"></div>`;
  const $ = s => body.querySelector(s);
  const sel = $("select"), ta = $("textarea"), gut = $(".asm-gut"), status = $(".asm-status"), editedMark = $(".edited"), tabBar = $(".asm-tabs");
  ta.setAttribute("aria-label", `The program, in ${isa ? isa.name : "assembly"}`);
  const setStatus = (text, bad) => { status.className = "asm-status" + (bad ? " bad" : ""); status.textContent = text; };

  /* ---- programs: the ones an add-on brings, else one that is yours */
  const starter = `# ${isa ? isa.name : "assembly"}: write a program here and press Assemble.\n# Comments start with # or ;  The Instructions tab lists what you can write.\n`;
  const programs = Object.keys(asm.programs).length ? asm.programs : { program: starter };
  const names = Object.keys(programs);
  names.forEach(n => sel.append(new Option(n, n)));
  if (names.length < 2) sel.hidden = true;
  let current = names.includes(store.get("last")) ? store.get("last") : names[0];
  sel.value = current;
  const saved = n => store.get("prog:" + n);
  const markEdited = () => { editedMark.textContent = ta.value !== programs[current] ? "(edited)" : ""; };
  function gutter(errLines) {
    const n = ta.value.split("\n").length, bad = new Set(errLines);
    let s = "";
    for (let i = 1; i <= n; i++) s += (bad.has(i) ? `<span class="err">${i}</span>` : i) + "\n";
    gut.innerHTML = s;
    gut.scrollTop = ta.scrollTop;
  }
  function loadText(n) {
    current = n;
    ta.value = saved(n) !== null ? saved(n) : programs[n];
    markEdited(); gutter([]); store.set("last", n);
  }
  sel.onchange = () => { loadText(sel.value); setStatus(`${current}: Assemble to load it.`); };
  $(".rev").onclick = () => { store.set("prog:" + current, null); loadText(current); setStatus(`${current}, as it comes.`); };
  ta.addEventListener("scroll", () => { gut.scrollTop = ta.scrollTop; });
  ta.addEventListener("input", () => {
    if (ta.value === programs[current]) store.set("prog:" + current, null); else store.set("prog:" + current, ta.value);
    markEdited(); gutter([]);
  });
  ta.addEventListener("keydown", e => {
    if (e.key === "Tab" && !e.shiftKey) {
      e.preventDefault();
      const s = ta.selectionStart, lineStart = ta.value.lastIndexOf("\n", s - 1) + 1, n = 8 - ((s - lineStart) % 8);
      ta.setRangeText(" ".repeat(n), s, ta.selectionEnd, "end");
      ta.dispatchEvent(new Event("input"));
    } else if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) { e.preventDefault(); go(true); }
    e.stopPropagation();   /* typing here is not the circuit's keys */
  });
  function goLine(ln) {
    const lines = ta.value.split("\n");
    let at = 0;
    for (let i = 0; i < ln - 1 && i < lines.length; i++) at += lines[i].length + 1;
    ta.focus();
    ta.setSelectionRange(at, at + (lines[ln - 1] || "").length);
    ta.scrollTop = Math.max(0, (ln - 4) * parseFloat(getComputedStyle(ta).lineHeight));
  }
  function showErrors(errors) {
    status.className = "asm-status bad"; status.textContent = "";
    errors.slice(0, 6).forEach(e => status.append(el("a", { textContent: `line ${e.line}: ${e.msg}`, onclick: () => goLine(e.line) })));
    if (errors.length > 6) status.append(`… and ${errors.length - 6} more`);
    gutter(errors.map(e => e.line));
  }

  /* ---- tabs */
  const panes = {};
  let tab = null;
  function addTab(id, title) {
    const b = el("button", { textContent: title, role: "tab" });
    b.dataset.p = id; b.setAttribute("aria-selected", "false");
    const pane = el("div", { className: "asm-pane", hidden: true });
    pane.dataset.p = id;
    tabBar.append(b); body.append(pane);
    panes[id] = pane;
    b.onclick = () => select(id);
    return pane;
  }
  function select(id) {
    tab = id;
    tabBar.querySelectorAll("button").forEach(x => x.setAttribute("aria-selected", x.dataset.p === id));
    Object.entries(panes).forEach(([k, p]) => { p.hidden = k !== id; });
    lastVals.length = 0; lastRow = -1;
  }

  /* ---- assemble and load */
  let listing = [], rowOf = new Map(), lastRow = -1, ranProgram = null;
  const lastVals = [];
  function load(r) {
    const words = 2 ** textNode.abits;
    const text = new Array(Math.max(r.text.length, 0));
    r.text.forEach((w, i) => { text[i] = w >>> 0; });
    page.setContents(textNode, text);
    if (lanes.length) {
      const laneBits = BigInt(lanes[0].bits), mask = (1n << laneBits) - 1n, wordUnits = lanes.length * lanes[0].bits / unit;
      const count = Math.ceil(r.data.length / wordUnits), cols = lanes.map(() => new Array(count).fill(0));
      for (let w = 0; w < count; w++) {
        let v = 0n;
        for (let j = wordUnits - 1; j >= 0; j--) v = (v << BigInt(unit)) | BigInt(r.data[w * wordUnits + j] || 0);
        lanes.forEach((n, k) => { cols[k][w] = Number((v >> (BigInt(k) * laneBits)) & mask) >>> 0; });
      }
      lanes.forEach((n, k) => page.setContents(n, cols[k]));
    }
    return words;
  }
  function go(run) {
    if (!isa) { setStatus(isaError, true); return; }
    const r = WireAsm.assemble(isa, ta.value, { textBase, dataBase });
    if (!r.errors.length && r.text.length > 2 ** textNode.abits)
      r.errors.push({ line: 1, msg: `the program is ${r.text.length} instructions; ${cpu.text} holds ${2 ** textNode.abits}` });
    if (!r.errors.length && r.data.length && !lanes.length)
      r.errors.push({ line: 1, msg: `the program has data, but the circuit has no data memory (give cpu a data = ram)` });
    if (!r.errors.length && lanes.length && r.data.length > 2 ** lanes[0].abits * lanes.length * lanes[0].bits / unit)
      r.errors.push({ line: 1, msg: `the data is ${r.data.length} bytes; the ram does not hold that much` });
    if (r.errors.length) { showErrors(r.errors); return; }
    gutter([]);
    load(r);
    page.reset();
    buildListing(r.listing);
    ranProgram = current;
    const what = `${r.text.length} instruction${r.text.length === 1 ? "" : "s"}${r.data.length ? `, ${r.data.length} bytes of data` : ""}`;
    if (run) {
      if (asm.speed) page.speed(asm.speed);
      page.run(true);
      setStatus(`${current}: ${what}. Running.`);
    } else setStatus(`${current}: ${what}, loaded. Step runs one clock; Run runs the clock.`);
  }
  $(".go").onclick = () => go(true);
  $(".asm").onclick = () => go(false);
  if (!isa) { $(".go").disabled = $(".asm").disabled = true; }

  /* ---- listing */
  const lp = addTab("lst", "Listing");
  lp.append(el("label", { className: "asm-follow" }, el("input", { type: "checkbox", checked: true }), " follow the pc"));
  const list = el("div", { className: "asm-list" });
  lp.append(list);
  const follow = lp.querySelector("input");
  const digits = (width / 4) | 0;
  function buildListing(entries) {
    listing = entries; rowOf = new Map(); lastRow = -1;
    list.textContent = "";
    const aw = Math.max(4, hex(Math.max(0, ...entries.map(e => e.addr)), 1).length);
    entries.forEach((e, i) => {
      const row = el("div", {}, el("span", { className: "a", textContent: hex(e.addr, aw) }),
                    el("span", { className: "w", textContent: e.hex.padEnd(digits + 3) }), el("span", { textContent: e.src.replace(/\s+$/, "") }));
      row.onclick = () => goLine(e.line);
      list.append(row);
      if (e.sec === "text" && !rowOf.has(e.addr)) rowOf.set(e.addr, i);
    });
  }

  /* ---- registers */
  const regCells = [];
  if (isa && regNets.length) {
    const rp = addTab("reg", "Registers");
    const box = el("div", { className: "asm-regs" });
    rp.append(box);
    const row = (name, title) => {
      const div = el("div", { title: title || "" }, el("span", { className: "n", textContent: name }), el("span", { className: "v", textContent: "-".repeat(digits) }));
      box.append(div);
      regCells.push(div);
    };
    if (pcNet) row("pc", "the address of the instruction being run");
    regNets.forEach((n, i) => { if (n) row(WireAsm.regLabel(isa, i)); else regCells.push(null); });
  }

  /* ---- instructions: what can be written */
  if (isa) {
    const ip = addTab("ins", "Instructions");
    ip.append(el("p", { className: "asm-note", textContent: `${isa.name}: ${width}-bit instructions, described by ${cpu.isa.file}. The program goes into ${cpu.text}${lanes.length ? `, .data into ${cpu.data.join(", ")}` : ""}.` }));
    const box = el("div", { className: "asm-forms" });
    WireAsm.forms(isa).forEach(f => box.append(el("div", { className: f.pseudo ? "ps" : "", textContent: f.text + (f.pseudo ? "   (pseudo)" : "") })));
    ip.append(box);
  }
  asm.tabs.forEach(t => { const p = addTab(t.id, t.title); p.className = "asm-pane"; t.build(p, { page, isa, cpu, setStatus, goLine, body }); });
  select(panes.lst ? "lst" : Object.keys(panes)[0]);
  if (asm.tabs.length) select(asm.tabs[0].id);

  /* ---- following the circuit */
  let wasHalted = false;
  function tick() {
    if (d.section.classList.contains("open")) {
      const pc = pcNet ? pcNet.v : 0;
      if (tab === "reg") {
        const vals = [];
        if (pcNet) vals.push(pc);
        regNets.forEach(n => { if (n) vals.push(n.v); });
        vals.forEach((v, i) => {
          if (!regCells[i]) return;
          if (v === lastVals[i]) { regCells[i].classList.remove("chg"); return; }
          if (lastVals[i] !== undefined) regCells[i].classList.add("chg");
          lastVals[i] = v;
          regCells[i].querySelector(".v").textContent = hex(v, digits);
        });
      } else if (tab === "lst") {
        const row = rowOf.has(pc) ? rowOf.get(pc) : -1;
        if (row !== lastRow) {
          list.querySelectorAll(".cur").forEach(x => x.classList.remove("cur"));
          if (row >= 0 && list.children[row]) {
            list.children[row].classList.add("cur");
            if (follow.checked) list.children[row].scrollIntoView({ block: "nearest" });
          }
          lastRow = row;
        }
      }
      const t = asm.tabs.find(x => x.id === tab);
      if (t && t.tick) t.tick({ page, isa, cpu });
    }
    const halted = haltNet && haltNet.v === 1;
    if (halted && !wasHalted && page.running()) {
      page.run(false);
      setStatus(`${ranProgram || "The program"} stopped (${cpu.halt} went to 1).`);
    }
    wasHalted = halted;
    requestAnimationFrame(tick);
  }

  loadText(current);
  if (isaError) setStatus(isaError, true);
  else setStatus(`Write a program for ${isa.name}, then Assemble. The Instructions tab lists what you can write.`);
  buildListing(isa ? WireAsm.assemble(isa, ta.value, { textBase, dataBase }).listing : []);
  requestAnimationFrame(tick);
  if (innerWidth >= 1200) page.openDrawer("asmdrawer");
});
})();
