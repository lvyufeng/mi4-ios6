OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(stage59_xnu_link_noentry)

SECTIONS
{
    . = 0x80008000;
    .text : ALIGN(16) {
        KEEP(*(.stage59_xnu_link_noentry))
        *(.text*)
        *(.rodata*)
    }
    .data : ALIGN(16) {
        *(.data*)
    }
    .bss : ALIGN(16) {
        *(.bss*)
        *(COMMON)
    }
    /DISCARD/ : {
        *(.comment*)
        *(.note*)
        *(.ARM.attributes*)
    }
}
