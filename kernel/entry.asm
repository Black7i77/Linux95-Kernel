BITS 64

global _start
extern kernel_main
extern _bss_start
extern _bss_end

section .text.boot

_start:
    cli
    cld

    mov rsp, 0x9F000
    and rsp, -16

    ; Preserve BootInfo* passed in RDI by Stage 2.
    mov r12, rdi

    ; Freestanding kernels must clear their own BSS.
    mov rdi, _bss_start
    mov rcx, _bss_end
    sub rcx, rdi

    xor eax, eax
    rep stosb

    mov rdi, r12
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt
