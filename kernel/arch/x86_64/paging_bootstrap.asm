BITS 64
DEFAULT REL

global linux95_reload_cr3_and_reenter

section .text

; System V AMD64 ABI:
;   rdi = new CR3 physical address
;   rsi = higher-half continuation virtual address
;   rdx = BootInfo pointer to preserve for the continuation
linux95_reload_cr3_and_reenter:
    cli

    mov r8, rdx
    mov r9, rsi

    mov cr3, rdi

    ; Emit the checkpoint only after CR3 has actually been loaded.
    lea rsi, [rel cr3_message]
    mov dx, 0x00E9
.print:
    lodsb
    test al, al
    jz .jump_high
    out dx, al
    jmp .print

.jump_high:
    mov rdi, r8
    jmp r9

section .rodata
cr3_message:
    db "[PASS] cr3_reloaded", 10, 0
