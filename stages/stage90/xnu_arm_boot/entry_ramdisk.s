/*
 * The executable of the first userland process, as bytes in this image.
 *
 * `g_stage90_ramdisk` is not a RAM disk in the sense of a filesystem image. It is the memory the
 * memory device `/dev/md0` was created over (`bsd/dev/memdev.c`'s `mdevadd`, handed the two words of
 * `/chosen/memory-map`'s `RAMDisk` property), and `mockfs` maps its one file node *directly onto
 * those physical pages* (`mockfs_fsnode.c:333-344`, `pager_map_to_phys_contiguous`), with the file's
 * size taken from the device (`mockfs_fsnode.c:80`, `mp->mnt_devvp->v_specinfo->si_devsize`). So the
 * bytes at offset 0 of this object **are** `/sbin/launchd`, byte for byte, with no filesystem
 * between them - which is what makes this file the whole of experiment 468.
 *
 * Why a Mach-O and not a script or a bare image: 467's run reached the three image activators for
 * the first time and every one of them declined the file, because its first four bytes were zero.
 * `execsw[]` is `{ exec_mach_imgact, exec_fat_imgact, exec_shell_imgact }` (`kern_exec.c:1353-1358`)
 * and the first of them claims a file by `magic == MH_MAGIC` (`kern_exec.c:855`). Nothing else in
 * this configuration can claim one - `mockfs_lookup` resolves only `path namei` on the way in, and
 * there is no interpreter (`exec_shell_imgact` wants `#!`).
 *
 * `si_devsize` is what makes this array the whole of the file. `bdevvp` opens the block device on
 * the way to `vfs_mountroot` (`bsd/vfs/vfs_subr.c:1059`, `VNOP_OPEN` after `vnode_create`), and that
 * open is `spec_open`, which sets `vp->v_specdevsize = blkcnt * 512` from `DKIOCGETBLOCKCOUNT`
 * (`bsd/miscfs/specfs/spec_vnops.c:405-441`); `mdev` answers that ioctl as
 * `((mdSize << 12) + mdSecsize - 1) / mdSecsize` (`bsd/dev/memdev.c:407-410`). With `mdSecsize` 512
 * and this object a whole number of 512-byte sectors, the file's size is **exactly this object's
 * size** - which is why the object is padded to `RAMDISK_BYTES` rather than left at whatever the
 * commands came to, and why the check below asserts that the two are the same number.
 *
 * ------------------------------------------------------------------------------------------------
 * The header, field by field, against the code that reads it
 * ------------------------------------------------------------------------------------------------
 *
 * `struct mach_header` (28 bytes, `mach-o/loader.h`):
 *
 *   magic      MH_MAGIC = 0xfeedface   claimed by `exec_mach_imgact` (`kern_exec.c:855`); the
 *                                      CIGAM/64 spellings are refused explicitly at `:845`
 *   cputype    CPU_TYPE_ARM = 12       `parse_machfile`'s first test (`mach_loader.c:611-614`) is
 *                                      `(header->cputype & ~CPU_ARCH_MASK) == (cpu_type() & ~CPU_ARCH_MASK)`
 *   cpusubtype CPU_SUBTYPE_ARM_V7K=12  and its second is `grade_binary(...)`, which for a V7K host
 *                                      accepts **only** V7K (`bsd/dev/arm/kern_machdep.c:105-113`:
 *                                      that case `break`s rather than falling through to `v7s`/`v7`
 *                                      the way the V8/V7S/V7F hosts do, so every other subtype
 *                                      grades 0 = not acceptable). The host's subtype is V7K
 *                                      because `ARMA7` makes `__ARM_SUB_ARCH__` =
 *                                      `CPU_ARCH_ARMv7k` (`proc_reg.h:74`) and `do_cpuid` maps that
 *                                      to `CPU_SUBTYPE_ARM_V7K` (`cpuid.c:93` into `cpu.c:251`).
 *   filetype   MH_EXECUTE = 2          `exec_mach_imgact` refuses anything else (`kern_exec.c:859`),
 *                                      and `parse_machfile`'s switch has a case for it
 *   ncmds      3                       the three below
 *   sizeofcmds 0xC4 (196)              `parse_machfile` reads the header plus `sizeofcmds` bytes
 *                                      from the file (`mach_loader.c:654-657`) and requires that to
 *                                      be inside it. Written here as an expression over the label
 *                                      the commands end at, so it is a measurement of these bytes
 *                                      rather than a transcription of them.
 *   flags      0                       **and this is the load-bearing zero.** With no `MH_DYLDLINK`
 *                                      the file is a *static* executable, which
 *                                      `parse_machfile:620-637` refuses outright unless
 *                                      `DEVELOPMENT || DEBUG` - the configuration experiment 468
 *                                      adds, and the branch Apple's own comment there names
 *                                      ("disallowed except for development"). With no `MH_PIE`
 *                                      `parse_machfile` leaves `slide` at 0 (`mach_loader.c:684`),
 *                                      so every `vmaddr` below is the absolute address it says it is
 *                                      and no two runs of this experiment can differ.
 *
 * `__PAGEZERO` (LC_SEGMENT 0x1, cmdsize 56): `vmaddr 0, vmsize 0x1000, fileoff 0, filesize 0,
 * maxprot 0, initprot 0, nsects 0, flags 0`. This is not decoration and it is not a mapping:
 * `load_segment` sees exactly this shape (`vmaddr == 0 && filesize == 0 && vmsize != 0` and both
 * protections `VM_PROT_NONE`) at `mach_loader.c:1645-1650` and calls
 * `vm_map_raise_min_offset(map, round_page(vmsize))`, then returns. Without it
 * `vm_map_has_hard_pagezero(map, 0x1000)` is false and `load_machfile` answers `LOAD_BADMACHO`
 * (`mach_loader.c:452-479`) - a whole exec lost to one missing command, which is why the check below
 * asserts this segment's four distinguishing fields and not just that a segment exists.
 *
 * `__TEXT` (LC_SEGMENT 0x1, cmdsize 56): `vmaddr 0x1000, vmsize 0x1000, fileoff 0, filesize 0x1000,
 * maxprot 7, initprot 5 (R|X), nsects 0, flags 0`. Three of its fields are constraints rather than
 * choices:
 *
 *   - `fileoff == 0 && filesize > 0` with `initprot` R|X makes this the `found_header_segment`
 *     `parse_machfile` requires at pass 3 (`mach_loader.c:906-916`: exactly one such segment, and
 *     `LOAD_BADMACHO` if a second appears or if its protection is not both);
 *   - `fileoff` must be page-aligned in the file (`mach_loader.c:1610-1622`) and the mapping must be
 *     page-aligned in both address spaces, because **`map_segment` panics** on an unaligned segment
 *     for a 32-bit binary (`mach_loader.c:1339-1350`: the `fourk_pager` alternative is `__arm64__`);
 *   - `vmaddr != 0`: under `CONFIG_EMBEDDED` any non-pagezero segment at address 0 is `LOAD_BADMACHO`
 *     (`mach_loader.c:1743-1748`), and 0x1000 is where an armv7 `__TEXT` normally is.
 *
 * The segment is one page and holds both the load commands and the code, which is why the entry
 * point below is a file offset plus `TEXT_VMADDR` rather than a number chosen here.
 *
 * `LC_UNIXTHREAD` (0x5, cmdsize 84): `flavor 1` (`ARM_THREAD_STATE`), `count 17`
 * (`ARM_THREAD_STATE_COUNT`), then the 17 words of `struct arm_thread_state` - `r[13], sp, lr, pc,
 * cpsr`. Three consumers, and each bounds a field:
 *
 *   - `load_threadstack` -> `thread_userstack` (`osfmk/arm/status.c:562-602`) refuses `count < 17`
 *     and returns `USRSTACK` with `customstack = 0` when `sp == 0`, which is what makes the kernel
 *     allocate the process's stack for it (`load_unixthread`'s `MAXSSIZ`,
 *     `create_unix_stack`'s `mach_vm_allocate_kernel`, `kern_exec.c:4911+`);
 *   - `load_threadentry` -> `thread_entrypoint` (`status.c:683-705`) accepts **only**
 *     `ARM_THREAD_STATE` and takes the entry point from `state->pc`; every other flavor is
 *     `KERN_INVALID_ARGUMENT` = `LOAD_FAILURE`. That is what makes `pc` here the whole of the
 *     entry-point mechanism, and `parse_machfile`'s pass-3 `validentry` test is what ties it back to
 *     `__TEXT` (`mach_loader.c:1878-1890`: inside an R|X segment or nothing);
 *   - `load_threadstate` only copies the blob into `result->threadstate`
 *     (`mach_loader.c:2000-2060`), and `exec_mach_imgact` applies it with
 *     `thread_setstatus(thread, flavor, ts, size)` -> `machine_thread_set_state`
 *     (`status.c:263-305`), which `memcpy`s the 17 words into the PcbData and then replaces the
 *     privileged bits of `cpsr` with the ones a fresh user thread has
 *     (`saved_state->cpsr = (cpsr & ~PSR_USER_MASK) | (old_psr & PSR_USER_MASK)`, where `old_psr` is
 *     `PSR_USERDFLT` = `PSR_USER_MODE` = 0x10, set by `machine_thread_state_initialize`).
 *
 * So `cpsr` here cannot make the thread privileged whatever it says, and the word written is 0x10
 * because that is the value the run below is *predicted* to show: the SPSR of an exception taken in
 * User mode is that user thread's CPSR, so the report should print `spsr = 0x10`.
 *
 * ------------------------------------------------------------------------------------------------
 * The one instruction, and what it proves
 * ------------------------------------------------------------------------------------------------
 *
 * `udf #0` (0xe7f000f0). An undefined instruction taken in **User mode** reaches slot 1 of this
 * image's vector page - `fleh_undef` in `entry_stubs.c`, still this image's handler - which reports
 * `xnu_entry_undef_pc = lr_und - 4`, `xnu_entry_undef_lr` and `xnu_entry_undef_spsr` through
 * `entry_panic_kv`, the buffer experiment 461 gave that handler precisely because the shared one was
 * full by then. 467 measured that route working: its `udf` in `DebuggerTrapWithState` came back as
 * `undef_pc = 0x800355ac` with `_dropped = 0`.
 *
 * So the instruction is the measurement, and it is a *two-sided* one: the address the report names
 * and the address this file enters at are the same number, computed here by the assembler from where
 * the word actually is. If the user's `__TEXT` mapping is not the physical page this object
 * occupies, or the fetch executes zeros, the report says so differently - a page of zeros from the
 * entry point to the segment's end would run off the end of `__TEXT` and raise a *prefetch abort* at
 * 0x2000 (`fleh_prefabt`), whose `ifar` and `spsr` are in the same report.
 *
 * What the instruction deliberately does not do is call the kernel: `svc` is slot 2, which is
 * `entry_stubs.c`'s own handler, and `fleh_swi` records that an exception happened and *not* where
 * (it takes no register readings), while XNU's `sleh_swi` - the real syscall path - is not reachable
 * from this vector page at all. Experiment 468's object is to prove the exec reached user mode and
 * to name the address it reached it at; the syscall path is the step after.
 */

    .syntax unified
    .arm

/* Apple's constants, spelled once. `mach-o/loader.h` and `mach/machine.h` are the definitions and
 * `tools/host_ramdisk_macho_check.py` reads these values back out of those headers, so a number here
 * that disagrees with them fails the build rather than the device run. */
    .equ MH_MAGIC,               0xfeedface
    .equ CPU_TYPE_ARM,           12
    .equ CPU_SUBTYPE_ARM_V7K,    12
    .equ MH_EXECUTE,             2
    .equ LC_SEGMENT,             0x1
    .equ LC_UNIXTHREAD,          0x5
    .equ VM_PROT_READ,           0x1
    .equ VM_PROT_WRITE,          0x2
    .equ VM_PROT_EXECUTE,        0x4
    .equ ARM_THREAD_STATE,       1
    .equ ARM_THREAD_STATE_COUNT, 17

/* The shape of the three load commands, and the two numbers derived from them. Neither
 * `sizeofcmds` nor the entry point is written down: the first is an expression over the label the
 * commands end at and the second is `TEXT_VMADDR` plus the file offset of the instruction, so
 * neither can drift from the bytes below them. */
    .equ SEGMENT_CMD_SIZE,       56     /* cmd, cmdsize, segname[16], vmaddr, vmsize, fileoff,
                                         * filesize, maxprot, initprot, nsects, flags */
    .equ THREAD_CMD_SIZE,        84     /* cmd, cmdsize, flavor, count, 17 state words */
    .equ RAMDISK_BYTES,          0x2000 /* two pages: `si_devsize`, and the whole file - see the
                                         * header. A multiple of 512, which is what makes `mdev`'s
                                         * `DKIOCGETBLOCKCOUNT` answer this exact byte count. */
    .equ PAGEZERO_VMSIZE,        0x1000
    .equ TEXT_VMADDR,            0x1000
    .equ TEXT_VMSIZE,            0x1000
    .equ TEXT_FILESIZE,          0x1000

    .section .data.ramdisk, "aw", %progbits
    .align 12                           /* `mdevadd` takes the base in pages, and a misaligned base
                                         * would round down to memory this image does not own */

    .global g_stage90_ramdisk
    .type g_stage90_ramdisk, %object
g_stage90_ramdisk:

/* --- struct mach_header, 28 bytes -------------------------------------------------------------- */
    .long MH_MAGIC                      /* +0   magic */
    .long CPU_TYPE_ARM                  /* +4   cputype */
    .long CPU_SUBTYPE_ARM_V7K           /* +8   cpusubtype */
    .long MH_EXECUTE                    /* +12  filetype */
    .long 3                             /* +16  ncmds */
    .long sizeofcmds_value              /* +20  sizeofcmds: the commands' own extent, below */
    .long 0                             /* +24  flags: no MH_DYLDLINK (static) and no MH_PIE
                                         *      (slide 0, absolute vmaddrs) */

/* --- __PAGEZERO, LC_SEGMENT, 56 bytes ---------------------------------------------------------- */
    .long LC_SEGMENT                    /* +28  cmd */
    .long SEGMENT_CMD_SIZE              /* +32  cmdsize */
    .ascii "__PAGEZERO"                 /* +36  segname[0..9] */
    .zero 6                             /* +46  the rest of the 16-byte name, NUL-padded */
    .long 0                             /* +52  vmaddr */
    .long PAGEZERO_VMSIZE               /* +56  vmsize: what vm_map_raise_min_offset is given */
    .long 0                             /* +60  fileoff */
    .long 0                             /* +64  filesize: zero, which is the shape load_segment
                                         *      tests for (mach_loader.c:1645) */
    .long 0                             /* +68  maxprot: VM_PROT_NONE */
    .long 0                             /* +72  initprot: VM_PROT_NONE */
    .long 0                             /* +76  nsects */
    .long 0                             /* +80  flags */

/* --- __TEXT, LC_SEGMENT, 56 bytes -------------------------------------------------------------- */
    .long LC_SEGMENT                    /* +84  cmd */
    .long SEGMENT_CMD_SIZE              /* +88  cmdsize */
    .ascii "__TEXT"                     /* +92  segname[0..5] */
    .zero 10                            /* +98  the rest of the name */
    .long TEXT_VMADDR                   /* +108 vmaddr */
    .long TEXT_VMSIZE                   /* +112 vmsize */
    .long 0                             /* +116 fileoff: zero, and filesize is not */
    .long TEXT_FILESIZE                 /* +120 filesize */
    .long (VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE)  /* +124 maxprot */
    .long (VM_PROT_READ|VM_PROT_EXECUTE)                /* +128 initprot: R|X, which
                                         *      found_header_segment and validentry both test */
    .long 0                             /* +132 nsects: no sections, and load_segment's own check
                                         *      is `total_section_size / sizeof(section) < nsects`,
                                         *      which 0 satisfies */
    .long 0                             /* +136 flags */

/* --- LC_UNIXTHREAD, 84 bytes ------------------------------------------------------------------- */
    .long LC_UNIXTHREAD                 /* +140 cmd */
    .long THREAD_CMD_SIZE               /* +144 cmdsize */
    .long ARM_THREAD_STATE              /* +148 flavor: the only one thread_entrypoint takes */
    .long ARM_THREAD_STATE_COUNT        /* +152 count: 17, the least thread_userstack takes */
    .rept 13                            /* +156 r[0..12]: all zero, nothing reads them */
    .long 0
    .endr
    .long 0                             /* +208 sp: zero, so the kernel picks USRSTACK and
                                         *      allocates the stack itself */
    .long 0                             /* +212 lr: zero; the thread never returns */
    .long entry_pc_value               /* +216 pc: the instruction below, as an expression */
    .long 0x10                          /* +220 cpsr: PSR_USER_MODE. machine_thread_set_state
                                         *      keeps only the flags out of this word and takes the
                                         *      mode from PSR_USERDFLT, so it cannot make the thread
                                         *      privileged; the value is here because the report
                                         *      should read it back as the SPSR of the fault */

/* The end of the load commands, and the two numbers the header above is built from. Neither is
 * written down as a literal: `sizeofcmds` is the commands' own extent and the entry point is
 * `TEXT_VMADDR` plus the file offset of the instruction, so neither can drift from the bytes.
 * Both are differences of two labels in this section, which the assembler resolves itself. */
load_commands_end:

/* Everything above is the load commands, and this is where the code they describe begins. The
 * header's `pc` is this label's file offset plus `__TEXT`'s `vmaddr`, so the address in the header
 * and the address in the image are one expression and cannot disagree. */
entry_udf:
    .word 0xe7f000f0                    /* udf #0 - see the header */

    .equ sizeofcmds_value, (load_commands_end - g_stage90_ramdisk) - 28
    .equ entry_pc_value,   TEXT_VMADDR + (entry_udf - g_stage90_ramdisk)

/* The two assertions that make the rest of this file checkable rather than merely commented.
 * `sizeofcmds` is the number `parse_machfile` reads the commands with, and a value that disagreed
 * with the bytes would be a load command the kernel never sees; the instruction has to be inside
 * `__TEXT`'s **file** range, because `pc` outside the segment it is loaded from is `validentry` = 0
 * and `LOAD_FAILURE` at `parse_machfile`'s pass 3. */
    .if sizeofcmds_value != 0xC4
    .error "sizeofcmds is not 0xC4 - the load commands are not the three this file describes"
    .endif
    .if ((entry_udf - g_stage90_ramdisk) >= TEXT_FILESIZE)
    .error "the instruction is outside __TEXT's filesize, so validentry would be 0"
    .endif
    .if ((entry_pc_value < TEXT_VMADDR) || (entry_pc_value >= (TEXT_VMADDR + TEXT_VMSIZE)))
    .error "the entry point is outside the segment it is loaded from"
    .endif

/* The rest of the segment is zeros, and they are *file* bytes rather than a `.bss` tail: the whole
 * of this object is what `/sbin/launchd` reads, and `__TEXT`'s `filesize` says the first 0x1000 of
 * it is the segment. Padded here rather than left to the linker so that the object's size is the
 * device's size - which is the check `build_entry.sh` makes over the linked image. */
    .zero RAMDISK_BYTES - (. - g_stage90_ramdisk)
    .size g_stage90_ramdisk, . - g_stage90_ramdisk
