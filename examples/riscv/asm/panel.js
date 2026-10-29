/* panel.js - the Assembly panel of the RISC-V computer (rv32im.wire).
 *
 * Write a program (or pick one of programs/), Assemble and run: the words
 * go into the rom (instr) and the .data bytes into the ram's four byte
 * lanes (ram_b0 ... ram_b24), the computer is reset and the clock runs.
 * Underneath: the two screens, big, with the display and the switches;
 * the registers (the wires regs_1 ... regs_31 of the register file); and
 * the listing, with the instruction at the pc lit up. It works the page
 * only through wirePage (see wire_view.html). */
(function () {
"use strict";
const ABI = "zero ra sp gp tp t0 t1 t2 s0 s1 a0 a1 a2 a3 a4 a5 a6 a7 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 t3 t4 t5 t6".split(" ");
const ROM_WORDS = 1024, RAM_BASE = 0x1000, RAM_BYTES = 4096;
const STORE = "wire-rv32im-";
const store = {
  get(k) { try { return localStorage.getItem(STORE + k); } catch (e) { return null; } },
  set(k, v) { try { if (v === null) localStorage.removeItem(STORE + k); else localStorage.setItem(STORE + k, v); } catch (e) {} }
};
const hex = (v, n = 8) => (v >>> 0).toString(16).toUpperCase().padStart(n, "0");

wirePage.ready(page => {
  const rom = page.find("instr", "rom");
  const lanes = [0, 8, 16, 24].map(b => page.find("ram_b" + b, "ram"));
  const pixels = page.find("pixels", "ram"), chars = page.find("chars", "ram");
  const out = name => { const n = page.find(name, "output"); return n ? page.nets[n.in[0]] : null; };
  const netNamed = name => page.nets.find(n => n.name === name) || null;
  const haltedNet = out("halted"), pcNet = out("pc"), displayNet = out("display"), cyclesNet = netNamed("cycles");
  const regs = ABI.map((_, i) => netNamed("regs_" + i));
  if (!rom || lanes.some(l => !l)) return;   /* not the computer this panel is for */

  /* ------------------------------------------------------------ the panel */
  const css = document.createElement("style");
  css.textContent = `
@media (min-width: 900px) { #asmdrawer { width: min(700px, 58vw); } }
#asmdrawer .scroll { display: flex; flex-direction: column; overflow: hidden; }
#asmdrawer .rv-bar, #asmdrawer .rv-status, #asmdrawer .rv-ed, #asmdrawer .rv-tabs { flex: none; }
.rv-bar { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; padding: 8px 12px; border-bottom: 1px solid var(--panel-line); }
.rv-bar select, .rv-bar button { font: inherit; font-size: 14px; color: var(--ink); background: var(--paper); border: 1px solid var(--panel-line); border-radius: 4px; min-height: 32px; padding: 0 10px; }
.rv-bar button.go { background: var(--ink); color: var(--paper); border-color: var(--ink); font-weight: 600; }
.rv-bar button:hover { background: var(--hi-soft); color: var(--ink); }
.rv-bar button.go:hover { background: var(--ink); color: var(--paper); box-shadow: 0 0 0 3px var(--hi-soft); }
.rv-bar .edited { font-size: 12px; color: var(--ink-soft); }
.rv-status { padding: 6px 12px; font-size: 13px; color: var(--ink-soft); min-height: 1.6em; border-bottom: 1px solid var(--panel-line); }
.rv-status.bad { color: #c0392b; }
.rv-status a { color: inherit; cursor: pointer; text-decoration: underline; display: block; }
.rv-ed { display: flex; height: min(260px, 32vh); min-height: 120px; resize: vertical; overflow: hidden; border-bottom: 1px solid var(--panel-line); font: 13px/1.5 "IBM Plex Mono", ui-monospace, monospace; }
.rv-gut { margin: 0; padding: 6px 8px 6px 6px; text-align: right; color: var(--ink-soft); background: var(--paper); user-select: none; overflow: hidden; min-width: 3.2em; box-sizing: border-box; font: inherit; }
.rv-gut .err { color: #fff; background: #c0392b; border-radius: 2px; }
.rv-ed textarea { flex: 1; border: 0; margin: 0; padding: 6px 8px; resize: none; font: inherit; color: var(--ink); background: var(--panel); outline: none; white-space: pre; overflow: auto; tab-size: 8; }
.rv-tabs { display: flex; gap: 0; padding: 0 12px; border-bottom: 1px solid var(--panel-line); }
.rv-tabs button { font: inherit; font-size: 14px; color: var(--ink-soft); background: none; border: 0; border-bottom: 2px solid transparent; padding: 8px 10px; min-height: 0; }
.rv-tabs button[aria-selected="true"] { color: var(--ink); border-bottom-color: var(--hi); font-weight: 600; }
.rv-pane { padding: 10px 12px; flex: 1 1 auto; min-height: 0; overflow: auto; }
.rv-pane[hidden] { display: none; }
.rv-screens { display: flex; flex-wrap: wrap; gap: 12px; align-items: flex-start; }
.rv-screens canvas { image-rendering: pixelated; background: #05090a; border: 1.5px solid var(--ink); border-radius: 2px; max-width: 100%; }
.rv-screens figure { margin: 0; }
.rv-screens figcaption, .rv-io { font-size: 12px; color: var(--ink-soft); margin-top: 3px; }
.rv-io { display: flex; flex-wrap: wrap; gap: 6px 16px; align-items: center; margin-top: 10px; font-size: 13px; }
.rv-io b { font: 600 15px "IBM Plex Mono", ui-monospace, monospace; color: var(--ink); }
.rv-sw { display: inline-flex; gap: 2px; }
.rv-sw button { width: 22px; min-height: 26px; padding: 0; font: 12px "IBM Plex Mono", ui-monospace, monospace; border: 1px solid var(--panel-line); border-radius: 3px; background: var(--paper); color: var(--ink); }
.rv-sw button[aria-pressed="true"] { background: var(--hi); color: #000; border-color: var(--hi); }
.rv-regs { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 2px 10px; font: 12.5px/1.6 "IBM Plex Mono", ui-monospace, monospace; }
.rv-regs div { display: flex; gap: 6px; padding: 0 4px; border-radius: 3px; }
.rv-regs .n { color: var(--ink-soft); width: 6.5em; }
.rv-regs .chg { background: var(--hi-soft); }
.rv-list { font: 12.5px/1.55 "IBM Plex Mono", ui-monospace, monospace; }
.rv-list div { display: flex; gap: 12px; white-space: pre; padding: 0 6px; cursor: pointer; }
.rv-list div:hover { background: var(--hi-soft); }
.rv-list .a { color: var(--ink-soft); }
.rv-list .w { color: var(--ink-soft); }
.rv-list .cur { background: var(--hi-soft); box-shadow: inset 3px 0 var(--hi); }
.rv-follow { font-size: 13px; color: var(--ink-soft); margin-bottom: 6px; display: block; }
`;
  document.head.append(css);

  const d = page.addDrawer("asmdrawer", "Assembly", "Write a program for this computer, assemble it and run it");
  const body = d.body;
  body.innerHTML = `
<div class="rv-bar">
  <select aria-label="Program"></select><span class="edited"></span>
  <button class="go" title="Assemble the program, put it in the rom and ram, reset the computer and run the clock">Assemble and run</button>
  <button class="asm" title="Assemble and load it, then reset; Step goes one instruction at a time">Assemble</button>
  <button class="rev" title="Back to the program as it comes">Revert</button>
</div>
<div class="rv-status" role="status"></div>
<div class="rv-ed"><pre class="rv-gut" aria-hidden="true"></pre><textarea spellcheck="false" autocomplete="off" autocapitalize="off" aria-label="The program, in RISC-V assembly"></textarea></div>
<div class="rv-tabs" role="tablist">
  <button role="tab" data-p="scr" aria-selected="true">Screens</button>
  <button role="tab" data-p="reg" aria-selected="false">Registers</button>
  <button role="tab" data-p="lst" aria-selected="false">Listing</button>
</div>
<div class="rv-pane" data-p="scr">
  <div class="rv-screens">
    <figure><canvas class="px" width="64" height="64" style="width:192px;height:192px"></canvas><figcaption>colour screen, 64 × 64 (0x2000)</figcaption></figure>
    <figure><canvas class="tx"></canvas><figcaption>text screen, 64 × 16 (0x3000)</figcaption></figure>
  </div>
  <div class="rv-io"><span>display (0x4000) <b class="disp">0</b></span><span>switches (0x4008) <span class="rv-sw"></span></span><span class="cyc"></span></div>
</div>
<div class="rv-pane" data-p="reg" hidden><div class="rv-regs"></div></div>
<div class="rv-pane" data-p="lst" hidden><label class="rv-follow"><input type="checkbox" checked> follow the pc</label><div class="rv-list"></div></div>`;
  const $ = s => body.querySelector(s);
  const sel = $("select"), ta = $("textarea"), gut = $(".rv-gut"), status = $(".rv-status"), editedMark = $(".edited");

  /* ------------------------------------------------------------ programs */
  const names = Object.keys(RV_PROGRAMS);
  names.forEach(n => sel.append(new Option(n, n)));
  let current = names.includes(store.get("last")) ? store.get("last") : names[0];
  sel.value = current;
  const saved = name => store.get("prog:" + name);
  function loadText(name) {
    current = name;
    ta.value = saved(name) !== null ? saved(name) : RV_PROGRAMS[name];
    markEdited();
    gutter([]);
    store.set("last", name);
  }
  function markEdited() { editedMark.textContent = ta.value !== RV_PROGRAMS[current] ? "(edited)" : ""; }
  sel.onchange = () => { loadText(sel.value); setStatus(`${current}: Assemble and run to load it.`); };
  $(".rev").onclick = () => { store.set("prog:" + current, null); loadText(current); setStatus(`${current}, as it comes.`); };

  /* --------------------------------------------------------------- editor */
  function gutter(errLines) {
    const n = ta.value.split("\n").length, bad = new Set(errLines);
    let s = "";
    for (let i = 1; i <= n; i++) s += (bad.has(i) ? `<span class="err">${i}</span>` : i) + "\n";
    gut.innerHTML = s;
    gut.scrollTop = ta.scrollTop;
  }
  ta.addEventListener("scroll", () => { gut.scrollTop = ta.scrollTop; });
  ta.addEventListener("input", () => {
    if (ta.value === RV_PROGRAMS[current]) store.set("prog:" + current, null); else store.set("prog:" + current, ta.value);
    markEdited();
    gutter([]);
  });
  ta.addEventListener("keydown", e => {
    if (e.key === "Tab" && !e.shiftKey) {   /* to the next column of 8 */
      e.preventDefault();
      const s = ta.selectionStart, lineStart = ta.value.lastIndexOf("\n", s - 1) + 1, n = 8 - ((s - lineStart) % 8);
      ta.setRangeText(" ".repeat(n), s, ta.selectionEnd, "end");
      ta.dispatchEvent(new Event("input"));
    } else if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) { e.preventDefault(); go(true); }
    e.stopPropagation();   /* typing here is not the keypad, and space is not Step */
  });
  function goLine(ln) {
    const lines = ta.value.split("\n");
    let at = 0;
    for (let i = 0; i < ln - 1 && i < lines.length; i++) at += lines[i].length + 1;
    ta.focus();
    ta.setSelectionRange(at, at + (lines[ln - 1] || "").length);
    ta.scrollTop = Math.max(0, (ln - 4) * parseFloat(getComputedStyle(ta).lineHeight));
  }
  function setStatus(text, bad) { status.className = "rv-status" + (bad ? " bad" : ""); status.textContent = text; }

  /* -------------------------------------------------------- assemble, run */
  let listing = [], rowOf = new Map(), lastRow = -1, ranProgram = null;
  function go(run) {
    const r = RVAsm.assemble(ta.value, { textBase: 0, dataBase: RAM_BASE });
    if (!r.errors.length && r.text.length > ROM_WORDS) r.errors.push({ line: 1, msg: `the program is ${r.text.length} instructions; the rom holds ${ROM_WORDS}` });
    if (!r.errors.length && r.data.length > RAM_BYTES) r.errors.push({ line: 1, msg: `the data is ${r.data.length} bytes; the ram holds ${RAM_BYTES}` });
    if (r.errors.length) {
      status.className = "rv-status bad";
      status.textContent = "";
      r.errors.slice(0, 6).forEach(e => {
        const a = document.createElement("a");
        a.textContent = `line ${e.line}: ${e.msg}`;
        a.onclick = () => goLine(e.line);
        status.append(a);
      });
      if (r.errors.length > 6) status.append(`… and ${r.errors.length - 6} more`);
      gutter(r.errors.map(e => e.line));
      return;
    }
    gutter([]);
    const text = new Array(ROM_WORDS).fill(0);
    r.text.forEach((w, i) => { text[i] = w >>> 0; });
    page.setContents(rom, text);
    lanes.forEach((n, k) => {
      const bytes = new Array(RAM_BYTES / 4).fill(0);
      for (let i = 0; i < RAM_BYTES / 4; i++) bytes[i] = r.data[4 * i + k] || 0;
      page.setContents(n, bytes);
    });
    page.reset();
    buildListing(r.listing);
    ranProgram = current;
    const what = `${r.text.length} instruction${r.text.length === 1 ? "" : "s"}${r.data.length ? `, ${r.data.length} bytes of data` : ""}`;
    if (run) {
      page.speed("max");
      page.run(true);
      setStatus(`${current}: ${what}. Running.`);
    } else setStatus(`${current}: ${what}, loaded. Step runs one instruction; Run runs the clock.`);
  }
  $(".go").onclick = () => go(true);
  $(".asm").onclick = () => go(false);

  /* -------------------------------------------------------------- listing */
  const list = $(".rv-list"), follow = $(".rv-follow input");
  function buildListing(entries) {
    listing = entries; rowOf = new Map(); lastRow = -1;
    list.textContent = "";
    entries.forEach((e, i) => {
      const row = document.createElement("div");
      const a = Object.assign(document.createElement("span"), { className: "a", textContent: hex(e.addr, 4) });
      const w = Object.assign(document.createElement("span"), { className: "w", textContent: e.hex.padEnd(11) });
      const s = Object.assign(document.createElement("span"), { textContent: e.src.replace(/\s+$/, "") });
      row.append(a, w, s);
      row.onclick = () => goLine(e.line);
      list.append(row);
      if (/^[0-9a-f]{8}$/.test(e.hex) && e.addr < RAM_BASE && !rowOf.has(e.addr)) rowOf.set(e.addr, i);
    });
  }
  /* the rom as it comes holds hello.s: its listing, before anything is assembled */
  buildListing(RVAsm.assemble(RV_PROGRAMS[names[0]], { textBase: 0, dataBase: RAM_BASE }).listing);

  /* ------------------------------------------------------------ registers */
  const regBox = $(".rv-regs"), regCells = [], lastVals = new Array(34).fill(-1);
  const regRow = (name, title) => {
    const div = document.createElement("div");
    div.innerHTML = `<span class="n"></span><span class="v">--------</span>`;
    div.querySelector(".n").textContent = name;
    div.title = title || "";
    regBox.append(div);
    return div;
  };
  regCells.push(regRow("pc", "the address of the instruction being run"));
  ABI.forEach((a, i) => { if (i) regCells.push(regRow(`x${i} ${a}`)); });

  /* -------------------------------------------------------------- screens */
  const pxCanvas = $("canvas.px"), pxCtx = pxCanvas.getContext("2d"), pxImg = pxCtx.createImageData(64, 64);
  const txCanvas = $("canvas.tx"), CW = 8, CH = 13;
  txCanvas.width = 64 * CW * 2; txCanvas.height = 16 * CH * 2;
  txCanvas.style.width = 64 * CW + "px"; txCanvas.style.height = 16 * CH + "px";
  const txCtx = txCanvas.getContext("2d");
  const swBox = $(".rv-sw"), swNode = page.find("sw", "input"), swBtns = [];
  for (let b = 7; b >= 0; b--) {
    const btn = Object.assign(document.createElement("button"), { textContent: b, title: `switch ${b}` });
    btn.onclick = () => { if (swNode) page.setInput("sw", swNode.state ^ (1 << b)); };
    swBox.append(btn); swBtns[b] = btn;
  }
  let lastPx = null, lastTx = null;
  const same = (a, b) => { if (!a || a.length !== b.length) return false; for (let i = 0; i < b.length; i++) if (a[i] !== b[i]) return false; return true; };
  function drawScreens() {
    const pw = pixels && page.words(pixels);
    if (pw && !same(lastPx, pw)) {
      lastPx = Uint32Array.from(pw);
      const px = new Uint32Array(pxImg.data.buffer);
      for (let i = 0; i < 4096; i++) {   /* RRRGGGBB */
        const c = pw[i] & 255, r = (c >> 5) * 255 / 7, g = ((c >> 2) & 7) * 255 / 7, bl = (c & 3) * 255 / 3;
        px[i] = 0xFF000000 | (bl << 16) | (g << 8) | r;
      }
      pxCtx.putImageData(pxImg, 0, 0);
    }
    const cw = chars && page.words(chars);
    if (cw && !same(lastTx, cw)) {
      lastTx = Uint32Array.from(cw);
      txCtx.fillStyle = "#05090a"; txCtx.fillRect(0, 0, txCanvas.width, txCanvas.height);
      txCtx.font = `${11 * 2}px ui-monospace, "SF Mono", Menlo, Consolas, monospace`;
      txCtx.textBaseline = "top"; txCtx.textAlign = "center"; txCtx.fillStyle = "rgb(90,240,120)";
      for (let i = 0; i < cw.length; i++) {
        const c = cw[i] & 255;
        if (c <= 32 || c >= 127) continue;
        txCtx.fillText(String.fromCharCode(c), ((i % 64) * CW + CW / 2) * 2, Math.floor(i / 64) * CH * 2 + 2);
      }
    }
    if (displayNet) $(".disp").textContent = "0x" + hex(displayNet.v);
    if (swNode) for (let b = 0; b < 8; b++) swBtns[b].setAttribute("aria-pressed", (swNode.state >> b) & 1 ? "true" : "false");
    if (cyclesNet) $(".cyc").textContent = `${cyclesNet.v.toLocaleString("en")} cycles`;
  }

  /* --------------------------------------------- following the computer */
  let wasHalted = false, tab = "scr";
  function tick() {
    if (d.section.classList.contains("open")) {
      const pc = pcNet ? pcNet.v : 0;
      if (tab === "scr") drawScreens();
      if (tab === "reg") {
        const vals = [pc, ...regs.slice(1).map(n => (n ? n.v : 0))];
        vals.forEach((v, i) => {
          if (v === lastVals[i]) { regCells[i].classList.remove("chg"); return; }
          if (lastVals[i] !== -1) regCells[i].classList.add("chg");
          lastVals[i] = v;
          regCells[i].querySelector(".v").textContent = hex(v);
        });
      }
      if (tab === "lst") {
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
    }
    /* stopped by ecall, ebreak or a 1 at 0x4004: stop the clock too */
    const halted = haltedNet && haltedNet.v === 1;
    if (halted && !wasHalted && page.running()) {
      page.run(false);
      setStatus(`${ranProgram || "The program"} stopped (ecall, ebreak or 0x4004) after ${cyclesNet ? cyclesNet.v.toLocaleString("en") + " cycles" : "a while"}.`);
    }
    wasHalted = halted;
    requestAnimationFrame(tick);
  }
  body.querySelectorAll(".rv-tabs button").forEach(b => b.onclick = () => {
    tab = b.dataset.p;
    body.querySelectorAll(".rv-tabs button").forEach(x => x.setAttribute("aria-selected", x === b));
    body.querySelectorAll(".rv-pane").forEach(p => { p.hidden = p.dataset.p !== tab; });
    lastVals.fill(-1); lastRow = -1; lastPx = lastTx = null;
  });

  loadText(current);
  setStatus(ta.value === RV_PROGRAMS[names[0]] && current === names[0]
    ? "The rom holds hello.s: Run the clock, or Assemble and run. Pick another program above, or write your own."
    : `${current}: Assemble and run to load it (the rom holds hello.s).`);
  requestAnimationFrame(tick);
  if (innerWidth >= 1200) page.openDrawer("asmdrawer");   /* room for the circuit beside it */
});
})();
