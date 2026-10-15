#!/usr/bin/env node
/* playground.js - makes the playground: Wire itself, compiled to
 * WebAssembly (make web), in a page where you write a circuit and see it.
 *
 *   node tools/playground.js build/web build/site      (make site does this)
 *
 * Writes playground.html (src/playground.html with every example, the files
 * they import and the standard library put in), wire.js and wire.wasm. */
"use strict";
const fs = require("fs");
const path = require("path");

function build(web, out) {
  const root = path.join(__dirname, "..");
  const ex = path.join(root, "examples");
  const walk = (d, keep) => fs.readdirSync(d, { withFileTypes: true }).flatMap(e =>
    e.isDirectory() ? walk(path.join(d, e.name), keep) : keep(e.name) ? [path.join(d, e.name)] : []);

  /* every example and what it reads (its lib/ and rom files), at its path
   * under examples/; the standard library at wire/lib/. No .vhd files: the
   * VHDL of a circuit you change is the one Wire makes for it. */
  const files = {};
  for (const f of walk(ex, n => /\.(wire|hex)$/.test(n))) files[path.relative(root, f).split(path.sep).join("/")] = fs.readFileSync(f, "utf8");
  for (const f of walk(path.join(root, "lib"), n => n.endsWith(".wire"))) files["wire/lib/" + path.basename(f)] = fs.readFileSync(f, "utf8");

  /* the example menu, in the order and under the headings of examples/index.txt */
  const sections = [];
  const title = p => (/^\s*circuit\s+"([^"]*)"/m.exec(files[p]) || [])[1] || p;
  const listed = new Set();
  for (const line of fs.readFileSync(path.join(ex, "index.txt"), "utf8").split(/\r?\n/)) {
    const l = line.replace(/#.*$/, "").trim();
    if (!l) continue;
    const h = /^==\s*([^|]*?)\s*(?:\|.*)?$/.exec(l);
    if (h) { sections.push({ title: h[1], items: [] }); continue; }
    const p = "examples/" + l + ".wire";
    if (!files[p]) continue;
    if (!sections.length) sections.push({ title: "Examples", items: [] });
    sections[sections.length - 1].items.push({ path: p, title: title(p) });
    listed.add(p);
  }
  const rest = Object.keys(files).filter(p => p.startsWith("examples/") && p.endsWith(".wire") && !p.includes("/lib/") && !listed.has(p)).sort();
  if (rest.length) sections.push({ title: "More", items: rest.map(p => ({ path: p, title: title(p) })) });

  /* JSON in a script: keep "</script>" and line separators from ending it */
  const js = v => JSON.stringify(v).replace(/</g, "\\u003c").replace(/\u2028/g, "\\u2028").replace(/\u2029/g, "\\u2029");
  let page = fs.readFileSync(path.join(root, "src", "playground.html"), "utf8");
  for (const [mark, v] of [["/*PLAYGROUND_FILES*/{}", files], ["/*PLAYGROUND_EXAMPLES*/[]", sections]]) {
    if (!page.includes(mark)) throw new Error(`src/playground.html has no ${mark}`);
    page = page.replace(mark, () => js(v));
  }
  fs.mkdirSync(out, { recursive: true });
  fs.writeFileSync(path.join(out, "playground.html"), page);
  for (const f of ["wire.js", "wire.wasm"]) fs.copyFileSync(path.join(web, f), path.join(out, f));
  return Object.keys(files).length;
}

if (require.main === module) {
  const [web = "build/web", out = "build/site"] = process.argv.slice(2);
  if (!fs.existsSync(path.join(web, "wire.wasm"))) { console.error(`playground.js: no ${web}/wire.wasm; run make web first (it needs Emscripten)`); process.exit(1); }
  const n = build(web, out);
  console.log(`wrote playground.html, wire.js and wire.wasm to ${out} (${n} example and library files inside)`);
}
module.exports = { build };
