; mul16.asm - multiply two 8-bit inputs into a 16-bit result with shifts
; and DAD: HL = in1 * in2. Shows the high byte on port 3, low on port 4.
; inputs: in1=200 in2=123
        LXI  SP, 0F000H
        IN   1
        MOV  E, A
        MVI  D, 0          ; DE = in1, doubled each round
        IN   2
        MOV  C, A          ; C = in2, one bit at a time
        LXI  H, 0
        MVI  B, 8
bit:    MOV  A, C
        RAR                ; next bit of in2 into the carry
        MOV  C, A
        JNC  skip
        DAD  D             ; add the shifted in1
skip:   XCHG
        DAD  H             ; DE = DE * 2
        XCHG
        DCR  B
        JNZ  bit
        MOV  A, H
        OUT  3
        MOV  A, L
        OUT  4             ; 200 * 123 = 24600 = 6018H
        SHLD 9000H
        HLT
