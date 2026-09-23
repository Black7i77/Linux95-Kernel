[BITS 64]

global _start
extern kernel_main

section .text

_start:
    cli
    cld

    ; Stage 2 gives us BootInfo* in RDI.
    ; Use a known stack and keep it 16-byte aligned.
    mov rsp, 0x9F000
    and rsp, -16

    call kernel_main

.halt:
    cli
    hlt
    jmp .halt
