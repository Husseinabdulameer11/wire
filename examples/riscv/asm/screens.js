/* screens.js - what is special about the RISC-V computer in its Assembly panel.
 *
 * The panel itself (the editor, Assemble and run, the listing, the registers,
 * the instruction list) is wire_asmpanel.js, and the assembler is wire_asm.js
 * with rv32im.isa: rv32im.wire has a cpu statement that points at them. This
 * script adds what only this computer has: the programs to pick from, a
 * clock as fast as the page allows, and a Screens tab with the 64 x 64 colour
 * screen, the 64 x 16 text screen, the hex display, the switches and a cycle
 * count. It works the page only through wirePage (see wire_view.html). */
(function () {
"use strict";
const hex = (v, n = 8) => (v >>> 0).toString(16).toUpperCase().padStart(n, "0");
const asm = wirePage.asm;
asm.programs = RV_PROGRAMS;
asm.speed = "max";

asm.addTab({
  id: "scr", title: "Screens",
  build(pane, { page }) {
    const css = document.createElement("style");
    css.textContent = `
.rv-screens { display: flex; flex-wrap: wrap; gap: 12px; align-items: flex-start; }
.rv-screens canvas { image-rendering: pixelated; background: #05090a; border: 1.5px solid var(--ink); border-radius: 2px; max-width: 100%; }
.rv-screens figure { margin: 0; }
.rv-screens figcaption, .rv-io { font-size: 12px; color: var(--ink-soft); margin-top: 3px; }
.rv-io { display: flex; flex-wrap: wrap; gap: 6px 16px; align-items: center; margin-top: 10px; font-size: 13px; }
.rv-io b { font: 600 15px "IBM Plex Mono", ui-monospace, monospace; color: var(--ink); }
.rv-sw { display: inline-flex; gap: 2px; }
.rv-sw button { width: 22px; min-height: 26px; padding: 0; font: 12px "IBM Plex Mono", ui-monospace, monospace; border: 1px solid var(--panel-line); border-radius: 3px; background: var(--paper); color: var(--ink); }
.rv-sw button[aria-pressed="true"] { background: var(--hi); color: #000; border-color: var(--hi); }`;
    document.head.append(css);
    pane.innerHTML = `
<div class="rv-screens">
  <figure><canvas class="px" width="64" height="64" style="width:192px;height:192px"></canvas><figcaption>colour screen, 64 × 64 (0x2000)</figcaption></figure>
  <figure><canvas class="tx"></canvas><figcaption>text screen, 64 × 16 (0x3000)</figcaption></figure>
</div>
<div class="rv-io"><span>display (0x4000) <b class="disp">0</b></span><span>switches (0x4008) <span class="rv-sw"></span></span><span class="cyc"></span></div>`;
    const $ = s => pane.querySelector(s);
    const top = (name, kind) => page.nodes.find(n => n.name === name && n.inst === 0 && n.kind === kind) || null;
    const out = name => { const n = top(name, "output"); return n ? page.nets[n.in[0]] : null; };
    const pixels = top("pixels", "ram"), chars = top("chars", "ram");
    const displayNet = out("display"), cyclesNet = page.nets.find(n => n.name === "cycles") || null;
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
    this.draw = function () {
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
    };
  },
  tick() { if (this.draw) this.draw(); }
});
})();
