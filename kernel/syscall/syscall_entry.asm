BITS 64

section .text

extern syscall_bridge

global syscall_entry
syscall_entry:
    swapgs
    mov [gs:16], rsp
    mov rsp, [gs:8]

    push qword 0x1B
    push qword [gs:16]
    push r11
    push qword 0x23
    push rcx

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rbx, rsp
    mov rdi, rbx
    and rsp, -16
    call syscall_bridge
    test rax, rax
    jz .invalid_or_nonreturn
    mov rsp, rbx

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    mov rcx, [rsp]
    mov r11, [rsp + 16]
    mov rsp, [rsp + 24]
    swapgs
    ; sysretq: NASM encodes the long-mode SYSRET instruction as `sysret`.
    o64 sysret

.invalid_or_nonreturn:
    cli
.halt:
    hlt
    jmp .halt
