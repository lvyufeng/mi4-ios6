# Experiment 505 — the first process the OS makes, and the child that came back with the pid

**One line.** `fork` was made, the OS built a second process for it (`pth_proc_hashinit` a second time,
for the proc `0xc06a68c8`, recorded while the call was still inside `forkproc`), and **the child ran** -
but the fixture's own `cmp r0, #0` sent it down the *parent's* arm, because on ARM the child's `r0` is
the pid too. The parent's `save_r0` is the return path's; the child's is `thread_set_child`'s, which
`fork1` calls *after* `thread_dup` (`bsd/kern/kern_fork.c:636`) and which writes `r[0] = pid; r[1] = 1`
(`osfmk/arm/status.c:722` - and the ARM64 port writes the same two words, `osfmk/arm64/status.c:1253`,
so this is the platform's fork ABI and not this tree's accident). **The two halves are told apart by
`r1`, not by the zero this fixture wrote before the `svc`**, and the run says so three times over: the
parent's `xnu_live_fork_ret_lo = 2`, the child's **first** `getpid` returning **2** at call `0x5e8b` (=
24107) while the parent was still being told 1, and the child's `udf #1` at `0x11d0` - the fixture's own
`entry_failed` label - which only the `bne entry_failed` after `cmp r0, #EXPECTED_PID` can reach.
**That is the first death in this walk that the OS survived**: the parent kept the loop (its `getpid`
count was still climbing at the dump), the trap became an exit reason (`os_reason_create` from
`threadsignal+0x198`, namespace 2, code 4) and then a corpse
(`gather_populate_corpse_crashinfo+0x5a0` through a pthread slot), and there was no panic -
`xnu_entry_abort_entries = 0`, `No errors detected`, and the device came back on its own. The next step
is one instruction **in the fixture, not in the kernel**: branch on the register the kernel writes for
the child.

    python3 tools/host_ramdisk_macho_check.py --selftest out/stage90/xnu_arm_entry.elf  # all 108 refused
    python3 tools/check_timer_sources.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 48
    python3 tools/check_sysent_table.py --elf out/stage90/xnu_arm_entry.elf  # fork: munger 0, sy_narg 0
    python3 tools/check_timer_line.py --selftest --verbose                        # all 20 refused
    python3 tools/check_os_entry.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 30 refused
    python3 tools/check_irq_routing.py --image out/stage90/xnu_arm_entry.elf --selftest # all 57 refused
    python3 tools/check_driver_catalogue.py --selftest                            # all 131 refused
    python3 tools/check_experiment_index.py                                       # 481 rows

## What the step was for

504's document ended with the ceiling in its own words: user mode "can now ask the kernel for time, for
a page, for a vnode by name, and for bytes out of a device, and the kernel answers all four. What it
still cannot do is *be* init: the fixture reads one word and spins." Every one of those four is one
process asking the kernel for something. 505 is the step that makes the OS do the two things it has
never done here: **make a second process**, and **lose one without dying**.

Nothing in this image had ever made a process. `load_init_program` created process 1 out of `kernproc`
(`bsd/kern/bsd_init.c:1079`, `:1147`) before the fixture existed, and everything since has been one
thread in one address space. `fork` is the ask: `2 AUE_FORK ALL { int fork(void) }` reaches
`sysent[2].sy_call` (`bsd/kern/kern_fork.c:872`), which builds the child out of this process
(`cloneproc` -> `forkproc` -> `fork_create_child` -> `task_create_internal(..., inherit_memory = TRUE,
...)`) and gives `forkproc` the new proc's `pth_proc_hashinit` call - which is why the run's
`xnu_live_pth_hashinit_seq = 2` for `p = 0xc06a68c8` is the first record of the step and sits *before*
the fork's own, the child being built inside the call.

And nothing had ever died. `udf #1` in the wrong process is `launchd_crashed_panic`; every previous step
had exactly one process, and it was `initproc`. 505 puts the `udf` behind a second process on purpose:
the child's arm ends in `exit`, the parent's arm in the loop it already had, and the `udf` is what a
*wrong answer* looks like rather than a *missing driver* - which is the property this run then measured
from the far side.

## The program: a fork, a branch, and an `exit` that never ran

The fixture is 61 words (52 in 504). Its first instruction is a `getpid` `svc` whose number rides in
`r12 = 0x14`, set by the Mach-O's own `LC_UNIXTHREAD` record - so the program's first act is to ask
which process it is and refuse to go on unless the answer is 1 (`EXPECTED_PID`). That is the run's
`xnu_live_getpid_seq = 1` / `value = 1`, which is why the *first* reading of this step is a `getpid`
before the `mmap` and not after it. The fork itself is the 9 words this step adds:

    1198: mov  r0, #0          (+184) the word the fixture believes is the child's answer
    119c: mov  r12, #SYS_FORK  (+188)
    11a0: svc  #0x80           (+192) the one call in this program that returns twice
    11a4: cmp  r0, #0          (+196) "zero here means this thread is the child"
    11a8: beq  entry_child     (+200)
    11ac: b    spin            (+204) the parent keeps the loop it came in with
    11b0: mov  r0, #EXIT_RVAL  (+208) entry_child: 3, and the kernel composes 0x300
    11b4: mov  r12, #SYS_EXIT  (+212)
    11b8: svc  #0x80           (+216) never returns to user mode

and the spin behind it is unchanged: `getpid`, compare with 1, `bne entry_failed`, `b spin`, with
`entry_failed: udf #1` at `+240` = **`0x11d0`** as the one instruction a wrong answer ends at.

### The prediction the checks made, and what the run answered

`tools/host_ramdisk_macho_check.py`'s 505 report says, in the step's own words:

> the parent comes back at word 49 with `xnu_live_fork_ret_lo` = the child's pid and
> `xnu_live_fork_ret_hi` = 0, the child comes back at the same word through `task_wait_to_return` with
> **r0 = 0** - the word at 46, copied out of the parent's saved state by `machine_thread_dup` and never
> read as an argument - so ... `xnu_live_exit_pid` = `xnu_live_fork_ret_lo` is the reading that both
> processes ran. The child's `exit` at word 53 carries 3, which `exit1` composes into 0x300 ...

The parent half is measured exactly as written (`ret_lo = 2`, `ret_hi = 0`, `error = 0`). The child
half is wrong in one word, and the run is what says so: there is no `xnu_live_exit_*` key in the log at
all, and there is an `xnu_live_undef_*` one instead. Both halves of the fixture's assumption - the copy
and the "exactly one writer" argument that the source header makes from it - were a claim about a
register that no check in this build could read, and the run read it.

### The register the child actually came back in

`machine_thread_dup` is what the header says it is - `osfmk/arm/status.c:469`, a `bcopy` of
`machine.PcbData` - and the object says how much:

    000006dc <machine_thread_dup>:
      6fc: add  r0, r5, #848   ; &self->machine.PcbData
      700: add  r1, r4, #848   ; &target->machine.PcbData
      704: mov  r2, #80        ; sizeof(struct arm_saved_state)
      708: bl   bcopy
      70c: add  r0, r5, #928   ; the VFP state, 264 bytes, copied separately

So the child's saved state begins as an exact copy of the parent's *as of the `svc`*: `save_pc` =
`0x11a4` (the return address of the `svc`, which is where the child's first fetch went), `save_sp` =
`0x00101efc`, and `save_cpsr` = the flags the parent had at that instant. The run confirms all three
independently - `xnu_live_sleh_pc = xnu_live_sleh_far = 0x000011a4`, `xnu_live_sleh_sp = 0x00101efc`,
`xnu_live_sleh_cpsr = 0x40000010` - and the cpsr word is the interesting one, because `0x40000010` is
`Z` set and `C` clear, and no instruction between the fork's `svc` and `0x11a4` set either: `Z` is left
over from `cmp r1, r0` at `+76` (`0x112c`, the read-back check of the mapped page, equal), and `C` is
clear because the *syscall entry* cleared it - the same carry-as-errno convention the fixture's own
`bcs entry_failed` reads, and the same one visible in the parent's two page faults (aborts 5 and 6,
`cpsr 0x40000010` at `0x1118` with `C` clear, then `0x60000010` at `0x1124` after `cmp r3, #0` set
`Z` and `C`). The child's first instruction therefore executed with the parent's flags, the parent's
stack and the parent's pc: **the copy is a copy, and the run can see the seam.**

What the copy *cannot* be is the last word on the child's `r0`, because `fork1` calls one more function
between the copy and the resume:

    kern_fork.c:590   if (!spawn) { thread_dup(child_thread); }
    kern_fork.c:636   thread_set_child(child_thread, child_proc->p_pid);

and `thread_set_child` is, on ARM, three instructions:

    0000084c <thread_set_child>:
      84c: mov  r2, #1
      850: str  r1, [r0, #848]   ; child_state->r[0] = (uint_t) pid
      854: str  r2, [r0, #852]   ; child_state->r[1] = 1
      858: bx   lr

The source is `osfmk/arm/status.c:722-730`, and the comment at its call site is the one that names its
purpose - "Blow thread state information; **this is what gives the child process its 'return' value
from a fork() call**" - so the child's return value on this platform is the pair `(r0 = pid, r1 = 1)`,
and the *parent's* is the pair `(r0 = uu_rval[0] = pid, r1 = uu_rval[1] = 0)`, written by
`arm_prepare_u32_syscall_return` (`bsd/dev/arm/systemcalls.c:293`). Both processes are told the pid;
the flag is what differs. **`machine_thread_dup` and `thread_set_child` are 250 lines apart in the same
file**, and the header's "`save_r0` has exactly one writer in this tree" was true of neither of them
being read as a pair: the writer the step was reasoning about is the one that does not run for the
child, and the writer that does run for the child is the one the step did not read.

### The fixture's `mov r0, #0` is a word nobody reads

That makes three readers of the fixture's own value, and none of them is the kernel:

  - **not an argument.** `fork`'s prototype is empty, so `sysent[2]`'s munger word is 0 and `sy_narg`
    is 0 (`python3 tools/check_sysent_table.py` prints both), `arm_get_syscall_args` is never called for
    this slot, and `uu_arg[0]` keeps what the *previous* call left in it. The run publishes that word
    beside the return: `xnu_live_fork_uap0 = 0x000011e0`, which is the `/dev/nosuch` `open`'s path
    address - `xnu_live_open_path = 0x000011e0` and `error = 2` two records earlier - and not
    `0x000011d4`, the `/dev/rmd0` one. (The 505 header predicted `0x000011bc` there from 504's run;
    the value moved with the program, which is the record doing its job.)
  - **overwritten in the parent** by `arm_prepare_u32_syscall_return` on the way out: `save_r0 = 2`.
  - **overwritten in the child** by `thread_set_child` while the parent is still inside the call:
    `r[0] = 2`.

The fixture's zero is read by nobody, and that is measurable rather than argued: the child's branch at
`0x11a4` went to `b spin` (`0x11ac`) instead of `beq entry_child`, and the run holds the child's `r0`
from the far side, in a syscall's own answer.

## The readings

In the order the log writes them (line numbers are `/tmp/cancro-last_kmsg.txt`):

| line | record | what it says |
|---|---|---|
| 6250 | `xnu_live_pth_hashinit_seq = 1`, `p = 0xc06a6610` | process 1's pthread hash, at boot |
| 7636 | `xnu_live_pth_wqmark_seq = 1` / `wqexit_seq = 1`, same `p` | `exec_mach_imgact`'s two workqueue slots, 473's pair |
| 7668 | `xnu_live_getpid_seq = 1`, `value = 1` | the program's *first* instruction: `svc` with `r12 = 0x14`, answered 1 |
| 7672-7746 | `mmap_seq = 1` `value = 0x00102000`, `poll_seq` 1 and 2 | 502's and 503's records, unchanged, `thread = 0xc060bec0` |
| 7804-7828 | `open` `/dev/rmd0` `error = 0` `fd = 0`; `read` 4 bytes, `word_before = 0x00102000`, `word_after = 0xfeedface`; `open` `/dev/nosuch` `error = 2` | 504's records, unchanged, including the driver's word |
| 7830 | `xnu_live_pth_hashinit_seq = 2`, `p = 0xc06a68c8` | **the OS made a process**: `forkproc`'s call, inside the fork |
| 7833 | `xnu_live_fork_seq = 1`, `caller = 0x80285848` (`unix_syscall+0x100`), `uap0 = 0x000011e0`, `error = 0`, `ret_lo = 2`, `ret_hi = 0` | the parent's answer: pid 2, no error, `r1 = 0` |
| 7839-7914 | `xnu_live_getpid_count` `2, 4, 8, ... 0x4000`, `last = 1` every time | the parent's spin, alone |
| 7914-7921 | `xnu_live_tmr_qexp_seq` 1..6, `thread = 0xc060bec0`; then `sleh_seq = 8` | **the parent's quantum expired six times before the child was ever dispatched** |
| 7921 | `sleh_seq = 8`: `type = 3` (prefetch abort), `fsr = 5`, `far = pc = 0x000011a4`, `thr = 0xc060a480`, `sp = 0x00101efc`, `cpsr = 0x40000010`, `user = 1` | **the child's first dispatch**: its first fetch, of the word right after the fork's `svc`, on a map it had just inherited - a lazy fault, and the child goes on to make a syscall, so it was recovered |
| 7936 | `xnu_live_getpid_change_seq = 0x5e8b`, `value = 2`, `error = 0` | **the child's first syscall, call 24107, answered 2** - its own pid |
| 7939 | `xnu_live_undef_seq = 1`, `pc = 0x000011d0`, `lr = 0`, `spsr = 0x20000010`, `user = 1`, `user_seq = 1` | `cmp r0, #1` with `r0 = 2` is `NE` (`Z` clear, `C` set - the `spsr`'s own two flags), so the next instruction is `bne entry_failed` and `0x11d0` is `udf #1` |
| 7947-7955 | `block_thr = 0xc060a480`, then `xnu_live_osr_caller = 0x8027cab8` (`threadsignal+0x198`), `ns = 2`, `code = 4`, `ret = 0xc0681f78` | **the death was handled**: the child's thread is in the signal path and the trap became an exit reason |
| 7963 | `xnu_live_stub_hit_seq = 1`, `name_ptr` = the pthread table's own name, `caller = 0x802933b4` (`gather_populate_corpse_crashinfo+0x5a0`) | the task became a corpse, and the corpse path is where it hit a slot this table supplies |

Five absences say as much as the rows: **no `xnu_live_exit_*`**, **no `xnu_live_sigchld_*`**, **no
`xnu_live_psignal_calls`**, no `panic`, and `xnu_entry_abort_entries = 0`. The first three are the
`exit` arm this run did not take (they are 506's readings, not 505's); the last two are the point of
the step.

The two threads are named by records rather than by inference: the parent's is `0xc060bec0` - the
thread the `mmap` record publishes, and the one the six quantum expiries belong to - and the child's is
`0xc060a480`, which is the faulting thread of abort 8, the thread the signal path blocks, and (with
`block_thr = 0xc05d3270` beside it) not any of the kernel threads.

**The one reading this run leaves open** is the schedule: the child's first instruction ran *after* the
parent had been round its loop 24105 times and after the parent's quantum had expired six times. That
is a measurement of order, not of time - the log has no tick at the fork, and this project has not
measured the quantum - so the duration is owed rather than claimed. What is not owed is the direction:
the child was not dispatched late *because* of its fault, since the fault is the first thing the child
did, and the fault record is after the counter's 16384 record.

## The image

  - the fixture is **61 words** against 52 in 504: 9 words of program, of which 4 are the branch pair
    and its `mov r0, #0` is the word this document is about. It lives in the payload's RAM disk blob
    (`g_stage90_ramdisk` at `0x8050d000`, 8192 bytes, unchanged) as a `__PAGEZERO` + `__TEXT` Mach-O:
    pc `0x10e0` is file offset `0xe0`, and `__TEXT`'s 4096 bytes are the file's first 4096.
  - **`.text` 5285824 -> 5286944** (+1120: the 36 bytes of program plus the instrument's new writers -
    `entry_stubs.c` +134 lines, `entry_trace.c` +158 lines), and **the entry image's file is 5503612
    bytes again**, the number 502's, 503's and both of 504's builds had, because the growth is inside
    `.text`'s slack before the 16 KB-aligned `.data` (504 measured that).
  - `__bss_start 0x8053fa80`, 362888 bytes to `0x80598408`; headroom 1473528 bytes below
    `topOfKernelData`; the wrap census is unchanged at **65 wrapped symbols: 54 reached by a branch,
    1 same-object-only, 1 never called here, 9 by address only** - and `exit` and `fork` are both in
    that last group, which is what a call made *by the fixture through a syscall slot* looks like.
  - the six files this step is: `stages/stage90/xnu_arm_boot/build_entry.sh` (+49),
    `entry_ramdisk.s` (+159), `entry_stubs.c` (+134), `entry_trace.c` (+158),
    `tools/check_sysent_table.py` (+128), `tools/host_ramdisk_macho_check.py` (+276) - 851 insertions
    and 53 deletions.
  - the payload: `stage90.bin` 5999040 md5 `2c289e187fe361cf8d6a4536ed29682a`, `stage90.img` 6002688
    sha256 `48cd7060f240a0e9e231aeca6b5154e7e06719045a3d516c45e18c21862c41a3`, `stage90-qcdt.img`
    8523776 sha256 `b2b293a530ce3c80b3d70f171db868604a6c3ea0217f9c96202d78e1316d132c`,
    `kernel_size=5999040 dt_size=2521088 page_size=2048`; `check_sysent_table.py` reads all nine slots
    the fixture uses and both zero-argument ones as zero-argument.

The build was run twice; the two logs differ in one line, the timestamps of the four artifacts, whose
sizes are identical.

## The rest of the boot is unmoved

The OS console block is the same 19 lines it has been since 459, character for character, including
`Added memory device md0/rmd0 (02000000/0D000000) at 000000008050D000 for 0000000000002000` and
`BSD root: md0, major 2, minor 0` - and it still ends at `load_init_program: attempting to load
/sbin/launchd` with **no `failed loading` after it**, which is 490's reading of a *successful* exec, so
the process that forked in this run is the process `load_init_program` loaded. 502's, 503's and 504's
readings are unchanged in this run and are quoted above: the `mmap` returns `0x00102000` with the same
seven arguments, the two `poll`s ask for 5 ms and 40 ms and return 0 with `0x0002f741` and `0x000e1eed`
ticks between their two `mach_absolute_time` reads, the first `open` answers fd 0, the `read` copies
`0xfeedface` over `0x00102000` in the page, and the control `open` answers `ENOENT`. The entry report is
full again - `xnu_entry_kv_written = 0x1fff` of its 8192 bytes, `_dropped = 0x92d4` (37588 refusals) -
so the readings above are the live channel's, as they have been since 461.

## What is owed

  - **The fixture's discriminator, which is the next step.** The child must test the register the kernel
    writes for it - `r1`, 1 in the child and 0 in the parent - instead of `r0`. Then the `exit` arm runs
    for the first time in this walk, and the readings that come with it have never been seen here:
    `xnu_live_exit_pid = 2` with `xnu_live_exit_rval` composed into `0x300`, and the OS's own
    inter-process event, `psignal(pp, SIGCHLD)` from `proc_exit` (`bsd/kern/kern_exit.c:1443`), which is
    what `xnu_live_sigchld_signal = 20` and `xnu_live_sigchld_to = 1` would record. The fixture's
    `r0`-shaped idiom is not a defect in the kernel and not a defect in the fixture either: it is the
    *Unix* idiom, and this platform distinguishes the two halves with a flag word instead.
  - **The `exit` path's absence is two absence-kinds, not one.** The child's `exit` never ran (the arm
    was not taken); the *parent's* `SIGCHLD` never ran either, because there was nothing to reap - and
    the child's death, which did happen, produced no `psignal` record even though it went through
    `threadsignal`. Whether the instrument's `psignal` wrapper is on that path at all is unmeasured.
  - **Which pthread slot the corpse path hit.** `xnu_live_stub_hit_caller = 0x802933b4` names the call
    site and the record's name is the *table*'s name, so the slot is owed (this is 440's "the name is
    the table's" hazard, still unresolved for the corpse path specifically).
  - **Why the child waited.** Six quantum expiries of the parent before the child's first instruction,
    with the child runnable the whole time. A quantum measurement (`xnu_live_tmr_dl_*` carries the
    deadline words but no tick at the fork) would turn that order into a duration.
  - **Still owed from 504**: `xnu_live_mdevadd_base` as a page number, an arrival record at `mdevopen`,
    and a second `read` at an offset. **From 503**: which row of `timer_compute_leeway`'s table the
    `poll` thread took, the ~1.8 ms `microuptime`-vs-`mach_absolute_time` difference, and a third ask
    (5/40/400 ms). **From 502**: the frame's `0x038`/`0x03C` pair, intid 39, the `AckC`/EOI question,
    the registry's 4-slot capacity stop, claim 16's one-level derivation reader, the citation rule over
    the other cited files, the unstamped `out/xnu_asm_obj`, 497's conditional-clause mutation, the
    unregister guard's asymmetry, `/timer`'s second definition, the other device nodes,
    `MSM8974RootResource`'s `state0 = 0`, a name/class reader wider than eight characters, the release
    as a reading, `vm_fault`, 488's flag-list, 490's frames band, `xnu_live_dec_same`.

## Safety

Every device touch went through `stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; the boot was a non-persistent `fastboot boot` and
nothing was flashed. **One run**, log `/tmp/cancro-last_kmsg.txt` (584213 bytes) with
`No errors detected`, the device back on its own inside the capture window, and the hardware watchdog
armed at `0xf9017000` with a 25 s timeout and not needed. That the run ended with *two* processes alive
and one of them dead is worth saying out loud next to the safety line: the death this step measured is
the first one in this walk whose blast radius was a process rather than the machine, and the fixture's
own comment names the reason - `udf #1` in the child kills pid 2, and `udf #1` in the parent would have
killed `initproc` exactly as 478 measured. The run's answer landed on the side of the two that this
image can survive, and it did survive it.
