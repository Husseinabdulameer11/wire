# Books

A Wire page can show a pdf beside the circuit (the **Book** button). With
*Follow selection* on, selecting a part or opening a chip turns the pdf to
the pages about it.

```
wire --book books/fpga_summary.pdf --topics books/fpga_summary.topics examples/notes/sap1.wire
wire --book-url fpga_summary.pdf --topics books/fpga_summary.topics examples/notes/sap1.wire
wire --book-url fpga_summary.pdf --book-name "My notes" --topics books/fpga_summary.topics \
     --book-url chu.pdf --book-name Chu --topics books/chu.topics examples/chu/ch6/fib.wire
make site                      # every example, with both books
```

Each `--book` or `--book-url` starts a book; `--book-name` and `--topics`
after it belong to it. With two or more, the panel has a menu to switch,
and the list of sections shows the pages of every book.

- `--book file.pdf` puts the pdf inside the page (one file, works anywhere,
  about 1.3 times the pdf's size).
- `--book-url name.pdf` only links to it: a file next to the page, or a web
  address. `make site` copies the pdf into the site once and links every page
  to it. Browsers do not let a page opened straight from disk (file://) read
  a file next to it, so there the panel asks you to pick the pdf with
  **Open PDF**; on a web server (Netlify and so on) it just loads.
- **Open PDF** reads any pdf. Without a topics file for it, the page searches
  the pdf's text for the part (NAND, multiplexer, flip-flop, adder ...) and
  lists the pages that mention it most.

The pdf is drawn with PDF.js, loaded from cdnjs the first time the book is
opened, so reading needs an internet connection; the circuit does not.

## Contents

The panel's **Contents** menu lists the book's sections and turns to the
one you pick. It comes from the pdf's own bookmarks when it has them (my
notes do), otherwise from the numbered sections of the topics file
(`3.7.4 Simplified floating-point adder`, `Chapter 3: ...`), which is how
Chu's book, which has no bookmarks, gets one: `chu.topics` holds its whole
table of contents.

## Topics files

One line per section of the pdf, in the order you want them listed:

```
# page[-last]  Title of the section              :  patterns
9              The basic gates                   : and or nand nor xor xnor not buf
13-14          The ALU: in parts and as a whole  : *alu* arith
16             Registers and shift registers     : dff *reg* *shift*
```

- Pages are the pdf's own page numbers (1 is the first page of the file).
- A line with nothing after the colon is a section no part leads to; it
  still shows in the book's **Contents** menu.
- The patterns come after the last ` : ` (a colon with a space before it), so
  titles may contain colons.
- A pattern matches a part's kind (`and`, `nand`, `mux`, `decoder`, `add`,
  `compare`, `dff`, `tff`, `jkff`, `counter`, `tri`, `bus`, `rom`, `ram`,
  `seg7`, `hex`, `keypad`, `button`, `clock`, ...), its name, the name of a
  chip (the component) or of its instance, or, for the whole circuit, the
  `.wire` file's name. `*` matches anything: `*reg*` matches `register`,
  `registers`, `reg4` and `flagreg`. Case does not matter.
- What is matched: the selected part; with nothing selected, the chip you are
  in; at the top, the circuit.
- A line naming the thing exactly comes before one that matches only with
  `*`; among those, the first in the file is the page opened. The others are
  listed to click.

`chu.topics` maps Pong P. Chu, *FPGA Prototyping by VHDL Examples* (Xilinx
Spartan-3 version): first every listing an example follows, then every
section of the book from its table of contents (printed page + 31 = pdf
page), with the parts and examples each one is about. The book itself is not here; open your
copy in the panel.

`fpga_summary.topics` maps the FPGA exam summary (`fpga_summary.pdf`): gates
to Part 0 ch. B, multiplexers, decoders, adders and the ALU to ch. C,
flip-flops, registers and counters to ch. D and Chu ch. 4, RAM and ROM to ch.
E, the SAP-1 and its control unit to ch. F and G, and so on.

## Checking a topics file

```
node tools/check-topics.js book.pdf book.topics
make check-books CHU=path/to/your/chu.pdf
```

reads the pdf's text (with `pdftotext`) and complains about a section
whose first page is blank, is a chapter's title page, is past the end,
doesn't hold the listing its title names, or holds few of its title's
words. Page numbers in a topics file are the pdf's, which in a printed
book are not the numbers on the pages.
