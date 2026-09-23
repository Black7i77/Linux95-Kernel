BITS 16
ORG 0x8000

BOOTINFO_ADDR      equ 0x7000
E820_ADDR          equ 0x5000
E820_ENTRY_SIZE    equ 24

KERNEL_TMP_SEG     equ 0x2000
KERNEL_TMP_ADDR    equ 0x20000

KERNEL_LBA         equ 17
KERNEL_SECTORS     equ 128

BOOTINFO_MAGIC     equ 0x4C393542

start:
    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7A00

    sti

    ; Stage 1 gives us the BIOS boot drive in DL.
    mov [boot_drive], dl

    mov si, stage2_message
    call print_string

    ; Enable access above 1 MiB.
    call enable_a20
    jc error_e3

    ; Ask BIOS for the physical memory map.
    call collect_e820
    jc error_e6

    ; Check that this CPU can enter x86_64 long mode.
    call check_long_mode
    jc error_e4

    mov si, kernel_message
    call print_string

    ; Load the reserved 64 KiB kernel area from disk.
    call load_kernel
    jc error_e5

    ; BIOS services end here.
    cli

    lgdt [gdt_descriptor]

    ; Enter 32-bit protected mode.
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:protected_mode_entry


; ------------------------------------------------------------
; Error handling
; ------------------------------------------------------------

error_e3:
    mov si, msg_e3
    jmp fatal

error_e4:
    mov si, msg_e4
    jmp fatal

error_e5:
    mov si, msg_e5
    jmp fatal

error_e6:
    mov si, msg_e6

fatal:
    call print_string

.halt:
    cli
    hlt
    jmp .halt


; ------------------------------------------------------------
; BIOS text output
; ------------------------------------------------------------

print_string:
.next:
    lodsb

    test al, al
    jz .done

    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07

    int 0x10

    jmp .next

.done:
    ret


; ------------------------------------------------------------
; A20
; ------------------------------------------------------------

enable_a20:
    in al, 0x92

    test al, 0x02
    jnz .enabled

    or al, 0x02
    and al, 0xFE

    out 0x92, al

    in al, 0x92
    test al, 0x02

    jz .failed

.enabled:
    clc
    ret

.failed:
    stc
    ret


; ------------------------------------------------------------
; BIOS E820 memory map
; ------------------------------------------------------------

collect_e820:
    xor ebx, ebx
    mov di, E820_ADDR
    xor bp, bp

.next:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, E820_ENTRY_SIZE

    ; Request extended attributes.
    mov dword [es:di + 20], 1

    int 0x15

    jc .bios_end

    cmp eax, 0x534D4150
    jne .failed

    add di, E820_ENTRY_SIZE
    inc bp

    cmp bp, 128
    jae .finished

    test ebx, ebx
    jnz .next

.finished:
    ; BootInfo.magic
    mov dword [BOOTINFO_ADDR + 0], BOOTINFO_MAGIC

    ; BootInfo.e820_count
    movzx eax, bp
    mov dword [BOOTINFO_ADDR + 4], eax

    ; BootInfo.e820_address
    mov dword [BOOTINFO_ADDR + 8], E820_ADDR
    mov dword [BOOTINFO_ADDR + 12], 0

    ; BootInfo.boot_drive
    mov al, [boot_drive]
    mov byte [BOOTINFO_ADDR + 16], al

    clc
    ret

.bios_end:
    cmp bp, 0
    je .failed

    jmp .finished

.failed:
    stc
    ret


; ------------------------------------------------------------
; CPU capability check
; ------------------------------------------------------------

check_long_mode:
    ; Check whether the CPUID instruction exists by toggling
    ; the ID flag in EFLAGS.
    pushfd
    pop eax

    mov ecx, eax
    xor eax, 1 << 21

    push eax
    popfd

    pushfd
    pop eax

    push ecx
    popfd

    xor eax, ecx
    test eax, 1 << 21

    jz .failed

    ; Check highest extended CPUID function.
    mov eax, 0x80000000
    cpuid

    cmp eax, 0x80000001
    jb .failed

    ; EDX bit 29 = Long Mode.
    mov eax, 0x80000001
    cpuid

    test edx, 1 << 29
    jz .failed

    clc
    ret

.failed:
    stc
    ret


; ------------------------------------------------------------
; Load kernel from LBAs 17-144
; ------------------------------------------------------------

load_kernel:
    ; First 64 sectors -> 0x20000.
    mov word [dap_count], 64
    mov word [dap_offset], 0
    mov word [dap_segment], KERNEL_TMP_SEG

    mov dword [dap_lba_low], KERNEL_LBA
    mov dword [dap_lba_high], 0

    mov si, dap

    mov ah, 0x42
    mov dl, [boot_drive]

    int 0x13
    jc .failed

    ; Second 64 sectors -> 0x28000.
    mov word [dap_count], 64
    mov word [dap_offset], 0
    mov word [dap_segment], KERNEL_TMP_SEG + 0x0800

    mov dword [dap_lba_low], KERNEL_LBA + 64
    mov dword [dap_lba_high], 0

    mov si, dap

    mov ah, 0x42
    mov dl, [boot_drive]

    int 0x13
    jc .failed

    clc
    ret

.failed:
    stc
    ret


; ------------------------------------------------------------
; Data
; ------------------------------------------------------------

boot_drive:
    db 0

stage2_message:
    db "Linux95 Stage 2: preparing x86_64...", 13, 10, 0

kernel_message:
    db "Linux95 Stage 2: loading kernel...", 13, 10, 0

msg_e3:
    db "E3", 13, 10, 0

msg_e4:
    db "E4", 13, 10, 0

msg_e5:
    db "E5", 13, 10, 0

msg_e6:
    db "E6", 13, 10, 0


; BIOS Extended Disk Drive packet.
dap:
    db 0x10
    db 0

dap_count:
    dw 0

dap_offset:
    dw 0

dap_segment:
    dw 0

dap_lba_low:
    dd 0

dap_lba_high:
    dd 0


; ------------------------------------------------------------
; Global Descriptor Table
; ------------------------------------------------------------

align 8

gdt_start:

gdt_null:
    dq 0x0000000000000000

; Selector 0x08: 32-bit code
gdt_code32:
    dq 0x00CF9A000000FFFF

; Selector 0x10: data
gdt_data:
    dq 0x00CF92000000FFFF

; Selector 0x18: 64-bit code
gdt_code64:
    dq 0x00AF9A000000FFFF

gdt_end:


gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start


; ============================================================
; 32-bit protected mode
; ============================================================

BITS 32

protected_mode_entry:
    mov ax, 0x10

    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, 0x9F000

    ; Copy 64 KiB kernel:
    ; 0x20000 -> 0x100000.
    mov esi, KERNEL_TMP_ADDR
    mov edi, 0x00100000

    mov ecx, (128 * 512) / 4

    cld
    rep movsd


    ; --------------------------------------------------------
    ; Clear PML4, PDPT and page directory.
    ; --------------------------------------------------------

    xor eax, eax

    mov edi, 0x1000
    mov ecx, (3 * 4096) / 4

    rep stosd


    ; PML4[0] -> PDPT at 0x2000.
    mov dword [0x1000], 0x2003
    mov dword [0x1004], 0


    ; PDPT[0] -> page directory at 0x3000.
    mov dword [0x2000], 0x3003
    mov dword [0x2004], 0


    ; Map physical 0-2 MiB as one huge page.
    mov dword [0x3000], 0x00000083
    mov dword [0x3004], 0


    ; Enable PAE.
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax


    ; Load PML4.
    mov eax, 0x1000
    mov cr3, eax


    ; Enable EFER.LME.
    mov ecx, 0xC0000080
    rdmsr

    or eax, 1 << 8

    wrmsr


    ; Enable paging.
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax


    ; Enter the 64-bit code segment.
    jmp 0x18:long_mode_entry


; ============================================================
; 64-bit long mode
; ============================================================

BITS 64

long_mode_entry:
    mov ax, 0x10

    mov ds, ax
    mov es, ax
    mov ss, ax

    xor eax, eax

    mov fs, ax
    mov gs, ax


    ; Initial Linux95 kernel stack.
    mov rsp, 0x9F000
    and rsp, -16


    ; First SysV argument:
    ; BootInfo*
    mov rdi, BOOTINFO_ADDR


    ; Kernel is linked and copied to 1 MiB.
    mov rax, 0x100000

    jmp rax


; Stage 1 always reads 16 sectors.
times 8192 - ($ - $$) db 0
