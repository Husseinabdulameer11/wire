; keypad.asm - adds up the keys you press on the keypad, and shows the
; total on port 3. Runs on the SAP-2 and the SAP-3; it never stops.
; sap2
; runs forever
        MVI  B, 0          ; the total
wait:   IN   3             ; the keypad: the last key, plus 10H while held
        ANI  10H
        JZ   wait          ; wait for a key to go down
        IN   3
        ANI  0FH           ; which key
        ADD  B
        MOV  B, A
        OUT  3
up:     IN   3
        ANI  10H
        JNZ  up            ; wait for it to come up again
        JMP  wait
