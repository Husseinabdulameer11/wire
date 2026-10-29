/* ref.js - the RISC-V computer of examples/riscv, instruction by
 * instruction, in plain JavaScript: what the circuit must do. Written from
 * the RISC-V spec (RV32IM) and the memory map in rv32im.vhd, not from the
 * circuit. run(text, data, cycles, inputs) -> { pc, display, halted, pixels, chars, x } */
"use strict";
function run(text, data, cycles, inputs = {}) {
  let inp = typeof inputs === "function" ? inputs(0) : inputs;   /* inputs(cycle) -> { sw, key }, or fixed */
  const x = new Int32Array(32), ram = new Uint8Array(4096), pixels = new Uint8Array(4096), chars = new Uint8Array(1024);
  data.forEach((b, i) => { ram[i] = b; });
  let pc = 0, display = 0, halted = false, cyc = 0, stoppedAt = 0;
  const u = v => v >>> 0;
  const sx = (v, b) => (v << (32 - b)) >> (32 - b);
  const load = (a, size) => {
    const area = (a >>> 12) & 15;
    let word = 0;
    if (area === 1) { const w = (a & 0xFFC); word = ram[w] | ram[w + 1] << 8 | ram[w + 2] << 16 | ram[w + 3] << 24; }
    else if (area === 2) { const b = pixels[a & 4095]; word = b * 0x01010101; }
    else if (area === 3) { const b = chars[a & 1023]; word = b * 0x01010101; }
    else if (area === 4) {
      const r = (a >>> 2) & 7;
      word = r === 0 ? display : r === 2 ? (inp.sw || 0) & 255 : r === 3 ? (inp.key || 0) & 31 : r === 4 ? cyc : 0;
    }
    word = u(word);
    return u(word >>> (8 * (a & 3)));
  };
  const store = (a, v, f3) => {
    const area = (a >>> 12) & 15, lane = a & 3;
    const bytes = f3 === 0 ? [[lane, v & 255]] : f3 === 1 ? (lane & 2 ? [[2, v & 255], [3, (v >>> 8) & 255]] : [[0, v & 255], [1, (v >>> 8) & 255]])
                : [[0, v & 255], [1, (v >>> 8) & 255], [2, (v >>> 16) & 255], [3, (v >>> 24) & 255]];
    /* the stored word, lanes filled as the circuit does */
    const word = f3 === 0 ? (v & 255) * 0x01010101 : f3 === 1 ? ((v & 0xFFFF) * 0x10001) : v;
    if (area === 1) bytes.forEach(([l, b]) => { ram[(a & 0xFFC) + l] = b; });
    else if (area === 2) pixels[a & 4095] = (u(word) >>> (8 * lane)) & 255;
    else if (area === 3) chars[a & 1023] = (u(word) >>> (8 * lane)) & 255;
    else if (area === 4) {
      const r = (a >>> 2) & 7;
      if (r === 0) display = u(word);
      if (r === 1 && (word & 1)) halted = true;
    }
  };
  for (cyc = 0; cyc < cycles; ) {
    if (typeof inputs === "function") inp = inputs(cyc);
    if (halted) break;   /* stopped: nothing changes any more */
    const w = text[(pc >>> 2) & 1023] || 0, op = w & 0x7F, rd = (w >> 7) & 31, f3 = (w >> 12) & 7;
    const rs1 = x[(w >> 15) & 31], rs2 = x[(w >> 20) & 31], f7 = w >>> 25;
    const ii = w >> 20, si = ((w >> 25) << 5) | ((w >> 7) & 31);
    const bi = sx(((w >>> 31) << 12) | (((w >> 7) & 1) << 11) | (((w >> 25) & 0x3F) << 5) | (((w >> 8) & 0xF) << 1), 13);
    const ji = sx(((w >>> 31) << 20) | (((w >> 12) & 0xFF) << 12) | (((w >> 20) & 1) << 11) | (((w >> 21) & 0x3FF) << 1), 21);
    let npc = u(pc + 4), res = null;
    if (op === 0x33 && f7 === 1) {
      const A = BigInt(rs1), B = BigInt(rs2), UA = BigInt(u(rs1)), UB = BigInt(u(rs2));
      switch (f3) {
      case 0: res = Math.imul(rs1, rs2); break;
      case 1: res = Number(BigInt.asIntN(32, (A * B) >> 32n)); break;
      case 2: res = Number(BigInt.asIntN(32, (A * UB) >> 32n)); break;
      case 3: res = Number(BigInt.asIntN(32, (UA * UB) >> 32n)); break;
      case 4: res = rs2 === 0 ? -1 : (rs1 === -0x80000000 && rs2 === -1) ? rs1 : (rs1 / rs2) | 0; break;
      case 5: res = rs2 === 0 ? -1 : (u(rs1) / u(rs2)) >>> 0; break;
      case 6: res = rs2 === 0 ? rs1 : (rs1 === -0x80000000 && rs2 === -1) ? 0 : rs1 % rs2; break;
      case 7: res = rs2 === 0 ? rs1 : u(rs1) % u(rs2); break;
      }
    } else if (op === 0x33 || op === 0x13) {
      const b = op === 0x33 ? rs2 : ii, sh = b & 31, alt = op === 0x33 ? f7 === 0x20 : (f3 === 5 && f7 === 0x20);
      switch (f3) {
      case 0: res = op === 0x33 && f7 === 0x20 ? rs1 - b : rs1 + b; break;
      case 1: res = rs1 << sh; break;
      case 2: res = rs1 < b ? 1 : 0; break;
      case 3: res = u(rs1) < u(b) ? 1 : 0; break;
      case 4: res = rs1 ^ b; break;
      case 5: res = alt ? rs1 >> sh : rs1 >>> sh; break;
      case 6: res = rs1 | b; break;
      case 7: res = rs1 & b; break;
      }
    } else if (op === 0x37) res = w & 0xFFFFF000;
    else if (op === 0x17) res = pc + (w & 0xFFFFF000);
    else if (op === 0x6F) { res = pc + 4; npc = u(pc + ji); }
    else if (op === 0x67) { res = pc + 4; npc = u((rs1 + ii) & ~1); }
    else if (op === 0x63) {
      const t = [rs1 === rs2, rs1 !== rs2, false, false, rs1 < rs2, rs1 >= rs2, u(rs1) < u(rs2), u(rs1) >= u(rs2)][f3];
      if (t) npc = u(pc + bi);
    } else if (op === 0x03) {
      const v = load(u(rs1 + ii));
      res = [sx(v, 8), sx(v, 16), v | 0, 0, v & 255, v & 0xFFFF, v | 0, v | 0][f3];
    } else if (op === 0x23) store(u(rs1 + si), u(rs2), f3 & 3);
    else if (op === 0x73) halted = true;
    if (res !== null && rd) x[rd] = res;
    pc = npc;
    cyc++;
    if (halted && !stoppedAt) stoppedAt = cyc;
  }
  return { pc, display, halted, pixels, chars, x, ram, cycles: halted ? stoppedAt : cyc };
}
module.exports = { run };
