/* rv32im.addon.js: made by `node tools/riscv.js build/wire` from programs/*.s
 * and asm/screens.js. Change those and run it again. wire puts this script
 * into the page of rv32im.wire, after the viewer. */
const RV_PROGRAMS = {
 "hello.s": "# Hello: text on the text screen, a picture on the colour screen, then,\n# for ever, the switches times themselves on the display.\n        .data\nline1:  .asciz \"Hello, RISC-V!\"\nline2:  .asciz \"An RV32IM processor, drawn and simulated by Wire.\"\nline3:  .asciz \"Flip the switches (sw): the display shows sw * sw.\"\n\n        .text\nmain:   li   sp, 0x2000          # the stack: the top of the ram\n        la   a0, line1\n        li   a1, 0x3000          # the text screen, row 0\n        call puts\n        la   a0, line2\n        li   a1, 0x3080          # row 2: 64 characters a row\n        call puts\n        la   a0, line3\n        li   a1, 0x30C0          # row 3\n        call puts\n\n# the colour screen, 64 x 64, a byte per pixel: RRRGGGBB\n        li   t2, 0x2000          # pixel (0, 0)\n        li   t5, 64\n        li   t0, 0               # y\nyloop:  li   t1, 0               # x\nxloop:  srli t3, t1, 3           # red: x / 8\n        slli t3, t3, 5\n        srli t4, t0, 3           # green: y / 8\n        slli t4, t4, 2\n        or   t3, t3, t4\n        xor  t4, t0, t1          # blue: from x xor y\n        srli t4, t4, 4\n        andi t4, t4, 3\n        or   t3, t3, t4\n        sb   t3, 0(t2)\n        addi t2, t2, 1\n        addi t1, t1, 1\n        blt  t1, t5, xloop\n        addi t0, t0, 1\n        blt  t0, t5, yloop\n\n# for ever: the display shows the switches squared\n        li   t0, 0x4000\nshow:   lw   t1, 8(t0)           # the switches\n        mul  t1, t1, t1\n        sw   t1, 0(t0)           # the display\n        j    show\n\n# puts: the string at a0 onto the screen at a1\nputs:   lbu  t0, 0(a0)\n        beqz t0, done\n        sb   t0, 0(a1)\n        addi a0, a0, 1\n        addi a1, a1, 1\n        j    puts\ndone:   ret\n",
 "bounce.s": "# Bounce: a dot bouncing round the colour screen, leaving a trail that\n# changes colour. Run it at Max.\n        .text\nmain:   li   s0, 3               # x\n        li   s1, 0               # y\n        li   s2, 1               # dx\n        li   s3, 1               # dy\n        li   s4, 0x2000          # the screen\n        li   s5, 63              # the last row and column\n        li   s6, 0x03            # the colour, RRRGGGBB\nloop:   slli t0, s1, 6           # the pixel at (x, y): 0x2000 + 64 y + x\n        add  t0, t0, s0\n        add  t0, t0, s4\n        sb   s6, 0(t0)\n        addi s6, s6, 7           # the next colour\n        andi s6, s6, 255\n        li   t2, 20              # wait a little\nwait:   addi t2, t2, -1\n        bnez t2, wait\n        add  s0, s0, s2\n        add  s1, s1, s3\n        beqz s0, flipx\n        bne  s0, s5, checky\nflipx:  neg  s2, s2\nchecky: beqz s1, flipy\n        bne  s1, s5, loop\nflipy:  neg  s3, s3\n        j    loop\n",
 "fib.s": "# Fibonacci: every third Fibonacci number, in decimal, a row each. The\n# digits come from divu and remu (RV32M); the program stops at the end.\n        .text\nmain:   li   sp, 0x2000\n        li   s0, 0               # n\n        li   s1, 0               # F(n)\n        li   s2, 1               # F(n + 1)\n        li   s3, 0x3000          # the start of the row\n        li   s4, 0x4000\nrow:    mv   a1, s3\n        li   t0, 'F'\n        sb   t0, 0(a1)\n        addi a1, a1, 1\n        mv   a0, s0\n        call putnum\n        li   t0, '='\n        sb   t0, 1(a1)\n        addi a1, a1, 3\n        mv   a0, s1\n        call putnum\n        sw   s1, 0(s4)           # the display shows it too\n        li   t1, 3               # three steps on\nstep:   add  t0, s1, s2\n        mv   s1, s2\n        mv   s2, t0\n        addi s0, s0, 1\n        addi t1, t1, -1\n        bnez t1, step\n        addi s3, s3, 64          # the next row\n        li   t0, 0x3400          # 16 rows\n        blt  s3, t0, row\n        ecall                    # stop\n\n# putnum: a0 in decimal, at a1; a1 ends up after it\nputnum: addi sp, sp, -16\n        mv   t1, sp              # the digits, the last one first\n        li   t2, 10\ndigit:  remu t3, a0, t2\n        addi t3, t3, '0'\n        sb   t3, 0(t1)\n        addi t1, t1, 1\n        divu a0, a0, t2\n        bnez a0, digit\ncopy:   addi t1, t1, -1\n        lbu  t3, 0(t1)\n        sb   t3, 0(a1)\n        addi a1, a1, 1\n        bne  t1, sp, copy\n        addi sp, sp, 16\n        ret\n",
 "keys.s": "# Keys: type on the keypad (or the keyboard: 0-9, a-f) and the keys show\n# on the text screen, the last one on the display.\n        .data\nprompt: .asciz \"Press keys (click the keypad, or type 0-9 and a-f):\"\n\n        .text\nmain:   li   sp, 0x2000\n        li   s1, 0x4000\n        la   a0, prompt\n        li   a1, 0x3000\n        call puts\n        li   s0, 0x3040          # where the next key goes: row 1\n        li   s2, 0x3400          # the end of the screen\n\ndown:   lw   t0, 12(s1)          # the keypad: bit 4 while a key is held\n        andi t1, t0, 16\n        beqz t1, down\n        andi t0, t0, 15          # which key\n        sw   t0, 0(s1)\n        li   t1, 10\n        blt  t0, t1, digit\n        addi t0, t0, 'A' - 10\n        j    put\ndigit:  addi t0, t0, '0'\nput:    sb   t0, 0(s0)\n        addi s0, s0, 1\n        blt  s0, s2, up\n        li   s0, 0x3040          # full: start again under the prompt\nup:     lw   t0, 12(s1)          # wait until it is let go\n        andi t1, t0, 16\n        bnez t1, up\n        j    down\n\nputs:   lbu  t0, 0(a0)\n        beqz t0, done\n        sb   t0, 0(a1)\n        addi a0, a0, 1\n        addi a1, a1, 1\n        j    puts\ndone:   ret\n",
 "primes.s": "# Primes: the sieve of Eratosthenes in the ram, then every prime below\n# 1000 on the text screen, and how many on the display. Each prime lights\n# its pixel on the colour screen too (pixel n is prime n). Stops at the end.\n        .equ N, 1000\n        .data\nsieve:  .space N                 # sieve[n] = 1: n is crossed out\n\n        .text\nmain:   li   sp, 0x2000\n        la   s0, sieve\n        li   s1, 2               # p\n        li   s2, N\ncross:  mul  t0, s1, s1          # from p * p on\n        bge  t0, s2, show\n        add  t1, s0, s1\n        lbu  t1, 0(t1)\n        bnez t1, nextp           # p is crossed out: not a prime\n        li   t2, 1\nmark:   add  t1, s0, t0\n        sb   t2, 0(t1)\n        add  t0, t0, s1\n        blt  t0, s2, mark\nnextp:  addi s1, s1, 1\n        j    cross\n\nshow:   li   s1, 2               # n\n        li   s3, 0x3000          # where the next number goes\n        li   s4, 0               # how many\n        li   s5, 0x2000          # the colour screen\neach:   add  t1, s0, s1\n        lbu  t1, 0(t1)\n        bnez t1, skip\n        addi s4, s4, 1\n        add  t0, s5, s1          # pixel n\n        li   t1, 0xFC            # yellow\n        sb   t1, 0(t0)\n        mv   a0, s1\n        mv   a1, s3\n        call putnum\n        addi s3, a1, 1           # and a space\nskip:   addi s1, s1, 1\n        blt  s1, s2, each\n        li   t0, 0x4000\n        sw   s4, 0(t0)           # 168 of them\n        ebreak                   # stop\n\n# putnum: a0 in decimal, at a1; a1 ends up after it\nputnum: addi sp, sp, -16\n        mv   t1, sp\n        li   t2, 10\ndigit:  remu t3, a0, t2\n        addi t3, t3, '0'\n        sb   t3, 0(t1)\n        addi t1, t1, 1\n        divu a0, a0, t2\n        bnez a0, digit\ncopy:   addi t1, t1, -1\n        lbu  t3, 0(t1)\n        sb   t3, 0(a1)\n        addi a1, a1, 1\n        bne  t1, sp, copy\n        addi sp, sp, 16\n        ret\n"
};
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
