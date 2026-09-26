/*
 * A Mach-O header for this image, and why an ELF needs one.
 *
 * `arm_vm_init` (`osfmk/arm/arm_vm_init.c:405-421`) asks where the kernel's own segments are:
 *
 *     segTEXTB = (vm_offset_t) getsegdatafromheader(&_mh_execute_header, "__TEXT", &segSizeTEXT);
 *     segDATAB = (vm_offset_t) getsegdatafromheader(&_mh_execute_header, "__DATA", &segSizeDATA);
 *     ...
 *     end_kern = round_page(getlastaddr());
 *
 * and `end_kern`, `avail_start` and `sane_size` are all derived from the answers. Those four
 * functions are `libkern/kernel_mach_header.c` - real XNU code that walks a `struct mach_header`
 * and its `LC_SEGMENT` load commands - and the header they walk is `_mh_execute_header`.
 *
 * This image is an ELF and has no such header, so until exp-194 `_mh_execute_header` was a
 * generated storage stub: a pointer to zeroed bytes. That is worse than useless, because the walk
 * does not stop at the first zero. `getsectbynamefromheader` returns NULL for a header with
 * `ncmds == 0`, and `arm_vm_init:427` then does `sectCONSTB = sectDCONST->addr` - a load from
 * address 0x20. So the header below is what makes the second half of `arm_vm_init` something other
 * than a data abort.
 *
 * It is not a description of a hypothetical image. Every number in it is this image: the linker
 * script exports the addresses and the sizes, and `__TEXT` is `.text`'s real extent while `__DATA`
 * is the real writable region, `.data` through the end of `.bss`. `vmsize` covers the whole
 * writable region and `filesize` stops where the zero-initialized part begins, which is the
 * distinction the two fields exist for.
 *
 * Three segments, because one of the four a real kernel has is not optional - and the reason is
 * arithmetic, not fidelity. `arm_vm_init` asks for `__LINKEDIT`, `__KLD`, `__LAST`,
 * `__PRELINK_TEXT` and `__PRELINK_INFO`, and `getsegdatafromheader` returns a null pointer *and sets
 * the size to zero* for a name that is not present, so it is tempting to leave them out: this
 * kernel has no kexts, so its prelink text really is empty. **That was wrong, and experiment 242
 * measured what it costs.** `arm_vm_prot_init` sizes one of its calls by subtraction:
 *
 *     arm_vm_page_granular_RWNX(segPRELINKTEXTB + segSizePRELINKTEXT,
 *                                 end_kern - (segPRELINKTEXTB + segSizePRELINKTEXT), force_coarse_physmap);
 *
 * With the segment absent both terms are 0, so the call became `RWNX(0, end_kern)` - the whole
 * address space below the kernel instead of the short prelink-info range just under `end_kern` -
 * and a 1024-iteration loop that takes a page-table page from `avail_start` per iteration faulted
 * on the first page the allocator had already made read-only. `__PRELINK_TEXT` is here so that
 * `segPRELINKTEXTB + segSizePRELINKTEXT` is a real address, and it is an *empty* segment at
 * `__entry_image_end` with `vmsize = 0`, which is where an empty segment belongs and which leaves
 * the call protecting exactly the round-up slop of the image's last page. The segment's `vmaddr` and
 * `vmsize` are the two fields the boot code acts on: `getsegdatafromheader` returns `sc->vmaddr` and
 * stores `sc->vmsize`.
 *
 * `__LINKEDIT`, `__KLD`, `__LAST` and `__PRELINK_INFO` stay absent, and that is still the truth
 * about this image: their sizes are used as sizes (`ROX(segKLDB, segSizeKLD)`,
 * `RWNX(segLINKB, segSizeLINK)`, `RWNX(segLASTB, segSizeLAST)`) or as a single address
 * (`vm_slinkedit`, `vm_prelink_sinfo`), so a zero is a zero and not a magnification. The rule to
 * carry out of 242 is narrower than "add every segment": **check every call of the form
 * `f(B + S, C - (B + S))` before leaving a segment out**, because there an absence multiplies
 * rather than cancels.
 *
 * `__DATA` carries two sections, and neither of them is a region of read-only data. The first is
 * `__const`, and it is **empty**: the image's read-only data lives in `.text`, so there is no
 * `__DATA,__const` to protect. `getsectbynamefromheader` returning a real section with a zero size is
 * what keeps `arm_vm_init:441`'s `round_page` and the `if (doconstro)` block from dereferencing a
 * null pointer, and the block's own sanity check (`sectSizeCONST == 0` -> `doconstro = FALSE`) is what
 * then makes `arm_vm_prot_init` map `__DATA` as a single blob. A zero-size section is the honest
 * answer; a fabricated one would claim a region of read-only data that does not exist.
 *
 * The second is **`__mod_init_func`, and it is the linked `.init_array` table itself** (experiment
 * 329). It is here because XNU's own C++ runtime looks for its constructors **by section name**:
 * `OSRuntimeInitializeCPP` (`libkern/c++/OSRuntime.cpp:403`) walks every segment's sections and calls
 * `sectionIsConstructor` (`:246`), which accepts `SECT_MODINITFUNC` = `__mod_init_func` or
 * `SECT_CONSTRUCTOR` = `__constructor`, and then calls each pointer the section holds (`:458-481`,
 * `(*constructors[i])()`, with `OSMetaClass::checkModLoad(metaHandle)` gating the loop between
 * entries). That code runs; experiment 324 watched it execute and match nothing, because until now
 * this header described no section by either name, and the consequence was measured in 328: the
 * constructors were linked, correct and unreachable, so `_ZL4pool` - written only by a static
 * constructor - was still zero when the boot reached `OSSymbol::withCStringNoCopy`, which took a data
 * abort on it. `addr` and `size` are exactly the two fields that call reads (`constructors =
 * (structor_t *)addr`, `num_constructors = size / sizeof(structor_t)`), and both are linker symbols
 * from `entry.ld`, so this record claims nothing the image does not already contain. `offset`,
 * `align`, `reloff`, `nreloc` and `flags` are read by neither that code nor anything else on the boot
 * path, and are zero rather than invented.
 *
 * `cputype`/`cpusubtype` are 12/12, `CPU_TYPE_ARM` and `CPU_SUBTYPE_ARM_V7K`, which are the values
 * exp-189 measured `cpu_init` derive from this device's MIDR.
 */

    .section .rodata.macho, "a"
    .balign 4

    .globl _mh_execute_header
_mh_execute_header:
    .long 0xfeedface                     /* magic      = MH_MAGIC */
    .long 12                             /* cputype    = CPU_TYPE_ARM */
    .long 12                             /* cpusubtype = CPU_SUBTYPE_ARM_V7K */
    .long 2                              /* filetype   = MH_EXECUTE */
    .long 3                              /* ncmds */
    .long 304                            /* sizeofcmds = 56 + (56 + 2*68) + 56 */
    .long 0                              /* flags */

    /* LC_SEGMENT, __TEXT - the executable and read-only part, no sections of its own. */
    .long 0x1                            /* cmd = LC_SEGMENT */
    .long 56                             /* cmdsize */
    .ascii "__TEXT"
    .zero 10
    .long __entry_text_start             /* vmaddr */
    .long __entry_text_size              /* vmsize */
    .long 0                              /* fileoff */
    .long __entry_text_size              /* filesize */
    .long 7                              /* maxprot  = rwx */
    .long 5                              /* initprot = r-x */
    .long 0                              /* nsects */
    .long 0                              /* flags */

    /* LC_SEGMENT, __DATA - the writable region, with the empty __const section and the constructor
     * table. */
    .long 0x1                            /* cmd = LC_SEGMENT */
    .long 192                            /* cmdsize = 56 + 2*68 */
    .ascii "__DATA"
    .zero 10
    .long __entry_data_start             /* vmaddr */
    .long __entry_data_size              /* vmsize: .data through the end of .bss */
    .long __entry_data_fileoff           /* fileoff */
    .long __entry_data_filesize          /* filesize: the part that is not zero-filled */
    .long 7                              /* maxprot  = rwx */
    .long 3                              /* initprot = rw- */
    .long 2                              /* nsects */
    .long 0                              /* flags */

    /* struct section: __const, empty and at the end of the segment. */
    .ascii "__const"
    .zero 9
    .ascii "__DATA"
    .zero 10
    .long __entry_image_end              /* addr */
    .long 0                              /* size */
    .long 0                              /* offset */
    .long 0                              /* align */
    .long 0                              /* reloff */
    .long 0                              /* nreloc */
    .long 0                              /* flags */
    .long 0                              /* reserved1 */
    .long 0                              /* reserved2 */

    /* struct section: __mod_init_func, the linked .init_array table; see the header comment. */
    .ascii "__mod_init_func"
    .zero 1                              /* `sectname` is 16 bytes; the name is 15 */
    .ascii "__DATA"
    .zero 10
    .long __entry_init_array             /* addr */
    .long __entry_init_array_size        /* size */
    .long 0                              /* offset */
    .long 0                              /* align */
    .long 0                              /* reloff */
    .long 0                              /* nreloc */
    .long 0                              /* flags */
    .long 0                              /* reserved1 */
    .long 0                              /* reserved2 */

    /*
     * LC_SEGMENT, __PRELINK_TEXT - empty, and present so that the call `arm_vm_init` sizes by
     * subtraction has a real address to start from. `vmsize = 0` is the honest size for a kernel
     * with no kexts, and `vmaddr = __entry_image_end` is the first free address after `__DATA`,
     * which puts the end of the segment - and therefore the start of that call's range - exactly
     * where an empty segment's would be. `fileoff`/`filesize` are 0 because there are no bytes.
     */
    .long 0x1                            /* cmd = LC_SEGMENT */
    .long 56                             /* cmdsize, no sections */
    .ascii "__PRELINK_TEXT"
    .zero 2
    .long __entry_image_end              /* vmaddr */
    .long 0                              /* vmsize */
    .long 0                              /* fileoff */
    .long 0                              /* filesize */
    .long 0                              /* maxprot */
    .long 0                              /* initprot */
    .long 0                              /* nsects */
    .long 0                              /* flags */
