bits 64

global _start
extern user_main

section .text.start
_start:
    call user_main
    movsxd rdi, eax
    mov eax, 2
    int 0x80
.halt:
    pause
    jmp .halt
