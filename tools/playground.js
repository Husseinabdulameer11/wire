#!/usr/bin/env node
/* playground.js - makes the playground: Wire itself, compiled to
 * WebAssembly (make web), in a page where you write a circuit and see it.
 *
 *   node tools/playground.js build/web build/site [--wire build/wire] [books]   (make site does this)
 *
 * Writes playground.html (src/playground.html with every example, the files
 * they import, the standard library and the books' topics put in), wire.js
 * and wire.wasm. The books are given as to tools/site.js (--book f.pdf,
 * --book-url u, --book-name n, --topics f, --book-dir d), so the circuits in
 * the playground get the same Book panel as the example pages. */
"use strict";
const fs = require("fs");
const path = require("path");
const { spawnSync } = require("child_process");

/* wire: the native wire, to find which examples' VHDL it reads (those can
 * be opened in the playground's VHDL editor) */
function build(web, out, books = [], wire = null) {
  const root = path.join(__dirname, "..");
  const ex = path.join(root, "examples");
  const walk = (d, keep) => fs.readdirSync(d, { withFileTypes: true }).flatMap(e =>
    e.isDirectory() ? walk(path.join(d, e.name), keep) : keep(e.name) ? [path.join(d, e.name)] : []);

  /* every example and what it reads (its lib/ and rom files, its .isa instruction sets, and the
   * add-on a page of it gets: name.addon.js), at its path
   * under examples/; the standard library at wire/lib/. No .vhd files: the
   * VHDL of a circuit you change is the one Wire makes for it. */
  const files = {};
  for (const f of walk(ex, n => /\.(wire|hex|isa)$|\.addon\.js$/.test(n))) files[path.relative(root, f).split(path.sep).join("/")] = fs.readFileSync(f, "utf8");
  for (const f of walk(path.join(root, "lib"), n => n.endsWith(".wire"))) files["wire/lib/" + path.basename(f)] = fs.readFileSync(f, "utf8");

  /* the books: a pdf next to the page (copied, for --book) and its topics,
   * which the page needs inside wire's file system */
  const bookList = books.map(b => {
    const r = { name: b.name || path.basename(b.book || b.url), url: b.book ? path.basename(b.book) : b.url, dir: b.dir || "" };
    if (b.book) { fs.mkdirSync(out, { recursive: true }); fs.copyFileSync(b.book, path.join(out, r.url)); }
    if (b.topics) { r.topics = "books/" + path.basename(b.topics); files[r.topics] = fs.readFileSync(b.topics, "utf8"); }
    return r;
  });

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
  /* the VHDL beside each example, where Wire reads it */
  let nvhd = 0;
  for (const s of sections) for (const it of s.items) {
    const v = it.path.replace(/\.wire$/, ".vhd"), full = path.join(root, v);
    if (!wire || !fs.existsSync(full)) continue;
    if (spawnSync(wire, ["--check", full], { stdio: "ignore" }).status !== 0) continue;
    files[v] = fs.readFileSync(full, "utf8");
    it.vhd = v;
    nvhd++;
  }
  const rest = Object.keys(files).filter(p => p.startsWith("examples/") && p.endsWith(".wire") && !p.includes("/lib/") && !listed.has(p)).sort();
  if (rest.length) sections.push({ title: "More", items: rest.map(p => ({ path: p, title: title(p) })) });

  /* JSON in a script: keep "</script>" and line separators from ending it */
  const js = v => JSON.stringify(v).replace(/</g, "\\u003c").replace(/\u2028/g, "\\u2028").replace(/\u2029/g, "\\u2029");
  let page = fs.readFileSync(path.join(root, "src", "playground.html"), "utf8");
  for (const [mark, v] of [["/*PLAYGROUND_FILES*/{}", files], ["/*PLAYGROUND_EXAMPLES*/[]", sections], ["/*PLAYGROUND_BOOKS*/[]", bookList]]) {
    if (!page.includes(mark)) throw new Error(`src/playground.html has no ${mark}`);
    page = page.replace(mark, () => js(v));
  }
  fs.mkdirSync(out, { recursive: true });
  fs.writeFileSync(path.join(out, "playground.html"), page);
  for (const f of ["wire.js", "wire.wasm"]) fs.copyFileSync(path.join(web, f), path.join(out, f));
  return { files: Object.keys(files).length, vhd: nvhd };
}

if (require.main === module) {
  const args = process.argv.slice(2), pos = [], books = [];
  let wire = null;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--wire") { wire = args[++i]; continue; }
    const key = { "--book": "book", "--book-url": "url", "--book-name": "name", "--topics": "topics", "--book-dir": "dir" }[args[i]];
    if (key === "book" || key === "url") books.push({ [key]: args[++i] });
    else if (key) { if (!books.length) { console.error(`playground.js: ${args[i]} before any --book`); process.exit(2); } books[books.length - 1][key] = args[++i]; }
    else pos.push(args[i]);
  }
  const [web = "build/web", out = "build/site"] = pos;
  if (!fs.existsSync(path.join(web, "wire.wasm"))) { console.error(`playground.js: no ${web}/wire.wasm; run make web first (it needs Emscripten)`); process.exit(1); }
  const n = build(web, out, books, wire);
  console.log(`wrote playground.html, wire.js and wire.wasm to ${out} (${n.files} example and library files inside, ` +
              `${n.vhd} examples with VHDL Wire reads)`);
}
module.exports = { build };
