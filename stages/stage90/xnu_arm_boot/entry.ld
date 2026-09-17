/*
 * Linker script for the XNU entry image.
 *
 * The image is linked at VA 0x00200000, and the boot_args handed to `_start` say
 * physBase = virtBase = 0x00200000. That makes every `LOAD_PHYS_ADDR` conversion in start.s an
 * identity, and it makes the page tables `_start` builds identity tables - which is what lets the
 * epilogue disable the MMU and keep executing, since its own address is the same either way.
 *
 * The whole image must fit inside [physBase, physBase + memSize) = 2 MB, because that is the only
 * window XNU's own page tables map, and everything that runs after the switch lives here.
 *
 * Layout: code and read-only data first (they are tens of KB), then the stacks and the vector
 * table. `topOfKernelData` is placed above all of it by the payload's boot_args, not here, since
 * `_start` only needs it to be 16 KB aligned, writable and inside the window.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x00200000;

    .text : {
        KEEP(*(.text._start))     /* start.s's _start, entered first */
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(32);
    }

    .data : {
        *(.data .data.*)
        . = ALIGN(8);
    }

    __bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
    }
    __bss_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.ARM.attributes)
        *(.eh_frame .eh_frame_hdr)
    }
}
