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
 * Two segments, because this image has two. A real kernel also has `__LINKEDIT`, `__KLD`,
 * `__PRELINK_TEXT` and `__PRELINK_INFO`; `getsegdatafromheader` returns a null pointer and a zero
 * size for a name that is not present, and `arm_vm_init` stores those zeroes into `vm_slinkedit`,
 * `vm_kext_base` and the rest - which is the truth about this image rather than a gap.
 *
 * `__DATA` carries one section, `__const`, and it is empty: the image's read-only data lives in
 * `.text`, so there is no `__DATA,__const` to protect. `getsectbynamefromheader` returning a real
 * section with a zero size is what keeps `arm_vm_init:441`'s `round_page` and the `if (doconstro)`
 * block from dereferencing a null pointer, and the block's own sanity check
 * (`sectSizeCONST == 0` -> `doconstro = FALSE`) is what then makes `arm_vm_prot_init` map `__DATA`
 * as a single blob. A zero-size section is the honest answer; a fabricated one would claim a
 * region of read-only data that does not exist.
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
    .long 2                              /* ncmds */
    .long 180                            /* sizeofcmds = 56 + (56 + 68) */
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

    /* LC_SEGMENT, __DATA - the writable region, with the empty __const section. */
    .long 0x1                            /* cmd = LC_SEGMENT */
    .long 124                            /* cmdsize = 56 + 68 */
    .ascii "__DATA"
    .zero 10
    .long __entry_data_start             /* vmaddr */
    .long __entry_data_size              /* vmsize: .data through the end of .bss */
    .long __entry_data_fileoff           /* fileoff */
    .long __entry_data_filesize          /* filesize: the part that is not zero-filled */
    .long 7                              /* maxprot  = rwx */
    .long 3                              /* initprot = rw- */
    .long 1                              /* nsects */
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
