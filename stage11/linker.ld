ENTRY(_start)

SECTIONS
{
    . = 0x00008000;

    .text : {
        KEEP(*(.text._start))
        KEEP(*(.vectors))
        *(.text*)
        *(.rodata*)
    }

    . = ALIGN(4);
    .data : { *(.data*) }

    . = ALIGN(4);
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(4096);
    __stage11_image_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.ARM.attributes)
        *(.eh_frame*)
    }
}
