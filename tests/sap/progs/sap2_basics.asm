; sap2_basics.asm - the SAP-2's instructions: a count, a subroutine,
; logic and rotates, the sign flag. Ends with 3 on port 3, 9 on port 4.
; sap2
; inputs: in1=7 in2=2
        IN   1
        MOV  B, A          ; B = 7
        IN   2
        MOV  C, A          ; C = 2
        CALL twice         ; A = 2 * B = 14
        SUB  B             ; 7
        ANI  0FH
        ORI  10H           ; 17H
        XRI  14H           ; 03H
        OUT  3
        MVI  A, 81H
        RAL                ; 03H (the SAP-2 rotates without a carry)
        RAR                ; 81H
        CMA                ; 7EH
        DCR  C
        DCR  C             ; C = 0: zero
        JZ   zero
        HLT
zero:    MVI  A, 1
        DCR  A
        DCR  A             ; FF: minus
        JM   neg
        HLT
neg:     MOV  A, B
        INR  A
        INR  A             ; 9
        OUT  4
        STA  9000H
        LDA  9000H
        HLT
twice:  MOV  A, B
        ADD  B
        RET
