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
        /*
         * The slot XNU's `cpu_init` writes into - see the note below the sections.
         */
        __entry_reset_handler_data = .;
        . += 16;
        . = ALIGN(8);
    }
    __bss_end = .;

    /*
     * **The two symbols XNU does arithmetic on, pinned here rather than stubbed.**
     *
     * `osfmk/arm/cpu.c:562-580` copies a page from `&ExceptionLowVectorsBase` to a fresh mapping of
     * physical `gPhysBase`, and then writes four bytes each to
     *
     *     gPhysBase + ((unsigned int)&ResetHandlerData.boot_args       - (unsigned int)&ExceptionLowVectorsBase)
     *     gPhysBase + ((unsigned int)&ResetHandlerData.cpu_data_entries - (unsigned int)&ExceptionLowVectorsBase)
     *
     * In XNU's own image both symbols live in the low-vector area at the base of the kernel, so the
     * copy is page zero onto itself and the two writes land in the vector area's `ResetHandlerData`
     * struct, which is where the trampoline reads them. `locore.s` is not linked here, so *nothing*
     * defined them: the stub generator gave both a 24-byte function body inside the stub object, and
     * that made every one of those three addresses an accident of where the stub object landed:
     *
     *   - the page copy took 4096 bytes of stub bodies over page zero, i.e. over start.s's own code;
     *   - the two writes landed wherever `ResetHandlerData - ExceptionLowVectorsBase` happened to be,
     *     which is a difference between two positions *inside the generated stub object*. Experiment
     *     281 measured it as 0x2404 and the entry code was arranged so that a 128-byte never-executed
     *     pad at 0x80002404 absorbed them; 288 found it at 0x24a8 (0x58 past the pad's end, back
     *     inside `entry_epilogue`, five silent runs) and widened the pad to 512 bytes; 290 saw 0x2448
     *     and 291 saw 0x2358 - below the pad's start - and **the build refused it**, which is the
     *     check 288 built doing exactly its job.
     *
     * The difference is a difference of two *stub positions*, so it moves by 0x18 for every stub name
     * that enters or leaves the alphabetically-ordered stub object between "E" and "R". Chasing it
     * with a pad is a treadmill. Both names are therefore defined here instead, at values chosen so
     * that cpu.c's arithmetic does the harmless thing by construction:
     *
     *   - `ExceptionLowVectorsBase` is the image base, so the page copy is page zero onto itself -
     *     a no-op through two mappings of the same physical page, and strictly better than copying
     *     stub bodies over start.s's code;
     *   - `ResetHandlerData` is four bytes below `__entry_reset_handler_data`, so `boot_args` (at +8)
     *     and `cpu_data_entries` (at +4) - i.e. the two write addresses - are exactly the sixteen
     *     bytes of that reserved `.bss` slot. Zeroed by the payload, never executed, nothing reads it.
     *
     * `build_entry.sh`'s `verify_pad` checks both write addresses against that slot in every build,
     * so the property is a check and not a comment. And because these are script symbols and the
     * script is what pass 1 links with, the generator never sees them as undefined and never stubs
     * them: there is one definition of each, where before there were two (a stub body and a
     * written-down number).
     */
    ExceptionLowVectorsBase = ENTRY_BASE;
    ResetHandlerData = __entry_reset_handler_data - 4;

    /*
     * The numbers entry_macho.s puts into the Mach-O header it defines. They are computed here
     * rather than in the assembly because the sizes are differences of two linker symbols, which
     * gas cannot fold into a single word; a script expression can.
     *
     * `__DATA` spans the whole writable region, `.data` through the end of `.bss`, which is what a
     * Mach-O segment's `vmsize` means; `filesize` stops where the zero-initialized part begins.
     * `__entry_image_end` is also the empty `__PRELINK_TEXT` segment's address in the header, which
     * is what makes `arm_vm_init`'s `end_kern - (segPRELINKTEXTB + segSizePRELINKTEXT)` the round-up
     * slop of the last page rather than the whole address space - see `entry_macho.s` and
     * experiment 242.
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
