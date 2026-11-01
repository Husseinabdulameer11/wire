/* webtest.js - checks that Wire compiled to WebAssembly (make web) does
 * exactly what the native wire does.
 *
 *   node tests/web/webtest.js build/wire build/web/wire.js
 *
 * For every example, every circuit in tests/sim and every file in
 * tests/errors it runs both on the same files and compares, byte for byte:
 * the page (wire -o), the netlist (--json), the VHDL (--to-vhdl), and the
 * error messages and exit codes. Every .vhd file is read as VHDL too
 * (--to-wire and the page, or the same error). The WebAssembly side gets the repository's
 * files in its in-memory file system at the same relative paths, and the
 * standard library in /wire/lib, where the playground puts it. */
"use strict";
const fs = require("fs");
const path = require("path");
const { spawnSync } = require("child_process");

const [native, webjs] = process.argv.slice(2);
if (!native || !webjs) { console.error("usage: node tests/web/webtest.js build/wire build/web/wire.js"); process.exit(2); }
const root = path.join(__dirname, "..", "..");
const WireCompiler = require(path.resolve(webjs));
const wasmBinary = fs.readFileSync(path.resolve(webjs).replace(/\.js$/, ".wasm"));
process.setMaxListeners(0);   /* every instance adds its own handlers */

const walk = (d, keep) => fs.readdirSync(d, { withFileTypes: true }).flatMap(e =>
  e.isDirectory() ? walk(path.join(d, e.name), keep) : keep(e.name) ? [path.join(d, e.name)] : []);
/* every file a circuit may read: .wire, .hex, .vhd, .isa (the instruction set
 * a cpu statement points at) under examples, tests, lib, and the add-ons a
 * page gets (name.addon.js) */
const files = ["examples", "tests", "lib"].flatMap(d => walk(path.join(root, d), n => /\.(wire|hex|vhd|isa)$|\.addon\.js$/.test(n)))
  .map(f => path.relative(root, f).split(path.sep).join("/"));

/* one run of the WebAssembly wire: a fresh instance, the files, the args */
async function web(args) {
  let out = "", err = "";
  /* quit: in Node, Emscripten's exit() would end this whole process */
  const m = await WireCompiler({ wasmBinary, quit: (status, e) => { throw e; },
                                 print: s => { out += s + "\n"; }, printErr: s => { err += s + "\n"; } });
  m.FS.mkdirTree("/r");
  for (const f of files) { m.FS.mkdirTree("/r/" + path.posix.dirname(f)); m.FS.writeFile("/r/" + f, fs.readFileSync(path.join(root, f))); }
  m.FS.mkdirTree("/wire/lib");
  for (const f of fs.readdirSync(path.join(root, "lib"))) m.FS.writeFile("/wire/lib/" + f, fs.readFileSync(path.join(root, "lib", f)));
  m.FS.chdir("/r");
  let code;
  try { code = m.ccall("wire_run", "number", ["string"], [args.join("\n")]); }
  catch (e) { if (e && typeof e.status === "number") code = e.status; else throw e; }
  const outFile = args.indexOf("-o") >= 0 ? args[args.indexOf("-o") + 1] : null;
  let page = null;
  if (outFile) { try { page = Buffer.from(m.FS.readFile(outFile)); } catch (e) { page = null; } }
  return { code, out, err, page };
}

function nat(args) {
  const r = spawnSync(path.resolve(native), args, { cwd: root, encoding: "buffer", maxBuffer: 1 << 28 });
  const outFile = args.indexOf("-o") >= 0 ? path.join(root, args[args.indexOf("-o") + 1]) : null;
  let page = null;
  if (outFile && fs.existsSync(outFile)) { page = fs.readFileSync(outFile); fs.unlinkSync(outFile); }
  return { code: r.status, out: r.stdout.toString(), err: r.stderr.toString(), page };
}

/* the native wire looks for the standard library next to itself, the
 * WebAssembly one next to /wire/bin/wire: the only place their messages may
 * differ (a missing import lists where it looked) */
const exeDir = path.dirname(fs.realpathSync(path.resolve(native))) + path.sep;
const norm = s => s.split(exeDir).join("/wire/bin/");

(async () => {
  const circuits = files.filter(f => f.endsWith(".wire") && !f.includes("/lib/") && !f.startsWith("tests/errors/") && !f.startsWith("tests/install/"));
  const errors = files.filter(f => f.startsWith("tests/errors/") && /\.(wire|vhd)$/.test(f));
  const vhdl = files.filter(f => f.endsWith(".vhd") && !f.startsWith("tests/errors/"));
  let failed = 0, runs = 0;
  const same = (what, f, a, b) => {
    runs++;
    const bad = [];
    if (a.code !== b.code) bad.push(`exit code ${a.code} native, ${b.code} wasm`);
    if (a.out !== b.out) bad.push("stdout differs");
    if (norm(a.err) !== b.err) bad.push(`stderr differs:\n    native: ${a.err.trim().split("\n")[0]}\n    wasm:   ${b.err.trim().split("\n")[0]}`);
    if ((a.page === null) !== (b.page === null) || (a.page && !a.page.equals(b.page))) bad.push("the page differs");
    if (bad.length) { failed++; console.log(`FAIL ${what} ${f}\n  ${bad.join("\n  ")}`); }
  };
  for (const f of circuits) {
    for (const args of [["--json", f], ["--to-vhdl", f], ["-o", "webtest-page.html", f]]) {
      const a = nat(args), b = await web(args);
      same(args[0], f, a, b);
    }
  }
  for (const f of vhdl) {
    for (const args of [["--to-wire", f], ["-o", "webtest-page.html", f]]) {
      const a = nat(args), b = await web(args);
      same(args[0], f, a, b);
    }
  }
  for (const f of errors) {
    const args = ["--check", f];
    same("error", f, nat(args), await web(args));
  }
  if (failed) { console.log(`${failed} of ${runs} runs differ between wasm and native`); process.exit(1); }
  console.log(`wasm: all ${runs} runs match native wire byte for byte (${circuits.length} circuits x page, json, vhdl; ${vhdl.length} VHDL files read; ${errors.length} error messages)`);
})().catch(e => { console.error(e); process.exit(1); });