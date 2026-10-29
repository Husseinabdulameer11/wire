# Keys: type on the keypad (or the keyboard: 0-9, a-f) and the keys show
# on the text screen, the last one on the display.
        .data
prompt: .asciz "Press keys (click the keypad, or type 0-9 and a-f):"

        .text
main:   li   sp, 0x2000
        li   s1, 0x4000
        la   a0, prompt
        li   a1, 0x3000
        call puts
        li   s0, 0x3040          # where the next key goes: row 1
        li   s2, 0x3400          # the end of the screen

down:   lw   t0, 12(s1)          # the keypad: bit 4 while a key is held
        andi t1, t0, 16
        beqz t1, down
        andi t0, t0, 15          # which key
        sw   t0, 0(s1)
        li   t1, 10
        blt  t0, t1, digit
        addi t0, t0, 'A' - 10
        j    put
digit:  addi t0, t0, '0'
put:    sb   t0, 0(s0)
        addi s0, s0, 1
        blt  s0, s2, up
        li   s0, 0x3040          # full: start again under the prompt
up:     lw   t0, 12(s1)          # wait until it is let go
        andi t1, t0, 16
        bnez t1, up
        j    down

puts:   lbu  t0, 0(a0)
        beqz t0, done
        sb   t0, 0(a1)
        addi a0, a0, 1
        addi a1, a1, 1
        j    puts
done:   ret
