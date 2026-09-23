ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    _kernel_start = .;

    .text : ALIGN(4096)
    {
        KEEP(*(.text.boot))
        *(.text*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata*)
    }

    .data : ALIGN(4096)
    {
        *(.data*)
    }

    .bss : ALIGN(4096)
    {
        _bss_start = .;
        *(COMMON)
        *(.bss*)
        _bss_end = .;
    }

    _kernel_end = .;

    /DISCARD/ :
    {
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
        *(.eh_frame_hdr*)
    }
}
