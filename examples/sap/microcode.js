#!/usr/bin/env node
/* microcode.js - writes the microcode ROMs of the SAP-2 and SAP-3.
 *
 *   node examples/sap/microcode.js           writes sap2_microcode.hex and
 *                                            sap3_microcode.hex next to it
 *   node examples/sap/microcode.js --check   says if they are out of date
 *
 * Both computers (examples/sap2.wire, examples/sap3.wire) have the same
 * kind of control unit: a 4-bit step counter and this ROM, addressed by
 *
 *   {cond, opcode, step}     cond: 1 if the instruction's condition holds
 *                            (bits 5..3 of the opcode pick NZ Z NC C PO PE P M)
 *
 * Each 25-bit word says, for one step:
 *
 *   [4:0]   src   who talks on the bus      [17] pci  PC + 1
 *   [9:5]   dst   who listens               [18] spi  SP + 1
 *   [14:10] alu   ALU operation             [19] spd  SP - 1
 *   [16:15] x     16-bit adder: X = bus + 0, +1, -1 or + Y
 *   [20] szp  flags S Z AC P from the ALU   [23] end  next step is 0
 *   [21] cy   carry from the ALU            [24] hlt  stop the clock
 *   [22] dad  carry from the 16-bit adder
 *
 * A step is written as text: "HL>MAR", "MEM>A pci end", "ALU>A alu=SUB szp cy".
 * Every instruction starts with the fetch, the same for all:
 *   step 0  PC>MAR
 *   step 1  MEM>IR pci
 */
"use strict";
const fs = require("fs");
const path = require("path");

const SRC = { none: 0, B: 1, C: 2, D: 3, E: 4, H: 5, L: 6, A: 7, ALU: 8, MEM: 9, PC: 10, SP: 11,
  BC: 12, DE: 13, HL: 14, WZ: 15, X: 16, PCH: 17, PCL: 18, FLAGS: 19, Z: 20, W: 21, IN: 22,
  RSTV: 23, KFFFE: 24, KFFFF: 25 };
const DST = { none: 0, B: 1, C: 2, D: 3, E: 4, H: 5, L: 6, A: 7, TMP: 8, MEM: 9, PC: 10, SP: 11,
  BC: 12, DE: 13, HL: 14, MAR: 15, X: 16, Y: 17, IR: 18, FLAGS: 19, Z: 20, W: 21, OUT: 22 };
const ALU = { ADD: 0, ADC: 1, SUB: 2, SBB: 3, ANA: 4, XRA: 5, ORA: 6, CMP: 7, INR: 8, DCR: 9,
  RLC: 10, RRC: 11, RAL: 12, RAR: 13, CMA: 14, STC: 15, CMC: 16, DAA: 17 };
const XSEL = { "0": 0, "+1": 1, "-1": 2, Y: 3 };
const BIT = { pci: 17, spi: 18, spd: 19, szp: 20, cy: 21, dad: 22, end: 23, hlt: 24 };

/* "HL>X x=+1 end" -> a 25-bit word */
function word(step) {
  let w = 0;
  for (const tok of step.trim().split(/\s+/)) {
    if (!tok) continue;
    let m;
    if ((m = /^(\w+)>(\w+)$/.exec(tok))) {
      if (!(m[1] in SRC)) throw new Error(`unknown source ${m[1]} in "${step}"`);
      if (!(m[2] in DST)) throw new Error(`unknown destination ${m[2]} in "${step}"`);
      w |= SRC[m[1]] | DST[m[2]] << 5;
    } else if ((m = /^alu=(\w+)$/.exec(tok))) {
      if (!(m[1] in ALU)) throw new Error(`unknown ALU op ${m[1]}`);
      w |= ALU[m[1]] << 10;
    } else if ((m = /^x=(\S+)$/.exec(tok))) {
      if (!(m[1] in XSEL)) throw new Error(`unknown x=${m[1]}`);
      w |= XSEL[m[1]] << 15;
    } else if (tok in BIT) w |= 1 << BIT[tok];
    else throw new Error(`cannot read "${tok}" in "${step}"`);
  }
  return w >>> 0;
}

const FETCH = ["PC>MAR", "MEM>IR pci"];
const REG = ["B", "C", "D", "E", "H", "L", "M", "A"];      /* 8080 register numbers */
const PAIR = ["BC", "DE", "HL", "SP"];
const HI = { BC: "B", DE: "D", HL: "H" }, LO = { BC: "C", DE: "E", HL: "L" };
const ALUOPS = ["ADD", "ADC", "SUB", "SBB", "ANA", "XRA", "ORA", "CMP"];
const COND = ["NZ", "Z", "NC", "C", "PO", "PE", "P", "M"];

/* mark the last step with end */
const done = steps => steps.map((s, i) => i === steps.length - 1 ? s + " end" : s);
const imm = dst => ["PC>MAR", `MEM>${dst} pci`];            /* the next byte of the program */
const wz = [...imm("Z"), ...imm("W")];                        /* a 16-bit operand, low byte first */

/* Every instruction of the SAP-3: an 8080 without interrupts.
 * Returns { name, steps } or { name, yes, no } for conditional ones. */
function sap3(op) {
  const d = (op >> 3) & 7, s = op & 7, p = (op >> 4) & 3, c = COND[(op >> 3) & 7];
  if (op === 0x76) return { name: "HLT", steps: ["hlt"] };
  if (op >= 0x40 && op < 0x80) {
    const name = `MOV ${REG[d]},${REG[s]}`;
    if (s === 6) return { name, steps: done(["HL>MAR", `MEM>${REG[d]}`]) };
    if (d === 6) return { name, steps: done(["HL>MAR", `${REG[s]}>MEM`]) };
    return { name, steps: done([`${REG[s]}>${REG[d]}`]) };
  }
  if (op >= 0x80 && op < 0xC0) {
    const k = ALUOPS[d], out = k === "CMP" ? "ALU>none" : "ALU>A";
    const fin = `${out} alu=${k} szp cy`;
    if (s === 6) return { name: `${k} M`, steps: done(["HL>MAR", "MEM>TMP", fin]) };
    return { name: `${k} ${REG[s]}`, steps: done([`${REG[s]}>TMP`, fin]) };
  }
  if ((op & 0xC7) === 0xC6) {
    const k = ALUOPS[d], out = k === "CMP" ? "ALU>none" : "ALU>A";
    return { name: ["ADI", "ACI", "SUI", "SBI", "ANI", "XRI", "ORI", "CPI"][d] + " n", steps: done([...imm("TMP"), `${out} alu=${k} szp cy`]) };
  }
  if ((op & 0xC7) === 0x06) {
    if (d === 6) return { name: "MVI M,n", steps: done([...imm("Z"), "HL>MAR", "Z>MEM"]) };
    return { name: `MVI ${REG[d]},n`, steps: done(imm(REG[d])) };
  }
  if ((op & 0xC6) === 0x04) {
    const k = op & 1 ? "DCR" : "INR";
    if (d === 6) return { name: `${k} M`, steps: done(["HL>MAR", "MEM>TMP", `ALU>MEM alu=${k} szp`]) };
    return { name: `${k} ${REG[d]}`, steps: done([`${REG[d]}>TMP`, `ALU>${REG[d]} alu=${k} szp`]) };
  }
  switch (op & 0xCF) {
  case 0x01: return { name: `LXI ${PAIR[p]},nn`, steps: done([...wz, `WZ>${PAIR[p]}`]) };
  case 0x03: return { name: `INX ${PAIR[p]}`, steps: done([`${PAIR[p]}>X x=+1`, `X>${PAIR[p]}`]) };
  case 0x0B: return { name: `DCX ${PAIR[p]}`, steps: done([`${PAIR[p]}>X x=-1`, `X>${PAIR[p]}`]) };
  case 0x09: return { name: `DAD ${PAIR[p]}`, steps: done([`${PAIR[p]}>Y`, "HL>X x=Y dad", "X>HL"]) };
  case 0xC5: {
    const [h, l] = p === 3 ? ["A", "FLAGS"] : [HI[PAIR[p]], LO[PAIR[p]]];
    return { name: `PUSH ${p === 3 ? "PSW" : PAIR[p]}`, steps: done(["spd", "SP>MAR", `${h}>MEM spd`, "SP>MAR", `${l}>MEM`]) };
  }
  case 0xC1: {
    const [h, l] = p === 3 ? ["A", "FLAGS"] : [HI[PAIR[p]], LO[PAIR[p]]];
    return { name: `POP ${p === 3 ? "PSW" : PAIR[p]}`, steps: done(["SP>MAR", `MEM>${l} spi`, "SP>MAR", `MEM>${h} spi`]) };
  }
  }
  const call = target => ["spd", "SP>MAR", "PCH>MEM spd", "SP>MAR", "PCL>MEM", `${target}>PC`];
  const ret = ["SP>MAR", "MEM>Z spi", "SP>MAR", "MEM>W spi", "WZ>PC"];
  const skip2 = ["pci", "pci"];                       /* step over an address not taken */
  switch (op) {
  case 0x00: return { name: "NOP", steps: done([""]) };
  case 0x02: return { name: "STAX B", steps: done(["BC>MAR", "A>MEM"]) };
  case 0x12: return { name: "STAX D", steps: done(["DE>MAR", "A>MEM"]) };
  case 0x0A: return { name: "LDAX B", steps: done(["BC>MAR", "MEM>A"]) };
  case 0x1A: return { name: "LDAX D", steps: done(["DE>MAR", "MEM>A"]) };
  case 0x22: return { name: "SHLD nn", steps: done([...wz, "WZ>MAR", "L>MEM", "WZ>X x=+1", "X>MAR", "H>MEM"]) };
  case 0x2A: return { name: "LHLD nn", steps: done([...wz, "WZ>MAR", "MEM>L", "WZ>X x=+1", "X>MAR", "MEM>H"]) };
  case 0x32: return { name: "STA nn", steps: done([...wz, "WZ>MAR", "A>MEM"]) };
  case 0x3A: return { name: "LDA nn", steps: done([...wz, "WZ>MAR", "MEM>A"]) };
  case 0x07: return { name: "RLC", steps: done(["ALU>A alu=RLC cy"]) };
  case 0x0F: return { name: "RRC", steps: done(["ALU>A alu=RRC cy"]) };
  case 0x17: return { name: "RAL", steps: done(["ALU>A alu=RAL cy"]) };
  case 0x1F: return { name: "RAR", steps: done(["ALU>A alu=RAR cy"]) };
  case 0x27: return { name: "DAA", steps: done(["ALU>A alu=DAA szp cy"]) };
  case 0x2F: return { name: "CMA", steps: done(["ALU>A alu=CMA"]) };
  case 0x37: return { name: "STC", steps: done(["ALU>none alu=STC cy"]) };
  case 0x3F: return { name: "CMC", steps: done(["ALU>none alu=CMC cy"]) };
  case 0xC3: return { name: "JMP nn", steps: done([...wz, "WZ>PC"]) };
  case 0xCD: return { name: "CALL nn", steps: done([...wz, ...call("WZ")]) };
  case 0xC9: return { name: "RET", steps: done(ret) };
  case 0xE3: return { name: "XTHL", steps: done(["SP>MAR", "MEM>Z", "L>MEM spi", "SP>MAR", "MEM>W", "H>MEM spd", "WZ>HL"]) };
  case 0xE9: return { name: "PCHL", steps: done(["HL>PC"]) };
  case 0xF9: return { name: "SPHL", steps: done(["HL>SP"]) };
  case 0xEB: return { name: "XCHG", steps: done(["HL>X", "DE>HL", "X>DE"]) };
  case 0xD3: return { name: "OUT n", steps: done([...imm("Z"), "A>OUT"]) };
  case 0xDB: return { name: "IN n", steps: done([...imm("Z"), "IN>A"]) };
  }
  if ((op & 0xC7) === 0xC2) return { name: `J${c} nn`, yes: done([...wz, "WZ>PC"]), no: done(skip2) };
  if ((op & 0xC7) === 0xC4) return { name: `C${c} nn`, yes: done([...wz, ...call("WZ")]), no: done(skip2) };
  if ((op & 0xC7) === 0xC0) return { name: `R${c}`, yes: done(ret), no: done([""]) };
  if ((op & 0xC7) === 0xC7) return { name: `RST ${d}`, steps: done(call("RSTV")) };
  /* EI, DI, RIM, SIM and the undocumented opcodes do nothing here */
  return { name: "NOP*", steps: done([""]) };
}

/* The SAP-2 (Malvino): 42 of the same instructions on a smaller machine.
 * It has registers A, B, C (and TMP), flags S and Z only, no stack: CALL
 * keeps the return address at FFFE (low) and FFFF (high), and RAL and RAR
 * rotate A without a carry (bit 7 into bit 0, and back). */
const SAP2_OPS = new Set([0x80, 0x81, 0xA0, 0xA1, 0xE6, 0xCD, 0x2F, 0x3D, 0x05, 0x0D, 0x76, 0xDB,
  0x3C, 0x04, 0x0C, 0xFA, 0xC3, 0xC2, 0xCA, 0x3A, 0x78, 0x79, 0x47, 0x41, 0x4F, 0x48, 0x3E, 0x06,
  0x0E, 0x00, 0xB0, 0xB1, 0xF6, 0xD3, 0x17, 0x1F, 0xC9, 0x32, 0x90, 0x91, 0xA8, 0xA9, 0xEE]);
function sap2(op) {
  if (!SAP2_OPS.has(op)) return { name: "NOP*", steps: done([""]) };
  const strip = steps => steps.map(s => s.replace(/\bcy\b/, "").trim());   /* no carry flag */
  switch (op) {
  case 0xCD: return { name: "CALL nn", steps: done([...wz, "KFFFE>MAR", "PCL>MEM", "KFFFF>MAR", "PCH>MEM", "WZ>PC"]) };
  case 0xC9: return { name: "RET", steps: done(["KFFFE>MAR", "MEM>Z", "KFFFF>MAR", "MEM>W", "WZ>PC"]) };
  case 0x17: return { name: "RAL", steps: done(["ALU>A alu=RLC"]) };
  case 0x1F: return { name: "RAR", steps: done(["ALU>A alu=RRC"]) };
  }
  const i = sap3(op);
  return i.steps ? { name: i.name, steps: strip(i.steps) } : { name: i.name, yes: strip(i.yes), no: strip(i.no) };
}

let check = false, stale = [];
function write(file, title, def) {
  const lines = [
    `# ${title} microcode, written by microcode.js - do not edit; change microcode.js.`,
    "# Address = cond * 4096 + opcode * 16 + step; see microcode.js for the bits."
  ];
  const fetch = FETCH.map(word);
  for (let cond = 0; cond < 2; cond++) {
    for (let op = 0; op < 256; op++) {
      const i = def(op), steps = i.steps || (cond ? i.yes : i.no);
      if (steps.length + 2 > 16) throw new Error(`${i.name}: ${steps.length + 2} steps, the most is 16`);
      const words = [...fetch, ...steps.map(word)];
      const hex = words.map(w => w.toString(16).toUpperCase().padStart(7, "0")).join(" ");
      const at = ((cond << 12) | (op << 4)).toString(16).toUpperCase();
      lines.push(`@${at} ${hex}  # ${op.toString(16).toUpperCase().padStart(2, "0")} ${i.name}${i.steps ? "" : cond ? " (taken)" : " (not taken)"}`);
    }
  }
  const text = lines.join("\n") + "\n", to = path.join(__dirname, file);
  if (!check) fs.writeFileSync(to, text);
  else if (!fs.existsSync(to) || fs.readFileSync(to, "utf8") !== text) stale.push(file);
}

if (require.main === module) {
  check = process.argv.includes("--check");     /* only say if the files are out of date */
  write("sap3_microcode.hex", "SAP-3", sap3);
  write("sap2_microcode.hex", "SAP-2", sap2);
  if (!check) console.log("wrote sap2_microcode.hex and sap3_microcode.hex");
  else if (stale.length) { console.log(`out of date: ${stale.join(", ")}; run node examples/sap/microcode.js`); process.exit(1); }
}
module.exports = { sap2, sap3, word, SRC, DST, ALU, SAP2_OPS };
