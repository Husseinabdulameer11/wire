#!/usr/bin/env node
/* check-topics.js - checks a topics file against its pdf.
 *
 *   node tools/check-topics.js book.pdf book.topics
 *
 * For every section it reads the pages (with pdftotext, from poppler) and
 * complains when the first page is blank or nearly so, is past the end, or
 * is a chapter's title page, or when a title naming "Listing 6.4" points
 * at pages where that listing is not, or when few of the title's words are
 * on those pages (a sign the section is elsewhere). Exit status 1 if
 * anything is wrong. */
"use strict";
const fs = require("fs");
const { execFileSync, spawnSync } = require("child_process");

const [pdf, topics] = process.argv.slice(2);
if (!pdf || !topics) { console.error("usage: node tools/check-topics.js book.pdf book.topics"); process.exit(2); }
if (spawnSync("pdftotext", ["-v"]).error) { console.error("check-topics: needs pdftotext (poppler-utils)"); process.exit(2); }

const pages = execFileSync("pdftotext", ["-layout", pdf, "-"], { maxBuffer: 1 << 28 }).toString().split("\f");
const n = pages.length - (pages[pages.length - 1].trim() ? 0 : 1);
const flat = s => s.replace(/\s+/g, " ").replace(/(\b\w) (?=\w\b)/g, "$1");   /* "L i s t i n g" in some scans */
let bad = 0, lines = 0;
fs.readFileSync(topics, "utf8").split(/\r?\n/).forEach((line, i) => {
  const m = /^\s*(\d+)(?:\s*-\s*(\d+))?\s+(.*)\s+:\s+(.+?)\s*$/.exec(line.replace(/#.*$/, ""));
  if (!m) return;
  lines++;
  const first = +m[1], last = +(m[2] || m[1]), title = m[3].trim();
  const say = msg => { bad++; console.log(`${topics}:${i + 1}: p.${first}${last !== first ? "-" + last : ""} "${title}": ${msg}`); };
  if (last < first) return say("the range is backwards");
  if (last > n) return say(`the pdf has ${n} pages`);
  const text = pages[first - 1], words = text.split(/\s+/).filter(Boolean).length;
  if (/intentionally left blank/i.test(text) || words < 8) return say(`page ${first} is blank`);
  if (/^\s*CHAPTER\s+\d+\b/.test(text) && !/^Chapter|chapter \d/.test(title)) say(`page ${first} is a chapter's title page; is the section later?`);
  const range = pages.slice(first - 1, last).map(flat).join(" ").toLowerCase();
  const words4 = title.replace(/Listings? [\d.,\s]+:?/g, "").toLowerCase().match(/[a-z][a-z-]{3,}/g) || [];
  const found = words4.filter(w => range.includes(w.replace(/s$/, ""))).length;
  if (words4.length >= 3 && found / words4.length < 0.5)
    say(`only ${found} of the title's ${words4.length} words are on these pages: ${words4.filter(w => !range.includes(w.replace(/s$/, ""))).join(", ")}`);
  for (const l of title.matchAll(/Listings? ([\d.]+(?:,\s*[\d.]+)*)/g))
    for (const id of l[1].split(/,\s*/)) {
      if (!new RegExp(`listing ${id.replace(".", "\\.")}\\b`).test(range)) {
        const at = pages.findIndex(p => new RegExp(`^\\s*Listing ${id.replace(".", "\\.")}\\s`, "m").test(p));
        say(`Listing ${id} is not on these pages${at >= 0 ? ` (it starts on page ${at + 1})` : ""}`);
      }
    }
});
console.log(bad ? `${bad} problem(s) in ${lines} sections` : `all ${lines} sections point at real pages`);
process.exit(bad ? 1 : 0);
