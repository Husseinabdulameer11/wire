/* wire_cpu.js - is this circuit a CPU? Which part holds its program?
 *
 * A circuit is a stored-program machine when what comes out of a memory can,
 * through any logic and any number of clocks, change the address that memory
 * is read at: the instruction steers the program counter (a jump), or loads
 * the address register (SAP-1), or decides what the next fetch is. A counter
 * stepping through a rom that only lights LEDs is not one; a FIFO is not one
 * (what it holds never moves its pointers). This works on any CPU, whoever
 * made it and however: Wire, VHDL or a drawing, one word wide or four.
 *
 *   WireCpu.detect(D) -> null, or {
 *     memories: [{ id, name, path, kind, bits, abits }]   memories in the loop, program first
 *     text      the id of the memory that most likely holds the program
 *     data      ids of rams that look like its data (byte lanes sharing one address), or []
 *     pc        the name of the wire that most likely is the program counter, or ""
 *     halt      the name of a 1-bit wire that says the CPU stopped, or ""
 *     regs      "name%d" when wires name0, name1, ... look like a register file, or ""
 *     watch     names of other registers worth showing (a, b, ir, mar ...)
 *   }
 *
 * It reads only the netlist (D.nodes, D.nets, D.insts), so the page knows at
 * once, before the simulator starts, and so tests can run it in node. */
"use strict";
const WireCpu = (() => {

const SEQ = new Set(["dff", "tff", "jkff", "counter"]);
const PASS = new Set(["pin_in", "pin_out", "buf", "split", "merge", "bus"]);
const PC_NAMES = /^(pc|ip|pc_?q|pc_?out|pc_?reg|prog_?count(er)?|program_?counter|r?ip|iptr)$/i;
const HALT_NAMES = /^(halt|halted|hlt|stop|stopped|done|finished)$/i;

function detect(D) {
  if (!D || !Array.isArray(D.nodes) || !Array.isArray(D.nets)) return null;
  const nodes = D.nodes, nets = D.nets;
  const driver = new Array(nets.length).fill(-1);
  nodes.forEach((n, j) => (n.out || []).forEach(net => { driver[net] = j; }));
  const inBy = (n, name) => { const i = (n.inNames || []).indexOf(name); return i < 0 ? -1 : n.in[i]; };

  /* the instance path of a part: "core.regfile" */
  const path = n => {
    const parts = [];
    for (let i = n.inst; D.insts && i > 0 && D.insts[i]; i = D.insts[i].parent)
      parts.unshift(D.insts[i].name || D.insts[i].comp || "chip" + i);
    return parts.concat(n.name || n.kind).join(".");
  };

  /* every part that can, through anything, change the value of a net */
  function cone(net) {
    const seen = new Uint8Array(nodes.length), stack = [];
    const push = x => { const d = x >= 0 ? driver[x] : -1; if (d >= 0 && !seen[d]) { seen[d] = 1; stack.push(d); } };
    push(net);
    while (stack.length) {
      const n = nodes[stack.pop()];
      if (n.kind === "clock") continue;   /* the clock steers everything; it is not data */
      (n.in || []).forEach(x => { if (!(n.inNames && n.inNames[n.in.indexOf(x)] === "clk")) push(x); });
    }
    return seen;
  }

  const mems = [];
  nodes.forEach((n, j) => {
    if (n.kind !== "rom" && n.kind !== "ram") return;
    if (n.screen) return;                         /* a screen is shown, not run */
    const addr = inBy(n, "addr");
    if (addr < 0) return;
    if (!cone(addr)[j]) return;                   /* what it holds never moves where it is read */
    mems.push({ id: j, name: n.name || "", path: path(n), kind: n.kind, bits: n.bits, abits: n.abits, addr });
  });
  if (!mems.length) return null;

  /* the program: a rom before a ram, a name that says so, then the widest */
  const score = m => (m.kind === "rom" ? 100 : 0) + (/instr|prog|imem|code|rom|text/i.test(m.name) ? 50 : 0)
                   - (/data|dmem|ram_b|stack/i.test(m.name) ? 40 : 0) + m.bits / 64 + m.abits / 1000;
  mems.sort((a, b) => score(b) - score(a));
  const text = mems[0];

  /* the data: rams read at one address together (byte lanes), not the program */
  let data = [];
  if (text.kind === "rom") {
    const groups = new Map();
    mems.filter(m => m.kind === "ram").forEach(m => {
      const k = baseNet(m.addr);
      (groups.get(k) || groups.set(k, []).get(k)).push(m);
    });
    let best = [];
    for (const g of groups.values()) if (g.length > best.length || (g.length === best.length && g[0].abits > best[0].abits)) best = g;
    /* most significant lane first: ram_b24 before ram_b0, hi before lo */
    const rank = m => { const d = /(\d+)$/.exec(m.name); return d ? +d[1] : /hi|high|msb/i.test(m.name) ? 1e6 : /lo|low|lsb/i.test(m.name) ? -1 : 0; };
    best.sort((a, b) => rank(b) - rank(a));
    if (best.every(m => m.bits === (best[0] || {}).bits)) data = best.map(m => m.id);
  }

  /* where a net's value comes from, through pins, buffers and bit slices */
  function baseNet(net) {
    for (let k = 0; k < 64 && net >= 0; k++) {
      const d = driver[net];
      if (d < 0) return net;
      const n = nodes[d];
      if (!PASS.has(n.kind) || n.kind === "merge" || (n.in || []).length !== 1) return net;
      net = n.in[0];
    }
    return net;
  }
  /* is a net held by a register (through pins, buffers, slices and merges)? */
  function isState(net, depth = 0) {
    if (depth > 64 || net < 0) return false;
    const d = driver[net];
    if (d < 0) return false;
    const n = nodes[d];
    if (SEQ.has(n.kind)) return true;
    if (PASS.has(n.kind)) return (n.in || []).length > 0 && n.in.every(x => isState(x, depth + 1));
    return false;
  }
  const named = nets.map((n, i) => ({ i, name: n.name, w: n.w })).filter(x => x.name);
  const firstByName = new Map();
  named.forEach(x => { if (!firstByName.has(x.name)) firstByName.set(x.name, x); });

  /* the program counter: a wire called pc, else the register the program address comes from */
  let pc = "";
  for (const x of firstByName.values()) if (x.w > 1 && PC_NAMES.test(x.name)) { pc = x.name; break; }
  if (!pc) {
    /* back from the address, through slices and pins, to a register */
    let net = text.addr;
    for (let k = 0; k < 64 && net >= 0 && !pc; k++) {
      const d = driver[net];
      if (d < 0) break;
      const n = nodes[d];
      if (SEQ.has(n.kind) || isState(net)) { pc = nets[net].name || ""; if (!pc) { const up = baseNet(net); pc = nets[up].name || ""; } break; }
      if (!PASS.has(n.kind) || !(n.in || []).length) break;
      net = n.in.reduce((a, b) => nets[b].w > nets[a].w ? b : a);   /* the widest piece of a merge */
    }
  }

  /* a 1-bit wire that says it stopped */
  let halt = "";
  for (const x of firstByName.values()) if (x.w === 1 && HALT_NAMES.test(x.name)) { halt = x.name; break; }

  /* a register file: name0, name1, ... from 0 up, all as wide */
  let regs = "", regCount = 0;
  const runs = new Map();
  for (const x of firstByName.values()) {
    const m = /^(.*?[A-Za-z_])(\d+)$/.exec(x.name);
    if (!m || x.w < 2) continue;
    const key = m[1] + "|" + x.w;
    (runs.get(key) || runs.set(key, new Set()).get(key)).add(+m[2]);
  }
  for (const [key, set] of runs) {
    let n = 0;
    while (set.has(n)) n++;
    const prefix = key.slice(0, key.lastIndexOf("|"));
    if (n < 2 || /^(ram|rom|mem|y|q|d|t|s|w|out|in|sel)_?$/i.test(prefix)) continue;
    const bonus = /reg|^r_?$|^x_?$|gpr/i.test(prefix) ? 1000 : 0;
    if (n + bonus > regCount) { regCount = n + bonus; regs = prefix + "%d"; }
  }

  /* other registers worth watching: named, more than one bit, held by flip-flops */
  const skip = new Set([pc, halt]);
  const regRe = regs ? new RegExp("^" + regs.replace("%d", "\\d+").replace(/[.$]/g, "\\$&") + "$") : null;
  const watch = [];
  for (const x of firstByName.values()) {
    if (watch.length >= 16) break;
    if (x.w < 2 || skip.has(x.name) || (regRe && regRe.test(x.name))) continue;
    if (/^(q|d|y|t|l|n|s|w|nq|rest|clk|clock)$/i.test(x.name)) continue;   /* the insides of library parts */
    if (isState(x.i)) watch.push(x.name);
  }

  return {
    memories: mems.map(({ addr, ...m }) => m),
    text: text.id, data, pc, halt, regs, watch
  };
}

/* a first instruction set for a CPU nobody has described yet: the right
 * width, its registers, and a few made-up instructions to change */
function starter(D, info, textBits) {
  const w = textBits || 8;
  const lines = [];
  const name = (D.title || "my_cpu").replace(/[^A-Za-z0-9_]/g, "_").toLowerCase() || "my_cpu";
  lines.push(`# The instructions of ${D.title || "this CPU"}. Change these to what your CPU does:`);
  lines.push(`# each instruction has a format (its fields, most significant bit first)`);
  lines.push(`# and the values of the fields that are fixed. Assembly assembles with it.`);
  lines.push(`#`);
  lines.push(`#   format F = op:4 addr:4         an 8-bit word: 4 bits of opcode, 4 of operand`);
  lines.push(`#   lda arg:u4 = F(op = 0x1)       lda 14  ->  0x1E (an operand fills the field of its name)`);
  lines.push(`#   reg r[0..3]                    registers r0..r3 (operands of kind reg)`);
  lines.push(`#   pseudo nop = add r0, r0        an instruction made of others`);
  lines.push(`#`);
  lines.push(`# An instruction can be longer than one word (8085, 6502): its format is`);
  lines.push(`# then a whole number of words, stored first field first, so an address`);
  lines.push(`# that goes low byte first is written addr[7:0] addr[15:8].`);
  lines.push(``);
  const regLine = info && info.regs ? (() => {
    const pre = info.regs.replace("%d", "");
    let n = 0;
    while (D.nets.some(x => x.name === pre + n)) n++;
    return n >= 2 ? `reg ${/^[A-Za-z_][A-Za-z0-9_.]*$/.test(pre) ? pre : "r"}[0..${n - 1}]` : "";
  })() : "";
  lines.push(`isa "${name}" (width = ${w})`);
  if (regLine) lines.push(regLine);
  lines.push(``);
  if (w >= 8 && w < 16) {
    const ab = w - 4;
    lines.push(`format F = op:4 arg:${ab}`);
    lines.push(`format N = op:4 pad:${ab}`);
    lines.push(``);
    lines.push(`nop        = N(op = 0x0, pad = 0)`);
    lines.push(`lda arg:u${ab} = F(op = 0x1)`);
    lines.push(`add arg:u${ab} = F(op = 0x2)`);
    lines.push(`sub arg:u${ab} = F(op = 0x3)`);
    lines.push(`sta arg:u${ab} = F(op = 0x4)`);
    lines.push(`ldi arg:b${ab} = F(op = 0x5)`);
    lines.push(`jmp arg:u${ab} = F(op = 0x6)`);
    lines.push(`out        = N(op = 0xE, pad = 0)`);
    lines.push(`hlt        = N(op = 0xF, pad = 0)`);
  } else if (w >= 16) {
    const rb = regLine ? Math.max(1, Math.ceil(Math.log2(+/\.\.(\d+)/.exec(regLine)[1] + 1))) : 0;
    if (rb) {
      const imm = w - 6 - 2 * rb;
      lines.push(`format R = op:6 rd:${rb} rs:${rb} pad:${imm}`);
      lines.push(`format I = op:6 rd:${rb} rs:${rb} imm:${imm}`);
      lines.push(``);
      lines.push(`add rd:reg, rs:reg = R(op = 0x01, pad = 0)`);
      lines.push(`sub rd:reg, rs:reg = R(op = 0x02, pad = 0)`);
      lines.push(`addi rd:reg, rs:reg, imm:s${imm} = I(op = 0x03)`);
      lines.push(`ld rd:reg, imm:s${imm}(rs:reg) = I(op = 0x04)`);
      lines.push(`st rd:reg, imm:s${imm}(rs:reg) = I(op = 0x05)`);
      lines.push(`beq rd:reg, rs:reg, imm:rel${imm} = I(op = 0x06)`);
      lines.push(`hlt = R(op = 0x3F, rd = 0, rs = 0, pad = 0)`);
      lines.push(``);
      lines.push(`pseudo nop = add ${/reg (\w+)\[/.exec(regLine)[1]}0, ${/reg (\w+)\[/.exec(regLine)[1]}0`);
    } else {
      const ab = w - 8;
      lines.push(`format F = op:8 arg:${ab}`);
      lines.push(``);
      lines.push(`nop = F(op = 0x00, arg = 0)`);
      lines.push(`lda arg:u${ab} = F(op = 0x01)`);
      lines.push(`add arg:u${ab} = F(op = 0x02)`);
      lines.push(`sta arg:u${ab} = F(op = 0x03)`);
      lines.push(`jmp arg:u${ab} = F(op = 0x04)`);
      lines.push(`hlt = F(op = 0xFF, arg = 0)`);
    }
  } else {
    lines.push(`format F = op:${w}`);
    lines.push(``);
    lines.push(`nop = F(op = 0)`);
  }
  lines.push(``);
  return lines.join("\n");
}

return { detect, starter };
})();
if (typeof module !== "undefined") module.exports = WireCpu;
