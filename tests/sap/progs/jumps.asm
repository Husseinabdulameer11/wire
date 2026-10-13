; jumps.asm - RST 0, PCHL, SPHL, nested CALL and RET, and every
; conditional return, taken and not. Writes a trail of bytes from 9000H.
        ORG  0
        LDA  booted        ; RST 0 also comes here: was the program started?
        ORA  A
        JNZ  rst0
        MVI  A, 1
        STA  booted
        JMP  main
rst0:   MVI  A, 0AAH       ; the RST 0 handler
        CALL mark
        RET

        ORG  100H
main:   LXI  SP, 0F000H
        LXI  H, 9000H
        SHLD trail
        RST  0             ; marks AA
        LXI  H, there      ; PCHL jumps to HL
        PCHL
        MVI  A, 0EEH       ; never runs
        CALL mark
there:  MVI  A, 0BBH
        CALL mark
        LXI  H, 0E000H     ; SPHL: a new stack
        SPHL
        CALL nest1         ; marks 11, 22, 33
        LXI  SP, 0F000H
        ; conditional returns: each sub returns early when its condition holds
        XRA  A             ; Z=1, P=1 (even), S=0, CY=0
        CALL t_rz            ; returns early: marks nothing
        CALL t_rnz           ; does not: marks 1
        STC
        CALL t_rc            ; early
        CALL t_rnc           ; marks 2
        MVI  A, 80H
        ORA  A             ; S=1, P=0 (odd)
        CALL t_rm            ; early
        CALL t_rp            ; marks 3
        CALL t_rpo           ; early
        CALL t_rpe           ; marks 4
        HLT

mark:   PUSH H             ; store A at the next byte of the trail
        LHLD trail
        MOV  M, A
        INX  H
        SHLD trail
        POP  H
        RET
nest1:  MVI  A, 11H
        CALL mark
        CALL nest2
        RET
nest2:  MVI  A, 22H
        CALL mark
        CALL nest3
        RET
nest3:  MVI  A, 33H
        CALL mark
        RET
t_rz:     RZ
        PUSH PSW
        MVI  A, 0FFH
        CALL mark
        POP  PSW
        RET
t_rnz:    RNZ
        PUSH PSW
        MVI  A, 1
        CALL mark
        POP  PSW
        RET
t_rc:     RC
        PUSH PSW
        MVI  A, 0FFH
        CALL mark
        POP  PSW
        RET
t_rnc:    RNC
        PUSH PSW
        MVI  A, 2
        CALL mark
        POP  PSW
        RET
t_rm:     RM
        PUSH PSW
        MVI  A, 0FFH
        CALL mark
        POP  PSW
        RET
t_rp:     RP
        PUSH PSW
        MVI  A, 3
        CALL mark
        POP  PSW
        RET
t_rpo:    RPO
        PUSH PSW
        MVI  A, 0FFH
        CALL mark
        POP  PSW
        RET
t_rpe:    RPE
        PUSH PSW
        MVI  A, 4
        CALL mark
        POP  PSW
        RET

        ORG  8000H
booted: DB   0
trail:  DW   0
