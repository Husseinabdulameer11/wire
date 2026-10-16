# Wire

[![ci](https://github.com/Husseinabdulameer11/wire/actions/workflows/ci.yml/badge.svg)](https://github.com/Husseinabdulameer11/wire/actions/workflows/ci.yml)
[![Open in GitHub Codespaces](https://github.com/codespaces/badge.svg)](https://codespaces.new/Husseinabdulameer11/wire)

**Try it without installing anything:** draw or write your own circuit in
the [playground](https://husseinabdulameer11.github.io/wire/playground.html),
or open any of the [examples](https://husseinabdulameer11.github.io/wire/).
The site is built and published from this repository on every push
(`.github/workflows/pages.yml`).

Wire is a small language for digital circuits, built with
[gram](https://github.com/Husseinabdulameer11/gram). You write which parts
there are and what is connected to what; `wire` checks the circuit (names,
bit widths, every wire driven exactly once) and writes one self-contained
web page that draws it with proper gate shapes, lays it out, and simulates
it. Click the inputs, hold the buttons, run the clock, watch the LEDs and
7-segment displays.

```
circuit "Full adder"

input  a, b, cin
output sum, cout

x1   = xor(a, b)
sum  = xor(x1, cin)
cout = or(and(a, b), and(x1, cin))
```

```
wire full_adder.wire          # writes full_adder.html
wire full_adder.vhd           # VHDL works too (see VHDL in)
```

## Install

**Windows:** download **wire-windows-setup.exe** from the
[latest release](https://github.com/Husseinabdulameer11/wire/releases/latest) and run it.
It installs for your user only (no admin needed) and adds `wire` to your
PATH. Open a *new* terminal afterwards. The Start menu also gets a "Wire
command prompt" and the examples folder.

**macOS and Linux:**

```
curl -fsSL https://raw.githubusercontent.com/Husseinabdulameer11/wire/main/install.sh | sh
```

This puts `wire` in `~/.local/bin`, its standard library in
`~/.local/share/wire/lib` and the examples in `~/.local/share/wire/examples`,
after checking the download's checksum. `WIRE_VERSION=0.1.0` picks a release,
`WIRE_PREFIX=/usr/local` installs somewhere else, and `sh -s -- --uninstall`
removes it again. Prefer doing it by hand? Every release also has plain
`.tar.gz` archives (`bin/wire` and `lib/` side by side).

**Docker**, on any OS. Run it in the folder with your `.wire` files:

```
docker run --rm -v "${PWD}:/work" ghcr.io/husseinabdulameer11/wire adder.wire
docker run --rm -v "${PWD}:/work" ghcr.io/husseinabdulameer11/wire -o page.html adder.wire
```

Then open the `.html` it wrote in your browser. Works the same in
PowerShell, bash and zsh. On Linux, add `-u "$(id -u):$(id -g)"` so the
files Wire writes belong to you, not root.

**GitHub Codespaces:** click the badge above. You get VS Code in the browser
with Wire, gram, Node, GHDL and a C compiler ready, so you can use Wire *and*
change it, and `make test` runs every check.

## The playground

`playground.html` is Wire itself, compiled to WebAssembly, in a page.
Nothing is installed and nothing leaves the browser.

- **Draw:** drag parts onto the circuit (inputs, buttons, clocks, outputs,
  LEDs, hex digits, every gate, a mux, an adder, a comparator, a D
  flip-flop, a counter), drag from pin to pin to wire them, and click a part
  to rename it or change its width, its number of inputs, or whether it has
  an enable, a reset or a carry in. It runs as you build it. A change that
  cannot work (a 4-bit bus into a 1-bit gate) is undone, and the page says
  why. The drawing is written as Wire as you go (**Edit as code** carries on
  from it), so it simulates and turns into VHDL like any other circuit.
- **Write:** write the circuit in Wire, or start from any example (its
  imports and rom files come with it). It is drawn and simulated as you
  type, with the error and its line marked when there is one. The menu next
  to Write switches the editor to **VHDL** (see [VHDL in](#vhdl-in)); the
  examples whose VHDL Wire reads open as VHDL there, and **Save .wire**
  gives the Wire your VHDL turned into.

**Save .vhd** gives the circuit in VHDL for Vivado, **Save .wire** keeps it,
and **Copy link** makes a link that opens the same circuit for someone else.
On a phone, the parts (or code) and the circuit are two tabs: tap a part,
tap where it goes, and wire it with a finger.

It is the same compiler as the `wire` command, byte for byte:
`tests/web/webtest.js` runs both on every example, every circuit in
`tests/sim` and every error in `tests/errors`, and compares the pages, the
netlists, the VHDL and the error messages.

## On the page

- **Inputs**: click a 1-bit input to flip it. Multi-bit inputs show one cell
  per bit (click a cell to flip that bit) and the value underneath (click it
  to type a number: decimal, `0x..` or `0b..`).
- **Buttons** are 1 while you hold them. **Clocks** have Run, Step (one full
  cycle) and a speed control from ½× up to 1M× and Max; space also steps;
  clicking a clock flips it by hand. While it runs fast, the rate actually
  simulated is shown next to the speed.
- **Wires** are bright green for 1, dark green for 0, blue when unknown,
  grey when floating (a bus nobody is driving); buses are drawn thick. Hover or tap a wire to see its name and value.
- **Keypads**: click a key, or type 0-9 and a-f on the keyboard.
- **Memory** shows the contents of every rom and ram, 256 words at a time,
  with the word being read lit up. Click a word to change it; Reset puts the
  contents written in the circuit back. Clicking a rom or ram opens it too.
- If two parts drive a bus at once, the page says **Bus conflict**.
- **VHDL** shows the circuit in VHDL, with the entity of the chip you select
  or open lit up: the `.vhd` file next to the `.wire` file if there is one,
  otherwise the VHDL Wire makes from the circuit (see [VHDL](#vhdl)).
  **Save** downloads it as a `.vhd` file, ready to add to a Vivado project.
- **Book** shows a pdf beside the circuit, and turns to the pages about the
  part you select or the chip you open (see `books/README.md`). A page can
  carry several books (pick one at the top of the panel). Build with
  `wire --book notes.pdf --topics notes.topics`, or use **Open PDF**.
- **Truth table** lists every input combination for circuits without
  memory (up to 12 input bits); click a row to set the inputs to it.
  Select a part (a gate, a mux, an adder...) and the truth table is for
  that part alone; **Show all** (or clicking empty space, or Escape) goes
  back. Inside a chip, the truth table is for that chip on its own: every
  combination of its inputs, with the row it is on now lit. A chip with more
  than 12 input bits gets a "try it" panel instead: type its inputs and see
  its outputs. Chips with memory inside (flip-flops, counters, ram) have
  none; open the chips inside them.
- **Source** shows the `.wire` file. Click a part to find its line, or a
  line to find its part.
- **Chips** (your own components) open when you click them; see below.
- Drag parts to move them (the layout is remembered in the browser), drag
  the background to pan, scroll or pinch to zoom. **Tidy** lays it out again.

## The language

Statements end at the end of the line or at `;`. `#` and `//` start comments.

### Declarations

```
circuit "Name shown on the page"

input  a, b, data[8]      # switches; [n] is the width in bits (default 1)
button go                 # 1 while held
clock  clk (hz = 2)       # square wave, 1 Hz unless given
output y, total[4]        # shows a value; width is inferred if left out
led    ok, err (color = red)      # err is red; red green blue yellow orange white or #rrggbb
seg7   disp               # 7 bits: bit 0 = segment a ... bit 6 = g (8 bits adds the dot)
hex    digit              # a 7-segment display that shows a 4-bit value as 0..F
keypad k                  # 16 keys 0..F: k[3:0] is the last key, k[4] is 1 while held
wire   w[16]              # a named wire, only needed to fix its width
bus    data[8]            # a wire that several tri() outputs may drive
```

`bus` and `keypad` are words of the language, so they cannot be names.

### Connections

Both directions work; use whichever reads better.

```
y = and(a, b)             # y is driven by an AND gate
and(a, b) -> y, led1      # same, and the result goes to two places
n = x1                    # a name for an existing wire
```

A name that is not declared becomes a named wire the first time it is given a
value, and can be used anywhere in the file, before or after that line, so
feedback loops are written directly:

```
q  = nor(r, nq)           # SR latch
nq = nor(s, q)
```

### Operators

| Write      | Means                     |
|------------|---------------------------|
| `~a` `!a`  | NOT                       |
| `a & b`    | AND                       |
| `a ^ b`    | XOR                       |
| `a \| b`   | OR (loosest)              |

`a & b & c` is one 3-input gate. On buses they work bit by bit. Identical
unnamed gates are shared, so writing `~b` in ten places draws one inverter.

### Bits and buses

```
data[3]        # one bit
data[7:4]      # bits 7 down to 4
{hi, lo}       # join wires into a bus, first one is the most significant
disp[0] = ...  # drive single bits of an output; together they must cover it
```

Slices of the same bus are drawn as one splitter, and `{...}` as a merger.

### Numbers

`0b1010` is 4 bits and `0xF0` is 8 (the digits fix the width). A decimal
number like `5` takes whatever width it is used at, and it must fit.

### Components

| Call | Outputs | Notes |
|------|---------|-------|
| `and or nand nor xor xnor (a, b, ...)` | value | 2 to 16 inputs, all the same width |
| `not(a)` `buf(a)` | value | |
| `mux(d0, d1, ..., sel = s)` | value | 2, 4, 8, 16 or 32 inputs; `s` has log2(n) bits |
| `demux(x, sel = s)` | `m[0]`, `m[1]`, ... | one output per value of `s` |
| `decoder(s, en = e)` | one bus of 2^n bits | `d[2]` is line 2; `en` is optional |
| `encoder(x)` | value, `.valid` | priority encoder: index of the highest 1 |
| `add(a, b, cin = c)` | sum, `.cout` | `cin` is optional |
| `compare(a, b)` | `.lt`, `.eq`, `.gt` | unsigned |
| `dff(d, clk, en = e, rst = r)` | q, `.nq` | rising edge; any width (`reg` is the same) |
| `tff(t, clk, rst = r)` | q, `.nq` | |
| `jkff(j, k, clk, rst = r)` | q, `.nq` | |
| `counter(clk, bits = 4, en = e, rst = r)` | q, `.carry` | carry is 1 on the last value |
| `counter(clk, load = l, d = x, down = dn)` | q, `.carry` | `load` takes `d` at the edge (even without `en`); `down` counts down |
| `tri(x, en)` | value | drives x while en is 1, floats while 0; see buses |
| `rom(addr, bits = 8, file = "f.hex")` | q | 2^addr words; contents from `file =` or `data = "..."` |
| `ram(addr, din, we, clk, file = "f.hex")` | q | reads at once; writes din at the rising edge while we is 1 |

Name a component to use its other outputs: `s = add(x, y)` then `s.cout`.
`rst` is asynchronous; flip-flops start at 0.

### Buses

A `bus` is a wire that several `tri()` outputs take turns driving, like the
bus of a breadboard computer. Assign it once per driver:

```
bus data[8]
data = tri(pc_out, CO)          # the PC talks while CO is 1
data = tri(mem, RO)             # the RAM talks while RO is 1
data = a.out                    # a chip whose output is a tri() works too
mar  = reg(data[3:0], clk, en = MI)
```

While nobody drives it, a bus floats (shown grey, `z` in values); parts
read a floating value as unknown. If two drive it at once, it is unknown
and the page warns about a bus conflict.

### Memory contents

`rom` and `ram` take their contents from a file (`file = "program.hex"`,
next to the `.wire` file) or inline (`data = "3E 05 76"`). Both are hex
numbers separated by spaces, commas or new lines, filling words from
address 0; `@10` moves to address 0x10; `#`, `;` and `//` start comments;
`%1010` is binary (`0b1010` would be read as the hex number 0xB1010). Up to 16 address bits (65536 words) and 32 bits a word.

```
@0
1E   # LDA 14
2F   # ADD 15
E0   # OUT
F0   # HLT
@E
05 03
```

## The examples

`examples/` follows two books, each example a `.wire` file with the same
circuit in VHDL beside it (`.vhd`). `examples/index.txt` gives the order.

- **`examples/notes/`**: from my FPGA exam summary (`books/fpga_summary.pdf`),
  gates, the full adder, the adder-subtractor, an ALU, the BCD decoder, the
  SR latch, flip-flops, registers and counters, up to the **SAP-1**
  (`notes/sap1.wire`), built block for block like the VHDL of the notes: the
  W bus as an AND-OR multiplexer, the ring counter T1..T6, the control
  matrix in gates, and HLT as a clock enable. Its program computes
  16 + 20 + 24 - 32 = 28 and halts. The blocks are in `notes/lib/`, one
  component per VHDL entity.
- **`examples/chu/`**: the examples of Pong P. Chu, *FPGA Prototyping by
  VHDL Examples* (Xilinx Spartan-3 version), chapter by chapter: gate-level
  comparators (ch. 1); decoder, priority encoder, adder, hex-to-7-segment,
  sign-magnitude adder, barrel shifter, floating-point adder (ch. 3);
  flip-flops, registers, register file, shift registers, counters, display
  multiplexing, stopwatch, FIFO (ch. 4); FSMs, edge detectors, a debouncer
  (ch. 5); FSMDs: debouncer, Fibonacci, division, binary to BCD, period
  counter (ch. 6); the UART, and one wired to itself (ch. 7); RAM and ROM
  templates (ch. 11). Each file names the listings it follows. The chapters
  on the board's devices (PS/2, VGA, SRAM, PicoBlaze) are left out.

The VHDL is written for this project, in the book's style and with the
book's entity and port names, so each example matches its listing; it is
not copied from the book. Where the book counts to millions (a 50 MHz
clock divided down to milliseconds), both versions count to a few, so the
circuits can be watched; the comments say so.

**How the VHDL is checked:** `tests/vhdl/cosim.js` runs every example in
Wire and its VHDL in [GHDL](https://github.com/ghdl/ghdl) on the same random
inputs, clock edges and resets, and checks that every output agrees before
and after every rising edge (`make test` does this when GHDL is installed).
Ports are matched by name; `# test:` lines in a `.wire` file tune the
stimulus, and `# vhdl ports: a=b` renames a port Wire cannot use (`hex` is a
word of Wire).

## VHDL

Wire writes any circuit as VHDL, so a circuit you design and test here can
go straight into Vivado (or GHDL):

```
wire --to-vhdl adder.wire              # prints it
wire --to-vhdl -o adder.vhd adder.wire # writes adder.vhd
```

A page shows the same VHDL under **VHDL** when there is no `.vhd` file next
to the circuit.

- It is **VHDL-93**, what Vivado reads a `.vhd` file as unless told
  otherwise: it never reads an `out` port, and every process lists its
  signals.
- **Every component is an entity**, instantiated where it is used, and the
  circuit is an entity named like the file. Inputs, buttons, keypads and
  clocks are `in` ports; outputs, LEDs and displays are `out` ports.
- **Names stay yours.** A name that is a VHDL word, or that clashes once
  case is ignored (`Q` and `q`), gets a suffix, and the first lines of the
  file list every port that was renamed (`-- renamed: out=out_s`).
- **It behaves like Wire.** Flip-flops, counters and ram start at 0 (as
  initial values), `rst` is asynchronous, a counter's `load` works without
  `en`, `tri()` drives `'Z'` and a bus is a resolved signal with one driver
  per `tri()`. A bit that nothing drives is stored as 0, as in Wire. Loops of
  gates (an SR latch) start in the state Wire settles to, so they power up
  the same way.
- One difference: when two `tri()`s drive a bus at once, Wire shows the bus
  as unknown (and warns of a bus conflict); VHDL resolves it bit by bit.

The VHDL is made from the circuit, so it reads like a netlist: a signal per
named wire, `n1`, `n2`... for wires with no name. Name the wires you care
about and they keep their names. The handwritten `.vhd` files of the
examples are the readable version of the same circuits.

**It is checked like the examples' VHDL:** `node tests/vhdl/cosim.js
build/wire --generated` makes the VHDL of every example and every circuit in
`tests/sim`, reads it with GHDL as strict VHDL-93, synthesizes it with GHDL
(so it is hardware, not only a simulation), and runs it against Wire on
random inputs and clocks (`make test` does this when GHDL is installed).
GHDL cannot synthesize a tri-state bus inside a design; Vivado can (it turns
it into logic), so those circuits are only simulated.

## VHDL in

Wire also reads VHDL, so a circuit from a lecture or a book can be drawn and
simulated without writing it again:

```
wire alu4.vhd                  # writes alu4.html
wire --to-wire alu4.vhd        # prints the Wire it was turned into
```

It turns the VHDL into Wire and goes on from there, so the checks, the
page, the truth table and the VHDL panel all work as usual (the Source and
VHDL panels show your VHDL, and clicking a part finds its line in it). It
reads the part of VHDL that is wiring, the same subset Vivado turns into
gates:

- **One entity and its architecture.** Ports and signals of `std_logic`,
  `std_logic_vector`, `unsigned`, `signed` and `boolean`, `downto` or `to`;
  integer generics (with their default values) and constants.
- **Concurrent statements:** assignments (`<=`), `when ... else` and
  `with ... select`, each becoming gates, multiplexers, adders and
  comparators.
- **Expressions:** `and or xor nand nor xnor not`, `= /= < <= > >=`, `+ -`
  (with `ieee.numeric_std`, or `std_logic_unsigned`), `&`, bits and slices,
  aggregates like `(others => '0')`, `unsigned() signed()
  std_logic_vector() to_unsigned() to_signed() resize()`, and shifts by a
  constant (`shift_left`, `shift_right`, `sll`, `srl`, `sra`, `rol`, `ror`).

Not yet: processes (so no flip-flops written as `if rising_edge(clk)`),
`generate`, component instances, more than one entity in a file, integer
signals and `type` declarations. A file that uses one says so, at its line:

```
counter.vhd:14:3: error: processes are not supported yet. For now Wire reads
concurrent VHDL: assignments (<=), when ... else and with ... select
```

VHDL's own rules are checked too, with the messages in VHDL's words: widths
must match (`the target is 4 bits, but the value is 8 bits`), `unsigned` is
not `std_logic_vector` without a conversion, `and` and `or` need parentheses
when mixed, a `with ... select` must cover every value, and a `when` without
an `else` is refused (it would be a latch).

**How it is checked:** `node tests/vhdl/cosim.js build/wire --vhdl-in` takes
every `.vhd` file under `examples/` and `tests/vhdl_in/`, turns each one Wire
reads into a circuit, and runs it against the same file in GHDL on random
inputs; every file it does not read must say "not supported yet" (`make test`
does this when GHDL is installed).

## Components and imports

Build your own chips in Wire, then use them like the built-in parts:

```
component half_adder(a, b) -> (s, c) {
    s = a ^ b
    c = a & b
}

component full_adder(a, b, cin) -> (s, cout) {
    h1   = half_adder(a, b)
    h2   = half_adder(h1.s, cin)
    s    = h2.s
    cout = h1.c | h2.c
}
```

- **The header is a contract.** Inputs go in the first list and outputs
  after `->`, each 1 bit unless given a width: `add4(x[4], y[4], cin) ->
  (sum[4], cout)`. Every use must connect exactly those widths, and the body
  must give every output a value of exactly its width. Breaking either side
  is an error at the line that does it:

  ```
  alu.wire:9:15: error: input x of add4 must be 4 bits, but a is 8 bits
  alu.wire:9:1: error: y is 8 bits, but add4 gives 4 (its output sum)
  adders.wire:24:5: error: output sum of add4 is 4 bits, but this is 1 bit
  ```

- **Using one**: `f = full_adder(a, b, c)` gives its first output, and
  `f.cout` the others. Inputs can also be named: `full_adder(a = x, b = y,
  cin = 0)`. A component with one output can be used straight in an
  expression: `y = half_adder(a, b) | z`.
- **Inside**, a component sees only its ports and its own names. Declare
  extra `wire`s if you need to fix a width; inputs, outputs, LEDs and clocks
  belong to the circuit, not to components. Components can use other
  components, but not themselves.
- **Every component is checked**, even ones nothing uses yet.

### Libraries

Put components in their own file and `import` it:

```
import "adders.wire"

s = add4(x, y, 0)
```

A library file holds only components and imports. Wire looks for it next to
the file that imports it, then in each `-I dir` given on the command line,
then in `$WIRE_PATH` (folders separated by `:`, or `;` on Windows), then in
the standard library that ships with Wire (`lib/`, installed to
`share/wire/lib`). Importing the same file twice, by any path, is fine.

The standard library has `adders.wire`: `half_adder`, `full_adder` built from
two half adders, `add4` (ripple carry) and `add8` built from two `add4`s.

### On the page

A component shows as a chip with its name and ports. Click it to go inside:
you see its gates live, with dashed pins for its inputs and outputs, and a
path at the top (Circuit › s add4 › f2 full_adder) to climb back out; Escape
also goes up a level. Source shows the file the component is written in.
Moving parts inside one chip moves them in every use of that component.

## Errors

Wire stops at the first problem and says where it is:

```
alu.wire:7:18: error: every input of and must be 4 bits, but b is 1 bit
alu.wire:9:8: error: add has no output called carry; it has sum and cout
```

`make test` runs the examples, checks the error messages in `tests/errors`,
and (if Node is installed) runs `tests/sim/simtest.js`, which checks the
WebAssembly simulator against the original JavaScript one on every example
and on the circuits in `tests/sim`: both get the same random clicks, clock
ticks and resets, and every wire must agree after every step. It also checks
that the SAP-1 computes 28, and (if GHDL is installed) runs
`tests/vhdl/cosim.js`, which checks every example against its VHDL, and
every circuit against the VHDL Wire makes for it.

## Building from source

Wire needs gram, like Crib does. With a gram checkout next to this folder
(`../gram`), or gram installed (`make install` in gram), just run:

```
make            # build/wire
make test
make site       # every example as a page, plus an index: build/site
make install    # PREFIX=/usr/local
```

`make site` makes a folder to drop onto Netlify Drop (or any static host).
It copies my notes into it, but not Chu's book, which is not ours to
publish: the Book panel asks for your own copy with **Open PDF** (or loads
it if you put it beside the pages as `chu_fpga_prototyping_vhdl.pdf`).

Otherwise `make GRAM=path/to/gram`. On Windows use the MSYS2 UCRT64 shell as
for gram.

Or build the Docker image: `docker build --build-context gram=../gram -t wire .`

The playground needs Wire built for the web, with
[Emscripten](https://emscripten.org) (`emcc`) and gram's source:

```
make web GRAM=../gram       # build/web/wire.js and wire.wasm
make web-test GRAM=../gram  # checks it against the native wire
make site                   # now with playground.html
```

Releases (installer, archives, Docker image) are made by pushing a version
tag; see [RELEASING.md](RELEASING.md).

`wire --json file.wire` prints the netlist, if you want to feed it to
something else.

`node tests/sim/run.js build/wire examples/notes/sap1.wire 200 outp`
runs a circuit's clock without a browser, with inputs set, until it halts,
and prints its outputs.

## How it works

`src/wire.gram` is the grammar. `src/wire.c` loads the file and its
imports, then reads each scope in three passes: every name gets a wire, bit
widths are inferred (only inputs and ports need `[n]`), then expressions
become parts and wires and everything is checked. A component is expanded
where it is used, with pin parts at its edge, so the simulator sees one flat
circuit while the page can still draw each use as a chip and open it.
The netlist is written as JSON into a copy of `src/wire_view.html`, which is
baked into the binary. The page does the rest: a layered layout (columns by
longest path, loops broken at feedback, crossings reduced by barycentre
sweeps) and channel routing with one vertical track per wire.

`src/wire_web.c` is the whole of `wire.c` with one entry point for the
browser, `wire_run("--to-vhdl\nfile.wire")`. The playground makes a fresh
WebAssembly instance for every run (the compiler stops at the first error),
fills its file system with the circuit, the examples and the standard
library, and shows the page Wire writes. In drawing mode it keeps the drawing as parts and
links, writes the Wire for it, and puts `window.WIRE_EDIT` (where each part
goes, the view, the input values) in the page; the page then lets parts be
placed and wired, routes wires straight from pin to pin, and hands every
change back to the playground.

`src/vhdl.gram` and `src/wire_vhdl_in.c` read VHDL: the file is parsed with
gram, checked with VHDL's rules, and turned into Wire that keeps the VHDL's
lines (each statement lands on the line it came from), so every error Wire
finds later, and every part's line on the page, points into the VHDL.

`src/wire_vhdl.c` (part of `wire.c`) writes the VHDL from the same netlist:
one entity per component, from its first use, and the circuit last. To give
loops of gates the right initial values it settles the circuit once in C,
in the same order as the simulator.

The simulator is `src/wire_sim.js`, pasted into the page when Wire is built.
When the page opens it compiles the netlist straight into a WebAssembly
module (it writes the bytes itself; no toolchain is involved):

- Every wire's value and known-mask, and every part's state, live in one
  WebAssembly memory. The page reads that memory to draw.
- The logic is put in order once. A flip-flop's outputs depend at once only
  on `rst`, so the logic between flip-flops becomes straight-line code that
  settles in one pass. Loops made of gates (an SR latch) are found and only
  they are repeated until nothing changes; after 200 passes the page says
  the circuit oscillates.
- Pins at the edge of a chip share one memory slot with the wire outside,
  so chips cost nothing extra.
- Every wire is a value and a known mask; a bit that is not known but has
  value 1 is floating (z). `tri()` makes it, pins, `buf`, splits and merges
  pass it on, and a bus part, which Wire adds where several `tri()`s meet,
  works out who is talking. rom and ram words live in the same memory.
- Running the clock calls `toggle(n)` once per animation frame, which does
  n clock flips inside WebAssembly, and the page is drawn once per frame.

In the browser console, `wireSim.dump()` shows where each wire lives in
memory and the order the logic runs in.

## License

MIT, see `LICENSE`.
