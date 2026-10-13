#!/usr/bin/env node
/* site.js - makes a folder of pages to put on the web (Netlify Drop,
 * GitHub Pages, any static host): every example circuit as its own page,
 * and an index.html that lists them.
 *
 *   node tools/site.js build/wire build/site      (make site does this)
 *
 * Each page is self-contained: the simulator, the circuit and its
 * programs are inside it. */
"use strict";
const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");

const args = process.argv.slice(2), opt = {};
for (let i = 0; i < args.length; i++) if (args[i].startsWith("--")) opt[args[i].slice(2)] = args[++i]; else (opt._ = opt._ || []).push(args[i]);
const [wire, out = "build/site"] = opt._ || [];
if (!wire) { console.error("usage: node tools/site.js path/to/wire [out-dir] [--book f.pdf --topics f.topics]"); process.exit(2); }
fs.mkdirSync(out, { recursive: true });
/* a book is copied next to the pages once, and every page links to it */
const bookArgs = [];
if (opt.book) {
  const name = path.basename(opt.book);
  fs.copyFileSync(opt.book, path.join(out, name));
  bookArgs.push("--book-url", name);
  if (opt.topics) bookArgs.push("--topics", opt.topics);
}

const root = path.join(__dirname, "..");
const files = fs.readdirSync(path.join(root, "examples")).filter(f => f.endsWith(".wire")).sort();
const pages = [];
for (const f of files) {
  const src = path.join(root, "examples", f), name = f.replace(/\.wire$/, "");
  execFileSync(wire, [...bookArgs, "-o", path.join(out, name + ".html"), src], { stdio: ["ignore", "ignore", "inherit"] });
  const text = fs.readFileSync(src, "utf8");
  const title = (/^\s*circuit\s+"([^"]*)"/m.exec(text) || [])[1] || name;
  /* the first comment paragraph after the title, if any */
  const m = /^\s*circuit[^\n]*\n\s*\n((?:#[^\n]*\n)+)/m.exec(text);
  const about = m ? m[1].split("\n").map(l => l.replace(/^#\s?/, "")).join(" ").split(/\s{2,}|\.\s/)[0].trim() : "";
  pages.push({ name, title, about });
}

/* computers first, then the rest */
const order = p => /^sap/.test(p.name) ? 0 : 1;
pages.sort((a, b) => order(a) - order(b) || a.name.localeCompare(b.name));
const esc = s => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

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
main { max-width: 760px; margin: 0 auto; padding: 48px 16px; }
h1 { font-size: 32px; margin: 0 0 4px; letter-spacing: -.01em; }
p.lead { color: var(--soft); margin: 0 0 32px; }
ul { list-style: none; padding: 0; margin: 0; display: grid; gap: 10px; }
a { display: block; background: var(--card); border: 1.5px solid var(--ink); border-radius: 6px; padding: 14px 16px; color: inherit; text-decoration: none; }
a:hover, a:focus-visible { border-color: var(--hi); box-shadow: 0 0 0 3px color-mix(in srgb, var(--hi) 30%, transparent); outline: none; }
a b { display: block; font-size: 17px; }
a span { color: var(--soft); font-size: 14px; }
code { font-family: ui-monospace, monospace; font-size: 13px; }
footer { margin-top: 32px; color: var(--soft); font-size: 14px; }
</style>
</head>
<body>
<main>
<h1>Wire circuits</h1>
<p class="lead">Digital circuits written in <a style="display:inline;border:0;padding:0;background:none;text-decoration:underline" href="https://github.com/Husseinabdulameer11/wire">Wire</a>, simulated in your browser. Click the inputs, run the clock, open the chips.</p>
<ul>
${pages.map(p => `<li><a href="${p.name}.html"><b>${esc(p.title)}</b>${p.about ? `<span>${esc(p.about)}.</span>` : `<span><code>${p.name}.wire</code></span>`}</a></li>`).join("\n")}
</ul>
<footer>Each page is one self-contained file${opt.book ? `; the <b>Book</b> button reads ${esc(path.basename(opt.book))} beside the circuit and turns to the pages about what you select` : ""}.</footer>
</main>
</body>
</html>
`);
console.log(`wrote ${pages.length} pages and index.html to ${out}`);
