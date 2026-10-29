# Fibonacci: every third Fibonacci number, in decimal, a row each. The
# digits come from divu and remu (RV32M); the program stops at the end.
        .text
main:   li   sp, 0x2000
        li   s0, 0               # n
        li   s1, 0               # F(n)
        li   s2, 1               # F(n + 1)
        li   s3, 0x3000          # the start of the row
        li   s4, 0x4000
row:    mv   a1, s3
        li   t0, 'F'
        sb   t0, 0(a1)
        addi a1, a1, 1
        mv   a0, s0
        call putnum
        li   t0, '='
        sb   t0, 1(a1)
        addi a1, a1, 3
        mv   a0, s1
        call putnum
        sw   s1, 0(s4)           # the display shows it too
        li   t1, 3               # three steps on
step:   add  t0, s1, s2
        mv   s1, s2
        mv   s2, t0
        addi s0, s0, 1
        addi t1, t1, -1
        bnez t1, step
        addi s3, s3, 64          # the next row
        li   t0, 0x3400          # 16 rows
        blt  s3, t0, row
        ecall                    # stop

# putnum: a0 in decimal, at a1; a1 ends up after it
putnum: addi sp, sp, -16
        mv   t1, sp              # the digits, the last one first
        li   t2, 10
digit:  remu t3, a0, t2
        addi t3, t3, '0'
        sb   t3, 0(t1)
        addi t1, t1, 1
        divu a0, a0, t2
        bnez a0, digit
copy:   addi t1, t1, -1
        lbu  t3, 0(t1)
        sb   t3, 0(a1)
        addi a1, a1, 1
        bne  t1, sp, copy
        addi sp, sp, 16
        ret
