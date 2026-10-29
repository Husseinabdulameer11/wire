# Primes: the sieve of Eratosthenes in the ram, then every prime below
# 1000 on the text screen, and how many on the display. Each prime lights
# its pixel on the colour screen too (pixel n is prime n). Stops at the end.
        .equ N, 1000
        .data
sieve:  .space N                 # sieve[n] = 1: n is crossed out

        .text
main:   li   sp, 0x2000
        la   s0, sieve
        li   s1, 2               # p
        li   s2, N
cross:  mul  t0, s1, s1          # from p * p on
        bge  t0, s2, show
        add  t1, s0, s1
        lbu  t1, 0(t1)
        bnez t1, nextp           # p is crossed out: not a prime
        li   t2, 1
mark:   add  t1, s0, t0
        sb   t2, 0(t1)
        add  t0, t0, s1
        blt  t0, s2, mark
nextp:  addi s1, s1, 1
        j    cross

show:   li   s1, 2               # n
        li   s3, 0x3000          # where the next number goes
        li   s4, 0               # how many
        li   s5, 0x2000          # the colour screen
each:   add  t1, s0, s1
        lbu  t1, 0(t1)
        bnez t1, skip
        addi s4, s4, 1
        add  t0, s5, s1          # pixel n
        li   t1, 0xFC            # yellow
        sb   t1, 0(t0)
        mv   a0, s1
        mv   a1, s3
        call putnum
        addi s3, a1, 1           # and a space
skip:   addi s1, s1, 1
        blt  s1, s2, each
        li   t0, 0x4000
        sw   s4, 0(t0)           # 168 of them
        ebreak                   # stop

# putnum: a0 in decimal, at a1; a1 ends up after it
putnum: addi sp, sp, -16
        mv   t1, sp
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
