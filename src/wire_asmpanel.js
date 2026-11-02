/* wire_asmpanel.js - the Assembly drawer of a circuit that is a CPU.
 *
 * A circuit gets it when it is a CPU: when it has a cpu statement (Wire), a
 * wire_cpu attribute (VHDL) or the Assembler settings (a drawing), or when
 * wire_cpu.js finds one in it by itself: a memory whose output can change
 * where it is read next. Whoever made the CPU, in whatever way, and however
 * wide its words are.
 *
 * Write a program, Assemble, and its words go into the program memory (and
 * .data into the data ram, or after the program when there is one memory),
 * the circuit is reset, and Run starts the clock. The listing lights the
 * instruction at the pc; the registers tab shows the register wires.
 *
 * The instructions come from a .isa file (src/isa.gram): the one a cpu
 * statement names, or one written here, in the Instruction set tab. Setup
 * says which part is which (found by wire_cpu.js, changed by hand). Both are
 * kept in this browser, or by the playground when the page is in it.
 *
 * An example can add to it from its add-on script, through wirePage.asm:
 *   programs    { name: source }          programs to pick from
 *   speed       "max"                     the clock speed Run uses
 *   addTab({ id, title, build(pane, ctx), tick(ctx) })   a tab of its own
 * It works the page only through wirePage (see wire_view.html). */
(function () {
"use strict";
const asm = window.wirePage.asm = { programs: {}, speed: null, tabs: [], addTab(t) { this.tabs.push(t); } };
const found = typeof WireCpu !== "undefined" ? WireCpu.detect(D) : null;
/* the playground looks here to know whether to offer an instruction set */
window.wireCpu = D.cpu || found ? { statement: !!D.cpu, detected: !!found } : null;
if (!D.cpu && !found) return;

const cpuName = D.cpu ? D.cpu.name : "cpu";
const store = {
  key: k => "wire-asm-" + (D.title || D.file || "") + "-" + cpuName + "-" + k,
  get(k) { try { return localStorage.getItem(this.key(k)); } catch (e) { return null; } },
  set(k, v) { try { if (v === null) localStorage.removeItem(this.key(k)); else localStorage.setItem(this.key(k), v); } catch (e) {} }
};
/* in the playground, the instruction set belongs to the circuit there (it goes in its links and its cpu statement) */
const host = (() => { try { const h = parent !== window && parent.wireCanvas; return h && h.asmIsa ? h.asmIsa : null; } catch (e) { return null; } })();
const hex = (v, n) => (v >>> 0).toString(16).toUpperCase().padStart(n, "0");
const bitsHex = w => Math.max(1, Math.ceil(w / 4));

/* the instance path of a part, "core.regfile.ram", as wire_cpu.js says it */
const pathOf = j => {
  const n = D.nodes[j], parts = [];
  for (let i = n.inst; D.insts && i > 0 && D.insts[i]; i = D.insts[i].parent) parts.unshift(D.insts[i].name || D.insts[i].comp || "chip" + i);
  return parts.concat(n.name || n.kind).join(".");
};
const MEMS = D.nodes.map((n, j) => j).filter(j => (D.nodes[j].kind === "rom" || D.nodes[j].kind === "ram") && !D.nodes[j].screen);
const memByPath = p => MEMS.find(j => pathOf(j) === p);

/* ---- the setup: which part is which ------------------------------------- */
function foundSetup() {
  if (D.cpu) {
    const top = (name, kinds) => MEMS.find(j => D.nodes[j].name === name && D.nodes[j].inst === 0 && kinds.includes(D.nodes[j].kind));
    return { text: top(D.cpu.text, ["rom", "ram"]), data: (D.cpu.data || []).map(n => top(n, ["ram"])).filter(j => j !== undefined).reverse(),
             textBase: D.cpu.textBase || 0, dataBase: D.cpu.dataBase !== undefined ? D.cpu.dataBase : null,
             pc: D.cpu.pc || "", halt: D.cpu.halt || "", regs: D.cpu.regs || "", watch: found ? found.watch : [] };
  }
  return { text: found.text, data: found.data.slice(), textBase: 0, dataBase: null,
           pc: found.pc, halt: found.halt, regs: found.regs, watch: found.watch };
}
/* data lanes are kept most significant first, as people write them; the loader wants the lowest first */
function loadSetup() {
  const s = foundSetup();
  let saved = null;
  try { saved = JSON.parse(store.get("setup") || "null"); } catch (e) {}
  if (saved && typeof saved === "object") {
    if (typeof saved.text === "string" && memByPath(saved.text) !== undefined) s.text = memByPath(saved.text);
    if (Array.isArray(saved.data)) s.data = saved.data.map(memByPath).filter(j => j !== undefined);
    for (const k of ["textBase", "dataBase"]) if (saved[k] === null || Number.isFinite(saved[k])) s[k] = saved[k];
    for (const k of ["pc", "halt", "regs"]) if (typeof saved[k] === "string") s[k] = saved[k];
    if (Array.isArray(saved.watch)) s.watch = saved.watch.filter(x => typeof x === "string");
  }
  return s;
}
function saveSetup(s) {
  const plain = { text: s.text !== undefined ? pathOf(s.text) : "", data: s.data.map(pathOf), textBase: s.textBase, dataBase: s.dataBase,
                  pc: s.pc, halt: s.halt, regs: s.regs, watch: s.watch };
  const base = foundSetup();
  const same = JSON.stringify(plain) === JSON.stringify({ text: base.text !== undefined ? pathOf(base.text) : "", data: base.data.map(pathOf), textBase: base.textBase,
                  dataBase: base.dataBase, pc: base.pc, halt: base.halt, regs: base.regs, watch: base.watch });
  store.set("setup", same ? null : JSON.stringify(plain));
}

/* ---- the instruction set ------------------------------------------------ */
/* the instruction set it starts with: the cpu statement's file, else name.isa next to the circuit, else a first one to change */
const isaFile = D.cpu ? D.cpu.isa.file.slice(D.cpu.isa.file.lastIndexOf("/") + 1)
              : D.isaNext ? D.isaNext.file : (D.file ? D.file.replace(/^.*[\/\\]/, "").replace(/\.[^.]*$/, "") : "my_cpu").replace(/[^A-Za-z0-9_]/g, "_") + ".isa";
const defaultIsa = () => D.cpu ? D.cpu.isa.source : D.isaNext ? D.isaNext.source
                       : WireCpu.starter(D, found, D.nodes[setup.text] ? D.nodes[setup.text].bits : 8);
let setup = loadSetup();
function startIsa() {
  if (host) { try { const t = host.get(); if (t && t.trim()) return t; } catch (e) {} }
  const s = store.get("isa");
  return s !== null ? s : defaultIsa();
}
function keepIsa(text) {
  const plain = text === defaultIsa();
  store.set("isa", plain ? null : text);
  if (host) { try { host.set(plain && !D.cpu ? "" : text, isaFile); } catch (e) {} }
}

/* after the whole page is read, so an add-on's programs and tabs (its script
 * comes after this one) are there, and once the simulator runs */
const whenParsed = f => document.readyState === "loading" ? document.addEventListener("DOMContentLoaded", f) : f();
whenParsed(() => window.wirePage.ready(page => {
  const el = (tag, props, ...kids) => { const e = Object.assign(document.createElement(tag), props || {}); e.append(...kids); return e; };
  let isaText = startIsa(), isa = null, isaError = null;
  function compileIsa() {
    try {
      const tree = D.cpu && isaText === D.cpu.isa.source ? D.cpu.isa.tree : WireIsaParse.parse(isaText);
      isa = WireAsm.load(tree); isaError = null;
    } catch (e) { isa = null; isaError = { line: e.line || 0, msg: e.message }; }
  }
  compileIsa();

  /* ---- the parts the setup names */
  const hw = {};
  const signal = name => {
    if (!name) return null;
    const n = page.nets.find(x => x.name === name);
    if (n) return n;
    const o = page.nodes.find(x => x.name === name && x.inst === 0 && x.kind === "output");
    return o ? page.nets[o.in[0]] : null;
  };
  function resolve() {
    hw.text = setup.text !== undefined ? page.nodes[setup.text] : null;
    hw.lanes = setup.data.map(j => page.nodes[j]).filter(n => n && n !== hw.text).reverse();   /* lowest first */
    hw.pc = signal(setup.pc); hw.halt = signal(setup.halt);
    hw.regs = [];
    if (isa && setup.regs && setup.regs.includes("%d")) for (let i = 0; i < isa.nreg; i++) hw.regs.push(signal(setup.regs.replace("%d", i)));
    hw.watch = setup.watch.map(n => ({ name: n, net: signal(n) })).filter(x => x.net);
  }
  /* what stops a program going in: said before anyone presses Assemble */
  function problem() {
    if (isaError) return `Instruction set, ${isaError.msg}`;
    if (!hw.text) return "Setup: choose the memory that holds the program.";
    if (isa.width !== hw.text.bits)
      return `${pathOf(hw.text.id)} holds ${hw.text.bits}-bit words, but the instruction set says width = ${isa.width}. Make them the same (Instruction set, or Setup).`;
    if (hw.lanes.length) {
      const b = hw.lanes[0].bits;
      if (hw.lanes.some(n => n.bits !== b || n.abits !== hw.lanes[0].abits)) return "Setup: the data rams must all be the same size.";
      if ((hw.lanes.length * b) % isa.unit) return `Setup: the data rams make ${hw.lanes.length * b}-bit words, not a whole number of ${isa.unit}-bit units.`;
    }
    return null;
  }

  const css = el("style", { textContent: `
@media (min-width: 900px) { #asmdrawer { width: min(700px, 58vw); } }
#asmdrawer .scroll { display: flex; flex-direction: column; overflow: hidden; }
#asmdrawer .asm-bar, #asmdrawer .asm-status, #asmdrawer .asm-ed, #asmdrawer .asm-tabs { flex: none; }
.asm-bar { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; padding: 8px 12px; border-bottom: 1px solid var(--panel-line); }
.asm-bar select, .asm-bar button, .asm-pane button, .asm-setup select, .asm-setup input[type=text] { font: inherit; font-size: 14px; color: var(--ink); background: var(--paper); border: 1px solid var(--panel-line); border-radius: 4px; min-height: 32px; padding: 0 10px; }
.asm-bar button.go { background: var(--ink); color: var(--paper); border-color: var(--ink); font-weight: 600; }
.asm-bar button:hover, .asm-pane button:hover { background: var(--hi-soft); color: var(--ink); }
.asm-bar button.go:hover { background: var(--ink); color: var(--paper); box-shadow: 0 0 0 3px var(--hi-soft); }
.asm-bar button:disabled { opacity: .5; }
.asm-bar .edited { font-size: 12px; color: var(--ink-soft); }
.asm-status { padding: 6px 12px; font-size: 13px; color: var(--ink-soft); min-height: 1.6em; border-bottom: 1px solid var(--panel-line); }
.asm-status.bad, .asm-isa-status.bad { color: #c0392b; }
.asm-status a { color: inherit; cursor: pointer; text-decoration: underline; display: block; }
.asm-ed { display: flex; height: min(260px, 32vh); min-height: 120px; resize: vertical; overflow: hidden; border-bottom: 1px solid var(--panel-line); font: 13px/1.5 "IBM Plex Mono", ui-monospace, monospace; }
.asm-pane .asm-ed { height: min(380px, 44vh); border: 1px solid var(--panel-line); border-radius: 4px; }
.asm-gut { margin: 0; padding: 6px 8px 6px 6px; text-align: right; color: var(--ink-soft); background: var(--paper); user-select: none; overflow: hidden; min-width: 3.2em; box-sizing: border-box; font: inherit; }
.asm-gut .err { color: #fff; background: #c0392b; border-radius: 2px; }
.asm-ed textarea { flex: 1; border: 0; margin: 0; padding: 6px 8px; resize: none; font: inherit; color: var(--ink); background: var(--panel); outline: none; white-space: pre; overflow: auto; tab-size: 8; }
.asm-tabs { display: flex; gap: 0; padding: 0 12px; border-bottom: 1px solid var(--panel-line); overflow-x: auto; }
.asm-tabs button { font: inherit; font-size: 14px; color: var(--ink-soft); background: none; border: 0; border-bottom: 2px solid transparent; padding: 8px 10px; min-height: 0; white-space: nowrap; }
.asm-tabs button[aria-selected="true"] { color: var(--ink); border-bottom-color: var(--hi); font-weight: 600; }
.asm-tabs button.warn::after { content: " •"; color: #c0392b; }
.asm-pane { padding: 10px 12px; flex: 1 1 auto; min-height: 0; overflow: auto; }
.asm-pane[hidden] { display: none; }
.asm-regs { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 2px 10px; font: 12.5px/1.6 "IBM Plex Mono", ui-monospace, monospace; }
.asm-regs div { display: flex; gap: 6px; padding: 0 4px; border-radius: 3px; }
.asm-regs .n { color: var(--ink-soft); width: 6.5em; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.asm-regs .chg { background: var(--hi-soft); }
.asm-list, .asm-forms, .asm-src { font: 12.5px/1.55 "IBM Plex Mono", ui-monospace, monospace; }
.asm-list div { display: flex; gap: 12px; white-space: pre; padding: 0 6px; cursor: pointer; }
.asm-list div:hover { background: var(--hi-soft); }
.asm-list .a, .asm-list .w { color: var(--ink-soft); }
.asm-list .cur { background: var(--hi-soft); box-shadow: inset 3px 0 var(--hi); }
.asm-follow { font-size: 13px; color: var(--ink-soft); margin-bottom: 6px; display: block; }
.asm-forms div { white-space: pre; padding: 0 6px; }
.asm-forms .ps { color: var(--ink-soft); }
.asm-note { font-size: 13px; color: var(--ink-soft); margin: 0 0 8px; }
.asm-isa-bar { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; margin-bottom: 8px; }
.asm-isa-status { font-size: 13px; color: var(--ink-soft); margin: 6px 0 0; min-height: 1.4em; }
.asm-isa-status a { color: inherit; cursor: pointer; text-decoration: underline; }
.asm-setup { display: grid; grid-template-columns: minmax(9em, max-content) 1fr; gap: 8px 12px; align-items: center; font-size: 13.5px; }
.asm-setup label { color: var(--ink-soft); }
.asm-setup input[type=text], .asm-setup select { width: 100%; box-sizing: border-box; font-size: 13.5px; }
.asm-setup .hint { grid-column: 2; font-size: 12px; color: var(--ink-soft); margin-top: -4px; }
.asm-src { white-space: pre-wrap; background: var(--paper); border: 1px solid var(--panel-line); border-radius: 4px; padding: 6px 8px; margin: 4px 0 10px; user-select: all; }
.asm-pane h3 { font-size: 14px; margin: 14px 0 4px; }
` });
  document.head.append(css);

  const d = page.addDrawer("asmdrawer", "Assembly", `Write a program for this CPU, assemble it and run it`);
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
  const setStatus = (text, bad) => { status.className = "asm-status" + (bad ? " bad" : ""); status.textContent = text; };
  const isaName = () => isa ? isa.name : "this CPU";

  /* an editor with line numbers: the program, and the instruction set */
  function editor(area, gutEl) {
    const marks = errLines => {
      const n = area.value.split("\n").length, bad = new Set(errLines);
      let s = "";
      for (let i = 1; i <= n; i++) s += (bad.has(i) ? `<span class="err">${i}</span>` : i) + "\n";
      gutEl.innerHTML = s;
      gutEl.scrollTop = area.scrollTop;
    };
    area.addEventListener("scroll", () => { gutEl.scrollTop = area.scrollTop; });
    area.addEventListener("keydown", e => {
      if (e.key === "Tab" && !e.shiftKey) {
        e.preventDefault();
        const s = area.selectionStart, lineStart = area.value.lastIndexOf("\n", s - 1) + 1, n = 8 - ((s - lineStart) % 8);
        area.setRangeText(" ".repeat(n), s, area.selectionEnd, "end");
        area.dispatchEvent(new Event("input"));
      }
      e.stopPropagation();   /* typing here is not the circuit's keys */
    });
    const goLine = ln => {
      const lines = area.value.split("\n");
      let at = 0;
      for (let i = 0; i < ln - 1 && i < lines.length; i++) at += lines[i].length + 1;
      area.focus();
      area.setSelectionRange(at, at + (lines[ln - 1] || "").length);
      area.scrollTop = Math.max(0, (ln - 4) * parseFloat(getComputedStyle(area).lineHeight));
    };
    return { marks, goLine };
  }
  const prog = editor(ta, gut);
  const gutter = prog.marks, goLine = prog.goLine;

  /* ---- programs: the ones an add-on brings, else one that is yours */
  const starterProgram = () => `# ${isaName()}: write a program here and press Assemble.\n# Comments start with # or ;  The Instructions tab lists what you can write.\n`;
  const programs = Object.keys(asm.programs).length ? asm.programs : { program: starterProgram() };
  const names = Object.keys(programs);
  names.forEach(n => sel.append(new Option(n, n)));
  if (names.length < 2) sel.hidden = true;
  let current = names.includes(store.get("last")) ? store.get("last") : names[0];
  sel.value = current;
  const saved = n => store.get("prog:" + n);
  const markEdited = () => { editedMark.textContent = ta.value !== programs[current] ? "(edited)" : ""; };
  function loadText(n) {
    current = n;
    ta.value = saved(n) !== null ? saved(n) : programs[n];
    markEdited(); gutter([]); store.set("last", n);
  }
  sel.onchange = () => { loadText(sel.value); setStatus(`${current}: Assemble to load it.`); };
  $(".rev").onclick = () => { store.set("prog:" + current, null); loadText(current); setStatus(`${current}, as it comes.`); };
  ta.addEventListener("input", () => {
    if (ta.value === programs[current]) store.set("prog:" + current, null); else store.set("prog:" + current, ta.value);
    markEdited(); gutter([]);
  });
  ta.addEventListener("keydown", e => { if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) { e.preventDefault(); go(true); } });
  function showErrors(errors) {
    status.className = "asm-status bad"; status.textContent = "";
    errors.slice(0, 6).forEach(e => status.append(el("a", { textContent: `line ${e.line}: ${e.msg}`, onclick: () => goLine(e.line) })));
    if (errors.length > 6) status.append(`… and ${errors.length - 6} more`);
    gutter(errors.map(e => e.line));
  }

  /* ---- tabs */
  const panes = {}, tabButtons = {};
  let tab = null;
  function addTab(id, title) {
    const b = el("button", { textContent: title, role: "tab" });
    b.dataset.p = id; b.setAttribute("aria-selected", "false");
    const pane = el("div", { className: "asm-pane", hidden: true });
    pane.dataset.p = id;
    tabBar.append(b); body.append(pane);
    panes[id] = pane; tabButtons[id] = b;
    b.onclick = () => select(id);
    return pane;
  }
  function select(id) {
    if (!panes[id]) return;
    tab = id;
    tabBar.querySelectorAll("button").forEach(x => x.setAttribute("aria-selected", x.dataset.p === id));
    Object.entries(panes).forEach(([k, p]) => { p.hidden = k !== id; });
    lastVals.length = 0; lastRow = -1;
  }

  /* ---- assemble and load */
  let listing = [], rowOf = new Map(), lastRow = -1, ranProgram = null;
  const lastVals = [];
  const U = () => isa.width / isa.unit;
  /* with one memory, .data goes after the program unless Setup says where */
  function assemble(src) {
    const textBase = setup.textBase || 0, inText = !hw.lanes.length;
    if (setup.dataBase !== null || !inText) {
      const dataBase = setup.dataBase !== null ? setup.dataBase : 0;
      return { r: WireAsm.assemble(isa, src, { textBase, dataBase }), dataBase };
    }
    let dataBase = textBase, r = null;
    for (let k = 0; k < 6; k++) {
      r = WireAsm.assemble(isa, src, { textBase, dataBase });
      const end = textBase + r.text.length * U();
      if (!r.data.length || r.errors.length || end === dataBase) break;
      dataBase = end;   /* again, now that the program's size is known */
    }
    return { r, dataBase };
  }
  function check(r, dataBase) {
    const words = 2 ** hw.text.abits, inText = !hw.lanes.length, Uw = U(), textBase = setup.textBase || 0;
    if (r.errors.length) return;
    const textEnd = textBase + r.text.length * Uw;
    if (r.text.length > words) r.errors.push({ line: 1, msg: `the program is ${r.text.length} words; ${pathOf(hw.text.id)} holds ${words}` });
    else if (inText && r.data.length) {
      if (dataBase < textEnd && dataBase + r.data.length > textBase)
        r.errors.push({ line: 1, msg: `the .data (from ${dataBase}) and the program (${textBase} to ${textEnd - 1}) are in the same place; move .data in Setup` });
      else if (Math.ceil((dataBase + r.data.length - textBase) / Uw) > words)
        r.errors.push({ line: 1, msg: `the program and its .data do not fit in ${pathOf(hw.text.id)} (${words} words)` });
    } else if (r.data.length && r.data.length > 2 ** hw.lanes[0].abits * hw.lanes.length * hw.lanes[0].bits / isa.unit)
      r.errors.push({ line: 1, msg: `the data is ${r.data.length} units; the ram does not hold that much` });
  }
  function load(r, dataBase) {
    const unit = isa.unit, Uw = U(), textBase = setup.textBase || 0;
    const mem = Array.from(r.text, w => (w >>> 0) || 0);
    if (!hw.lanes.length && r.data.length) {   /* one memory: the data goes in it too */
      const umask = (1n << BigInt(unit)) - 1n;
      for (let i = 0; i < r.data.length; i++) {
        const a = dataBase + i - textBase, wi = Math.floor(a / Uw), sub = a % Uw;
        const shift = BigInt((isa.endian === "big" ? Uw - 1 - sub : sub) * unit);
        let w = BigInt(mem[wi] || 0);
        w = (w & ~(umask << shift)) | ((BigInt(r.data[i]) & umask) << shift);
        mem[wi] = Number(BigInt.asUintN(32, w));
      }
      for (let i = 0; i < mem.length; i++) if (mem[i] === undefined) mem[i] = 0;
    }
    page.setContents(hw.text, mem);
    if (hw.lanes.length) {
      const laneBits = BigInt(hw.lanes[0].bits), mask = (1n << laneBits) - 1n, wordUnits = hw.lanes.length * hw.lanes[0].bits / unit;
      const count = Math.ceil(r.data.length / wordUnits), cols = hw.lanes.map(() => new Array(count).fill(0));
      for (let w = 0; w < count; w++) {
        let v = 0n;
        for (let j = wordUnits - 1; j >= 0; j--) v = (v << BigInt(unit)) | BigInt(r.data[w * wordUnits + j] || 0);
        hw.lanes.forEach((n, k) => { cols[k][w] = Number((v >> (BigInt(k) * laneBits)) & mask) >>> 0; });
      }
      hw.lanes.forEach((n, k) => page.setContents(n, cols[k]));
    }
  }
  function go(run) {
    const p = problem();
    if (p) { setStatus(p, true); if (isaError) select("isa"); else if (!hw.text) select("setup"); return; }
    const { r, dataBase } = assemble(ta.value);
    check(r, dataBase);
    if (r.errors.length) { showErrors(r.errors); return; }
    gutter([]);
    load(r, dataBase);
    page.reset();
    buildListing(r.listing);
    ranProgram = current;
    const what = `${r.text.length} word${r.text.length === 1 ? "" : "s"} of program${r.data.length ? `, ${r.data.length} ${isa.unit === 8 ? "byte" : "unit"}${r.data.length === 1 ? "" : "s"} of data` : ""}`;
    if (run) {
      if (asm.speed) page.speed(asm.speed);
      page.run(true);
      setStatus(`${current}: ${what}. Running.`);
    } else setStatus(`${current}: ${what}, loaded. Step runs one clock; Run runs the clock.`);
  }
  $(".go").onclick = () => go(true);
  $(".asm").onclick = () => go(false);

  /* ---- listing */
  const lp = addTab("lst", "Listing");
  lp.append(el("label", { className: "asm-follow" }, el("input", { type: "checkbox", checked: true }), " follow the pc"));
  const list = el("div", { className: "asm-list" });
  lp.append(list);
  const follow = lp.querySelector("input");
  function buildListing(entries) {
    listing = entries; rowOf = new Map(); lastRow = -1;
    list.textContent = "";
    const aw = Math.max(4, hex(Math.max(0, ...entries.map(e => e.addr)), 1).length);
    const hw2 = Math.max(4, ...entries.map(e => e.hex.length));
    entries.forEach((e, i) => {
      const row = el("div", {}, el("span", { className: "a", textContent: hex(e.addr, aw) }),
                    el("span", { className: "w", textContent: e.hex.padEnd(hw2 + 1) }), el("span", { textContent: e.src.replace(/\s+$/, "") }));
      row.onclick = () => goLine(e.line);
      list.append(row);
      if (e.sec === "text" && !rowOf.has(e.addr)) rowOf.set(e.addr, i);
    });
  }

  /* ---- registers: the pc, the register file, and whatever else Setup shows */
  const rp = addTab("reg", "Registers");
  let regCells = [], regSources = [];
  function buildRegs() {
    rp.textContent = "";
    regCells = []; regSources = []; lastVals.length = 0;
    const box = el("div", { className: "asm-regs" });
    const row = (name, net, title) => {
      const div = el("div", { title: title || name }, el("span", { className: "n", textContent: name }), el("span", { className: "v", textContent: "-".repeat(bitsHex(net.w)) }));
      box.append(div);
      regCells.push(div); regSources.push(net);
    };
    if (hw.pc) row("pc", hw.pc, `the address of the instruction being run (${setup.pc})`);
    hw.regs.forEach((n, i) => { if (n) row(WireAsm.regLabel(isa, i) || setup.regs.replace("%d", i), n, setup.regs.replace("%d", i)); });
    hw.watch.forEach(x => row(x.name, x.net));
    if (!regCells.length) rp.append(el("p", { className: "asm-note", textContent: "No registers to show: name the program counter and the register wires in Setup." }));
    rp.append(box);
  }

  /* ---- instructions: what can be written */
  const ip = addTab("ins", "Instructions");
  function buildForms() {
    ip.textContent = "";
    if (!isa) { ip.append(el("p", { className: "asm-note", textContent: "The instruction set has a mistake; the Instruction set tab says where." })); return; }
    const memName = hw.text ? pathOf(hw.text.id) : "the program memory";
    const sizes = [...new Set([...isa.formats.values()].map(f => f.width / isa.width))].sort((a, b) => a - b);
    ip.append(el("p", { className: "asm-note", textContent: `${isa.name}: ${isa.width}-bit words${sizes.some(s => s > 1) ? `, instructions of ${sizes.join(", ")} word${sizes.length === 1 && sizes[0] === 1 ? "" : "s"}` : ""}; addresses count ${isa.unit === isa.width ? "words" : `${isa.unit}-bit units`}. The program goes into ${memName}${hw.lanes.length ? `, .data into ${hw.lanes.slice().reverse().map(n => pathOf(n.id)).join(", ")}` : ", and .data after it"}.` }));
    const box = el("div", { className: "asm-forms" });
    WireAsm.forms(isa).forEach(f => box.append(el("div", { className: f.pseudo ? "ps" : "", textContent: f.text + (f.pseudo ? "   (pseudo)" : "") })));
    ip.append(box);
  }

  /* ---- the instruction set, written here */
  const sp = addTab("isa", "Instruction set");
  sp.innerHTML = `
<p class="asm-note"></p>
<div class="asm-isa-bar">
  <button class="isa-rev" title="Back to the instruction set as it comes">Revert</button>
  <button class="isa-save" title="Download it, to keep beside the circuit and name in a cpu statement">Download .isa</button>
</div>
<div class="asm-ed"><pre class="asm-gut" aria-hidden="true"></pre><textarea spellcheck="false" autocomplete="off" autocapitalize="off" aria-label="The instruction set"></textarea></div>
<p class="asm-isa-status" role="status"></p>`;
  const isaTa = sp.querySelector("textarea"), isaStatus = sp.querySelector(".asm-isa-status");
  const isaEd = editor(isaTa, sp.querySelector(".asm-gut"));
  sp.querySelector(".asm-note").textContent = D.cpu
    ? `The instructions of ${cpuName}, from ${isaFile}. Change them here to try something; Download keeps what you made.`
    : D.isaNext ? `Wire found a CPU in this circuit (${pathOf(setup.text)} holds its program); its instructions are ${isaFile}, next to it. Change them here to try something; Download keeps what you made.`
    : `Wire found a CPU in this circuit: ${pathOf(setup.text)} holds its program. Describe its instructions here (the format of each, and its fixed bits), then write a program for it above. Download it as ${isaFile} next to the circuit and the page starts with it. README, "An assembler for any CPU", has the language.`;
  function showIsaState() {
    isaStatus.className = "asm-isa-status" + (isaError ? " bad" : "");
    isaStatus.textContent = "";
    if (isaError) {
      const a = el("a", { textContent: isaError.msg, onclick: () => isaError.line && isaEd.goLine(isaError.line) });
      isaStatus.append(a);
    } else isaStatus.textContent = `${isa.name}: ${[...isa.over.values()].reduce((t, l) => t + l.length, 0)} instructions and forms, ${isa.nreg} register${isa.nreg === 1 ? "" : "s"}.${isaText === defaultIsa() ? "" : " (changed here)"}`;
    isaEd.marks(isaError && isaError.line ? [isaError.line] : []);
    tabButtons.isa.classList.toggle("warn", !!isaError);
  }
  isaTa.value = isaText;
  let isaTimer = 0;
  isaTa.addEventListener("input", () => {
    clearTimeout(isaTimer);
    isaTimer = setTimeout(() => { isaText = isaTa.value; keepIsa(isaText); changed(); }, 300);
  });
  sp.querySelector(".isa-rev").onclick = () => { isaTa.value = isaText = defaultIsa(); keepIsa(isaText); changed(); };
  sp.querySelector(".isa-save").onclick = () => {
    const a = el("a", { href: URL.createObjectURL(new Blob([isaTa.value], { type: "text/plain" })), download: isaFile });
    document.body.append(a); a.click(); a.remove();
  };

  /* ---- setup: which part is which */
  const up = addTab("setup", "Setup");
  const netNames = [...new Set(page.nets.map(n => n.name).filter(Boolean))].slice(0, 600);
  function buildSetup() {
    up.textContent = "";
    up.append(el("p", { className: "asm-note", textContent: D.cpu
      ? `The cpu statement of the circuit says these; a change here lasts in this browser.`
      : `What Wire found in the circuit. Change it if it guessed wrong; it lasts in this browser.` }));
    const grid = el("div", { className: "asm-setup" });
    const list = el("datalist", { id: "asm-nets" });
    netNames.forEach(n => list.append(el("option", { value: n })));
    up.append(list, grid);
    const row = (label, input, hint) => {
      const id = "asm-f-" + label.replace(/\W+/g, "");
      input.id = id;
      grid.append(el("label", { htmlFor: id, textContent: label }), input);
      if (hint) grid.append(el("div", { className: "hint", textContent: hint }));
    };
    const text = el("select");
    const loop = new Set(found ? found.memories.map(m => m.id) : []);
    MEMS.forEach(j => { const n = D.nodes[j]; text.append(new Option(`${pathOf(j)}  (${n.kind}, ${n.bits} bits × ${2 ** n.abits})${loop.has(j) ? "" : "  - not in a loop"}`, j)); });
    text.value = setup.text;
    text.onchange = () => edit(s => { s.text = +text.value; s.data = s.data.filter(j => j !== s.text); });
    row("Program memory", text, "Its words are instructions: the width of the instruction set is its width.");
    const field = (label, value, set, hint, nets) => {
      const i = el("input", { type: "text", value, spellcheck: false, autocomplete: "off" });
      i.setAttribute("autocapitalize", "off");
      if (nets) i.setAttribute("list", "asm-nets");
      i.onchange = () => edit(s => set(s, i.value.trim()));
      row(label, i, hint);
    };
    const num = v => { v = v.trim(); if (!v) return null; const n = /^0x/i.test(v) ? parseInt(v, 16) : /^0b/i.test(v) ? parseInt(v.slice(2), 2) : parseInt(v, 10); return Number.isFinite(n) && n >= 0 ? n : null; };
    field("Data rams", setup.data.map(pathOf).join(", "), (s, v) => { s.data = v ? v.split(/[\s,]+/).map(memByPath).filter(j => j !== undefined && j !== s.text) : []; },
          "Where .data goes: one ram, or the byte lanes of a word, most significant first. Empty: in the program memory, after the program.");
    field("Program starts at", "0x" + (setup.textBase || 0).toString(16), (s, v) => { s.textBase = num(v) || 0; }, "The address of the first word of the program memory.");
    field("Data starts at", setup.dataBase === null ? "" : "0x" + setup.dataBase.toString(16), (s, v) => { s.dataBase = num(v); },
          setup.data.length ? "The address of the first unit of the data rams (empty: 0)." : "Empty: right after the program.");
    field("Program counter", setup.pc, (s, v) => { s.pc = v; }, "A wire that holds the address of the instruction being run: the listing lights it.", true);
    field("Stops when", setup.halt, (s, v) => { s.halt = v; }, "A 1-bit wire that goes to 1 when the program is done: Run stops then.", true);
    field("Register wires", setup.regs, (s, v) => { s.regs = v; }, "A name with %d for the register number, like regs_%d: the Registers tab shows them by their names in the instruction set.", true);
    field("Also show", setup.watch.join(", "), (s, v) => { s.watch = v ? v.split(/[\s,]+/).filter(Boolean) : []; }, "Other wires for the Registers tab: a, b, ir, mar ...", true);
    const reset = el("button", { textContent: D.cpu ? "Back to the cpu statement" : "Back to what Wire found", onclick: () => { store.set("setup", null); setup = loadSetup(); changed(); } });
    up.append(el("div", { className: "asm-isa-bar", style: "margin-top:12px" }, reset));

    /* the same, for the source: so the circuit keeps it */
    const top = setup.text !== undefined && D.nodes[setup.text].inst === 0 && setup.data.every(j => D.nodes[j].inst === 0);
    up.append(el("h3", { textContent: "To keep it in the circuit" }));
    if (!top) {
      up.append(el("p", { className: "asm-note", textContent: `A cpu statement names memories at the top of the circuit; ${pathOf(setup.text)} is inside a chip. The page keeps this setup in your browser instead.` }));
    } else {
      const o = [`isa = "${isaFile}"`, `text = ${D.nodes[setup.text].name}`];
      if (setup.textBase) o.push(`text_base = 0x${setup.textBase.toString(16)}`);
      if (setup.data.length) o.push(`data = ${setup.data.length > 1 ? "{" + setup.data.map(j => D.nodes[j].name).join(", ") + "}" : D.nodes[setup.data[0]].name}`);
      if (setup.dataBase !== null) o.push(`data_base = 0x${setup.dataBase.toString(16)}`);
      if (setup.pc) o.push(`pc = ${setup.pc}`);
      if (setup.halt) o.push(`halt = ${setup.halt}`);
      if (setup.regs) o.push(`regs = "${setup.regs}"`);
      const vh = o.map(x => x.replace(/ = /, "=").replace(/"/g, "").replace(/[{}]/g, "").replace(/, /g, ",")).join("; ");
      up.append(el("p", { className: "asm-note", textContent: `Download the instruction set (${isaFile}) next to the circuit, and add this line. In Wire:` }),
                el("div", { className: "asm-src", textContent: `cpu ${cpuName === "cpu" ? "my_cpu" : cpuName} (${o.join(", ")})` }),
                el("p", { className: "asm-note", textContent: "In VHDL, in the architecture of the top entity:" }),
                el("div", { className: "asm-src", textContent: `attribute wire_cpu : string;\nattribute wire_cpu of rtl : architecture is "${vh}";` }));
    }
  }
  function edit(f) { f(setup); saveSetup(setup); changed(); }

  /* everything that hangs on the instruction set and the setup */
  function changed() {
    compileIsa();
    resolve();
    buildRegs(); buildForms(); buildSetup(); showIsaState();
    const p = problem();
    $(".go").disabled = $(".asm").disabled = !!p;
    if (p) setStatus(p, true);
    else setStatus(`Write a program for ${isa.name}, then Assemble. The Instructions tab lists what you can write.`);
    if (!p) buildListing(assemble(ta.value).r.listing);
  }

  asm.tabs.forEach(t => { const p = addTab(t.id, t.title); p.className = "asm-pane"; t.build(p, { page, isa, cpu: D.cpu, setStatus, goLine, body }); });
  /* the page's way in for the playground: open the drawer at a tab */
  window.wireAsm = {
    open(id) { page.openDrawer("asmdrawer"); select(id || "isa"); },
    get isa() { return isaText; },
    /* the setup by top-level names, for a cpu statement (null when a memory is inside a chip) */
    setup() {
      const top = j => D.nodes[j] && D.nodes[j].inst === 0 ? D.nodes[j].name : null;
      if (!top(setup.text) || setup.data.some(j => !top(j))) return null;
      return { text: top(setup.text), data: setup.data.map(top), textBase: setup.textBase || 0, dataBase: setup.dataBase || 0, pc: setup.pc, halt: setup.halt, regs: setup.regs };
    }
  };

  /* ---- following the circuit */
  let wasHalted = false;
  function tick() {
    if (d.section.classList.contains("open")) {
      const pc = hw.pc ? hw.pc.v : 0;
      if (tab === "reg") {
        regSources.forEach((n, i) => {
          const v = n.v;
          if (v === lastVals[i]) { regCells[i].classList.remove("chg"); return; }
          if (lastVals[i] !== undefined) regCells[i].classList.add("chg");
          lastVals[i] = v;
          regCells[i].querySelector(".v").textContent = hex(v, bitsHex(n.w));
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
      if (t && t.tick) t.tick({ page, isa, cpu: D.cpu });
    }
    const halted = hw.halt && hw.halt.v === 1;
    if (halted && !wasHalted && page.running()) {
      page.run(false);
      setStatus(`${ranProgram || "The program"} stopped (${setup.halt} went to 1).`);
    }
    wasHalted = halted;
    requestAnimationFrame(tick);
  }

  loadText(current);
  changed();
  /* a CPU nobody has described yet starts at its instruction set */
  const fresh = !D.cpu && !D.isaNext && isaText === defaultIsa();
  select(asm.tabs.length ? asm.tabs[0].id : fresh ? "isa" : "lst");
  if (fresh && !problem()) setStatus(`Wire found a CPU here. Describe its instructions in the Instruction set tab (a starting point is there), then write a program and Assemble.`);
  requestAnimationFrame(tick);
  if (D.cpu && innerWidth >= 1200) page.openDrawer("asmdrawer");
}));
})();
