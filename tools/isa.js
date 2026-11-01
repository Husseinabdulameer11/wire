/* isa.js - an assembler for a .isa file, for the tools and tests that run
 * under node: wire reads the file (wire --isa-json, with isa.gram), and
 * src/wire_asm.js, the assembler the page uses, assembles with it.
 *
 *   const asm = require("./isa.js")("build/wire", "examples/riscv/rv32im.isa");
 *   asm.assemble(source, { textBase, dataBase })  ->  { text, data, errors, listing, symbols } */
"use strict";
const path = require("path");
const { execFileSync } = require("child_process");
const WireAsm = require(path.join(__dirname, "..", "src", "wire_asm.js"));

module.exports = function (wire, isaFile) {
  const tree = JSON.parse(execFileSync(wire, ["--isa-json", isaFile], { maxBuffer: 1 << 26 }).toString());
  const isa = WireAsm.load(tree);
  return { isa, assemble: (src, opts) => WireAsm.assemble(isa, src, opts) };
};
