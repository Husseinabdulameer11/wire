# Books

A Wire page can show a pdf beside the circuit (the **Book** button). With
*Follow selection* on, selecting a part or opening a chip turns the pdf to
the pages about it.

```
wire --book books/fpga_summary.pdf --topics books/fpga_summary.topics examples/sap3.wire
wire --book-url fpga_summary.pdf --topics books/fpga_summary.topics examples/sap3.wire
make site                      # every example, linked to books/fpga_summary.pdf
make site BOOK=books/other.pdf TOPICS=books/other.topics
```

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

## Topics files

One line per section of the pdf, in the order you want them listed:

```
# page[-last]  Title of the section              :  patterns
9              The basic gates                   : and or nand nor xor xnor not buf
13-14          The ALU: in parts and as a whole  : *alu* arith
16             Registers and shift registers     : dff *reg* *shift*
```

- Pages are the pdf's own page numbers (1 is the first page of the file).
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
- The first matching line is the page opened; the others are listed to click.

`fpga_summary.topics` maps the FPGA exam summary (`fpga_summary.pdf`): gates
to Part 0 ch. B, multiplexers, decoders, adders and the ALU to ch. C,
flip-flops, registers and counters to ch. D and Chu ch. 4, RAM and ROM to ch.
E, the SAP computers and their control units to ch. F and G, and so on.
