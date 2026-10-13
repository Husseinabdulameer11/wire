; square.asm - (in1 + in2) squared, shown on port 3.
; For the SAP-2 and SAP-3. The answer fits in 8 bits while in1 + in2 <= 15.
; sap2
; inputs: in1=3 in2=4

        IN   1          ; A = the first input
        MOV  B, A
        IN   2          ; A = the second input
        ADD  B          ; A = in1 + in2
        MOV  B, A       ; B = the sum, to add again and again
        MOV  C, A       ; C = how many times
        MVI  A, 0
loop:   ADD  B          ; A = A + sum, C times
        DCR  C
        JNZ  loop
        OUT  3
        HLT
