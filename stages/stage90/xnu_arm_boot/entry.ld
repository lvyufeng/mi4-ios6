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
        /*
         * **The Mach-O names, which this script used to leave to the linker.** The objects this
         * image links carry sections named after Mach-O segment/section pairs, because the XNU
         * sources say so - `vm_types.h`'s `VM_ALLOC_SITE_STATIC` names `"__DATA, __data"`,
         * `kern_memorystatus.h` puts two functions in `"__TEXT, initcode"`. Six names appear over
         * the whole object set, and each is quoted here because a pattern containing a space or a
         * comma has to be: `*("__TEXT,*")` matches `__TEXT,__const`, `__TEXT,__os_log` and
         * `__TEXT, initcode` (the space before `initcode` is in XNU's source) with one pattern.
         *
         * Until experiment 298 they were orphan sections and the linker placed them by its own
         * rules. Measured, those rules are consistent - it put the `A`-only read-only ones with the
         * read-only group and the `WA` writable ones after `.data` - but "consistent with the
         * linker's rules" is not "consistent with this image's layout", and the `WA` pair landing
         * between `.data` and `.bss` is what experiment 297's `__bss_start` defect was made of.
         *
         * `__TEXT, initcode` is the one with a consequence beyond tidiness: it holds
         * `memorystatus_init` and `memorystatus_freeze_init` (`bsd/sys/kern_memorystatus.h:370,463`)
         * and is *code*, so as an orphan it sat inside what this script's Mach-O header calls
         * `__DATA` - the writable region. It belongs in `__TEXT`, which is where it is now.
         */
        *("__TEXT,*")
        *(.ARM.exidx)
        . = ALIGN(32);
    }
    __entry_text_end = .;
    __entry_text_size = __entry_text_end - __entry_text_start;

    __entry_data_start = .;
    .data : {
        *(.data .data.*)
        /*
         * The two writable Mach-O sections. `__DATA, __data` is where `VM_ALLOC_SITE_STATIC` puts
         * every `vm_allocation_site` (207 objects carry one) and `__DATA, __const` is where
         * `arm_init.c:112` puts `const_boot_args` and `BootArgs`. Both spellings of each name are
         * matched - XNU's sources are not consistent about the space after the comma.
         */
        *("__DATA,*__data" "__DATA,*__const")
        . = ALIGN(8);
    }

    /*
     * **`__DATA,__sysctl_set`, and why it gets an output section of its own.** Every `SYSCTL_OID`
     * drops a four-byte `struct linker_set_entry` here (`bsd/sys/linker_set.h:114`), and the set is
     * read **by name at runtime** - `LINKER_SET_BEGIN(__sysctl_set)` is
     * `getsectdatafromheader(_mh_execute_header, "__DATA", "__sysctl_set", &size)`
     * (`linker_set.h:193`), i.e. a walk of *this image's Mach-O header*. Three things therefore have
     * to be true of it and only one of them was:
     *
     *   - the section has to exist and keep its name, which is its interface, not an implementation
     *     detail - so it is not merged into `.data` even though its contents are four-byte pointers;
     *   - its address has to be inside the `__DATA` *segment*, because that is the segment the
     *     lookup asks for - and as an orphan the linker placed it with the read-only group after
     *     `.text`, i.e. inside `__TEXT`, where the lookup cannot find it however the header is
     *     written. That is why this section is placed here, between `.data` and `.bss`;
     *   - `entry_macho.s` has to describe it, and does not yet: the header declares no such section,
     *     so nothing can find the set today. That is a change to `entry_macho.s` and
     *     `tools/host_entry_macho_check.sh` and it waits for the step that links a consumer of the
     *     set (`bsd/kern/kern_sysctl.c`) - the same step that makes the section non-empty in a way
     *     that matters. The two symbols below are what that entry will be written from.
     *
     * Nothing looks the set up yet, and nothing in this image calls `SYSCTL_OID`'s consumer, so this
     * is preparation rather than a fix - but it is preparation with a reason written down rather
     * than an accident of where ld happened to put a section.
     */
    . = ALIGN(4);
    .sysctl_set : {
        __entry_sysctl_set = .;
        *("__DATA,*__sysctl_set")
        . = ALIGN(4);
        __entry_sysctl_set_end = .;
    }
    __entry_sysctl_set_size = __entry_sysctl_set_end - __entry_sysctl_set;

    /*
     * **`.init_array`, which experiment 318 made appear.** A C++ object linked into this image carries a
     * four-byte table of static-constructor pointers, and `libkern_c++_OSKext.o` is the first one to have
     * a non-empty one. Nothing here matched it, so it became an **orphan output section** - a fifth
     * allocated section in a script that named four - and the build's `verify_sections` check refused the
     * step by name, which is exactly what that check was written for after 297 and 298.
     *
     * It is placed here, between `.sysctl_set` and `.bss`, for three reasons and each is checkable:
     *
     *   - the section's flags are `WA` (writable, allocatable), so it belongs in the writable region. Its
     *     contents are absolute addresses the link resolved, and nothing writes them at run time, but a
     *     `WA` input inside `.text` would put a writable section inside the region `entry_macho.s`
     *     describes as read-only `__TEXT` - which is 298's whole point;
     *   - it sits with `.sysctl_set` because both are four-byte pointer tables laid out by the linker and
     *     read by address, and keeping the two adjacent keeps the writable table area in one place;
     *   - it keeps its **name** rather than being merged into `.data` by a `*(.init_array)` inside that
     *     section, so the table stays visible in the map - the same argument 298 used for the six Mach-O
     *     input-section names, applied to a named output section instead of a wildcard.
     *
     * **What is in it, and what it means that nothing calls it.** One entry, an `R_ARM_TARGET1`
     * relocation to `_GLOBAL__sub_I_OSKext.cpp`, and that function is not a no-op:
     *
     *     OSMetaClass::OSMetaClass("OSKext", &OSObject::gMetaClass, 96)   // on OSKext::gMetaClass
     *     OSKext::gMetaClass.vtable = &OSKext::MetaClass::vtable + 8
     *     __cxa_atexit(OSMetaClass::~OSMetaClass, &OSKext::gMetaClass, __dso_handle)
     *
     * Nothing in this image runs it. There is no C runtime here, and XNU's own initializer walk
     * (`OSRuntimeInitializeCPP`, `libkern/c++/OSRuntime.cpp:364`) finds constructors by *section name* -
     * `sectionIsConstructor` at `OSRuntime.cpp:208` accepts `__mod_init_func` or `__constructor` - and
     * `entry_macho.s` describes no section with either name.
     *
     * So the consequence to carry forward is stated here rather than discovered later: **`OSKext::gMetaClass`
     * is 96 bytes of this object's `.bss` and stays zero**, because the initializer that would fill it is
     * linked, correct, and unreachable. That is the stand-in hazard in its sharpest form - the definition is
     * present and the right size, and zero because the *initializer* lives somewhere nothing calls. A step
     * that runs `.init_array` (or describes this table to XNU's runtime) is a step of its own.
     */
    . = ALIGN(4);
    .init_array : {
        __entry_init_array = .;
        KEEP(*(.init_array .init_array.*))
        . = ALIGN(4);
        __entry_init_array_end = .;
    }
    __entry_init_array_size = __entry_init_array_end - __entry_init_array;

    /*
     * **`__bss_start` is the first byte of `.bss`, and it is placed *inside* the output section so that
     * this is true by construction** - which is what `stages/stage90/linker.ld:19` has always done for
     * the payload, and what this script did not do before experiment 297.
     *
     * It used to be assigned *between* the `.data` and `.bss` output sections, and that is not the same
     * statement. It names the end of `.data`, and it names the start of `.bss` only if the linker places
     * nothing between them. **The linker places something there.** This script matches `*(.data
     * .data.*)`, and the objects carry sections whose names are Mach-O's - `"__DATA, __const"` and
     * `"__DATA, __data"`, with the space in the name that makes the pattern miss them - so those are
     * orphans, and the linker put them after `.data` and therefore **after** the old `__bss_start`.
     *
     * The payload zeroes `[__bss_start, __bss_end)` *after* copying the image in (step 2 of
     * `stage90_xnu_entry_run`), which is the right thing for a `.bss` and the wrong thing for
     * initialized data: every run this project made before 297 zeroed 3252 bytes of it, which is 122
     * `vm_allocation_site`s, each with the `refcount = 2` and one of the `tag`/`flags` that
     * `VM_ALLOC_SITE_STATIC` sets. Experiment 296 found it from a `__bss_start` 0xCB4 away from its
     * prediction; 297 is the fix, and `entry_macho.s`'s statement that the image has no real
     * `__DATA,__const` is one of the things it makes true again.
     *
     * `build_entry.sh` states the property twice and checks it twice: `verify_bss` asserts that this
     * symbol *is* the `.bss` output section's first byte, and a layout check asserts that it is at or
     * beyond the end of the binary `objcopy` produces. The second is the one that matters, because it
     * is arithmetic over the two numbers the payload actually uses rather than a claim about where the
     * linker put things - it holds however the linker chooses to order or merge anything.
     */
    .bss : {
        __bss_start = .;
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
     *
     * Experiment 297 changed `filesize`, and for the better. `__bss_start` is now the start of the
     * `.bss` output section rather than the end of `.data`, so this number covers `.data` *and* the
     * orphan sections the linker placed between the two - which is exactly what "the part of
     * `__DATA` that is not zero-filled" means. It was 0xCC8 short before. No XNU code reads a kernel
     * segment's `filesize` (`kxld_seg.c` and `kern_core.c` only write it), so this is a correctness of
     * description rather than of behaviour - but it is the description the two fields exist to make,
     * and it was wrong.
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
