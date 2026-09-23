BITS 16
ORG 0x7C00

STAGE2_ADDRESS equ 0x8000
STAGE2_SECTORS equ 16

start:
    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    sti

    ; BIOS gives us the boot drive in DL.
    mov [boot_drive], dl

    mov si, boot_message
    call print_string

    ; Use BIOS Extended Disk Read (INT 13h AH=42h).
    mov dl, [boot_drive]
    mov si, disk_packet
    mov ah, 0x42
    int 0x13

    jc disk_error

    ; Restore boot drive for Stage 2.
    mov dl, [boot_drive]

    ; Jump to Stage 2 loaded at physical address 0x8000.
    jmp 0x0000:STAGE2_ADDRESS

disk_error:
    mov si, disk_error_message
    call print_string

halt:
    cli
    hlt
    jmp halt

; ------------------------------------------------------------
; BIOS text output
; DS:SI -> zero terminated string
; ------------------------------------------------------------

print_string:
    lodsb
    test al, al
    jz .done

    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10

    jmp print_string

.done:
    ret

boot_drive:
    db 0

boot_message:
    db "Linux95 BIOS Loader v0.1", 13, 10, 0

disk_error_message:
    db "ERROR: unable to load Stage 2", 13, 10, 0

; ------------------------------------------------------------
; INT 13h Extended Disk Address Packet
;
; Stage 2 begins immediately after the boot sector:
; LBA 1 -> load 16 sectors -> address 0000:8000
; ------------------------------------------------------------

disk_packet:
    db 0x10
    db 0x00
    dw STAGE2_SECTORS

    dw STAGE2_ADDRESS
    dw 0x0000

    dq 1

; Pad the boot sector to 510 bytes.
times 510 - ($ - $$) db 0

; BIOS boot signature.
dw 0xAA55
