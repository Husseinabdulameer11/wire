#!/usr/bin/env node
/* site.js - makes a folder of pages to put on the web (Netlify Drop,
 * GitHub Pages, any static host): every example circuit as its own page,
 * and an index.html that lists them under the headings of
 * examples/index.txt.
 *
 *   node tools/site.js build/wire build/site [books]      (make site does this)
 *
 * Books, each started by --book or --book-url, then its options:
 *   --book f.pdf       copied into the site; every page links to it
 *   --book-url u       only linked (a file you put beside the pages, or a
 *                      web address); a missing file just asks for Open PDF
 *   --book-name n      what the page calls it
 *   --topics f         its topics file
 *   --book-dir d       list it first on the pages of examples/d/...
 *
 * Each page is self-contained: the simulator, the circuit, its VHDL and
 * its programs are inside it. */
"use strict";
const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");

const args = process.argv.slice(2), pos = [], books = [];
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  const key = { "--book": "book", "--book-url": "url", "--book-name": "name", "--topics": "topics", "--book-dir": "dir" }[a];
  if (key === "book" || key === "url") books.push({ [key]: args[++i] });
  else if (key) {
    if (!books.length) { console.error(`site.js: ${a} before any --book`); process.exit(2); }
    books[books.length - 1][key] = args[++i];
  } else pos.push(a);
}
const [wire, out = "build/site"] = pos;
if (!wire) { console.error("usage: node tools/site.js path/to/wire [out-dir] [--book f.pdf | --book-url u] [--book-name n] [--topics f] [--book-dir d] ..."); process.exit(2); }
fs.mkdirSync(out, { recursive: true });

/* a book given as a file is copied next to the pages once */
for (const b of books) if (b.book) {
  b.url = path.basename(b.book);
  fs.copyFileSync(b.book, path.join(out, b.url));
}
const bookArgs = rel => {
  const first = books.filter(b => b.dir && (rel + "/").startsWith(b.dir.replace(/\/?$/, "/")));
  return [...first, ...books.filter(b => !first.includes(b))].flatMap(b => [
    "--book-url", b.url, ...(b.name ? ["--book-name", b.name] : []), ...(b.topics ? ["--topics", b.topics] : [])]);
};

const root = path.join(__dirname, "..");
const ex = path.join(root, "examples");
/* every .wire under examples/, except libraries (lib/ folders) */
const walk = d => fs.readdirSync(d, { withFileTypes: true }).flatMap(e =>
  e.isDirectory() ? (e.name === "lib" ? [] : walk(path.join(d, e.name))) : e.name.endsWith(".wire") ? [path.join(d, e.name)] : []);
const all = walk(ex).map(f => path.relative(ex, f).replace(/\\/g, "/").replace(/\.wire$/, "")).sort();

/* sections from examples/index.txt; the rest under "More" */
const sections = [], listed = new Set();
const idx = path.join(ex, "index.txt");
if (fs.existsSync(idx)) for (const line of fs.readFileSync(idx, "utf8").split(/\r?\n/)) {
  const l = line.replace(/#.*$/, "").trim();
  if (!l) continue;
  const h = /^==\s*([^|]*?)\s*(?:\|\s*(.*))?$/.exec(l);
  if (h) { sections.push({ title: h[1], about: h[2] || "", items: [] }); continue; }
  if (!all.includes(l)) { console.error(`site.js: examples/index.txt lists ${l}, which is not an example`); process.exit(1); }
  if (!sections.length) sections.push({ title: "Examples", about: "", items: [] });
  sections[sections.length - 1].items.push(l);
  listed.add(l);
}
const rest = all.filter(r => !listed.has(r));
if (rest.length) sections.push({ title: "More", about: "", items: rest });

let n = 0;
for (const s of sections) s.items = s.items.map(rel => {
  const src = path.join(ex, rel + ".wire"), page = rel.replace(/\//g, "-") + ".html";
  execFileSync(wire, [...bookArgs(rel), "-o", path.join(out, page), src], { stdio: ["ignore", "ignore", "inherit"] });
  n++;
  const text = fs.readFileSync(src, "utf8");
  const title = (/^\s*circuit\s+"([^"]*)"/m.exec(text) || [])[1] || rel;
  /* the first sentence of the first comment paragraph after the title */
  const m = /^\s*circuit[^\n]*\n\s*\n((?:#[^\n]*\n)+)/m.exec(text);
  const about = m ? m[1].split("\n").map(l => l.replace(/^#\s?/, "")).join(" ").replace(/\s+/g, " ").split(/(?<=\.)\s/)[0].trim() : "";
  const short = about.length > 150 ? about.slice(0, 150).replace(/\s+\S*$/, "") + " …" : about;
  const vhdl = fs.existsSync(src.replace(/\.wire$/, ".vhd"));
  return { rel, page, title, about: short, vhdl };
});

const esc = s => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
const bookNote = books.length ? ` The <b>Book</b> button opens ${books.map(b => esc(b.name || b.url)).join(" and ")} beside the circuit, at the pages about what you select${books.some(b => !b.book) ? " (a book that is not in the site asks for your own copy with <b>Open PDF</b>)" : ""}.` : "";

fs.writeFileSync(path.join(out, "index.html"), `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Wire circuits</title>
<style>
:root { --paper: #f3f6f8; --ink: #1d2b3a; --soft: #5b6b7c; --line: #c9d3dc; --hi: #e09a12; --card: #fff; }
@media (prefers-color-scheme: dark) { :root { --paper: #121a23; --ink: #d4dde7; --soft: #8797a8; --line: #2c3a4a; --card: #18222d; --hi: #f0b23a; } }
* { box-sizing: border-box; }
body { margin: 0; background: var(--paper); color: var(--ink); font: 16px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif;
  background-image: linear-gradient(var(--line) 1px, transparent 1px), linear-gradient(90deg, var(--line) 1px, transparent 1px);
  background-size: 50px 50px; background-attachment: fixed; }
main { max-width: 860px; margin: 0 auto; padding: 48px 16px; }
h1 { font-size: 32px; margin: 0 0 4px; letter-spacing: -.01em; }
h2 { font-size: 21px; margin: 40px 0 2px; }
p.lead { color: var(--soft); margin: 0 0 8px; }
p.about { color: var(--soft); margin: 0 0 12px; font-size: 15px; }
nav { display: flex; flex-wrap: wrap; gap: 6px 14px; margin: 16px 0 0; font-size: 14px; }
nav a { color: var(--ink); }
ul { list-style: none; padding: 0; margin: 0; display: grid; gap: 10px; grid-template-columns: repeat(auto-fill, minmax(250px, 1fr)); }
li a { display: block; height: 100%; background: var(--card); border: 1.5px solid var(--ink); border-radius: 6px; padding: 12px 14px; color: inherit; text-decoration: none; }
li a:hover, li a:focus-visible { border-color: var(--hi); box-shadow: 0 0 0 3px color-mix(in srgb, var(--hi) 30%, transparent); outline: none; }
li b { display: block; font-size: 16px; }
li span { display: block; color: var(--soft); font-size: 13.5px; margin-top: 2px; }
li code { font-family: ui-monospace, monospace; font-size: 12px; color: var(--soft); }
footer { margin-top: 40px; color: var(--soft); font-size: 14px; }
</style>
</head>
<body>
<main>
<h1>Wire circuits</h1>
<p class="lead">Digital circuits written in <a href="https://github.com/Husseinabdulameer11/wire">Wire</a> and simulated in your browser, following my FPGA notes and the examples of Pong P. Chu's <i>FPGA Prototyping by VHDL Examples</i>. Click the inputs, run the clock, open the chips; <b>VHDL</b> shows the same circuit in VHDL, checked against it with GHDL.</p>
<nav>${sections.map((s, i) => `<a href="#s${i}">${esc(s.title.replace(/^Chu, chapter (\d+):.*/, "Chu ch. $1").replace(/,.*$/, ""))}</a>`).join("")}</nav>
${sections.map((s, i) => `<h2 id="s${i}">${esc(s.title)}</h2>
${s.about ? `<p class="about">${esc(s.about)}</p>\n` : ""}<ul>
${s.items.map(p => `<li><a href="${p.page}"><b>${esc(p.title)}</b>${p.about ? `<span>${esc(p.about)}</span>` : ""}<code>${esc(p.rel)}.wire${p.vhdl ? " · .vhd" : ""}</code></a></li>`).join("\n")}
</ul>`).join("\n")}
<footer>Each page is one self-contained file.${bookNote} The VHDL is written for these pages in the style of the book; the listings it follows are named in each file.</footer>
</main>
</body>
</html>
`);
console.log(`wrote ${n} pages and index.html to ${out}`);
