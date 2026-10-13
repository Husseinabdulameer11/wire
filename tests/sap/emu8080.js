/* emu8080.js - a plain 8080 emulator, the reference that tests/sap/saptest.js
 * checks the SAP-2 and SAP-3 (built in Wire, run on their microcode)
 * against. Written separately from the microcode on purpose.
 *
 * It follows the same choices as the SAP-3:
 *   - flags as the 8085 sets them: SUB, SBB, CMP set AC from the carry out
 *     of bit 3 of a + ~b + 1; ANA sets AC = 1; XRA, ORA clear AC and CY;
 *     DCR sets AC from (low 4 bits of the operand) + 0xF
 *   - EI, DI, RIM, SIM and the undocumented opcodes do nothing
 *   - IN and OUT use the low 3 bits of the port: IN 1 = in1, IN 2 = in2,
 *     IN 3 = the keypad; OUT 3 and OUT 4 are the two display ports
 * With { sap2: true } it is Malvino's SAP-2: only its 42 instructions (the
 * rest do nothing), flags S and Z only, CALL keeps the return address at
 * FFFE/FFFF, and RAL/RAR rotate A without a carry. */
"use strict";

const SAP2_OPS = new Set([0x80, 0x81, 0xA0, 0xA1, 0xE6, 0xCD, 0x2F, 0x3D, 0x05, 0x0D, 0x76, 0xDB,
  0x3C, 0x04, 0x0C, 0xFA, 0xC3, 0xC2, 0xCA, 0x3A, 0x78, 0x79, 0x47, 0x41, 0x4F, 0x48, 0x3E, 0x06,
  0x0E, 0x00, 0xB0, 0xB1, 0xF6, 0xD3, 0x17, 0x1F, 0xC9, 0x32, 0x90, 0x91, 0xA8, 0xA9, 0xEE]);

class Emu8080 {
  constructor(opts = {}) {
    this.sap2 = !!opts.sap2;
    this.mem = new Uint8Array(65536);
    this.r = [0, 0, 0, 0, 0, 0, 0, 0];   /* B C D E H L (M) A */
    this.sp = 0; this.pc = 0;
    this.s = 0; this.z = 0; this.ac = 0; this.p = 0; this.cy = 0;
    this.in = [0, 0, 0, 0, 0, 0, 0, 0];  /* ports by number & 7 */
    this.out = [0, 0, 0, 0, 0, 0, 0, 0];
    this.halted = false;
  }
  load(bytes, at = 0) { bytes.forEach((b, i) => { this.mem[(at + i) & 0xFFFF] = b; }); }
  get a() { return this.r[7]; }
  hl() { return this.r[4] << 8 | this.r[5]; }
  rd(i) { return i === 6 ? this.mem[this.hl()] : this.r[i]; }
  wr(i, v) { if (i === 6) this.mem[this.hl()] = v & 255; else this.r[i] = v & 255; }
  next() { const v = this.mem[this.pc]; this.pc = (this.pc + 1) & 0xFFFF; return v; }
  next16() { const lo = this.next(); return this.next() << 8 | lo; }
  szp(v) {
    v &= 255;
    this.s = v >> 7; this.z = v === 0 ? 1 : 0;
    if (!this.sap2) { let n = 0; for (let b = 0; b < 8; b++) n += (v >> b) & 1; this.p = n % 2 ? 0 : 1; }
  }
  psw() { return this.s << 7 | this.z << 6 | this.ac << 4 | this.p << 2 | 2 | this.cy; }
  setPsw(v) { this.s = v >> 7 & 1; this.z = v >> 6 & 1; this.ac = v >> 4 & 1; this.p = v >> 2 & 1; this.cy = v & 1; }
  pair(p) { return p === 3 ? this.sp : this.r[2 * p] << 8 | this.r[2 * p + 1]; }
  setPair(p, v) { if (p === 3) this.sp = v & 0xFFFF; else { this.r[2 * p] = v >> 8 & 255; this.r[2 * p + 1] = v & 255; } }
  push(v) {
    this.sp = (this.sp - 1) & 0xFFFF; this.mem[this.sp] = v >> 8 & 255;
    this.sp = (this.sp - 1) & 0xFFFF; this.mem[this.sp] = v & 255;
  }
  pop() {
    const lo = this.mem[this.sp]; this.sp = (this.sp + 1) & 0xFFFF;
    const hi = this.mem[this.sp]; this.sp = (this.sp + 1) & 0xFFFF;
    return hi << 8 | lo;
  }
  cond(c) {
    return [!this.z, this.z, !this.cy, this.cy, !this.p, this.p, !this.s, this.s][c] ? 1 : 0;
  }
  /* the ALU: k is ADD ADC SUB SBB ANA XRA ORA CMP */
  alu(k, t) {
    const a = this.a;
    let res;
    if (k <= 3 || k === 7) {
      const sub = k === 2 || k === 3 || k === 7;
      const y = sub ? ~t & 255 : t;
      const cin = k === 0 ? 0 : k === 1 ? this.cy : k === 3 ? 1 - this.cy : 1;
      res = a + y + cin;
      if (!this.sap2) {
        this.ac = ((a & 15) + (y & 15) + cin) > 15 ? 1 : 0;
        this.cy = (res > 255 ? 1 : 0) ^ (sub ? 1 : 0);
      }
    } else if (k === 4) { res = a & t; if (!this.sap2) { this.ac = 1; this.cy = 0; } }
    else if (k === 5) { res = a ^ t; if (!this.sap2) { this.ac = 0; this.cy = 0; } }
    else { res = a | t; if (!this.sap2) { this.ac = 0; this.cy = 0; } }
    this.szp(res);
    if (k !== 7) this.r[7] = res & 255;
  }
  step() {
    if (this.halted) return;
    const op = this.next();
    if (this.sap2 && !SAP2_OPS.has(op)) return;              /* not a SAP-2 instruction */
    const d = op >> 3 & 7, s = op & 7, p = op >> 4 & 3;
    if (op === 0x76) { this.halted = true; return; }
    if (op >= 0x40 && op < 0x80) { this.wr(d, this.rd(s)); return; }
    if (op >= 0x80 && op < 0xC0) { this.alu(d, this.rd(s)); return; }
    if ((op & 0xC7) === 0xC6) { this.alu(d, this.next()); return; }
    if ((op & 0xC7) === 0x06) { this.wr(d, this.next()); return; }
    if ((op & 0xC7) === 0x04) {          /* INR */
      const t = this.rd(d), v = t + 1;
      if (!this.sap2) this.ac = ((t & 15) + 1) > 15 ? 1 : 0;
      this.szp(v); this.wr(d, v); return;
    }
    if ((op & 0xC7) === 0x05) {          /* DCR */
      const t = this.rd(d), v = t + 0xFF;
      if (!this.sap2) this.ac = ((t & 15) + 15) > 15 ? 1 : 0;
      this.szp(v); this.wr(d, v); return;
    }
    switch (op & 0xCF) {
    case 0x01: this.setPair(p, this.next16()); return;
    case 0x03: this.setPair(p, this.pair(p) + 1); return;
    case 0x0B: this.setPair(p, this.pair(p) - 1); return;
    case 0x09: { const v = this.hl() + this.pair(p); this.cy = v > 0xFFFF ? 1 : 0; this.setPair(2, v); return; }
    case 0xC5: this.push(p === 3 ? this.a << 8 | this.psw() : this.pair(p)); return;
    case 0xC1: { const v = this.pop(); if (p === 3) { this.r[7] = v >> 8; this.setPsw(v & 255); } else this.setPair(p, v); return; }
    }
    const call = target => {
      if (this.sap2) { this.mem[0xFFFE] = this.pc & 255; this.mem[0xFFFF] = this.pc >> 8; }
      else this.push(this.pc);
      this.pc = target;
    };
    const ret = () => { this.pc = this.sap2 ? this.mem[0xFFFF] << 8 | this.mem[0xFFFE] : this.pop(); };
    const a = this.a;
    switch (op) {
    case 0x00: return;
    case 0x02: this.mem[this.pair(0)] = a; return;
    case 0x12: this.mem[this.pair(1)] = a; return;
    case 0x0A: this.r[7] = this.mem[this.pair(0)]; return;
    case 0x1A: this.r[7] = this.mem[this.pair(1)]; return;
    case 0x22: { const n = this.next16(); this.mem[n] = this.r[5]; this.mem[(n + 1) & 0xFFFF] = this.r[4]; return; }
    case 0x2A: { const n = this.next16(); this.r[5] = this.mem[n]; this.r[4] = this.mem[(n + 1) & 0xFFFF]; return; }
    case 0x32: this.mem[this.next16()] = a; return;
    case 0x3A: this.r[7] = this.mem[this.next16()]; return;
    case 0x07: this.cy = a >> 7; this.r[7] = (a << 1 | a >> 7) & 255; return;
    case 0x0F: this.cy = a & 1; this.r[7] = (a >> 1 | (a & 1) << 7) & 255; return;
    case 0x17:
      if (this.sap2) { this.r[7] = (a << 1 | a >> 7) & 255; return; }
      { const c = this.cy; this.cy = a >> 7; this.r[7] = (a << 1 | c) & 255; return; }
    case 0x1F:
      if (this.sap2) { this.r[7] = (a >> 1 | (a & 1) << 7) & 255; return; }
      { const c = this.cy; this.cy = a & 1; this.r[7] = (a >> 1 | c << 7) & 255; return; }
    case 0x27: {
      let corr = 0, cy = this.cy;
      if (this.ac || (a & 15) > 9) corr |= 0x06;
      if (this.cy || a > 0x99) { corr |= 0x60; cy = 1; }
      this.ac = ((a & 15) + (corr & 15)) > 15 ? 1 : 0;
      this.r[7] = (a + corr) & 255; this.cy = cy; this.szp(this.r[7]); return;
    }
    case 0x2F: this.r[7] = ~a & 255; return;
    case 0x37: this.cy = 1; return;
    case 0x3F: this.cy ^= 1; return;
    case 0xC3: this.pc = this.next16(); return;
    case 0xCD: { const n = this.next16(); call(n); return; }
    case 0xC9: ret(); return;
    case 0xE3: { const lo = this.mem[this.sp], hi = this.mem[(this.sp + 1) & 0xFFFF];
      this.mem[this.sp] = this.r[5]; this.mem[(this.sp + 1) & 0xFFFF] = this.r[4]; this.r[4] = hi; this.r[5] = lo; return; }
    case 0xE9: this.pc = this.hl(); return;
    case 0xF9: this.sp = this.hl(); return;
    case 0xEB: { const de = this.pair(1); this.setPair(1, this.hl()); this.setPair(2, de); return; }
    case 0xD3: this.out[this.next() & 7] = a; return;
    case 0xDB: this.r[7] = this.in[this.next() & 7] & 255; return;
    }
    if ((op & 0xC7) === 0xC2) { const n = this.next16(); if (this.cond(d)) this.pc = n; return; }
    if ((op & 0xC7) === 0xC4) { const n = this.next16(); if (this.cond(d)) call(n); return; }
    if ((op & 0xC7) === 0xC0) { if (this.cond(d)) ret(); return; }
    if ((op & 0xC7) === 0xC7) { call(d * 8); return; }
    /* EI, DI, RIM, SIM, undocumented: nothing */
  }
  run(max = 1e6) {
    let n = 0;
    while (!this.halted && n < max) { this.step(); n++; }
    return n;
  }
}

module.exports = { Emu8080, SAP2_OPS };
