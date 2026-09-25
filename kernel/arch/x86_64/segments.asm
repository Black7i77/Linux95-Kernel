BITS 64
DEFAULT REL

section .text

global linux95_load_gdt
linux95_load_gdt:
    lgdt [rdi]

    mov ax, dx
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    push rsi
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    ret

global linux95_load_task_register
linux95_load_task_register:
    mov ax, di
    ltr ax
    ret
