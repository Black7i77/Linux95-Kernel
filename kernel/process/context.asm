BITS 64

section .text

global process_save_host
global process_restore_host
global process_resume_user

process_save_host:
    lea rax, [rsp + 8]
    mov [rdi + 0], rax
    mov rax, [rsp]
    mov [rdi + 8], rax
    mov [rdi + 16], rbx
    mov [rdi + 24], rbp
    mov [rdi + 32], r12
    mov [rdi + 40], r13
    mov [rdi + 48], r14
    mov [rdi + 56], r15
    xor eax, eax
    ret

process_restore_host:
    mov rsp, [rdi + 0]
    mov rbx, [rdi + 16]
    mov rbp, [rdi + 24]
    mov r12, [rdi + 32]
    mov r13, [rdi + 40]
    mov r14, [rdi + 48]
    mov r15, [rdi + 56]
    mov eax, 1
    jmp [rdi + 8]

process_resume_user:
    mov rax, rdi
    cmp byte [rax + 148], 1
    je .resume_sysret
    push qword [rax + 146]
    push qword [rax + 128]
    push qword [rax + 136]
    push qword [rax + 144]
    push qword [rax + 120]
    mov r15, [rax + 0]
    mov r14, [rax + 8]
    mov r13, [rax + 16]
    mov r12, [rax + 24]
    mov r11, [rax + 32]
    mov r10, [rax + 40]
    mov r9, [rax + 48]
    mov r8, [rax + 56]
    mov rbp, [rax + 64]
    mov rsi, [rax + 80]
    mov rdx, [rax + 88]
    mov rcx, [rax + 96]
    mov rbx, [rax + 104]
    mov rdi, [rax + 72]
    mov rax, [rax + 112]
    iretq

.resume_sysret:
    mov r15, [rax + 0]
    mov r14, [rax + 8]
    mov r13, [rax + 16]
    mov r12, [rax + 24]
    mov r10, [rax + 40]
    mov r9, [rax + 48]
    mov r8, [rax + 56]
    mov rbp, [rax + 64]
    mov rsi, [rax + 80]
    mov rdx, [rax + 88]
    mov rbx, [rax + 104]
    mov rdi, [rax + 72]
    mov rcx, [rax + 120]
    mov r11, [rax + 136]
    or r11, 2
    and r11, ~(1 << 8 | 1 << 9 | 1 << 10)
    mov rsp, [rax + 128]
    mov rax, [rax + 112]
    swapgs
    o64 sysret
