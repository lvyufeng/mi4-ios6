/*
 * Linker script for the XNU entry image.
 *
 * **This script does not know its own base, and does not define one.** `ENTRY_BASE` is a symbol the
 * build injects with `--defsym`, from the one place the base is written down -
 * `build_entry.sh`'s `ENTRY_BASE` - and a link that forgets it fails with
 * `undefined symbol `ENTRY_BASE' referenced in expression` rather than producing an image at zero.
 * That is deliberate: this value used to live here *and* in build_entry.sh, which is the
 * one-value-two-definitions defect this project has a memory about, and which is exactly what
 * experiments 168 and 169 cost.
 *
 * The image is linked at ENTRY_BASE and the boot_args handed to `_start` say
 * physBase = virtBase = ENTRY_BASE. That makes every `LOAD_PHYS_ADDR` conversion in start.s an
 * identity, and it makes the page tables `_start` builds identity tables - which is what lets the
 * epilogue disable the MMU and keep executing, since its own address and its physical address are
 * the same number either way. It is also why the payload can physically *copy* the image to
 * ENTRY_BASE and enter it there with no relocation step.
 *
 * **Experiment 241 moved the base from 0x00200000 to 0x80000000**, which is where the device's RAM
 * starts (`RAM_PHYS_BASE`) and where this SoC's kernel normally loads - because
 * `is_sane_zone_ptr`'s first test, `pmap_kernel_va`, is `[0x80000000, 0xFFFEFFFF]` at compile time,
 * so an image below `VM_MIN_KERNEL_ADDRESS` has every address XNU's own allocator hands out
 * rejected by XNU's own kernel-address test. See experiment 241 and the module comment above
 * `fleh_undef` in entry_stubs.c. Nothing else in this script changed with it: every offset above
 * the image is derived from the image's own size, so moving the base moves the whole layout.
 *
 * The whole image must fit inside [physBase, physBase + memSize), because that is the only window
 * XNU's own page tables map, and everything that runs after the switch lives here. `memSize` is
 * 8 MB as of experiment 175; it is the window the payload computes, and `_start` walks it.
 *
 * Layout: code and read-only data first (they are tens of KB), then the stacks and the vector
 * table. `topOfKernelData` is placed above all of it by the payload's boot_args, not here, since
 * `_start` only needs it to be 16 KB aligned, writable and inside the window.
 */

ENTRY(_start)

SECTIONS
{
    . = ENTRY_BASE;    /* --defsym, from build_entry.sh; see the header */
    __entry_text_start = .;

    .text : {
        KEEP(*(.text._start))     /* start.s's _start, entered first */
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(32);
    }
    __entry_text_end = .;
    __entry_text_size = __entry_text_end - __entry_text_start;

    __entry_data_start = .;
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

    /*
     * The numbers entry_macho.s puts into the Mach-O header it defines. They are computed here
     * rather than in the assembly because the sizes are differences of two linker symbols, which
     * gas cannot fold into a single word; a script expression can.
     *
     * `__DATA` spans the whole writable region, `.data` through the end of `.bss`, which is what a
     * Mach-O segment's `vmsize` means; `filesize` stops where the zero-initialized part begins.
     */
    __entry_image_end = .;
    __entry_data_size = __entry_image_end - __entry_data_start;
    __entry_data_filesize = __bss_start - __entry_data_start;
    __entry_data_fileoff = __entry_data_start - __entry_text_start;

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.ARM.attributes)
        *(.eh_frame .eh_frame_hdr)
    }
}
