# Bounce: a dot bouncing round the colour screen, leaving a trail that
# changes colour. Run it at Max.
        .text
main:   li   s0, 3               # x
        li   s1, 0               # y
        li   s2, 1               # dx
        li   s3, 1               # dy
        li   s4, 0x2000          # the screen
        li   s5, 63              # the last row and column
        li   s6, 0x03            # the colour, RRRGGGBB
loop:   slli t0, s1, 6           # the pixel at (x, y): 0x2000 + 64 y + x
        add  t0, t0, s0
        add  t0, t0, s4
        sb   s6, 0(t0)
        addi s6, s6, 7           # the next colour
        andi s6, s6, 255
        li   t2, 20              # wait a little
wait:   addi t2, t2, -1
        bnez t2, wait
        add  s0, s0, s2
        add  s1, s1, s3
        beqz s0, flipx
        bne  s0, s5, checky
flipx:  neg  s2, s2
checky: beqz s1, flipy
        bne  s1, s5, loop
flipy:  neg  s3, s3
        j    loop
