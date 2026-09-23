BITS 16
ORG 0x8000

start:
    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000

    sti

    mov si, stage2_message
    call print_string

halt:
    cli
    hlt
    jmp halt

print_string:
    lodsb
    test al, al
    jz .done

    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07
    int 0x10

    jmp print_string

.done:
    ret

stage2_message:
    db "Linux95 Stage 2 loaded successfully!", 13, 10, 0

; Stage 1 loads exactly 16 sectors.
; Pad Stage 2 to 16 * 512 = 8192 bytes.
times 8192 - ($ - $$) db 0
