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
 * The program, and what it proves
 * ------------------------------------------------------------------------------------------------
 *
 * Twenty-seven instructions, and they are the first thing `/sbin/launchd` runs:
 *
 *     entry_code:  svc  #0x80                ; +0   getpid() - a *Unix* syscall, r12 = +20
 *                  cmp  r0, #EXPECTED_PID    ; +4   the pid the kernel assigned this process?
 *                  bne  entry_failed         ; +8
 *                  mov  r0, #0               ; +12  mmap(0, 0x1000, PROT_READ|PROT_WRITE,
 *                  movw r1, #MMAP_LENGTH     ; +16                MAP_PRIVATE|MAP_ANON, -1, 0)
 *                  mov  r2, #MMAP_PROT       ; +20
 *                  movw r3, #MMAP_FLAGS      ; +24
 *                  mvn  r4, #0               ; +28  fd = -1
 *                  movw r5, #MMAP_PAD_MARKER ; +32  the word between fd and the off_t, unread
 *                  mov  r6, #0               ; +36  the 64-bit off_t's low word (r6, r8 - see below)
 *                  mov  r8, #0               ; +40  ... and its high word
 *                  mov  r12, #SYS_MMAP       ; +44
 *                  svc  #0x80                ; +48
 *                  bcs  entry_failed         ; +52  the *carry* is the errno convention, not a sign
 *                  ldr  r3, [r0]             ; +56  <- the read: a page nothing has touched
 *                  cmp  r3, #0               ; +60      ... a page the kernel never wrote is zero
 *                  bne  entry_failed         ; +64
 *                  str  r0, [r0]             ; +68  <- the write: the read mapped it read-only
 *                  ldr  r1, [r0]             ; +72
 *                  cmp  r1, r0               ; +76      ... and now the process owns it, writable
 *                  bne  entry_failed         ; +80
 *     spin:        mov  r12, #SYS_GETPID     ; +84
 *                  svc  #0x80                ; +88  ask again, so the loop's liveness is a record
 *                  cmp  r0, #EXPECTED_PID    ; +92
 *                  bne  entry_failed         ; +96
 *                  b    spin                 ; +100
 *     entry_failed: udf #1                   ; +104 the kernel answered something else
 *
 * **The first three are 479's five minus its loop, unchanged, and they are why this program does not
 * end in a fault.** Until 479 the first instruction was `udf #0`, and the address it named was the
 * whole measurement: an undefined instruction taken in **User mode** reaches slot 1 of this image's
 * vector page, whose report gives `undef_pc = lr_und - 4`, `_lr` and `_spsr` (474, 475, 477). That
 * worked - 475 measured `xnu_live_undef_pc = 0x000010e0` with `_spsr = 0x10` and `_user = 1` - and it
 * ended the boot: 478's run shows the kernel triaging the bad instruction, killing pid 1 with SIGILL
 * (`pid 1 exited -- exit reason namespace 2 subcode 0x4`) and panicking in `launchd_crashed_panic`,
 * which `proc_prepareexit` makes unconditional for `initproc`. **So the marker had to go, and what
 * replaced it had to be something the kernel can *service*.**
 *
 * **`getpid` is that something, and it is chosen because it answers the process instead of parking
 * it.** The first draft of 479 took the 478 doc's own suggestion - `thread_switch`
 * (`mach_trap_table[61]`, `osfmk/kern/syscall_sw.c:166`), "the one user-visible primitive whose
 * effect is to give the CPU up" - and reading `thread_switch` in
 * `osfmk/kern/syscall_subr.c:238-380` is what rules it out: every option it accepts ends in a
 * *block*, and in this image a block is a hang.
 *
 *   - `SWITCH_OPTION_WAIT` (2) calls `assert_wait_timeout(..., option_time = 0, ...)` first, and
 *     `clock_interval_to_deadline(0, ...)` makes the deadline *now* -
 *     `assert_wait_timeout` arms `thread->wait_timer` and marks the thread `TH_WAIT`. The only thing
 *     that fires a waitq timer is the timer interrupt, and this image has no timer (it is still
 *     owed), so the timer never fires and nothing else ever wakes an event that has no sender.
 *   - `SWITCH_OPTION_NONE` (0) is worse, not better: it reaches
 *     `thread_block_reason(thread_switch_continue, NULL, AST_YIELD)` with **no wait asserted at
 *     all**. `thread_select` then finds the thread *"eligible to keep running"* only while
 *     `(state & (TH_TERMINATE|TH_IDLE|TH_WAIT|TH_RUN|TH_SUSP)) == TH_RUN`; with no other runnable
 *     thread of equal priority it returns the idle thread and the blocked thread is on no run queue,
 *     so no later `thread_setrun` can ever name it. A block is defined by what wakes it, and this
 *     fixture has nobody to be woken by.
 *
 * What is left is a syscall that **returns a value**, and `getpid` is the smallest one: it reads
 * `p->p_pid` and hands it back, touching no lock that can wait, no port, no timer and no scheduler.
 *
 * **The answer is the reading, because the kernel is what chose it.** `bsd_utaskbootstrap` clones
 * the init process out of `kernproc` and then holds it by name - `initproc = proc_find(1)`
 * (`bsd/kern/bsd_init.c:1147`) - and `load_init_program(p)` (`:1079`) is what execs this Mach-O into
 * it; 478's run measured the same identity from the other side, `pid 1 exited -- exit reason
 * namespace 2 subcode 0x4`. So `getpid` returning 1 says all of: the trap reached the kernel's own
 * *Unix* dispatcher, `sysent[20]` is `getpid`, the call ran on *this* proc, and the ABI wrote the
 * value into the register the *caller* reads. A wrong answer cannot pass silently either: `cmp r0,
 * #EXPECTED_PID` sends the fixture to `udf #1`, and 478 proved that instruction reaches slot 1's
 * report and then `launchd_crashed_panic` with the trap record's format string in `r9`.
 *
 * `r12 = +20` is a **Unix** syscall, and that is the other half of the ABI this step measures.
 * `fleh_swi` computes `r5 = -r12` and calls `fleh_swi_unix` when that is `<= 0`, so a positive number
 * in r12 is BSD and a negative one is a mach trap: 478's run measured the mach side of that branch
 * (`thread_block`'s fifteen returns) and 479's measures the BSD side. `SYS_getpid` = 20 is
 * `bsd/kern/syscalls.master`'s line `20 AUE_GETPID ALL { int getpid(void); }`, which
 * `tools/host_ramdisk_macho_check.py` reads back out of that file, and the entry the kernel will look
 * up is read back out of the **linked image** by `tools/check_sysent_table.py` - the same 20, the same
 * `getpid`, plus the neighbouring entries that make the stride a checked fact rather than a
 * convention.
 *
 * ------------------------------------------------------------------------------------------------
 * What 480 adds: process 1's own memory, and the first *user* data abort this walk has ever taken
 * ------------------------------------------------------------------------------------------------
 *
 * **The second syscall is `mmap`, and the two instructions after it are the step's whole object.**
 * Every `xnu_live_sleh_*` record this project has ever read - four of them, in 479's run - is
 * `_user = 0`: the kernel's *own* data aborts, serviced and retried, which is what made the exec
 * work. The user side of that handler has never run, because the only user-mode exception this image
 * has taken is a *trap* (`udf`, slot 1, split by 477) and never an *abort* (slot 4). It cannot run
 * until the process has a page of its own that is not resident, and no fixture so far has had one:
 * the exec's stack is the kernel's, and its own memory is the image, which is `R|X`.
 *
 * So `mmap(0, 0x1000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0)` asks the kernel for one
 * page, and the fixture then does the two accesses that page's life consists of:
 *
 *   - `ldr r3, [r0]` - a **read of a page nothing has touched**, so it faults. `vm_fault` resolves
 *     it against the anonymous map entry `mmap` just entered, and for a read the classic answer is
 *     the zero-filled page mapped read-only, so the retry reads 0 and `cmp r3, #0` holds;
 *   - `str r0, [r0]` - a **write to a page that is now mapped read-only**, so it faults *again*, by a
 *     different route (`vm_fault` has to give the process its own writable page), and the value it
 *     writes is the address `mmap` returned, so `ldr`/`cmp` afterwards prove the page is the
 *     process's. Two faults, two `slot 4` entries, two records where there were none - and a third
 *     reading on top of them: the getpid loop keeps running afterwards, which says the fault was
 *     *serviced and retried* rather than fatal.
 *
 * **The arguments are the other half of what this step measures, and they are why the program is
 * twenty-seven instructions rather than five.** On this target `arm_get_syscall_args`
 * (`bsd/dev/arm/systemcalls.c:337`) is the *munging* one - the file's
 * `#if __arm__ && (__BIGGEST_ALIGNMENT__ > 4)` is on, because the host is `CPU_SUBTYPE_ARM_V7K` - so
 * a BSD syscall's arguments are marshalled out of the saved state by `sysent[197].sy_arg_munge32`,
 * which is `munge_wwwwwl`: in **direct** style (`r12 != 0`) it takes `munge_wlll`, which copies six
 * words from `r[0..5]` and then takes words six and seven from `r[6]` and `r[8]`. The generated
 * `struct mmap_args` (`out/xnu_generated/bsd/sys/sysproto.h`) is `addr, len, prot, flags, fd` - five
 * words - and then `off_t pos`, 8-byte aligned, at word six, so **the word between them is the
 * struct's padding and r5 lands in it unread**. `r6` and `r8` are the ABI's 64-bit register pair: a
 * 64-bit argument sits at an even index of the register sequence `r0..r6, r8` (r7 is the frame
 * pointer on this target), and after five word arguments the pair is index 6 and 7, i.e. `r6` and
 * `r8`. `getpid`'s `sy_arg_bytes` is zero (`{ int getpid(void); }`), which is why 479 could ignore
 * all of this and why its fixture is argument-free.
 *
 * **And the error path is not a sign.** `unix_syscall` calls the syscall and then
 * `arm_prepare_u32_syscall_return` (`systemcalls.c:279`), which on error puts the errno in `save_r0`
 * *and sets the carry bit* of the saved CPSR (`regs->cpsr |= PSR_CF`) - the convention libc's
 * `cerror` reads. So the instruction after `svc` is `bcs`, not `cmp`/`blt`: a `cmp` here would
 * overwrite the flag it is supposed to test, and a failed `mmap` would be read as a valid address.
 * (The positive return of a *successful* call is `_SYSCALL_RET_ADDR_T`'s `uu_rval[0]`, written by the
 * same function.)
 *
 * **What the program deliberately does not do is end in a fault, call `mmap` twice, or pass silently.**
 * `udf #1` is behind four independent checks - the pid, the errno, the fresh page's zero, and the
 * read-back - so a wrong answer cannot be mistaken for a right one. `mmap` is called **once**,
 * outside the loop: calling it every iteration would allocate a page per iteration, and after a few
 * million the map would be full, `mmap` would start returning an errno, and the fixture's own error
 * path would kill process 1 - a leak that ends in 478's panic. The loop keeps only the syscall that
 * cannot fill anything. Everything else in this file is unchanged: the header, the three load
 * commands, `sizeofcmds` 0xC4 (this step adds no load command), and `r[12]`'s initial value, which is
 * still the first syscall's number.
 *
 * The `.if` assertions below read those claims back out of the bytes rather than out of this prose:
 * the program's length, `sizeofcmds`, that the entry point is inside `__TEXT`, and that no branch in
 * the program leaves the file range it is loaded from.
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

/* The two syscalls the program makes, from the headers rather than from a disassembly: each number is
 * the one in `bsd/kern/syscalls.master`'s own line for that name, and the *sign* is the ABI and not a
 * convention - `fleh_swi` computes `r5 = -r12` and branches to the unix path when that is `<= 0`, so a
 * positive number in r12 is a BSD syscall and a negative one is a mach trap. `EXPECTED_PID` is the
 * identity this image's own boot gave the process: `bsd_utaskbootstrap` holds the init process by name
 * (`initproc = proc_find(1)`, `bsd/kern/bsd_init.c:1147`) and 478's console printed `pid 1 exited`.
 * `MMAP_LENGTH`/`MMAP_PROT`/`MMAP_FLAGS` are the kernel's own numbers for the one page this fixture
 * asks for: `PROT_READ|PROT_WRITE` and `MAP_PRIVATE|MAP_ANON` from `bsd/sys/mman.h`, with the length
 * being `1 << ARM_PGSHIFT` (`osfmk/arm/proc_reg.h`), `fd` -1 and offset 0, so the mapping needs no
 * vnode at all. `tools/host_ramdisk_macho_check.py` reads every one of them back out of those files -
 * the two syscall numbers from the master, the pid from the sentence above it, the page size from the
 * kernel's own page shift, `prot` and `flags` from `mman.h` - and decodes the twenty-seven words below
 * to check they are the program this comment describes, *including* the comparisons that use them. */
    .equ SYS_GETPID,             20
    .equ EXPECTED_PID,           1
    .equ SYS_MMAP,               197
    .equ MMAP_LENGTH,            0x1000
    .equ MMAP_PROT,              0x3    /* PROT_READ | PROT_WRITE */
    .equ MMAP_FLAGS,             0x1002 /* MAP_PRIVATE | MAP_ANON */
    .equ MMAP_PAD_MARKER,        0x5a5a /* r5, which the munger copies into the padding word of
                                         * `struct mmap_args` and `mmap` never reads - so a marker
                                         * here changes nothing about the call, and the wrapper
                                         * reading it back is what measures the layout */

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
    .long 0                             /* +156 r[0]: getpid's first argument register. `sy_narg`
                                         *      is 0 for the *first* syscall, so `arm_get_syscall_args`
                                         *      is never called for it and no register is read as an
                                         *      argument - they are zero because a zero is unreadable
                                         *      and a stale value would be a second, silent
                                         *      definition. The program sets every register the mmap
                                         *      below needs from inside itself */
    .long 0                             /* +160 r[1] */
    .long 0                             /* +164 r[2] */
    .rept 9                             /* +168 r[3..11] */
    .long 0
    .endr
    .long SYS_GETPID                    /* +204 r[12]: the *first* syscall's number, read by
                                         *      `fleh_swi` and then by `arm_get_syscall_number`. It is
                                         *      also the register that decides the argument *style*
                                         *      for the munger - nonzero is direct - so the program
                                         *      reloads it before every `svc` that is not this one */
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
 * and the address in the image are one expression and cannot disagree. Every offset below is the
 * offset of the instruction inside this label, which is what the header comment's listing and the
 * check tool's five-word decode both count from. */
entry_code:
    svc     #0x80                       /* +0: getpid() - the syscall number is r12 */
    cmp     r0, #EXPECTED_PID           /* +4: the pid the kernel reports for this process? */
    bne     entry_failed                /* +8 */

/* mmap(0, 0x1000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0). The direct style means the
 * syscall number is in r12, not r0, and the armv7k munger takes the arguments from r0..r5 plus the
 * 64-bit pair r6/r8 - see the ABI note in the header. r5 is not written here on purpose: the munger
 * copies it into the word of `struct mmap_args` that the compiler's alignment of `off_t` leaves as
 * padding, and `mmap` never reads it. */
    mov     r0, #0                      /* +12: addr = NULL - the kernel chooses the address */
    movw    r1, #MMAP_LENGTH            /* +16: len */
    mov     r2, #MMAP_PROT              /* +20: prot */
    movw    r3, #MMAP_FLAGS             /* +24: flags */
    mvn     r4, #0                      /* +28: fd = -1 - MAP_ANON needs no vnode */
    movw    r5, #MMAP_PAD_MARKER        /* +32: the word the munger puts between fd and the
                                         *      8-byte-aligned off_t, which `mmap` never reads -
                                         *      a marker here is free, and reading it back is how
                                         *      the layout below is measured rather than assumed */
    mov     r6, #0                      /* +36: offset, low word of the 64-bit off_t */
    mov     r8, #0                      /* +40: offset, high word */
    mov     r12, #SYS_MMAP              /* +44 */
    svc     #0x80                       /* +48 */
    bcs     entry_failed                /* +52: the carry bit is the errno convention - see above */

/* The page the kernel has just entered into this process's map, and the two accesses that page's
 * life consists of. The read faults because nothing has touched it; the write faults again because
 * the read mapped it read-only. `str r0, [r0]` writes the address into the address it names, so the
 * load-and-compare after it is the whole proof that the page is the process's own and writable. */
    ldr     r3, [r0]                    /* +56: <- the read: a page nothing has touched */
    cmp     r3, #0                      /* +60: ... and a page the kernel never wrote is zero */
    bne     entry_failed                /* +64 */
    str     r0, [r0]                    /* +68: <- the write: the read mapped it read-only */
    ldr     r1, [r0]                    /* +72 */
    cmp     r1, r0                      /* +76: ... and now the process owns it */
    bne     entry_failed                /* +80 */

/* And the syscall that cannot fill anything, so that the loop is alive after the fault and the log
 * says so: r12 has to be reloaded because the mmap above left 197 in it. */
spin:
    mov     r12, #SYS_GETPID            /* +84 */
    svc     #0x80                       /* +88: getpid() again */
    cmp     r0, #EXPECTED_PID           /* +92 */
    bne     entry_failed                /* +96 */
    b       spin                        /* +100 */

entry_failed:
    udf     #1                          /* +104: the kernel answered something else */
entry_code_end:

    .equ sizeofcmds_value, (load_commands_end - g_stage90_ramdisk) - 28
    .equ entry_pc_value,   TEXT_VMADDR + (entry_code - g_stage90_ramdisk)

/* The assertions that make the rest of this file checkable rather than merely commented.
 * `sizeofcmds` is the number `parse_machfile` reads the commands with, and a value that disagreed
 * with the bytes would be a load command the kernel never sees; the program has to be inside
 * `__TEXT`'s **file** range, because `pc` outside the segment it is loaded from is `validentry` = 0
 * and `LOAD_FAILURE` at `parse_machfile`'s pass 3. */
    .if sizeofcmds_value != 0xC4
    .error "sizeofcmds is not 0xC4 - the load commands are not the three this file describes"
    .endif
    .if ((entry_code_end - g_stage90_ramdisk) >= TEXT_FILESIZE)
    .error "the program is outside __TEXT's filesize, so validentry would be 0"
    .endif
    .if ((entry_pc_value < TEXT_VMADDR) || (entry_pc_value >= (TEXT_VMADDR + TEXT_VMSIZE)))
    .error "the entry point is outside the segment it is loaded from"
    .endif
/* And what the program's length is, because every branch in it is relative: a `b` that left the file
 * range would raise a fault instead of making a syscall, and the check tool decodes all 27 words by
 * offset. 27 words is the 5 of the getpid call, the 11 of the mmap call and its argument registers,
 * the 7 of the two faults, and the 4 of the loop. */
    .if (entry_code_end - entry_code) != 108
    .error "the program is not the twenty-seven instructions the header describes"
    .endif

/* The rest of the segment is zeros, and they are *file* bytes rather than a `.bss` tail: the whole
 * of this object is what `/sbin/launchd` reads, and `__TEXT`'s `filesize` says the first 0x1000 of
 * it is the segment. Padded here rather than left to the linker so that the object's size is the
 * device's size - which is the check `build_entry.sh` makes over the linked image. */
    .zero RAMDISK_BYTES - (. - g_stage90_ramdisk)
    .size g_stage90_ramdisk, . - g_stage90_ramdisk
