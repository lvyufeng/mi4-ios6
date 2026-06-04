ENTRY(_start)

SECTIONS
{
    . = 0x00008000;

    .text : {
        KEEP(*(.text._start))
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
    __stage1_image_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.ARM.attributes)
    }
}
