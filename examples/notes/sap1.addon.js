/* sap1.addon.js - programs for the SAP-1's Assembly panel. The panel and
 * the instructions (sap1.isa, next to the circuit) do the rest. */
(function () {
"use strict";
wirePage.asm.programs = {
  "notes.s": `# The program of my notes: 16 + 20 + 24 - 32 = 28 (1C hex) on OUT, then stop.
        lda a
        add b
        add c
        sub d
        out
        hlt

        .data
a:      .byte 16
b:      .byte 20
c:      .byte 24
d:      .byte 32
`,
  "double.s": `# 7 + 7 = 14 (0E hex): add reads the same byte lda did.
        lda x
        add x
        out
        hlt

        .data
x:      .byte 7
`
};
})();
