/* computer.addon.js - programs for the Assembly panel of the computer that
 * draws (its instructions: computer.isa, next to it). Run them at Max. */
(function () {
"use strict";
wirePage.asm.programs = {
  "line.s": `# The program in the rom: the screen blue, then a red line from the
# bottom right corner to the top left, and stop.
fill:   pix  1          # blue
        adi  1
        jz   line       # A went 255 -> 0: the screen is full
        jmp  fill
line:   sbi  1          # A = 255, the bottom right
step:   pix  4          # red
        sbi  15         # 17 less: one up and one left
        sbi  2
        jz   last
        jmp  step
last:   pix  4
        hlt
`,
  "stripes.s": `# Stripes of red, green and blue that move: 256 pixels is not a whole
# number of stripes, so each pass starts one colour later.
loop:   pix  4
        adi  1
        pix  2
        adi  1
        pix  1
        adi  1
        jmp  loop
`
};
wirePage.asm.speed = "max";
})();
