/* asmtest.js - tests the assembler that any CPU gets (src/wire_asm.js, driven
 * by a .isa file, src/isa.gram) and Wire's `cpu` statement and wire_cpu
 * attribute that point a circuit at it.
 *
 *   node tests/asm/asmtest.js build/wire
 *
 * The RISC-V description is checked the strong way by tests/riscv (every
 * program ends the same in the circuit and in ref.js); this checks the rest:
 * a second, tiny instruction set, what the assembler says when a program is
 * wrong, mistakes in a .isa, and the cpu settings round-tripping through VHDL. */
"use strict";
const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync, spawnSync } = require("child_process");
const root = path.join(__dirname, "..", "..");
const [wireArg] = process.argv.slice(2);
const wire = wireArg && path.resolve(wireArg);
if (!wire) { console.error("usage: node tests/asm/asmtest.js path/to/wire"); process.exit(2); }
const WireAsm = require(path.join(root, "src", "wire_asm.js"));
const isaTool = require(path.join(root, "tools", "isa.js"));

let failures = 0, checks = 0;
const check = (ok, what, detail) => { checks++; if (!ok) { failures++; console.error(`FAIL ${what}${detail ? "\n     " + detail : ""}`); } };
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const hex = ws => ws.map(w => (w >>> 0).toString(16));

/* ---- a second instruction set: tiny8 */
const tiny = isaTool(wire, path.join(__dirname, "tiny8.isa"));
{
  const r = tiny.assemble(`start: clr r1
        ldi  r2, 3
loop:   add  r1, r2          # r1 += r2
        sub  r2, r2
        jz   r2, next        # r2 is 0 after the sub, so on to the next word
next:   jmp  loop
        nop
        hlt
`);
  check(!r.errors.length, "tiny8: a program assembles", JSON.stringify(r.errors));
  check(same(hex(r.text), ["25", "4b", "16", "2a", "99", "82", "30", "f0"]), "tiny8: the words", hex(r.text).join(" "));
  check(r.symbols.get("loop") === 2 && r.symbols.get("next") === 5, "tiny8: labels count words, not bytes");
  const e = tiny.assemble("ldi r1, 9\njmp 99\nadd r1\nmov r1, r7\njz r1, far\n" + "nop\n".repeat(6) + "far: hlt\n");
  const msgs = e.errors.map(x => `${x.line}: ${x.msg}`);
  check(e.errors.length === 5, "tiny8: five wrong lines are five errors", msgs.join(" | "));
  check(/the immediate 9 does not fit in 2 bits \(0 to 3\)/.test(msgs[0] || ""), "tiny8: immediate range", msgs[0]);
  check(/expected add rd, rs/.test(msgs[2] || ""), "tiny8: wrong shape names the form", msgs[2]);
  check(/"r7" is not a register \(r0-r3\)/.test(msgs[3] || ""), "tiny8: register help", msgs[3]);
  check(/too far/.test(msgs[4] || ""), "tiny8: a skip that is too far", msgs[4]);
}

/* ---- RISC-V: what the assembler says */
const rv = isaTool(wire, path.join(root, "examples", "riscv", "rv32im.isa"));
for (const [src, want] of [
  ["addi a0, a0, 5000", "the immediate 5000 does not fit in 12 bits (-2048 to 2047)"],
  ["slli a0, a0, 32", "does not fit in 5 bits (0 to 31)"],
  ["lui a0, 0x100000", "does not fit in 20 bits"],
  ["foo a0", "there is no instruction called foo"],
  ["addi a0, a0", "expected addi rd, rs1, imm"],
  ["add a0, a1, 7", "\"7\" is not a register"],
  ["x: nop\nx: nop", "the label x is defined twice"],
  ["li a0, nolabel", "there is no label or constant called nolabel"],
  ["j 3", "a jump target must be at an even address"],
  ["beq a0, a1, far\n" + "nop\n".repeat(1100) + "far: nop", "too far"],
  [".byte 1", "goes in .data"],
]) {
  const r = rv.assemble(src + "\n");
  check(r.errors.some(e => e.msg.includes(want)), `rv32im: "${src.replace(/\n/g, " / ")}" says "${want}"`, JSON.stringify(r.errors));
}
for (const src of ["fence", "fence rw", "fence rw, rw", "fence iorw, iorw"]) {
  const r = rv.assemble(src + "\n");
  check(!r.errors.length && r.text.length === 1 && r.text[0] >>> 0 === 0x0FF0000F, `rv32im: ${src} is 0x0FF0000F`, JSON.stringify(r.errors) + hex(r.text));
}
{   /* li picks its form in the first pass, and a forward label gets the long one */
  const r = rv.assemble("li a0, 5\nli a1, 0x12345000\nli a2, 0x12345678\nli a3, later\nlater: nop\n");
  check(same(r.listing.map(l => l.addr), [0, 4, 8, 12, 16, 20]), "rv32im: li is 1, 1, 2, 2 words", JSON.stringify(r.listing.map(l => l.addr)));
}

/* ---- mistakes in a .isa: the loader says which line */
const bad = [
  ["isa \"t\" (width = 8)\nreg r[0..3]\nformat RR = op:4 rd:2 rs:2\nadd rd:reg = RR(op = 1)\n", "line 4: add: rs in format RR is neither an operand nor fixed"],
  ["isa \"t\" (width = 8)\nreg r[0..7]\nformat RR = op:4 rd:2 rs:2\nadd rd:reg, rs:reg = RR(op = 1)\n", "register rd needs 3 bits, the field has 2"],
  ["isa \"t\" (width = 8)\nformat RR = op:4 rd:2\nadd = RR(op = 1, rd = 0)\n", "format RR is 6 bits, the instruction is 8"],
  ["isa \"t\" (width = 8)\nformat RR = op:4 rd:4\nadd = RX(op = 1)\n", "there is no format RX"],
  ["isa \"t\" (width = 8)\nformat RR = op:4 rd:4\nadd rd:u4, x:u4 = RR(op = 1)\n", "x is not a field of format RR"],
  ["isa \"t\" (width = 8)\nformat RR = op:4 rd:4\npseudo nop = mov r0, r0\n", "nop becomes mov, which the instruction set does not have"],
];
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "asmtest-"));
bad.forEach(([text, want], i) => {
  const f = path.join(tmp, `bad${i}.isa`);
  fs.writeFileSync(f, text);
  let msg = "loaded";
  try { WireAsm.load(JSON.parse(execFileSync(wire, ["--isa-json", f], { stdio: ["ignore", "pipe", "ignore"] }).toString())); } catch (e) { msg = e.message; }
  check(msg.includes(want), `bad .isa ${i}: "${want}"`, msg);
});
{   /* a syntax mistake is found by wire itself, with the file, line and column */
  const f = path.join(tmp, "syntax.isa");
  fs.writeFileSync(f, "isa \"t\" (width = 8)\nreg r[0..3\n");
  const r = spawnSync(wire, ["--isa-json", f], { encoding: "utf8" });
  check(r.status !== 0 && /syntax\.isa:\d+:\d+: .*syntax error/.test(r.stderr), "a syntax mistake in a .isa has a line", r.stderr);
}

/* ---- the cpu statement and the wire_cpu attribute */
const demo = `circuit "demo"
clock clk (hz = 4)
output pc[4], shown[8]
count = counter(clk, bits = 4)
word = rom(count, bits = 8)
pc = count
shown = word
cpu demo (isa = "tiny8.isa", text = word, pc = count)
`;
fs.copyFileSync(path.join(__dirname, "tiny8.isa"), path.join(tmp, "tiny8.isa"));
fs.writeFileSync(path.join(tmp, "demo.wire"), demo);
const run = (...args) => {   /* wire's stdout is CR LF on Windows (text mode): lines are compared as LF */
  const r = spawnSync(wire, args, { cwd: tmp, encoding: "utf8", maxBuffer: 1 << 26 });
  r.stdout = (r.stdout || "").replace(/\r\n/g, "\n");
  r.stderr = (r.stderr || "").replace(/\r\n/g, "\n");
  return r;
};
{
  const j = run("--json", "demo.wire");
  check(j.status === 0, "a circuit with a cpu statement compiles", j.stderr);
  const d = JSON.parse(j.stdout);
  check(d.cpu && d.cpu.name === "demo" && d.cpu.text === "word" && d.cpu.pc === "count" && d.cpu.width === 8 && d.cpu.isa.tree.t === "isa",
        "the page's data has the cpu settings and the instruction set's tree", JSON.stringify(d.cpu && Object.keys(d.cpu)));
  check(d.cpu.isa.tree.c.every(s => s.l > 0), "each statement of the tree says its line");
  /* ... and the page assembles with it, as the page does */
  const isa = WireAsm.load(d.cpu.isa.tree);
  check(same(hex(WireAsm.assemble(isa, "clr r1\nhlt\n").text), ["25", "f0"]), "the tree from the page's data assembles");

  /* VHDL and back: the cpu statement survives, naming the signals VHDL has */
  const v = run("--to-vhdl", "-o", "demo.vhd", "demo.wire");
  check(v.status === 0, "to VHDL", v.stderr);
  const vhd = fs.readFileSync(path.join(tmp, "demo.vhd"), "utf8").replace(/\r\n/g, "\n");
  check(/attribute wire_cpu of rtl : architecture is "isa=tiny8\.isa; text=shown; pc=pc_i";/.test(vhd), "the VHDL says wire_cpu", vhd.split("\n").filter(l => /wire_cpu/.test(l)).join("\n"));
  const w = run("--to-wire", "demo.vhd");
  check(w.status === 0 && /^cpu demo \(isa = "tiny8\.isa", text = shown, pc = pc_i\)$/m.test(w.stdout), "VHDL back to Wire has the cpu statement", w.stdout.split("\n").filter(l => /^cpu/.test(l)).join("\n") + w.stderr);
}
{   /* the RISC-V computer: its cpu statement comes from the VHDL attribute, and survives the round trip */
  const rvw = fs.readFileSync(path.join(root, "examples", "riscv", "rv32im.wire"), "utf8").replace(/\r\n/g, "\n");
  const line = (rvw.match(/^cpu .*$/m) || [""])[0];
  check(/^cpu rv32im \(isa = "rv32im\.isa", text = instr, data = \{ram_b24, ram_b16, ram_b8, ram_b0\}, data_base = 0x1000, pc = pc, halt = halted, regs = "regs_%d"\)$/.test(line), "rv32im.wire has the cpu statement", line);
  for (const f of ["rv32im.wire", "rv32im.isa"]) fs.copyFileSync(path.join(root, "examples", "riscv", f), path.join(tmp, f));
  const v = run("--to-vhdl", "-o", "rv.vhd", "rv32im.wire");
  check(v.status === 0, "rv32im to VHDL", v.stderr);
  const w = run("--to-wire", "rv.vhd");
  const back = (w.stdout.match(/^cpu .*$/m) || [""])[0];
  check(w.status === 0 && /^cpu rv32im \(isa = "rv32im\.isa", text = instr, data = \{ram_b24, ram_b16, ram_b8, ram_b0\}, data_base = 0x1000, pc = pc_i, halt = halted_i, regs = "regs_%d"\)$/.test(back),
        "rv32im: the cpu statement comes back from VHDL (the signals have their VHDL names)", back + w.stderr);
}
fs.rmSync(tmp, { recursive: true, force: true });

console.log(failures ? `asm: ${failures} of ${checks} checks failed` : `asm: all ${checks} checks pass`);
process.exit(failures ? 1 : 0);
