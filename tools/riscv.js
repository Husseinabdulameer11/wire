#!/usr/bin/env node
/* riscv.js - keeps the RISC-V example (examples/riscv) in step with its
 * sources, the way computer.wire is kept with computer.vhd:
 *
 *   node tools/riscv.js build/wire            writes the three files below
 *   node tools/riscv.js build/wire --check    only says if one is out of date (make test)
 *
 *   rv32im.vhd        its rom and ram get programs/hello.s (between the
 *                     "-- program:" and "-- data:" lines)
 *   rv32im.wire       wire --to-wire rv32im.vhd, with a title, comments, and
 *                     the keypad (VHDL has no keypad: key is a port there)
 *   rv32im.addon.js   what is special about this computer in the page's
 *                     Assembly panel: the programs in programs/ and
 *                     asm/screens.js, in one script (wire puts name.addon.js
 *                     into the page of name.wire). The panel itself and the
 *                     assembler are the page's (src/wire_asmpanel.js,
 *                     src/wire_asm.js), told what to do by rv32im.isa and the
 *                     cpu statement, which comes from rv32im.vhd. */
"use strict";
const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");

const [wire, flag] = process.argv.slice(2);
if (!wire) { console.error("usage: node tools/riscv.js path/to/wire [--check]"); process.exit(2); }
const dir = path.join(__dirname, "..", "examples", "riscv");
const at = f => path.join(dir, f);
const RVAsm = require("./isa.js")(wire, at("rv32im.isa"));
const ROM_WORDS = 1024, RAM_BASE = 0x1000, RAM_BYTES = 4096;

/* the program in the rom: words, and the ram's words from its .data */
function image(file) {
  const r = RVAsm.assemble(fs.readFileSync(at(file), "utf8"), { textBase: 0, dataBase: RAM_BASE });
  if (r.errors.length) throw new Error(r.errors.map(e => `${file}:${e.line}: ${e.msg}`).join("\n"));
  if (r.text.length > ROM_WORDS) throw new Error(`${file}: ${r.text.length} instructions, but the rom holds ${ROM_WORDS}`);
  if (r.data.length > RAM_BYTES) throw new Error(`${file}: ${r.data.length} bytes of data, but the ram holds ${RAM_BYTES}`);
  const words = [];
  for (let i = 0; i < r.data.length; i += 4)
    words.push((r.data[i] | (r.data[i + 1] || 0) << 8 | (r.data[i + 2] || 0) << 16 | (r.data[i + 3] || 0) << 24) >>> 0);
  return { text: r.text, data: words };
}

const hex = w => (w >>> 0).toString(16).toUpperCase().padStart(8, "0");
/* the lines between "-- name: begin" and "-- name: end" replaced */
function fill(vhd, name, words) {
  const lines = vhd.split("\n");
  const b = lines.findIndex(l => l.trim().startsWith(`-- ${name}: begin`)), e = lines.findIndex(l => l.trim() === `-- ${name}: end`);
  if (b < 0 || e < b) throw new Error(`rv32im.vhd has no "-- ${name}: begin" ... "-- ${name}: end" lines`);
  const body = words.map((w, i) => `    ${i} => x"${hex(w)}",`).filter((_, i) => words[i] || name === "program");
  return [...lines.slice(0, b + 1), ...body, ...lines.slice(e)].join("\n");
}

const programs = fs.readdirSync(at("programs")).filter(f => f.endsWith(".s")).sort((a, b) =>
  (a === "hello.s" ? -1 : b === "hello.s" ? 1 : a.localeCompare(b)));
const hello = image("programs/hello.s");
programs.forEach(p => image("programs/" + p));   /* every one must assemble */

let vhd = fs.readFileSync(at("rv32im.vhd"), "utf8");
vhd = fill(fill(vhd, "program", hello.text), "data", hello.data);

/* the Wire, from the VHDL as it will be */
const tmp = fs.mkdtempSync(path.join(require("os").tmpdir(), "riscv-"));
fs.writeFileSync(path.join(tmp, "rv32im.vhd"), vhd);
fs.copyFileSync(at("rv32im.isa"), path.join(tmp, "rv32im.isa"));   /* the cpu statement is checked against it */
let w = execFileSync(wire, ["--to-wire", path.join(tmp, "rv32im.vhd")], { stdio: ["ignore", "pipe", "ignore"], maxBuffer: 1 << 26 }).toString();
fs.rmSync(tmp, { recursive: true });
w = w.replace(/\r\n/g, "\n");   /* on Windows wire's stdout is text mode: CR LF; the file is kept with LF (.gitattributes) */
if (!/^input key\[5\]$/m.test(w)) throw new Error("the Wire of rv32im.vhd has no input key[5] to make a keypad");
if (!/^circuit "rv32im"$/m.test(w)) throw new Error('the Wire of rv32im.vhd has no circuit "rv32im"');
w = w.replace(/^input key\[5\]$/m, "keypad key")
     .replace(/^circuit "rv32im"$/m, `circuit "A RISC-V computer"

# An RV32IM processor (all of RV32I, and RV32M's multiply and divide) with
# its program in a rom, 4 KB of ram, a 64 x 64 colour screen, a 64 x 16
# text screen, a hex display, switches and a keypad. Assembly opens an
# assembler: pick a program or write your own, Assemble and run, and watch
# it on the screens. The rom starts with hello.s. Double-click core to go
# inside the processor; rv32im.vhd beside this file has the memory map.`);
w = `# rv32im.wire: made by \`node tools/riscv.js build/wire\` from rv32im.vhd (the
# design; wire --to-wire, with this title and these comments, and key as a
# keypad). Change rv32im.vhd or programs/hello.s and run it again.

` + w;

/* the add-on: the programs and the screens, in one script */
const progs = {};
programs.forEach(p => { progs[p] = fs.readFileSync(at("programs/" + p), "utf8"); });
const addon = `/* rv32im.addon.js: made by \`node tools/riscv.js build/wire\` from programs/*.s
 * and asm/screens.js. Change those and run it again. wire puts this script
 * into the page of rv32im.wire, after the viewer. */
const RV_PROGRAMS = ${JSON.stringify(progs, null, 1)};
${fs.readFileSync(at("asm/screens.js"), "utf8")}`;

const out = { "rv32im.vhd": vhd, "rv32im.wire": w, "rv32im.addon.js": addon };
const stale = Object.keys(out).filter(f => !fs.existsSync(at(f)) || fs.readFileSync(at(f), "utf8") !== out[f]);
if (flag === "--check") {
  if (stale.length) {
    console.error(`examples/riscv: ${stale.join(", ")} out of date; run node tools/riscv.js build/wire`);
    process.exit(1);
  }
  console.log(`examples/riscv: up to date (${programs.length} programs; hello.s is ${hello.text.length} instructions)`);
} else {
  stale.forEach(f => fs.writeFileSync(at(f), out[f]));
  console.log(stale.length ? `examples/riscv: wrote ${stale.join(", ")}` : "examples/riscv: already up to date");
}
