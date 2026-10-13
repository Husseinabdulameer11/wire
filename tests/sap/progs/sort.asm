; sort.asm - bubble sort 12 bytes in place, smallest first.
        LXI  SP, 0F000H
again:  MVI  B, 0          ; nothing swapped yet
        LXI  H, list
        MVI  C, 11         ; compare 11 pairs
pass:   MOV  A, M
        INX  H
        CMP  M             ; A - next: carry if A < next
        JC   next
        JZ   next
        MOV  D, M          ; swap the pair
        MOV  M, A
        DCX  H
        MOV  M, D
        INX  H
        MVI  B, 1
next:   DCR  C
        JNZ  pass
        MOV  A, B
        ORA  A
        JNZ  again
        LDA  list
        OUT  3             ; the smallest: 02
        LDA  list+11
        OUT  4             ; the largest: F7
        HLT
list:   DB   37H, 0F7H, 02H, 90H, 4AH, 4AH, 11H, 0C3H, 08H, 66H, 0A1H, 25H
