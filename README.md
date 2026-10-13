# Wire

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
```

## On the page

- **Inputs**: click a 1-bit input to flip it. Multi-bit inputs show one cell
  per bit (click a cell to flip that bit) and the value underneath (click it
  to type a number: decimal, `0x..` or `0b..`).
- **Buttons** are 1 while you hold them. **Clocks** have Run, Step (one full
  cycle) and a speed control from ½× up to 1M× and Max; space also steps;
  clicking a clock flips it by hand. While it runs fast, the rate actually
  simulated is shown next to the speed.
- **Wires** are bright green for 1, dark green for 0, blue when unknown;
  buses are drawn thick. Hover or tap a wire to see its name and value.
- **Truth table** lists every input combination for circuits without
  memory (up to 12 input bits); click a row to set the inputs to it.
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
wire   bus[16]            # a named wire, only needed to fix its width
```

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

Name a component to use its other outputs: `s = add(x, y)` then `s.cout`.
`rst` is asynchronous; flip-flops start at 0.

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
ticks and resets, and every wire must agree after every step.

## Building

Wire needs gram, like Crib does. With a gram checkout next to this folder
(`../gram`), or gram installed (`make install` in gram), just run:

```
make            # build/wire
make test
make install    # PREFIX=/usr/local
```

Otherwise `make GRAM=path/to/gram`. On Windows use the MSYS2 UCRT64 shell as
for gram.

`wire --json file.wire` prints the netlist, if you want to feed it to
something else.

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
- Running the clock calls `toggle(n)` once per animation frame, which does
  n clock flips inside WebAssembly, and the page is drawn once per frame.

In the browser console, `wireSim.dump()` shows where each wire lives in
memory and the order the logic runs in.

## License

MIT, see `LICENSE`.
