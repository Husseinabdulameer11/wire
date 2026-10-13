; bcd.asm - count from 0 to 99 in decimal with DAA, and add two 4-digit
; decimal numbers (1234 + 5678 = 6912), shown on ports 3 and 4.
        LXI  SP, 0F000H
        XRA  A
        MVI  C, 100
count:  ADI  1             ; one more, kept as two decimal digits
        DAA
        DCR  C
        JNZ  count         ; A = 00 (100 wraps), carry = 1
        STA  9000H
        ; 1234 + 5678, low digits first
        LDA  n1
        LXI  H, n2
        ADD  M
        DAA
        STA  sum
        LDA  n1+1
        INX  H
        ADC  M
        DAA
        STA  sum+1
        OUT  3             ; 69
        LDA  sum
        OUT  4             ; 12
        HLT
n1:     DB   34H, 12H
n2:     DB   78H, 56H
sum:    DS   2
