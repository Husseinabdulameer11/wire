# Hello: text on the text screen, a picture on the colour screen, then,
# for ever, the switches times themselves on the display.
        .data
line1:  .asciz "Hello, RISC-V!"
line2:  .asciz "An RV32IM processor, drawn and simulated by Wire."
line3:  .asciz "Flip the switches (sw): the display shows sw * sw."

        .text
main:   li   sp, 0x2000          # the stack: the top of the ram
        la   a0, line1
        li   a1, 0x3000          # the text screen, row 0
        call puts
        la   a0, line2
        li   a1, 0x3080          # row 2: 64 characters a row
        call puts
        la   a0, line3
        li   a1, 0x30C0          # row 3
        call puts

# the colour screen, 64 x 64, a byte per pixel: RRRGGGBB
        li   t2, 0x2000          # pixel (0, 0)
        li   t5, 64
        li   t0, 0               # y
yloop:  li   t1, 0               # x
xloop:  srli t3, t1, 3           # red: x / 8
        slli t3, t3, 5
        srli t4, t0, 3           # green: y / 8
        slli t4, t4, 2
        or   t3, t3, t4
        xor  t4, t0, t1          # blue: from x xor y
        srli t4, t4, 4
        andi t4, t4, 3
        or   t3, t3, t4
        sb   t3, 0(t2)
        addi t2, t2, 1
        addi t1, t1, 1
        blt  t1, t5, xloop
        addi t0, t0, 1
        blt  t0, t5, yloop

# for ever: the display shows the switches squared
        li   t0, 0x4000
show:   lw   t1, 8(t0)           # the switches
        mul  t1, t1, t1
        sw   t1, 0(t0)           # the display
        j    show

# puts: the string at a0 onto the screen at a1
puts:   lbu  t0, 0(a0)
        beqz t0, done
        sb   t0, 0(a1)
        addi a0, a0, 1
        addi a1, a1, 1
        j    puts
done:   ret
