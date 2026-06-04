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
    .bss : { *(.bss*) *(COMMON) }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.ARM.attributes)
    }
}
