ENTRY(_start)

SECTIONS
{
    . = 0x0000400000000000;

    .text : ALIGN(4096) {
        *(.text.start)
        *(.text*)
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss*)
    }
}
