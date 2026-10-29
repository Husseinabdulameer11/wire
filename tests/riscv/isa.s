# isa.s: made by tests/riscv/isa_gen.js. Every RV32IM instruction on edge
# values; after each block a checksum of its results goes to the next word
# from 0x1000, so tests/riscv/run.js can say which block differs between
# the circuit and ref.js. Ends with ebreak.
        .data
        .space 512               # the checksums
vals:   .word 0, 1, -1, 2, -2, 7, -7, 2147483647, -2147483648, 305419896, -305419896, 255, 32768, 31, 32, 1437226410
        .text
main:   li   s0, 0x1000
        la   s1, vals
        li   t4, 64
        li   s8, 0
        li   s2, 0
i0:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j0:    add  t3, s1, s3
        lw   t2, 0(t3)
        add t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j0
        addi s2, s2, 4
        blt  s2, t4, i0
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i1:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j1:    add  t3, s1, s3
        lw   t2, 0(t3)
        sub t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j1
        addi s2, s2, 4
        blt  s2, t4, i1
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i2:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j2:    add  t3, s1, s3
        lw   t2, 0(t3)
        sll t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j2
        addi s2, s2, 4
        blt  s2, t4, i2
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i3:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j3:    add  t3, s1, s3
        lw   t2, 0(t3)
        slt t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j3
        addi s2, s2, 4
        blt  s2, t4, i3
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i4:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j4:    add  t3, s1, s3
        lw   t2, 0(t3)
        sltu t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j4
        addi s2, s2, 4
        blt  s2, t4, i4
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i5:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j5:    add  t3, s1, s3
        lw   t2, 0(t3)
        xor t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j5
        addi s2, s2, 4
        blt  s2, t4, i5
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i6:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j6:    add  t3, s1, s3
        lw   t2, 0(t3)
        srl t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j6
        addi s2, s2, 4
        blt  s2, t4, i6
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i7:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j7:    add  t3, s1, s3
        lw   t2, 0(t3)
        sra t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j7
        addi s2, s2, 4
        blt  s2, t4, i7
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i8:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j8:    add  t3, s1, s3
        lw   t2, 0(t3)
        or t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j8
        addi s2, s2, 4
        blt  s2, t4, i8
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i9:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j9:    add  t3, s1, s3
        lw   t2, 0(t3)
        and t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j9
        addi s2, s2, 4
        blt  s2, t4, i9
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i10:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j10:    add  t3, s1, s3
        lw   t2, 0(t3)
        mul t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j10
        addi s2, s2, 4
        blt  s2, t4, i10
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i11:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j11:    add  t3, s1, s3
        lw   t2, 0(t3)
        mulh t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j11
        addi s2, s2, 4
        blt  s2, t4, i11
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i12:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j12:    add  t3, s1, s3
        lw   t2, 0(t3)
        mulhsu t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j12
        addi s2, s2, 4
        blt  s2, t4, i12
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i13:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j13:    add  t3, s1, s3
        lw   t2, 0(t3)
        mulhu t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j13
        addi s2, s2, 4
        blt  s2, t4, i13
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i14:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j14:    add  t3, s1, s3
        lw   t2, 0(t3)
        div t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j14
        addi s2, s2, 4
        blt  s2, t4, i14
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i15:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j15:    add  t3, s1, s3
        lw   t2, 0(t3)
        divu t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j15
        addi s2, s2, 4
        blt  s2, t4, i15
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i16:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j16:    add  t3, s1, s3
        lw   t2, 0(t3)
        rem t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j16
        addi s2, s2, 4
        blt  s2, t4, i16
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i17:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j17:    add  t3, s1, s3
        lw   t2, 0(t3)
        remu t0, t1, t2
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j17
        addi s2, s2, 4
        blt  s2, t4, i17
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i18:    add  t3, s1, s2
        lw   t1, 0(t3)
        addi t0, t1, 0
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi t0, t1, -1
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi t0, t1, 2047
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi t0, t1, -2048
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi t0, t1, 5
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s2, s2, 4
        blt  s2, t4, i18
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i19:    add  t3, s1, s2
        lw   t1, 0(t3)
        slti t0, t1, 0
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        slti t0, t1, -1
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        slti t0, t1, 2047
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        slti t0, t1, -2048
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        slti t0, t1, 5
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s2, s2, 4
        blt  s2, t4, i19
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i20:    add  t3, s1, s2
        lw   t1, 0(t3)
        sltiu t0, t1, 0
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        sltiu t0, t1, -1
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        sltiu t0, t1, 2047
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        sltiu t0, t1, -2048
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        sltiu t0, t1, 5
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s2, s2, 4
        blt  s2, t4, i20
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i21:    add  t3, s1, s2
        lw   t1, 0(t3)
        xori t0, t1, 0
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        xori t0, t1, -1
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        xori t0, t1, 2047
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        xori t0, t1, -2048
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        xori t0, t1, 5
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s2, s2, 4
        blt  s2, t4, i21
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i22:    add  t3, s1, s2
        lw   t1, 0(t3)
        ori t0, t1, 0
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        ori t0, t1, -1
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        ori t0, t1, 2047
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        ori t0, t1, -2048
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        ori t0, t1, 5
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s2, s2, 4
        blt  s2, t4, i22
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i23:    add  t3, s1, s2
        lw   t1, 0(t3)
        andi t0, t1, 0
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        andi t0, t1, -1
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        andi t0, t1, 2047
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        andi t0, t1, -2048
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        andi t0, t1, 5
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s2, s2, 4
        blt  s2, t4, i23
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i24:    add  t3, s1, s2
        lw   t1, 0(t3)
        slli t0, t1, 0
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        slli t0, t1, 1
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        slli t0, t1, 13
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        slli t0, t1, 31
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s2, s2, 4
        blt  s2, t4, i24
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i25:    add  t3, s1, s2
        lw   t1, 0(t3)
        srli t0, t1, 0
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        srli t0, t1, 1
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        srli t0, t1, 13
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        srli t0, t1, 31
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s2, s2, 4
        blt  s2, t4, i25
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i26:    add  t3, s1, s2
        lw   t1, 0(t3)
        srai t0, t1, 0
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        srai t0, t1, 1
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        srai t0, t1, 13
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        srai t0, t1, 31
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s2, s2, 4
        blt  s2, t4, i26
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i27:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j27:    add  t3, s1, s3
        lw   t2, 0(t3)
        li   t0, 1
        beq t1, t2, k28
        li   t0, 2
k28:
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j27
        addi s2, s2, 4
        blt  s2, t4, i27
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i29:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j29:    add  t3, s1, s3
        lw   t2, 0(t3)
        li   t0, 1
        bne t1, t2, k30
        li   t0, 2
k30:
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j29
        addi s2, s2, 4
        blt  s2, t4, i29
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i31:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j31:    add  t3, s1, s3
        lw   t2, 0(t3)
        li   t0, 1
        blt t1, t2, k32
        li   t0, 2
k32:
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j31
        addi s2, s2, 4
        blt  s2, t4, i31
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i33:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j33:    add  t3, s1, s3
        lw   t2, 0(t3)
        li   t0, 1
        bge t1, t2, k34
        li   t0, 2
k34:
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j33
        addi s2, s2, 4
        blt  s2, t4, i33
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i35:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j35:    add  t3, s1, s3
        lw   t2, 0(t3)
        li   t0, 1
        bltu t1, t2, k36
        li   t0, 2
k36:
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j35
        addi s2, s2, 4
        blt  s2, t4, i35
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i37:    add  t3, s1, s2
        lw   t1, 0(t3)
        li   s3, 0
j37:    add  t3, s1, s3
        lw   t2, 0(t3)
        li   t0, 1
        bgeu t1, t2, k38
        li   t0, 2
k38:
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        addi s3, s3, 4
        blt  s3, t4, j37
        addi s2, s2, 4
        blt  s2, t4, i37
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s2, 0
i39:    add  t3, s1, s2
        lw   t1, 0(t3)
        add  zero, t1, t1
        mv   t0, zero
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        sub  t3, t1, t1
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t3
        mv   t3, t1
        mul  t3, t3, t3
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t3
        addi s2, s2, 4
        blt  s2, t4, i39
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        lui  t0, 0
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        auipc t0, 0
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lui  t0, 1
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        auipc t0, 1
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lui  t0, 524288
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        auipc t0, 524288
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lui  t0, 1048575
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        auipc t0, 1048575
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lui  t0, 74565
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        auipc t0, 74565
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        jal  ra, sub1
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, a0
        la   t3, sub2
        jalr ra, 4(t3)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, a0
        auipc t0, 0
        jal  t1, link1
link1:  sub  t0, t1, t0
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s5, 0x1F00
        li   t1, 0x89ABCDEF
        sw   t1, 0(s5)
        lb t0, 0(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lb t0, 1(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lb t0, 2(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lb t0, 3(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lbu t0, 0(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lbu t0, 3(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lh t0, 0(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lh t0, 2(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lhu t0, 0(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lhu t0, 2(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lw t0, 0(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        li   t1, 0x5A
        sb   t1, 1(s5)
        sb   t1, 3(s5)
        lw   t0, 0(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        li   t1, 0x1234
        sh   t1, 2(s5)
        lw   t0, 0(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        sh   t1, 0(s5)
        lw   t0, 0(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        li   t1, -1
        sw   t1, 4(s5)
        lh   t0, 6(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lbu  t0, 5(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lw   t0, -4(s5)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        li   s6, 0x4000
        li   t1, 0xCAFE
        sw   t1, 0(s6)
        lw   t0, 0(s6)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lw   t0, 16(s6)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        li   s7, 0x3000
        li   t1, 0x41
        sb   t1, 5(s7)
        lbu  t0, 5(s7)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        li   t1, 0x4342
        sh   t1, 6(s7)
        lbu  t0, 6(s7)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        lw   t0, 4(s7)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        li   s7, 0x2000
        li   t1, 0x112233E0
        sw   t1, 64(s7)
        lbu  t0, 64(s7)
        slli t5, s8, 5
        srli t6, s8, 27
        or   s8, t5, t6
        xor  s8, s8, t0
        sw   s8, 0(s0)
        addi s0, s0, 4
        li   s8, 0
        ebreak
sub1:   li   a0, 77
        ret
sub2:   li   a0, 1
        li   a0, 99
        ret
