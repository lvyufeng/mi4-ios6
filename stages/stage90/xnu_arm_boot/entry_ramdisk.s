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
 * Seventy-nine instructions, and they are the first thing `/sbin/launchd` runs:
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
 *                  mov  r9, r0               ; +84  503 keeps the page for 504's read (r9 survives
 *                  mov  r0, #0               ; +88      a syscall: the return path writes r0 and r1)
 *                  mov  r1, #0               ; +92  poll(NULL, 0, 5)  - the kernel's own deadline,
 *                  movw r2, #POLL_SHORT_MS   ; +96      no descriptor waited on, so the timeout is
 *                  mov  r12, #SYS_POLL       ; +100     the whole of the call - and then the same
 *                  svc  #0x80                ; +104     call again eight times longer, because one
 *                  mov  r0, #0               ; +108     sample cannot tell a countdown from a fixed
 *                  mov  r1, #0               ; +112     latency. **Neither branches on the answer**:
 *                  movw r2, #POLL_LONG_MS    ; +116     a wrong one is a fact about the kernel's
 *                  mov  r12, #SYS_POLL       ; +120     timer path, and a fault here kills initproc.
 *                  svc  #0x80                ; +124
 *                  adr  r0, path_rmd0        ; +128 open("/dev/rmd0", O_RDONLY, 0) - the first call in
 *                  mov  r1, #0               ; +132     this image that a *driver* answers: `mdev`
 *                  mov  r2, #0               ; +136     made this node at boot and `mdevopen`
 *                  mov  r12, #SYS_OPEN       ; +140     returns 0 without touching the disk.
 *                  svc  #0x80                ; +144
 *                  mov  r1, r9               ; +148 read(fd, page, 4) - `mdevrw`'s `uiomove64` copies
 *                  mov  r2, #READ_BYTES      ; +152     the RAM disk's first bytes into the page
 *                  mov  r12, #SYS_READ       ; +156     `mmap` just gave this process, so what the
 *                  svc  #0x80                ; +160     log reads back is this file's own magic
 *                  adr  r0, path_missing     ; +164 open("/dev/nosuch", O_RDONLY, 0) - the control:
 *                  mov  r1, #0               ; +168     a name devfs cannot have a node for, so the
 *                  mov  r2, #0               ; +172     pair separates a lookup that worked from an
 *                  mov  r12, #SYS_OPEN       ; +176     open that returns something either way.
 *                  svc  #0x80                ; +180
 *                  mov  r0, #0               ; +184 fork() - a word with no reader: the slot's munger
 *                  mov  r12, #SYS_FORK       ; +188     word is NULL, its narg is 0, and both processes
 *                  svc  #0x80                ; +192     are handed the pid rather than this value
 *                  cmp  r1, #CHILD_FLAG      ; +196 506's register: the flag `thread_set_child` writes
 *                  beq  entry_child          ; +200     for the child, 1 there and 0 in the parent -
 *                  b    entry_parent         ; +204     505 branched on r0, which is the pid in both
 *     entry_child: mov  r0, #EXIT_RVAL      ; +208 the child leaves by the syscall that cannot
 *                  mov  r12, #SYS_EXIT       ; +212     return, and that from pid 1 is fatal
 *                  svc  #0x80                ; +216
 *     entry_parent: mov r1, r9              ; +220 508: the parent's half, and the third of the three
 *                  mov  r2, #0               ; +224     calls a Unix process's life is. `wait4(2,
 *                  mov  r3, #0               ; +228     &status, 0, NULL)`: r0 is still the pid `fork`
 *                  mov  r12, #SYS_WAIT4      ; +232     returned in *both* processes, r1 is the page
 *                  svc  #0x80                ; +236     504's read was given, and rusage is NULL so
 *                  cmp  r0, #WAIT_PID        ; +240     `p_ru` is never touched. The answer is the
 *                  bne  entry_failed         ; +244     pid (r0, `retval[0] = p->p_pid`) and, through
 *                  ldr  r3, [r9]             ; +248     the pointer, the word the kernel composed
 *                  cmp  r3, #EXIT_STATUS     ; +252     - `0xffff & p->p_xstat`
 *                  bne  entry_failed         ; +256     (`kern_exit.c:1782`), which is
 *                  mov  r0, #WAIT_PID        ; +260     `W_EXITCODE(EXIT_RVAL, 0)` = 0x300 by the
 *                  mov  r1, r9               ; +264     time it gets there. Then the same call again,
 *                  mov  r2, #0               ; +268     on the pid of a child that has been reaped:
 *                  mov  r3, #0               ; +272     a second `wait4` is the kernel's own
 *                  mov  r12, #SYS_WAIT4      ; +276     statement that there is no such process,
 *                  svc  #0x80                ; +280     and a wrong answer *is* recorded rather than
 *                  b    park                 ; +284     branched on - see below
 *     park:        mov  r0, #0               ; +288 512 parks the process instead of spinning it:
 *                  mov  r1, #0               ; +292     poll(NULL, 0, PARK_MS) with the answer
 *                  movw r2, #PARK_MS         ; +296     recorded and never tested, so the kernel
 *                  mov  r12, #SYS_POLL       ; +300     has nothing left to run and its own idle
 *                  svc  #0x80                ; +304     path does the work instead
 *                  b    park                 ; +308     (an expiring timeout just parks again)
 *     entry_failed: udf #1                   ; +312 the kernel answered something else
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
 *     that fires a waitq timer is the timer interrupt, and in 479 this image had no timer (it was
 *     still owed), so the timer never fired and nothing else ever woke an event that has no sender.
 *     **503 gave the kernel one, and the two asks further down measure it** - which is what makes
 *     512's park possible at all: the objection above is an objection to a block, and a block with a
 *     working countdown behind it is a *sleep*. What 479 settled on instead of one was `getpid`, and
 *     what 512 changed is which of the two the process's last act is.
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
 * **It is no longer the process's last act** - 512 replaced the loop that called it with a park, for
 * the reason given at `park` below - and it stays as the program's first act and as 479's reading of
 * the ABI.
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
 *
 * ------------------------------------------------------------------------------------------------
 * What 503 adds: the two asks, and the clock that answers them
 * ------------------------------------------------------------------------------------------------
 *
 * 480 gave process 1 a page of its own, which is a fact about the *kernel's memory*. What every
 * driver in this image has been missing since 419 is time: the boot's own threads park on deadlines
 * and nothing has ever woken one of them, because until 483 this machine had no timer interrupt at
 * all and no run has ever made a *process* ask for a deadline. `poll(NULL, 0, ms)` is that ask.
 *
 * **It is a block, and the block is the object.** The chain is
 * `poll` -> `poll_nocancel` -> `kqueue_scan` -> `waitq_assert_wait64_leeway` (which arms the thread's
 * own `wait_timer` through `timer_call_enter_with_leeway`, `osfmk/kern/waitq.c:2565`) ->
 * `thread_block_parameter`; the expiry runs in the interrupt 483 armed, as
 * `rtclock_intr` -> `timer_intr` -> `timer_queue_expire` -> `thread_timer_expire` ->
 * `clear_wait_internal(thread, THREAD_TIMED_OUT)` (`osfmk/kern/sched_prim.c:639`); and
 * `kqueue_scan`'s own switch maps `THREAD_TIMED_OUT` to `EWOULDBLOCK`, which `poll` turns into a
 * return of 0 with `retval` 0 (`bsd/kern/sys_generic.c:1816-1820`). Not one of those links has ever
 * been exercised by a *process* on this machine, and the two ends of it are what the run reads: the
 * deadline the kernel computed for the wait timer, and the duration the call actually took.
 *
 * **Two asks, and the ratio is the point.** The first is 5 ms and the second 40. A wake that came
 * from anywhere but the countdown - the scheduler's quantum, a stray interrupt, a polling loop -
 * would return after roughly the same number of ticks whatever was asked for, and one sample cannot
 * tell that from a clock. The wrapper measures each call's duration in `mach_absolute_time`'s own
 * ticks, so the pair is a prediction with a *shape* rather than a single number: at the 19.2 MHz this
 * target counts at, 5 ms is 96000 ticks and 40 ms is 768000, and the two readings have to be in that
 * ratio.
 *
 * **And nothing here branches on the answer, which is a change of kind rather than of degree.**
 * Every other check in this program ends at `udf #1`, and this program *is* `/sbin/launchd`: a fault
 * taken in it kills `initproc`, whose death `proc_prepareexit` turns into
 * `launchd_crashed_panic` - 478's run measured exactly that. A `poll` that returned an errno is a
 * reading about the kernel's timer path, so the fixture makes both calls and falls into the loop;
 * the wrapper publishes the arguments, the return and the duration either way, and the loop's own
 * liveness (`getpid` still being answered after both blocks) is the reading that says control came
 * back to user mode. A step that could only report a success would be a step that cannot fail.
 *
 * ------------------------------------------------------------------------------------------------
 * What 504 adds: the first call a *driver* answers, and the value it hands back
 * ------------------------------------------------------------------------------------------------
 *
 * 480 gave process 1 a page; 503 gave it a deadline. Both are the *kernel's* own services - memory
 * and time. What no user-mode instruction in this image has ever done is reach a **device**: the
 * `svc` path so far ends in `kern_mman` or in the timer queue, and neither of those is a thing a
 * driver owns. `/sbin/launchd` on a real machine opens character devices before it does anything
 * else, and the first thing it can open *here* was already named in the console log before this
 * program ran - `Added memory device md0/rmd0 (02000000/0D000000) at 000000008050D000 for
 * 0000000000002000` is `bsd/dev/memdev.c`'s own `mdevadd` print, and the two nodes it made two lines
 * later are this file's `RAMDISK_BYTES` pages.
 *
 * **`/dev/rmd0` and not `/dev/md0`.** `mdevadd` makes *both* (`:607` and `:616`: `devfs_make_node`
 * with `DEVFS_BLOCK` and `"md%d"`, then `DEVFS_CHAR` and `"rmd%d"`), and the difference that decides
 * this fixture is which entry point `read` reaches. The character device's `read` *is* `mdevrw`
 * (`mdevcdevsw`, `:146`) - the function whose `uiomove64` copies from the RAM disk - while the block
 * device has no `d_read` at all (`mdevbdevsw`, `:133`), so a read on it is a `spec_read` into the
 * buffer cache and `mdevstrategy`, a different path with its own `buf_map` and its own trimming at
 * the end of the device. The character device is the one whose copy is one function, and a fixture
 * that wants to read one word of its own image wants the short path.
 *
 * **And `md0` is the root device**, which is why the *read* had to be thought about before it was
 * written: `mdevrw` is safe for a user buffer here only because `IOKitBSDInit` adds this device with
 * `phys` 0 (`iokit/bsddev/IOKitBSDInit.cpp:447`, `mdevadd(-1, ml_static_ptovirt(...) >> 12, ..., 0)`)
 * - so `mdFlags` has no `mdPhys` and the copy is a plain `uiomove64` to the address the process
 * passed. With that bit set the same call would treat the *virtual* user address as a physical one.
 * So the fixture's buffer is the page 480's `mmap` returned, the length is one word, and the word it
 * reads is the RAM disk's first four bytes - **this file's own `MH_MAGIC`**.
 *
 * **That is the reading, and it is self-checking: the page held something else a moment ago.** 480's
 * store wrote the page's *address* into the page (`str r0, [r0]`), so the word at the buffer is the
 * address `mmap` returned when the read is made, and `0xfeedface` afterwards. The wrapper reads those
 * two words either side of the call and publishes both, so the pair says the driver moved data into
 * a page whose previous content is in the same record. A `read` that returned an errno leaves the
 * address in place, which is a reading too - and neither possibility is fatal, because nothing here
 * branches on the answer.
 *
 * **The control is a name devfs cannot have a node for.** `open("/dev/nosuch", O_RDONLY, 0)` after
 * the real one: devfs resolves names against a tree that drivers populate, so a name no driver ever
 * made has no vnode and `namei` answers `ENOENT` = 2 - the same errno XNU's own loader printed for
 * its own missing path three lines earlier (`load_init_program: failed loading
 * /usr/local/sbin/launchd.development: errno 2`). A pair of opens whose second is *also* answered
 * positively would mean the first told us nothing; a pair in which both fail would mean the path
 * shape is wrong rather than the driver missing. The `/dev/` prefix and the name are the fixtures'
 * own choice - they are checked as properties, against `memdev.c`'s two format strings, in
 * `tools/host_ramdisk_macho_check.py`.
 *
 * **The addresses come from `adr`, not from a number written down.** `adr r0, path_rmd0` is a
 * PC-relative `add` that the assembler resolves within this section, so the path's address appears
 * *once* in this file - in the instruction that computes it - and the strings below live inside
 * `__TEXT`'s file range where the mapping that carries this program carries them too. A literal
 * address here would be the project's oldest defect: one value with two definitions and nothing
 * comparing them. The check decodes both `adr`s and requires each to land on the bytes of its own
 * path string, which it finds by scanning the file for them.
 *
 * ------------------------------------------------------------------------------------------------
 * What 505 adds: the first process the OS makes for itself, and the first death that is not fatal
 * ------------------------------------------------------------------------------------------------
 *
 * 504's document ended with the ceiling in its own words: user mode "can now ask the kernel for time,
 * for a page, for a vnode by name, and for bytes out of a device, and the kernel answers all four.
 * What it still cannot do is *be* init: the fixture reads one word and spins." Every one of those four
 * is the *kernel* doing work for one process; what no instruction in this image has ever made the OS
 * do is **make another process** - `load_init_program` created process 1 out of `kernproc`
 * (`bsd/kern/bsd_init.c:1079`, `:1147`) before this program existed, and nothing since has asked for a
 * second one.
 *
 * **`fork` is that ask, and it is the one call in this program that returns twice.** `2
 * AUE_FORK ALL { int fork(void) }` reaches `sysent[2].sy_call` - `bsd/kern/kern_fork.c:872` - which
 * builds the child out of this process (`cloneproc` -> `forkproc` -> `fork_create_child` ->
 * `task_create_internal(..., inherit_memory = TRUE, ...)`, `kern_fork.c:1000-1030`, `:760`) and then
 * does the one thing that makes two processes out of one call: `retval[0] = child_proc->p_pid` for the
 * caller, and `task_clear_return_wait` for the child (`:895`, `:905`).
 *
 * **The two answers come from two different places, and the fixture's `mov r0, #0` is the one it
 * controls.** The *parent* returns through `unix_syscall`'s tail, where
 * `arm_prepare_u32_syscall_return` writes `save_r0 = uu_rval[0]` (`bsd/dev/arm/systemcalls.c:293`) -
 * the pid the kernel just made. The *child* never runs that code: its saved state was copied word for
 * word out of the parent's by `machine_thread_dup` (`osfmk/arm/status.c:469`, a `bcopy` of
 * `machine.PcbData`) while the parent was inside the `svc`, and it resumes at `load_and_go_user`
 * through `task_wait_to_return` -> `thread_bootstrap_return` (`osfmk/kern/task.c:479`,
 * `osfmk/arm/locore.s:1904`) with whatever `save_r0` that copy holds. **And that copy is not the last
 * writer.** `fork1` calls `thread_dup(child_thread)` and then, forty-six lines later,
 * `thread_set_child(child_thread, child_proc->p_pid)` (`kern_fork.c:590`, `:636`), whose comment names
 * its purpose - "this is what gives the child process its 'return' value from a fork() call" - and
 * ARM's body writes `r[0] = pid; r[1] = 1` (`osfmk/arm/status.c:722-730`; the object's whole function is
 * `mov r2, #1` / `str r1, [r0, #848]` / `str r2, [r0, #852]` / `bx lr`, and the ARM64 port writes the
 * same pair at `osfmk/arm64/status.c:1253`). So the child is handed the **pid**, not zero, and the word
 * that tells the two halves apart is **`r1`**: 1 in the child, 0 in the parent, whose `r1` is
 * `uu_rval[1]` as `fork` zeroed it (`kern_fork.c:895`).
 *
 * **The 505 run measured that, and this paragraph is the correction.** Its readings are
 * `xnu_live_fork_ret_lo = 2` to the parent, the child's first `getpid` returning **2** at call
 * `0x5e8b`, and the child's own `udf #1` at `0x11d0`: the `cmp r0, #0` below sends the child to
 * `b spin`, its spin asks for its pid, is told 2 instead of 1, and `bne entry_failed` finishes it. What
 * this paragraph used to predict - "`save_r0` has exactly one writer in this tree ... the child's is
 * the register the fixture set before the `svc`: **zero**" - was a claim about a register no check in
 * this build could read, and it was wrong in the one word the run *could* read. So the fixture's
 * `mov r0, #0` is a word with no reader at all: not an argument (`sy_narg` 0, munger NULL), overwritten
 * in the parent by the return path and in the child by `thread_set_child`. **506's step is the branch
 * below, not the kernel.**
 *
 * **The `mov r0, #0` is also a word the kernel does not read, and that claim stands.** `fork`
 * takes no arguments: `sy_narg` is 0 and the munger word is NULL, so `arm_get_syscall_args` is never
 * called for this slot and `uu_arg[0]` keeps whatever the *previous* call left in it - the ENOENT
 * `open`'s path address, `0x000011e0` in 505's run (`xnu_live_fork_uap0`, and the same word the
 * `open` record published a moment earlier). The wrapper publishes that word beside the return, so the
 * run says in one record that the child's `r0` came from neither the argument buffer nor this
 * instruction.
 *
 * **Then the child ends, and that is the step's other half.** `1 AUE_EXIT ALL { void exit(int rval) }`
 * reaches `sysent[1].sy_call` - `bsd/kern/kern_exit.c:677`, `__attribute__((noreturn))` - whose
 * `exit1(p, W_EXITCODE(uap->rval, 0), retval)` reaches `exit_with_reason`, and whose first branch is
 * the one that has killed every previous run: `if (p == initproc) launchd_crashed_panic(p, rv);` in
 * `proc_prepareexit` (`:849`), the function 478's run measured as the end of the boot. `p` here is
 * **pid 2**, so that branch is not taken, and the run's readings are the ones that say which arm ran:
 * the pid and the argument at the `exit` slot, and - after the child is a zombie -
 * `psignal(pp, SIGCHLD)` at `proc_exit`'s `:1443`, where `pp` is the parent, i.e. **pid 1**: the
 * first inter-process event in this walk, and the number the OS chose to tell process 1 with is
 * `SIGCHLD` = 20 (`bsd/sys/signal.h:108`). **The 505 run took neither of those arms** - the child went
 * down the parent's, so no `xnu_live_exit_*`, no `xnu_live_sigchld_*` and no `psignal` record appears
 * in its log, and those readings belong to the step that fixes the branch. **Run 2 took the first and
 * not the second**, and the reason is falsifier (b) below.
 *
 * **The composed status 0x300 is not published by this instrument, and that is the one number in this
 * file's prediction that the run corrected.** `xnu_live_exit_rval` is `uap->rval` as the munger
 * marshalled it - the wrapper's own header says so, and the ABI it is declared by is
 * `(*(callp->sy_call))(proc, &uthread->uu_arg[0], &uthread->uu_rval[0])` - so the record is **3**.
 * `W_EXITCODE(3, 0)` is computed *inside* the kernel, by `exit`'s own
 * `exit1(p, W_EXITCODE(uap->rval, 0), retval)` (`kern_exit.c:684`), and lands in `p->p_xstat`
 * (`:826`); nothing in this image reads that field back. A prediction written into a header is a
 * claim like any other, and this one named the argument's key for the composed value.
 *
 * **Nothing here branches on any of those answers**, for 503's and 504's reason, and the reason is
 * sharper here than anywhere before it: `cmp r0, #0` is not a check on the kernel, it is the *only*
 * way a single instruction stream can be two programs. A wrong answer from `fork` - an errno taken as
 * a pid, or **a child that saw the pid instead of zero** - leaves `initproc` in the parent arm, which
 * is the arm it was in before this step, and the run's records say so rather than the boot ending.
 * **505's run is that case**, and it is the falsifier this paragraph named doing its job: the child
 * followed the parent's arm, failed the pid check, and the records say so - `getpid_change_value` 2,
 * `undef_pc` `0x11d0`, `osr_*`, the corpse path - with the boot still running. The one thing this
 * program must not do is fault after the fork: `udf #1` in the *child* would kill pid 2, not the boot,
 * and `udf #1` in the *parent* would kill `initproc` exactly as 478 measured.
 *
 * The `spin` is unchanged and was the last thing *both* processes did in 505's run: the parent fell
 * back into it after the fork, and the child, sent there by the `cmp r0, #0` above, followed it until
 * its own pid failed the check. So `xnu_live_getpid_count` climbing past the fork's record *and*
 * `xnu_live_getpid_change_value` = 2 are the two numbers that say the OS came back to user mode **with
 * two processes alive**, and the step after this one is the one that makes the child's number an
 * `exit` record instead.
 *
 * ------------------------------------------------------------------------------------------------
 * What 506 adds: the register the kernel writes the child's flag in
 * ------------------------------------------------------------------------------------------------
 *
 * **One instruction's register and one instruction's number, and 506 changed no address in 505's
 * document**, so every address 505's document named - `0x11a4` where the child was first fetched,
 * `0x11d0` where it died - is unchanged. `cmp r0, #0` becomes `cmp r1, #CHILD_FLAG` and the branch stays `beq entry_child`: the two
 * processes are `(r0 = pid, r1 = 0)` and `(r0 = pid, r1 = 1)`, so the arm that tests *the child's own
 * value* is the one whose first instruction is `mov r0, #EXIT_RVAL`. The `mov r0, #0` stays, because it
 * is the word `xnu_live_fork_uap0` names as *not* the one the argument buffer took, and because a
 * 61-word program is the one 505 measured.
 *
 * **The first run of this step inverted the condition, and it is the one reading this file has that a
 * human wrote twice from one derivation.** Run 1 was `cmp r1, #CHILD_FLAG` with **`bne`**, which asks
 * "is r1 *not* the child's flag" - true of the parent - so `initproc` took the child's arm. The device
 * said so in the two places this step is about: the console's `pid 1 exited -- no exit reason available
 * -- (signal 0, exit 3)` and the live records
 *
 *     xnu_live_exit_seq = 1   _caller = 0x80285848 (unix_syscall+0x100)   _pid = 1   _rval = 3
 *
 * with no `xnu_live_sigchld_*` (the parent has no parent), no `xnu_live_undef_*` (the arm it took does
 * not fault) and a panic: `xnu_entry_panic_entered = 1` with `_len = 0x13fa`. **The check could not
 * refuse it**: the decode table asserted the same `bne`, and the mutation that would have caught the
 * inversion (`beq`) was written *from that same table*, so the two agreed with each other and only the
 * device disagreed. That is the falsifier (d) below, named before the run and taken by it.
 *
 * **What this buys is the first `exit` this machine will ever run for a process it made itself**, and
 * the first inter-process event in this walk. `exit` is `sysent[1].sy_call`, `__attribute__((noreturn))`,
 * whose `exit1(p, W_EXITCODE(uap->rval, 0), ...)` reaches `proc_prepareexit`'s
 * `if (p == initproc) launchd_crashed_panic(p, rv)` - and `p` here is pid 2, so the branch that has
 * ended every wrong run before this one is not the one taken. Then the parent is told: `psignal(pp,
 * SIGCHLD)` at `proc_exit`'s `:1443`, where `pp` is pid 1.
 *
 * **Prediction, written before the build:**
 *
 *     xnu_live_exit_seq = 1   _caller = 0x80285848 (unix_syscall+0x100)   _pid = 2   _rval = 3
 *     xnu_live_sigchld_signal = 20   _to = 1
 *     xnu_live_getpid_count still climbing (the parent never leaves the loop)
 *     no xnu_live_undef_*, no xnu_live_osr_*, no corpse-path stub_hit, no panic
 *
 * (The `_rval` in the first draft of this block was `0x300`, which is the *composed* status.
 * `W_EXITCODE` runs inside the kernel and the key holds the argument; see the paragraph above.)
 *
 * **Run 1's answer, and what it cost.** The first three of those keys came back with `_pid` = **1**
 * and `exit 3` on the console, and the panic flags above: the falsifier (d) this file named before the
 * build. The machine panicked and the device came back on its own inside the capture window, which is
 * what the recovery nets are for - and the run is the *reason* this step has two, because the sense of
 * the comparison is the one field in this program that no host-side reading of the fixture can settle
 * without the kernel's own table: the fixture says `CHILD_FLAG` is 1, `thread_set_child` says the child
 * gets 1, and "then the branch must be taken on equality" is an argument, not a measurement. **The
 * device is the measurement.** Run 2 is the same image with the condition this file was written for.
 *
 * **Run 2's answer.** `beq` sent the child where this step wanted it, and the two records that say so
 * are adjacent in the log: `xnu_live_sleh_seq = 8` - the child's first dispatch, the same lazy prefetch
 * abort at the same `0x000011a4` with the parent's `sp` and `cpsr` as 505's - and then
 *
 *     xnu_live_exit_seq = 1   _caller = 0x80285848   _pid = 2   _rval = 3
 *
 * The parent never left its loop (`xnu_live_getpid_count` reached `0x4000`, `_last` = 1 at every
 * record) and the console has no `pid 1 exited` line, so the arm that ran is the child's. That is
 * falsifier (d) refused and the step's first half measured.
 *
 * **And the second half is falsifier (b), which the run took.** The run's *last* live record is a stub
 * hit, and it is the only one in the run:
 *
 *     xnu_live_stub_hit_seq = 1
 *     name:  0x804c157a -> "stage90_pthread_functions.pth_proc_hashdelete"
 *            (the first eight bytes are 0x67617473 / 0x5f303965, "stag" / "e90_")
 *     caller: 0x80294150 = proc_exit + 0x188
 *
 * `proc_exit` calls `pth_proc_hashdelete(p)` (`bsd/kern/kern_exit.c:1105`, under `#if PSYNCH`) **338**
 * source lines *before* the notify block (`:1443`) - this paragraph said 275 when it was written, which
 * was an estimate repeated rather than a distance computed from the two line numbers, and 507's run is
 * what closed it - and the shim is Apple's own three lines -
 * `bsd/kern/pthread_shims.c:364`: `pthread_functions->pth_proc_hashdelete(p)` - which is the `ldr r1,
 * [r1, #36]` / `bx r1` at `0x80205d0c..0x80205d1c` in the linked image. The slot this image's table
 * fills is a stand-in (`stage90_pthread_functions.c:477`), and a stand-in is **terminal**:
 * `entry_stub_hit` ends with `entry_epilogue("a symbol this image does not provide was called")`
 * (`entry_stubs.c:6107`). So the child's `exit` reached the kernel's teardown, stopped one call later,
 * and the run ended there - which is why `xnu_live_sigchld_*` is absent for the second, unrelated
 * reason: **the parent was never told because the boot stopped first.** The exit arm this step added is
 * 338 lines long and the SIGCHLD is at the far end of it; the next step is the one that gives that
 * slot a body, the way 465 did for `pth_proc_hashinit` and 473 for the two workqueue slots - both of
 * which this same run measures working (`xnu_live_pth_hashinit_seq = 2` inside `forkproc` at 465's
 * slot, `xnu_live_pth_wqmark_seq` / `_wqexit_seq` = 2 with `p = 0xc058d3a8` in the child's `proc_exit`
 * at 473's).
 *
 * **And 507 gave that slot its body, so the arm ran to its end.** Run 3 of this image (nothing in the
 * fixture changed; the whole step is one function in `xnu_supply`) measured the two records this file
 * predicted and could not have: `xnu_live_pth_delete_seq = 1` for the child's proc `0xc05b3e38`, then
 * `xnu_live_sigchld_seq = 1`, `_signal = 0x14` = 20, `_to = 1`, `xnu_live_psignal_calls = 1` - the OS
 * telling process 1 that process 2 is gone - with **no `stub_hit` anywhere in the run**, the parent's
 * `getpid_count` still climbing to `0x800000`, and no report, because a run with nothing left to stop
 * it ends on the hardware watchdog instead of on the epilogue. `xnu_live_sigchld_from` is **0**, not the
 * dying child's pid: `set_bsdtask_info(task, NULL)` (`kern_exit.c:1371`) clears the task 72 lines above
 * the call, so `current_proc()` answers `kernproc` (pid 0, `bsd_stubs.c:104`).
 *
 *
 * Falsifiers, named in advance (and kept after run 1, because run 2 can still take any of them):
 *
 *   (a) `xnu_live_undef_pc` = `0x11d0` again with no `xnu_live_exit_*` - the child took the parent's
 *       arm once more, i.e. its `r1` is not 1 at `+196`, which would make `thread_set_child` not the
 *       writer of the flag the object says it writes;
 *   (b) the run stops at a `stub_hit` inside the exit path - a name, and the next step's subject: the
 *       teardown needs a slot this image's table does not supply, and the run says which. **Run 2 took
 *       it**, with the name `stage90_pthread_functions.pth_proc_hashdelete` and the caller
 *       `proc_exit + 0x188`;
 *   (c) a panic, or a non-zero `xnu_entry_abort_entries` - a stand-in on the exit path returned a value
 *       the kernel believed, or `initproc` left;
 *   (d) `xnu_live_exit_*` with `_pid` = **1**: the flag is inverted (the parent's `r1` is not 0), which
 *       puts `initproc` on `launchd_crashed_panic` - the outcome 478 measured, and what run 1 took.
 *
 * ------------------------------------------------------------------------------------------------
 * What 508 adds: the parent asks for its child, and the kernel answers with the status it composed
 * ------------------------------------------------------------------------------------------------
 *
 * **The program's third and last call, and the one its two halves were built to leave missing.** A
 * process that makes another and never asks about it leaves a zombie nothing will reap - which is
 * exactly the state 507's run ended in: two processes alive, one of them dead and unclaimed, and the
 * only reading in the log that says so is the *absence* of a reaping parent. `wait4(2, &status, 0,
 * NULL)` is `sysent[7]`, and it is where the kernel's own answer to "what did that process exit
 * with" is composed out of things the fixture never sees: the exit status is `p->p_xstat`, written by
 * `exit1` in the kernel from `W_EXITCODE(uap->rval, 0)` (`kern_exit.c:823`, `:682`), and `wait4`
 * copies it out masked (`:1782`). **The fixture's 3 becomes 0x300 three source lines and one syscall
 * away from the register that carried it**, which is why this is a reading and not a tautology: a
 * zero-filled field, a stale register, or a wrapper publishing its own argument cannot produce it.
 *
 * **The four words are the four fields, and that is measured rather than read off the prototype.**
 * `sysent[7]`'s munger is `munge_wwww` - four words, contiguously - and `wait4_nocancel` reads them
 * at byte offsets 0, 4, 8 and 12 of the argument struct (`ldr r0, [r6]`, `ldr r1, [r6, #4]`,
 * `ldrb r0, [r6, #8]`, `ldr r1, [r6, #12]` in `bsd_kern_kern_exit.o`, disassembled), so `user_addr_t`
 * is one word on this target and r0..r3 are `pid`, `status`, `options`, `rusage` in the master's own
 * order. `tools/check_sysent_table.py` checks the munger word and the two counts
 * (`sy_arg_munge32` = `munge_wwww`, `sy_narg` = 4, `sy_arg_bytes` = 16) against the image, because a
 * slot with three marshalled words would leave r3 stale and the record would be a fact about whatever
 * the last syscall left in that register.
 *
 * **The reap is the other half of this step, and it is the free side of the corpse path 505 hit.** A
 * zombie found by `wait4` is reaped by `reap_child_locked(q, p, 0, reparentedtoinit, 0, 0)`
 * (`kern_exit.c:1834`), called *before* `wait4` returns, so the pid it answers with is a process that
 * no longer exists by the time the parent sees the number - which is what the **second** call is for:
 * the same `wait4` on the same pid can only come back `ECHILD` (`:1888`), and the wrapper records it.
 * The second call is deliberately not branched on (503's rule): an answer that is not `ECHILD` is a
 * fact about the reap path, and a `udf #1` here would kill `initproc` for a reason with nothing to do
 * with the call it is standing behind.
 *
 * **And the wait may *block*, which makes the log's order a reading of its own.** The child is
 * created microseconds before the parent asks, so the first scan can find it still exiting; the
 * source then sleeps on `(caddr_t)q` with `msleep0(..., PWAIT | PCATCH | PDROP, "wait", 0,
 * wait1continue)` (`:1906`) and the child's own `proc_exit` wakes it two instructions after the
 * SIGCHLD - `wakeup((caddr_t)pp)` under the same list lock, `:1446-1448`. The wrapper therefore writes
 * a record **before** the call and one **after** it, and if the child's `pth_delete` / `sigchld`
 * records appear between the two in the log, that is the wakeup measured rather than argued: the
 * parent was parked inside `wait4` while the child finished dying.
 *
 * (The instrument is `entry_trace.c`'s `__wrap_wait4` with `entry_stubs.c`'s two record writers, and
 * the reason the before/after pair is split the way 505's `psignal` pair is split - one record
 * written before the real call, one after it - is that the *duration* is not a number any key can
 * hold, while the log's own order is.)
 *
 *
 * Prediction, written before the build:
 *
 *     xnu_live_wait_seq = 1        _caller = <unix_syscall+0x100>   _who = 1  (the *waiter*, proc_pid)
 *     _pid = 2   _status_ptr = <the page>   _options = 0   _rusage = 0
 *     xnu_live_wait_done_seq = 1   _error = 0   _ret = 2   _copy_error = 0   _status = 0x300
 *     xnu_live_wait_seq = 2  ... _pid = 2   ...  and  xnu_live_wait_done_seq = 2  _error = 10 (ECHILD)
 *     no xnu_live_undef_*, no xnu_live_osr_*, no stub_hit, no panic
 *     xnu_live_getpid_count still climbing (the parent is back in its loop, with nothing left to do)
 *
 * Falsifiers, named in advance:
 *
 *   (a) `_error` non-zero on the *first* call, with the fixture's `udf #1` behind it - the marshalling
 *       is not the four words the disassembly says, or the child was never put in `SZOMB`;
 *   (b) `_copy_error` = 0 and `_status` != `0x300` - the composition or the mask is not the chain this
 *       section names, and the number would then be a fact about something else;
 *   (c) a `stub_hit` inside `reap_child_locked` / `proc_reap` - the free side calls through a slot this
 *       image's table does not supply (506's falsifier (b), one layer further in), and the run names
 *       the slot the way 506's run named `pth_proc_hashdelete`;
 *   (d) `xnu_live_wait_done_seq` absent - the call never came back: a lost wakeup would leave `initproc`
 *       parked in `msleep0` for the rest of the run, which is visible as a `getpid_count` that stops
 *       climbing, and is the one outcome here that is not a stop but an ending;
 *   (e) the *second* call blocking or answering 2 - `reap_child_locked` did not run, and the child is
 *       still a zombie;
 *   (f) a panic with `xnu_live_wait_*` present - the reap path panicked *after* answering, which would
 *       make `proc_checkdeadrefs`' `p_refcount != 0` real (`kern_proc.c:749`; it is empty unless
 *       `__PROC_INTERNAL_DEBUG`, which is why the wrapper takes no reference of its own on the child).
 *
 *
 * What the run measured, written after it (comments only - the image the device ran is byte-identical to
 * the one this file builds, proved by `cmp` on the three artifacts):
 *
 *   - **The first call blocked, and the sleep record is the proof** rather than the tick count:
 *     `xnu_live_sleep_site = 0x802950ac` is the instruction after the `bl __wrap_msleep0` at `0x802950a8`
 *     in `wait4_nocancel`, with `_pri = 0x520` (`PWAIT | PCATCH | PDROP`), `_chan` the parent's own proc
 *     and `_tmo = 0`. The child's `exit` / `pth_delete` / `sigchld` records then land between the call's
 *     two records, which is the wakeup read out of the log's order.
 *   - **The status is the kernel's, and the missing record is not what proves it.** The page held
 *     `0xfeedface` at line 7818 of the same run (504's pair: `_read_word_before = 0x00102000`,
 *     `_read_word_after = 0xfeedface`), the second call returns `ECHILD` before any `copyout`, and this
 *     program reached its loop without taking a `udf` - so both of its checks passed and the word it
 *     tested was `0x300`.
 *   - **Falsifier (d) is the one the run took, and it took it in a way that was not predicted**: the
 *     record `xnu_live_wait_done_seq = 1` is absent, but the *call* came back. The wrapper's second
 *     writer is unconditional, `xnu_live_capped` is absent and the run used 4409 of `ENTRY_LIVE_CAP`
 *     (8192) records, so the cause is the console's two-writer exposure, not the writer and not the cap.
 *     A falsifier that reads "the record is absent" as "the call never came back" cannot tell those
 *     apart; the reading that can is the second call's own answer.
 *   - **The kernel's `copyout` of the status faulted on this program's page and was retried**:
 *     `sleh_seq = 8`, `_user = 0`, `_far = 0x00102000`, `_pc = 0x80016360` (the store in
 *     `Lcopyout_bytewise`), `_lr = 0x80295114` (after `bl copyout` in `wait4_nocancel`),
 *     `_recover = 0x8001644c` = `copyio_error`, `_redirect = 0` - armed, serviced, retried, which is why
 *     `wait4` answered 2 and the page holds `0x300`.
 *
 * ------------------------------------------------------------------------------------------------
 * What 512 adds: the process stops asking and starts waiting, so the kernel has nothing to run
 * ------------------------------------------------------------------------------------------------
 *
 * Every run of this image from 506 to 511 has ended the same way - on the hardware watchdog's 25 s
 * timeout - and 511's log, read to its end, says why: `xnu_live_getpid_count` was still climbing at
 * `0x80000` when the watchdog fired, the OS's own service threads were all parked, and the only
 * process in the system was this fixture, busy in a loop that never blocks. **The OS was healthy and
 * had nothing to do, and the one thing it could not be measured doing is the thing an operating system
 * does most of its life: idling.** That is what this step changes, and it is a change to one block:
 * the `getpid` loop becomes `poll(NULL, 0, PARK_MS)`.
 *
 * **The reading the step is for is `machine_idle`'s own entry**, because it is a call the kernel makes
 * only when it has nothing runnable: `processor_idle` walks its queues, and when every one of them is
 * empty it calls `machine_idle()` (`osfmk/kern/sched_prim.c:4532`), which disables FIQs and IRQs and
 * hands the CPU to `Idle_context` until something wakes it. So a run whose log carries an
 * `xnu_live_idle_*` record has measured the OS in the state it is in when no work exists - and a run
 * whose log does not is a run where this fixture never stopped asking. The caller the wrapper publishes
 * is the other half: it is inside `processor_idle`, which is a fact about the linked image rather than
 * about the boot.
 *
 * **Prediction, written before the build:**
 *
 *     xnu_live_poll_seq = 3 then 4, both with _nfds = 0, _fds = 0, _error = 0, _retval = 0,
 *         _timeout_ms = 2000 and _ticks of about 2000 ms - the park's first two turns, which the
 *         wrapper publishes in full because `entry_note_poll` caps its records at four and 503's two
 *         asks are the first two. After them `xnu_live_poll_over` climbs at powers of two.
 *     xnu_live_idle_seq = 1, 2, 4, 8, ... with _caller inside processor_idle (0x800b68d4..),
 *         _thr the idle thread (whose _pid is 0 - it belongs to `kernel_task`), _cpsr_live = 0x13
 *         (SVC mode, the mode `machine_idle` is called in, live and not saved)
 *     no stub_hit, no undef, no osr, no panic, no trap record, no report, and no `pid 1 exited`
 *
 * Falsifiers, named in advance:
 *
 *   (a) `xnu_live_idle_*` absent while `xnu_live_poll_over` climbs - the process is parking and the
 *       kernel is still not idle, which would mean something else in this system is runnable and is
 *       the *finding* rather than the failure;
 *   (b) `_poll_over` absent as well as `_idle` - the park never returned, i.e. the timeout never
 *       expired: the timer is not waking a `poll` whose deadline it armed, which would make this
 *       image's two asks a coincidence rather than a measurement;
 *   (c) the run ending with `pid 1 exited` or a panic - this is the one call in the program whose
 *       answer is deliberately not tested, and a `udf` reached from it would kill `initproc`;
 *   (d) an idle record whose `_caller` is *not* inside `processor_idle` - `machine_idle` is called
 *       from one place in the whole kernel, and the build checks that, so a record from elsewhere
 *       would mean the clause and the image disagree.
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

/* The syscall numbers the program makes, from the headers rather than from a disassembly: each number
 * is the one in `bsd/kern/syscalls.master`'s own line for that name, and the *sign* is the ABI and not
 * a convention - `fleh_swi` computes `r5 = -r12` and branches to the unix path when that is `<= 0`, so
 * a positive number in r12 is a BSD syscall and a negative one is a mach trap. `EXPECTED_PID` is the
 * identity this image's own boot gave the process: `bsd_utaskbootstrap` holds the init process by name
 * (`initproc = proc_find(1)`, `bsd/kern/bsd_init.c:1147`) and 478's console printed `pid 1 exited`.
 * `MMAP_LENGTH`/`MMAP_PROT`/`MMAP_FLAGS` are the kernel's own numbers for the one page this fixture
 * asks for: `PROT_READ|PROT_WRITE` and `MAP_PRIVATE|MAP_ANON` from `bsd/sys/mman.h`, with the length
 * being `1 << ARM_PGSHIFT` (`osfmk/arm/proc_reg.h`), `fd` -1 and offset 0, so the mapping needs no
 * vnode at all. `tools/host_ramdisk_macho_check.py` reads every one of them back out of those files -
 * the syscall numbers from the master, the pid from the sentence above it, the page size from the
 * kernel's own page shift, `prot` and `flags` from `mman.h`, and the character device's own name from
 * the `devfs_make_node` format string in `bsd/dev/memdev.c` that made the node 504 opens, the two
 * syscall numbers 505 makes from the master's own lines for `fork` and `exit`, the number and the pid
 * 508 makes from the master's line for `wait4` - whose four 4-byte arguments the slot's own
 * `sy_arg_munge32` and `sy_arg_bytes` are checked against in `tools/check_sysent_table.py` - and the
 * composed status `W_EXITCODE` derives from the child's - and decodes
 * the seventy-nine words below
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
    .equ SYS_POLL,               230    /* `230 AUE_POLL ALL { int poll(struct pollfd *fds, u_int
                                         * nfds, int timeout); }`, and the slot's munger is
                                         * `munge_www` (`out/xnu_generated/init_sysent.c:1317`), so
                                         * r0..r2 are exactly the three arguments. Both numbers are
                                         * read back out of those two files by the check below. */
    .equ POLL_SHORT_MS,          5      /* the two timeouts are the step's own control: an 8:1
                                         * ratio, so a wake that came from anything other than the
                                         * countdown cannot pass for one. 19.2 MHz makes them 96000
                                         * and 768000 ticks, which is what the two durations the
                                         * wrapper measures are predictions of. */
    .equ POLL_LONG_MS,           40
    .equ PARK_MS,                2000   /* 512's park, and the number is a *bound* rather than a
                                         * measurement: the property is that the process is still
                                         * parked when the hardware watchdog fires, so the timeout has
                                         * to be longer than one loop turn's worth of scheduling and
                                         * long enough that the watchdog - not the timeout - is what
                                         * ends the run. 2000 ms is 48,000,000 ticks at 24 MHz, well
                                         * inside a 32-bit `CNTV_TVAL`, and the loop re-parks if it
                                         * ever expires, so the exact value decides nothing except how
                                         * often the process wakes up to park again. The check states
                                         * the bound (nonzero, and no comparison on the answer);
                                         * `tools/host_ramdisk_macho_check.py` reads the number. */
    .equ SYS_READ,               3      /* `3 AUE_NULL ALL { user_ssize_t read(int fd, user_addr_t
                                         * cbuf, user_size_t nbyte); }` - three 4-byte arguments, so
                                         * `munge_www` like `poll`'s. The *return* is 64-bit, which
                                         * does not change the munger (that is about arguments) but
                                         * does mean the return path writes r1 as well as r0 - which
                                         * is why the page address 504 reads into lives in r9. */
    .equ SYS_OPEN,               5      /* `5 AUE_OPEN_RWTC ALL { int open(user_addr_t path, int
                                         * flags, int mode) ...; }` - also `munge_www`, and the
                                         * first argument is an *address*, so the fixture has to be
                                         * able to name a string in its own mapping. */
    .equ OPEN_RDONLY,            0      /* `O_RDONLY` is 0 in `bsd/sys/fcntl.h`, which is what
                                         * `mdevopen` reads as "no FWRITE" and answers 0 to. */
    .equ READ_BYTES,             4      /* one word: enough for the wrapper to publish the word the
                                         * driver copied, and no more than the page it lands in. The
                                         * check states both bounds rather than this number. */
    .equ SYS_FORK,               2      /* `2 AUE_FORK ALL { int fork(void) NO_SYSCALL_STUB; }` - and
                                         * the *empty* prototype is the argument the step rests on:
                                         * `sysent[2].sy_narg` is 0 and its munger word is NULL
                                         * (`out/xnu_generated/init_sysent.c`), so
                                         * `arm_get_syscall_args` is never called for this slot and no
                                         * register is read as an argument. The `mov r0, #0` before it is
                                         * therefore a word the kernel does *not* read - and 505's run
                                         * measured the rest of it: it is not either process's return
                                         * value either, because the fork wrapper publishing `uu_arg[0]`
                                         * beside the return is what makes that a reading, and the two
                                         * processes' `r0` are written by the kernel. */
    .equ CHILD_FLAG,             1      /* What `fork`'s child is told instead of zero. `fork1` calls
                                         * `thread_set_child(child_thread, child_proc->p_pid)`
                                         * (`bsd/kern/kern_fork.c:636`, after `thread_dup` at `:590`) and
                                         * ARM's body writes `r[0] = pid; r[1] = 1`
                                         * (`osfmk/arm/status.c:722-730`) - the same pair the ARM64 port
                                         * writes (`osfmk/arm64/status.c:1253`) - so `r1` is 1 in the child
                                         * and 0 in the parent, whose `r1` is `uu_rval[1]` as `fork` zeroed
                                         * it (`:895`, and the run's own `xnu_live_fork_ret_hi`). This is
                                         * the register the branch at `+196` tests, and 505's run is why:
                                         * the `r0` it tested before is the pid in both processes, and
                                         * pid 2 took the parent's arm and died on `udf #1`. */
    .equ SYS_EXIT,               1      /* `1 AUE_EXIT ALL { void exit(int rval) NO_SYSCALL_STUB; }` -
                                         * `munge_w` and `_SYSCALL_RET_NONE`: one 4-byte argument and
                                         * no return value, because the call does not return to user
                                         * mode at all (`void exit(...)` is `__attribute__((noreturn))`,
                                         * `bsd/kern/kern_exit.c:677`). */
    .equ EXIT_RVAL,              3      /* The child's own exit status, and the value is chosen for what
                                         * the kernel *does* with it: `exit` hands `exit1` the
                                         * expression `W_EXITCODE(uap->rval, 0)` (`kern_exit.c:682`),
                                         * and that macro is `((ret) << 8 | (sig))`
                                         * (`bsd/sys/wait.h:158`) - so 3 becomes **0x300**, a number the
                                         * kernel composed and that no zero-filled field and no read of
                                         * the fixture's own register can produce. A 0 here would be
                                         * the one value that says nothing. 508 is the step that reads
                                         * that number back, from the process that asked for it: the
                                         * word is `0xffff & p->p_xstat`, and the mask is
                                         * `wait4_nocancel`'s (`kern_exit.c:1782`, "Legacy apps expect
                                         * only 8 bits of status") and not the composition's - the
                                         * macro has no mask, and this comment said `(rval & 0xff) << 8`
                                         * until 508 quoted the header instead of paraphrasing it. */
    .equ SYS_WAIT4,              7      /* `7 AUE_WAIT4 ALL { int wait4(int pid, user_addr_t status,
                                         * int options, user_addr_t rusage) NO_SYSCALL_STUB; }` -
                                         * the third of the three calls a Unix process's life is, and
                                         * the one 508 adds. Four 4-byte words, so the slot's munger
                                         * is `munge_wwww` (`out/xnu_generated/init_sysent.c`) and
                                         * `sy_arg_bytes` is 16: r0..r3 land in the argument struct at
                                         * offsets 0, 4, 8 and 12, which is where `wait4_nocancel`
                                         * reads them. `tools/check_sysent_table.py` checks the slot's
                                         * munger and the two counts, and
                                         * `tools/host_ramdisk_macho_check.py` checks the four
                                         * registers against the master's own prototype. */
    .equ WAIT_PID,               2      /* The pid to ask about, and the value is 505's reading rather
                                         * than this file's choice: the system has one user process
                                         * when `/sbin/launchd` starts (`initproc = proc_find(1)`), so
                                         * the first process a user request can make is 2 - which is
                                         * what `xnu_live_fork_ret_lo`, `xnu_live_exit_pid` and
                                         * `xnu_live_pth_delete_p`'s proc have all been. `fork` returns
                                         * the pid in r0 for both halves, so the parent's `r0` at
                                         * `entry_parent` is that number already and this constant is
                                         * only what `cmp` and the second call write. */
    .equ EXIT_STATUS,            (EXIT_RVAL << 8)   /* `W_EXITCODE(EXIT_RVAL, 0)`, computed here and
                                         * not written as 0x300, because the *derivation* is the claim
                                         * - the number is the kernel's arithmetic on the fixture's own
                                         * register, and a literal here would be a second definition of
                                         * one value. The shift is the header's: the check reads
                                         * `#define W_EXITCODE(ret, sig) ((ret) << 8 | (sig))` out of
                                         * `bsd/sys/wait.h` and compares the instruction's immediate
                                         * with what that macro makes of `EXIT_RVAL` - so a change to
                                         * either number fails the build rather than the run. */

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

/* 503 keeps the page for 504, and the register is r9 for a reason: a syscall's return path writes
 * r0 - and, for a call whose return is 64-bit like `read`'s, r1 as well (`unix_syscall`'s
 * `arm_prepare_u32_syscall_return`, `bsd/dev/arm/systemcalls.c:289-298`) - so the only registers a
 * value can survive a call in are the ones the kernel does not write back: r2..r12, and r9 is one
 * this program uses for nothing else. The word the page holds here is the address `mmap` returned,
 * because the store above wrote it there; that is what makes 504's read a *before and after*. */
    mov     r9, r0                      /* +84: the page, for the read below */

/* And the two syscalls that ask the kernel for a *deadline* (503). The programs above ask the kernel
 * what it knows - its pid, and for a page it can own; these ask it for *time*, which is the one thing
 * in this image that a driver and a process both need and that nothing in user mode can produce.
 *
 * `poll(NULL, 0, ms)` is Apple's own spelling for it, and the comment at `bsd/kern/sys_generic.c:1785`
 * says so in Apple's words: "If user space passed 0 FDs, then respect any timeout value passed. This
 * is an extremely inefficient sleep." The path is `poll` -> `poll_nocancel` ->
 * `kqueue_scan` (`kern_event.c:6100`), where the deadline becomes a *wait timer*:
 *
 *     waitq_assert_wait64_leeway(...)   -> timer_call_enter_with_leeway(&thread->wait_timer, ...)
 *     thread_block_parameter(cont, kq)  -> the thread parks
 *     ... the virtual timer's interrupt -> rtclock_intr -> timer_intr -> timer_queue_expire
 *                                       -> thread_timer_expire -> clear_wait(THREAD_TIMED_OUT)
 *     ... and kqueue_scan's switch takes THREAD_TIMED_OUT to EWOULDBLOCK, which `poll` maps to 0.
 *
 * So the answer is a *register*: `poll` returns 0 and its `retval` is 0 - the timeout expired and no
 * descriptor was ready - and r0 == 0 is what the kernel hands back to this instruction stream.
 *
 * **Two asks of different length, because one cannot distinguish a deadline from a latency.** A wake
 * that came from anywhere other than the countdown - a fixed polling interval, a stray interrupt, the
 * quantum - would return after about the same number of ticks whichever timeout was asked for, and a
 * single sample cannot tell that from a clock. Five milliseconds and forty are an 8:1 ratio, so a
 * fixed latency shows up as a ratio of about 1 in the readings and the two asks' own durations are
 * proportional to what was asked for. The wrapper that measures both is in `entry_trace.c`;
 *
 * **and neither ask branches on the answer.** Every other check in this program takes a wrong answer
 * to `entry_failed`, which is `udf #1` - and this program *is* `/sbin/launchd`, so a fault taken here
 * kills `initproc` and 478's run measured what follows (`pid 1 exited -- exit reason namespace 2
 * subcode 0x4`, then `launchd_crashed_panic`). A `poll` that returned an errno is a fact about the
 * kernel's timer path and not a reason to end the boot: the wrapper records the return either way,
 * and the `spin` below is the reading that says control came back to user mode. So the two calls are
 * made, and the next instruction is the loop. */
    mov     r0, #0                      /* +84: fds = NULL - no descriptor is waited on */
    mov     r1, #0                      /* +88: nfds = 0 - Apple's zero-descriptor sleep */
    movw    r2, #POLL_SHORT_MS          /* +92: timeout = 5 ms */
    mov     r12, #SYS_POLL              /* +96: 230; `munge_www` marshals r0..r2 as these three */
    svc     #0x80                       /* +100: the first timed block */
    mov     r0, #0                      /* +104: the same call again, and this time the timeout */
    mov     r1, #0                      /* +108: is eight times longer - the ratio is the reading */
    movw    r2, #POLL_LONG_MS           /* +112: timeout = 40 ms */
    mov     r12, #SYS_POLL              /* +116 */
    svc     #0x80                       /* +120: the second timed block */

/* And the two opens and the read that reach a *driver* (504). `/dev/rmd0` is the character device
 * `mdevadd` made a node for at boot, whose `read` is `mdevrw`'s one `uiomove64`; `/dev/nosuch` is a
 * name no driver can have made a node for, and it is the control that keeps the first open's answer
 * from being a fact about `open` rather than about the device tree. Both are non-branching for 503's
 * reason: this program is `initproc`, and an `open` that failed is a reading rather than a reason to
 * fault. The paths are addressed with `adr`, so their addresses are computed once, here, from the
 * strings below rather than written down. */
    adr     r0, path_rmd0               /* +128: path = "/dev/rmd0", in this segment's own bytes */
    mov     r1, #OPEN_RDONLY            /* +132: flags = O_RDONLY, so `mdevopen` cannot answer EACCES */
    mov     r2, #0                      /* +136: mode, which `open` reads only with O_CREAT */
    mov     r12, #SYS_OPEN              /* +140 */
    svc     #0x80                       /* +144: the first user-mode call a driver answers */
    mov     r1, r9                      /* +148: cbuf = the page, whose first word is the address */
    mov     r2, #READ_BYTES             /* +152: nbyte = 4 */
    mov     r12, #SYS_READ              /* +156 */
    svc     #0x80                       /* +160: `mdevrw` copies this file's own first four bytes */
    adr     r0, path_missing            /* +164: path = "/dev/nosuch" - the control */
    mov     r1, #OPEN_RDONLY            /* +168 */
    mov     r2, #0                      /* +172 */
    mov     r12, #SYS_OPEN              /* +176 */
    svc     #0x80                       /* +180: devfs has no such node, so this one is ENOENT */

/* And the ask that makes a second process (505), with the branch 506 fixes. One `svc`, two returns,
 * and the two halves are told apart by the flag the kernel writes beside the pid: `fork1` copies the
 * parent's saved state into the child (`thread_dup` -> `machine_thread_dup`) and then overwrites the
 * child's `r[0]` with the pid and sets `r[1]` to 1 (`thread_set_child`, `osfmk/arm/status.c:722`), so
 * the pair `(r0, r1)` is `(pid, 0)` for the parent - whose `r1` is the `uu_rval[1]` the fork record
 * publishes - and `(pid, 1)` for the child. 505 branched on `r0`, which is the pid in both, and its
 * run measured the consequence: the child walked the parent's arm and died on its own `udf #1` at
 * `0x11d0`. The `mov r0, #0` below is read by nobody - the fork slot takes no argument (`sy_narg` 0,
 * munger NULL), and both processes' `r0` are written by the kernel after it - and it stays because the
 * run's `xnu_live_fork_uap0` record names it as the word the argument buffer did *not* take. The
 * branch on `r1` is the only thing that decides which process runs which half, and with it the
 * child's arm is the first `exit` this machine has ever run. */
    mov     r0, #0                      /* +184: a word with no reader - see the header */
    mov     r12, #SYS_FORK              /* +188: 2, and a slot whose munger word is NULL */
    svc     #0x80                       /* +192: returns twice, and the branch below splits them */
    cmp     r1, #CHILD_FLAG             /* +196: r1 is 1 in the child and 0 in the parent */
    beq     entry_child                 /* +200: r1 == CHILD_FLAG, so this is the one `fork` made */
    b       entry_parent                /* +204: r1 == 0, so this is the caller - 508's half */

/* The child's half: one call, and it does not return to user mode. `exit` runs `exit1` and then
 * `thread_exception_return()` (`kern_exit.c:684`), and `proc_prepareexit`'s first branch - the one
 * that panics for `initproc` - is not taken here because this process is the one `fork` just made.
 * Its status is 3 and the kernel composes 0x300 out of it, which is the number the instrument reads
 * back from *inside* the exit path rather than from this register - and 508 is the step that reads it
 * back from the *outside* too. */
entry_child:
    mov     r0, #EXIT_RVAL              /* +208: 3, so the composed status is 0x300 */
    mov     r12, #SYS_EXIT              /* +212: 1 */
    svc     #0x80                       /* +216: never returns; the thread is terminated */

/* And the parent's half, which is 508: the third of the three calls a Unix process's life is - make
 * one, lose one, **ask for it**. `fork` returned the child's pid in r0 for *both* processes and the
 * branch above sent the child away, so r0 here is the number to ask about; r9 is 504's page, whose
 * first word the read left holding this file's own magic (`0xfeedface`) and which becomes the word
 * the kernel answers with.
 *
 * **The four words are the four fields of `struct wait4_args` in order, and that is a measurement
 * rather than a reading of the prototype.** `sysent[7]`'s munger is `munge_wwww`
 * (`out/xnu_generated/init_sysent.c`), which copies the four words contiguously, and `wait4_nocancel`
 * reads them back at byte offsets 0, 4, 8 and 12 - `ldr r0, [r6]`, `ldr r1, [r6, #4]`,
 * `ldrb r0, [r6, #8]`, `ldr r1, [r6, #12]` in the object this image links. So `user_addr_t` is *one
 * word* on this target and the four registers are the four fields in the order the master writes them
 * (`7 AUE_WAIT4 ALL { int wait4(int pid, user_addr_t status, int options, user_addr_t rusage); }`).
 * rusage is 0 and that is the one argument nothing checks: it is read at offset 12 and a stale word
 * there would send `munge_user32_rusage`'s `copyout` to an address this process does not own, which
 * would come back as an EFAULT rather than as a fault - the branch on r0 below would then kill
 * `initproc` for a reason with nothing to do with this step. It is set, and the wrapper publishes it
 * so that the log says which word the kernel read.
 *
 * **The two answers are checked and the second call's is not.** r0 is the pid the kernel answered
 * with - `retval[0] = p->p_pid` (`kern_exit.c:1779`) - and the word behind the pointer is the status
 * `wait4` copied out (`:1782`), both of which are facts this program is entitled to fault on: a
 * `wait4` that returns something else has not done what this step claims. The *second* call is a
 * different kind of reading - it asks for the same pid again, and the source says it can only come
 * back `ECHILD` with `retval[0]` untouched (`:1888`) - so it is made, recorded by the wrapper and
 * **not** branched on, for 503's reason: an answer that is not the expected one is a fact about the
 * reap path, and a fault here would kill `initproc` and end the boot 478's way. */
entry_parent:
    mov     r1, r9                      /* +220: status = the page `mmap` gave and the read filled */
    mov     r2, #0                      /* +224: options = 0 - and not WNOHANG: the child is a */
    mov     r3, #0                      /* +228:     zombie, so this returns rather than parking */
    mov     r12, #SYS_WAIT4             /* +232: 7, `munge_wwww` - see the header */
    svc     #0x80                       /* +236: and r0 is still the pid the `fork` returned */
    cmp     r0, #WAIT_PID               /* +240: the pid the kernel answered with */
    bne     entry_failed                /* +244 */
    ldr     r3, [r9]                    /* +248: the word the kernel *wrote through* the pointer */
    cmp     r3, #EXIT_STATUS            /* +252: `W_EXITCODE(EXIT_RVAL, 0)` = 0x300 */
    bne     entry_failed                /* +256 */
    mov     r0, #WAIT_PID               /* +260: the same question, of a child that is gone */
    mov     r1, r9                      /* +264 */
    mov     r2, #0                      /* +268 */
    mov     r3, #0                      /* +272 */
    mov     r12, #SYS_WAIT4             /* +276 */
    svc     #0x80                       /* +280: ECHILD, and the wrapper records that it was */
    b       park                        /* +284: deliberately not a `bne entry_failed` */

/* **512: the process's last act is to stop asking and start waiting.** 479's loop called `getpid` and
 * branched on the answer, which is what made this process's liveness a *record* - and it is also why
 * every run since has ended on the hardware watchdog with the CPU busy: an `init` that never blocks
 * gives the kernel's idle path nothing to do, so this image has never been measured in the state an
 * operating system spends most of its life in. `poll(NULL, 0, PARK_MS)` is the same call this program
 * already makes twice, and the one `bsd/kern/sys_generic.c:1785` calls "an extremely inefficient
 * sleep": no descriptor to wait on, so the timeout is the whole of the call, the thread parks in
 * `msleep0`, and the processor it was running on has nothing runnable - which is what
 * `processor_idle` answers with `machine_idle` (`osfmk/kern/sched_prim.c:4532`).
 *
 * **The answer is not tested, and every argument is reloaded on each turn.** Both are deliberate. A
 * `poll` that came back with an errno is a reading about the kernel's timer path, and a `udf #1` here
 * would kill `initproc` and panic the boot 478's way - so, like the two asks above and like 508's
 * second `wait4`, this call's answer is recorded by the wrapper and never branched on. And the loop
 * goes back to `park` rather than to the `mov r12, #SYS_POLL`, because a syscall's return writes r0: a
 * loop that re-asked without reloading it would ask `poll(0, 0, PARK_MS)` - the same call, by luck -
 * and a loop that skipped the reload after a *different* call would ask something else entirely.
 * Reloading all three is what makes every turn the call the listing says it is.
 *
 * **This is the first `park` this image has been able to have.** 479's header rules `thread_switch`
 * out because every option it accepts ends in a block and the timer was still owed then, so a block
 * was a hang; 503 gave the kernel a working timer and the two asks above are what measured it (the
 * run's `xnu_live_poll_ticks` are the deadlines that really expired). A park that could not be woken
 * would be the same hang by another route, and the reading that it is not is the run's. */
park:
    mov     r0, #0                      /* +288: fds = NULL - no descriptor is waited on */
    mov     r1, #0                      /* +292: nfds = 0 */
    movw    r2, #PARK_MS                /* +296: so the timeout is the whole of the call */
    mov     r12, #SYS_POLL              /* +300 */
    svc     #0x80                       /* +304: the thread parks here and the CPU goes idle */
    b       park                        /* +308: and if the timeout ever expires, park again */

entry_failed:
    udf     #1                          /* +312: the kernel answered something else */
entry_code_end:

/* The two paths, as *file* bytes inside `__TEXT`'s file range - so the mapping that carries the
 * program carries them, and the process can hand their addresses to `open` without asking the kernel
 * for anything. They are placed after `entry_code_end` rather than among the instructions because
 * the program's length is what the `.if` below and the host check both count; the `adr`s above are
 * PC-relative and the assembler resolves them here, in this section, with no literal in either.
 *
 * The word padding between them is written as a count of bytes and not as `.balign`, and that is not
 * taste: an alignment directive emits a frag whose size the assembler does not know until the section
 * is finished, and an `.if` over a difference that spans one is a **non-constant expression** - so
 * the two assertions below, which are what tie these strings to the segment's file range and to each
 * other, could not be written at all. `.zero 2` takes "/dev/rmd0" (10 bytes) to the next word, and
 * the assertions are what say the two lengths still add up. */
path_rmd0:
    .asciz "/dev/rmd0"
    .zero 2
path_missing:
    .asciz "/dev/nosuch"
paths_end:

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
/* And the same bound for the two path strings 504 adds, which sit after the program: a string past
 * `filesize` is not mapped from the file, so `adr` would hand `open` an address that is not there and
 * the fixture would take a data abort in `copyinstr` instead of making the call. */
    .if ((paths_end - g_stage90_ramdisk) >= TEXT_FILESIZE)
    .error "the path strings are outside __TEXT's filesize, so their addresses are not mapped"
    .endif
/* And both strings word-aligned, which is what makes each `adr` above a single instruction: an `adr`
 * whose target is not a multiple of four from the pc has to be assembled as a pair, so a string that
 * drifted would change the program's *length* and its word numbering at once. The two `.zero`s are
 * the padding and these are the statements that they are the right amount of it. */
    .if ((path_rmd0 - g_stage90_ramdisk) % 4) != 0
    .error "the first path is not word-aligned"
    .endif
    .if ((path_missing - g_stage90_ramdisk) % 4) != 0
    .error "the second path is not word-aligned"
    .endif
    .if ((entry_pc_value < TEXT_VMADDR) || (entry_pc_value >= (TEXT_VMADDR + TEXT_VMSIZE)))
    .error "the entry point is outside the segment it is loaded from"
    .endif
/* And what the program's length is, because every branch in it is relative: a `b` that left the file
 * range would raise a fault instead of making a syscall, and the check tool decodes all 79 words by
 * offset. 79 words is the 3 of the getpid call, the 11 of the mmap call and its argument registers,
 * the 7 of the two faults, the 1 that keeps the page for 504, the 10 of the two timed asks, the 16 of
 * the two opens and the read between them, the 9 of the fork and the child's exit and its entry
 * label, the 17 of 508's two `wait4`s with the two checks and the two answers between them, and the
 * 6 of 512's park with the `udf` behind it. **The count moves with the fixture and the tool's
 * `PROGRAM_WORDS` is the same number**: a change here that the tool did not follow would leave the
 * last word of the program unchecked. */
    .if (entry_code_end - entry_code) != 316
    .error "the program is not the seventy-nine instructions the header describes"
    .endif

/* The rest of the segment is zeros, and they are *file* bytes rather than a `.bss` tail: the whole
 * of this object is what `/sbin/launchd` reads, and `__TEXT`'s `filesize` says the first 0x1000 of
 * it is the segment. Padded here rather than left to the linker so that the object's size is the
 * device's size - which is the check `build_entry.sh` makes over the linked image. */
    .zero RAMDISK_BYTES - (. - g_stage90_ramdisk)
    .size g_stage90_ramdisk, . - g_stage90_ramdisk
