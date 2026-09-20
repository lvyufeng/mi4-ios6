# Experiment 480 — process 1 got a page of its own, and the first user-mode aborts this walk has taken

**479's agenda was "give process 1 a syscall whose effect is on the machine, not in a register", and it
named the shape: `mmap` of an anonymous page followed by a store to it.** Both went in. The fixture is
now twenty-seven instructions: it asks `getpid` (479's five, unchanged), then asks
`mmap(0, 0x1000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0)`, then **reads** the page it was
given and **writes** the address into it — and only then enters the loop 479's run measured.

Three things no run of this walk has produced before are in the log, and they are of three different
kinds:

  - **The kernel's own argument munger was measured.** On this target a BSD syscall's arguments are
    marshalled by `sysent[197].sy_arg_munge32`, and the eight words it left in `uap` are all eight
    present in the log in the ABI's own order, with the fixture's deliberate marker (`movw r5, #0x5a5a`)
    landing in **word 5 — the padding between `fd` and `off_t pos` that `mmap` never reads**. The
    call succeeding is not what makes this a reading: `MAP_ANON` makes `fd` and `pos` inoperative, so
    the call would work with the words in any order. The *words* are the reading.
  - **The first two user-mode data aborts this walk has ever taken.** All four `xnu_live_sleh` records
    479's run read are `_user = 0`; this run has those same four and then **`seq 5` with `_user = 1`,
    `fsr 0x7` and `far 0x00102000`** (the `ldr r3, [r0]` of the page `mmap` returned) and **`seq 6`
    with `_user = 1`, `fsr 0x80f`, the same `far`** (the `str r0, [r0]`). Two classes of fault, one
    page: translation, then permission-plus-write.
  - **`thread->map` and `map->pmap` were non-zero on process 1's own thread**, read by the `mmap`
    wrapper one syscall before the fixture touched anything. This is 474's question — its run stopped
    on exactly those two dereferences with the map pointer *zero* — answered by measurement for the
    path process 1's own faults take.

The rest of the run is the machine being where 479 left it: the OS console is **byte-identical to
479's**, including the `md0` line; the block set is unchanged (70 entered, 13 returned, 57 parked on a
timer that still does not exist); no trap report, no `xnu_live_undef_*`, no panic, no `pid 1 exited`.

## The capture, in the ring's own order

The live channel is a ring, and the order of its records is the order the machine produced them. Read
in that order this run says one thing very plainly: **the fixture's 27 instructions ran in the order
they are written**, with the two aborts exactly where the program puts them.

    xnu_live_sleh_seq       1 .. 4        the exec's four kernel data aborts, _user = 0, _back = 1..4
    xnu_live_getpid_seq     1             the fixture's first instruction
    xnu_live_getpid_value   0x00000001    the kernel's answer: this process is pid 1
    xnu_live_getpid_error   0x00000000
    xnu_live_getpid_caller  0x8027e248    = unix_syscall+0x100, the `mov r4, r0` after the `blx r3`
    xnu_live_mmap_seq       1             the second instruction group: one call, outside the loop
    xnu_live_mmap_caller    0x8027e248    the same call site, the table's own slot
    xnu_live_mmap_error     0x00000000
    xnu_live_mmap_value     0x00102000    the page the kernel gave the process
    xnu_live_mmap_thread    0xc047a270    TPIDRPRW at the call  \
    xnu_live_mmap_map       0xc0001340    thread->map             |  what 474 measured as 0
    xnu_live_mmap_pmap      0xc046e900    map->pmap               /
    xnu_live_mmap_arg0..7   see below     the eight words the munger wrote
    xnu_live_sleh_seq       5             _user = 1, fsr 0x00000007, far 0x00102000, pc 0x00001118
    xnu_live_sleh_seq       6             _user = 1, fsr 0x0000080f, far 0x00102000, pc 0x00001124
    xnu_live_getpid_count   0x00000002 .. 0x00800000    the loop, 23 powers of two, _last = 1

`0x1118` and `0x1124` are the fixture's own `ldr r3, [r0]` and `str r0, [r0]` (`entry_code + 0x38` and
`+ 0x44`, with `entry_code` at `0x10e0` — the address 475 measured `undef_pc` at). The fixture's `sp`
on both faults is `0x00101efc`, `_lr` is 0 on both because the fixture never writes `lr`, and
`_frame_ok = 1` on both: the frame's `SS_STATUS`/`SS_VADDR` agree with `cp15`, **so the offset control
that 472/473 built for kernel frames holds for user frames too**.

**The last record is a milestone, not a stop**, exactly as in 479: `0x00800000` is the last power of two
the `getpid` wrapper writes, so 8 388 608 loop iterations are a floor and the loop was still running
when the log ended. What ended the log is the same single reset source — the SoC watchdog the payload
arms before the jump (`hw_watchdog_bite_ticks_written = 0x000dffac` at `hw_watchdog_hz = 0x00007ffd`,
28.0 s, bark at `0x000c7fb5`, 25.0 s), which nothing disarms because the disarm lives in the payload's
exit path, which this run never reaches. **The two faults cost the boot nothing measurable**: the loop
still reaches its last power of two inside the same ≤28 s window 479's run did.

## The eight words, and the munger that wrote them

The fixture's second syscall is the first one in this walk to *have* arguments, so it is the first run
in which the argument path can be wrong without anything visibly failing. `arm_get_syscall_args`
(`bsd/dev/arm/systemcalls.c:337`) is the **munging** variant in this image — the file's
`#if __arm__ && (__BIGGEST_ALIGNMENT__ > 4)` is on, because the host grades as `CPU_SUBTYPE_ARM_V7K` —
so `unix_syscall` fills `uap` by calling `callp->sy_arg_munge32`. For index 197 that is
`munge_wwwwwl` (`bsd/dev/arm/munge.c:491`), and `tools/check_sysent_table.py` reads that **word out of
the linked image** rather than out of the source: `sysent[197].sy_arg_munge32 = 0x8027d7f0 =
munge_wwwwwl`.

What that munger does depends on the *style*, and the style is a property of the saved state:

    SS_TO_STYLE(ss) == kDirect   iff  ss->r[12] != 0            (osfmk/arm/munge.h)

`r12` is the register the user puts the syscall number in, and the fixture sets it immediately before
each `svc` — so a Unix syscall is always direct, and the indirect arm exists for the `r12 == 0` calling
convention. Direct, `munge_wwwwwl` is `munge_wlll` (`:364`) which is `munge_wll` (`:347`) plus two
words, and `munge_wll` in direct style is one `memcpy`:

    memcpy(args, regs, 6 * sizeof(uint32_t));   /* uap words 0..5 = ss->r[0..5] */
    uu_args[6] = ss->r[6];                      /* munge_wlll:   word 6 = r6         */
    uu_args[7] = ss->r[8];                      /*               word 7 = r8         */

`r7` is the frame pointer on this target, so the 8-byte `off_t` argument sits in `r6`/`r8` — the ABI's
even-index register pair after five word arguments. And the generated `struct mmap_args`
(`out/xnu_generated/bsd/sys/sysproto.h`) is `addr, len, prot, flags, fd` followed by `off_t pos`, which
alignment puts at word 6, leaving **word 5 as padding that the munger writes from `r5` and `mmap` never
reads**. Measured, all eight:

    xnu_live_mmap_arg0  0x00000000     r0 = addr  0
    xnu_live_mmap_arg1  0x00001000     r1 = len   0x1000
    xnu_live_mmap_arg2  0x00000003     r2 = prot  PROT_READ|PROT_WRITE
    xnu_live_mmap_arg3  0x00001002     r3 = flags MAP_PRIVATE|MAP_ANON
    xnu_live_mmap_arg4  0xffffffff     r4 = fd    -1
    xnu_live_mmap_arg5  0x00005a5a     r5 - the padding word, deliberately unread
    xnu_live_mmap_arg6  0x00000000     r6 = pos, low word
    xnu_live_mmap_arg7  0x00000000     r8 = pos, high word

**Words 4 and 5 together are the style reading and not just a layout one.** In the *indirect* arm of
`munge_wwwwwl` the word sequence shifts: `uu_args[4] = ss->r[5]`, and word 5 is left alone. So a
kDirect/kIndirect confusion would show up as `_arg4 = 0x5a5a` and a `fd` that is whatever the argument
buffer happened to hold — one number moved one place, with a call that still succeeds. The marker was
put in `r5` for that reason, and the run shows it where the direct style puts it.

That is what the eight keys are *for*, and it is why the fixture is 27 instructions rather than 5: 479's
`getpid` has `sy_arg_bytes = 0` (`{ int getpid(void); }` in `bsd/kern/syscalls.master`), so the whole
argument path — munger, style, `uap`, the 8-byte alignment of `off_t` — is a part of the kernel this
walk had never run.

**And the error test is the carry bit, not a sign.** `unix_syscall` ends in
`arm_prepare_u32_syscall_return` (`systemcalls.c:279`), which on error writes the errno into `save_r0`
*and* sets `PSR_CF` in the saved CPSR — libc's `cerror` convention. So the instruction after `svc` is
`bcs entry_failed`: a `cmp` there would overwrite the flag it is meant to test, and a failed `mmap`
would be read as an address.

## The two user aborts, and the zero-fill page read twice

`osfmk/arm/trap.c:293` reduces the DFSR to a *class* before anything is dispatched
(`status = regs->fsr & FSR_MASK`, `FSR_MASK = 0x40F`) and the write bit reaches `fault_type` by its own
test (`:339`, `regs->fsr & DFSR_WRITE`). Both of this run's user faults pass
`TEST_FSR_VMFAULT(status)` (`proc_reg.h:286`) and so take the user branch at `:511`, and the two
numbers say two different things:

    seq 5  fsr 0x00000007  = FSR_PFAULT ("Translation Page", proc_reg.h:270)   -> fault_type = R
    seq 6  fsr 0x0000080f  = DFSR_WRITE | FSR_PPERM ("Permission Page", :275)  -> fault_type = R|W

So the read of a page with **no translation** was serviced by installing a translation for it — and the
very next write found that translation **present and not writable**. Those are the two halves of the
classic anonymous-fault sequence, and the *value* half is measured by the fixture rather than inferred
from the FSR: after `ldr r3, [r0]` the program does `cmp r3, #0` / `bne entry_failed`, and the next
record in the ring is `seq 6` — the `str` — and not a `udf`. So the page the kernel gave the process
**read as zero**, which is what makes `vm_fault`'s zero-fill page the reading here and not a guess, and
`seq 6`'s `FSR_PPERM` says that same mapping had no write permission. The write fault then has to be
serviced *again*, by a path that gives the process a writable page of its own, and it is: the ring goes
straight on to the `getpid` loop, and there is no `pid 1 exited` line and no trap record anywhere in the
log.

**What is *not* in this run's log is which of the two mechanisms serviced the write.** `trap.c:561-568`
tries `arm_fast_fault(map->pmap, trunc_page(fault_addr), fault_type, TRUE)` first and falls through to
`vm_fault(map, fault_addr, fault_type, ...)` when that returns non-`KERN_SUCCESS`; a page that was
mapped read-only and is not a ref/mod-fault placeholder fails the first and takes the second, and that
is the expected route — but *expected* is not *measured*, and this image has no record of it. The
instrument is cheap and is the next step's: `--wrap=arm_fast_fault` in the entry link records it (the
reference is in `trap.o`, which is in the link, so the wrapper sees the handler's own call), and a
wrapper that writes one record per call would separate "handled quickly" from "faulted in" for the first
time.

**`seq 5` and `seq 6` have no `xnu_live_sleh_back` record, and that is the shape of the code.** 479's
four kernel aborts all have one (`_back = 1..4`); these two do not, and neither does `_at_back` go past
4. The reason is a `goto` and not a missing write: the kernel-mode path ends at

    exit:
        if (recover) thread->recover = recover;
        return;                                   (trap.c:604-607)

so a serviced kernel fault *returns to its caller* — the wrapper in `entry_trace.c`, whose post-call
code writes `_back`. The user-mode path ends at

    exception_return:
        if (recover) thread->recover = recover;
        thread_exception_return();                (trap.c:600-602)

which never returns to the wrapper at all: `thread_exception_return` restores the user state and
re-enters user mode. **So `_back` is absent exactly when a fault was serviced *in user mode*, and the
absence is a positive reading as long as it is read with its complement**: a user fault that was *not*
serviced would have gone to `exception_triage` (`:589`) — SIGSEGV, `pid 1 exited`, and
`launchd_crashed_panic`, which 478 measured — and none of those exists. The pair *(no `_back`) and (the
loop still climbing)* is what says "serviced and retried"; either one alone says nothing.

## `thread->map`, `map->pmap`, and what 474's zero was not

474's run is served four data aborts and returns from all four, and then records a fifth whose own fault
address is `0x00000028` — that is `MAP_PMAP`, 40 bytes from a **NULL** map pointer — whereupon the
handler re-enters itself 0x250 bytes further down the kernel stack until the stack runs out and the boot
stops without writing a report. The measured field that run reported was
`current_thread()->map = 0` on thread `0xc0495480`, which its doc named "the thread that ran the exec".

This run reads the same two fields on the path process 1's own faults take, one syscall early:

    xnu_live_mmap_thread = 0xc047a270   TPIDRPRW (mrc p15,0,r,c13,c0,4), the same thread as seq 5/6
    xnu_live_mmap_map    = 0xc0001340   thread->map  (offset 692, ACT_MAP)
    xnu_live_mmap_pmap   = 0xc046e900   map->pmap    (offset 40,  MAP_PMAP)

and there is a **second, independent reading of the same field in the same run**: `seq 4` is a *user*
address (`far 0x00101f28`, the exec's string copyout) taken in kernel mode, and `trap.c:443` chooses
`thread->map` for a non-kernel address — the fault returned (`_back = 4`), so the map it dereferenced
was not NULL either. Two faults that both *require* a non-NULL map, on two different threads (the exec's
`0xc04b2ec0` for `seq 1..4`, the fixture's `0xc047a270` for the mmap record and `seq 5/6`), were both
serviced.

So the honest statement is narrower than "474 is fixed", and it is narrower in a useful direction:

  - **It is not a property of the exec path in general.** Whatever moment 474's record captured, this
    run does not reproduce it, and the field is non-zero at *its* two checkpoints.
  - **474's zero was a moment, not a state.** The two loads that faulted there are the same two loads
    `entry_saved_state.h` now records offsets for, and the address that faulted was the *offset from
    the NULL base* — which is why the record's fault address and its `MAP_PMAP` constant are the same
    number. Which of the map-choice conditions was in force at that moment is still owed; what this run
    adds is that the condition on *this* path is satisfied, measured, on the thread that runs the
    fixture.

## Where process 1's stack actually is, and why `mmap`'s first answer is 0x00102000

This started as a puzzle about one number and turned into a reading about the image. The run gives three
addresses in the same neighbourhood, and each is a different kind of evidence:

    0x00101efc   the fixture's sp on both user faults      (the frame's SS_SP)
    0x00101f28   the exec's string copyout, far of seq 4   (479's fourth abort, same address)
    0x00102000   what mmap returned                        (the fixture's own r0)

`bsd/arm/vmparam.h` says `USRSTACK = 0x27E00000` and `MAXSSIZ = 1 MB`, and the *built* image agrees —
`thread_userstackdefault` is two instructions, `movw r2,#0 / movt r2,#0x27e0`. A fixture whose
`LC_UNIXTHREAD` sets `sp = 0` (this one does) is therefore supposed to get a stack near `0x27E00000`,
and the run says `0x00102000`. The chain that explains it is short and every step is in the tree:

  - `load_unixthread` (`bsd/kern/mach_loader.c:1960`) sets `user_stack_alloc_size = MAXSSIZ` for a
    non-custom stack, and `create_unix_stack` (`bsd/kern/kern_exec.c:4917`) allocates
    `size = round_page(MAXSSIZ)` at `addr = trunc_page(user_stack - size)` = `0x26E00000` with
    `VM_FLAGS_FIXED`;
  - when that fails, and **only** then, Apple's own next statement runs: `addr = 0;` … *"Can't allocate
    at default location, try anywhere"* … `user_stack = addr + size; load_result->user_stack =
    user_stack; p->user_stack = user_stack;` (`:4930-4946`);
  - and the address the process's strings land at is `p->user_stack`, because `exec_mach_imgact` does
    `ap = p->user_stack` and passes `&ap` down into `exec_copyout_strings`, then sets the thread's
    `sp` from the `ap` that came back (`:1109-1120`).

So `p->user_stack = 0x00102000` is not a coincidence of three numbers: it is `addr + size` with
`size = MAXSSIZ = 0x100000`, which pins `addr = 0x2000` — **the first free megabyte in the process's
map, immediately above `__TEXT`** (which `__PAGEZERO` raises `min_offset` to 0x1000 above, and which is
one page). The fallback branch is *measured*, not inferred: no other line in the kernel writes
`load_result->user_stack`, and the strings and the `sp` are both read from it.

Two things follow, and the second is why this matters for a *fixture* rather than for a stack:

  - **This image's `USRSTACK` is unreachable for the stack's own `VM_FLAGS_FIXED` allocation**, so
    process 1's stack is a 1 MB reservation at `[0x2000, 0x102000)` with the process's strings and `sp`
    at its top. Derived rather than measured, the guard boundary follows from the same constants:
    `prot_size = trunc_page(MAXSSIZ - unix_stack_size(p))`, `unix_stack_size(p) = DFLSSIZ =
    1 MB - 16 KB`, so `trunc_page(0x4000)` is one 16 KB page — and that is the same number as
    `vm_initial_limit_stack`'s `rlim_max` (`MAXSSIZ - PAGE_MAX_SIZE`, `bsd/kern/bsd_init.c:383`) seen
    from the other side. The run does not touch the low end of the stack, so the split is a derivation,
    not a reading.
  - **`mmap`'s first free page is the page above the stack**, so `0x00102000` is exactly what an
    `VM_FLAGS_ANYWHERE` allocation must answer in this map, and the fixture's `ldr`/`str` faults are
    taken on a page the kernel had *just* entered into the process's map. The fixture's address was
    predicted by the mechanism rather than chosen, which is the strongest thing that can be said about
    a fixture that maps one anonymous page.

**What is still owed here is *why* the `VM_FLAGS_FIXED` attempt at `0x26E00000` is refused.** A
map-level `KERN_NO_SPACE` is the shape of it, and an address-limit or hole-list condition inside
`vm_map_enter` is what to look for; the cheap instrument is `--wrap=mach_vm_allocate_kernel` (its
reference is in `kern_exec.o`, which is in the link), recording `addr`, `size` and the return of the
first call — which would turn "the fallback ran" into "the fallback ran *because this*".

## The console, the layout, and the 24 keys

The OS's own console is **byte-identical to 479's**, for the first step in three — `diff` of the two
logs' captured regions is empty, including the line that moved in 478 → 479:

    Added memory device md0/rmd0 (02000000/0D000000) at 0000000080501000 for 0000000000002000

25 lines, 24 carriage returns, 1290 bytes by `wc -c` (1289 without the final terminator, which is 479's
published figure for the same text). `md0` did not move because nothing it is computed from moved, and
the build's own numbers say why: `.text` grew by 864 bytes (5230176 → 5231040) and grew *into the gap*
below `.data`'s base — the copied image still ends at `0x533a74` = 5454452 bytes, `__bss_start` is still
`0x80533a80`, and the new initialised globals (`g_mmap_first_value`, `_thread`, `_map`, `_pmap`,
`args[8]` — 15 words at `0x80500008`) fill slack inside `.data`'s first page, which ends where the RAM
disk's fixed 0x1000 offset begins. The `.bss` **size** did move, by 0x40 (`0x57ac8` → `0x57b08`), and
that number is a layout number rather than a storage one: the new *uninitialised* storage in it is three
words (0xC), and the rest is per-input-section alignment below `entry_stubs.o`'s `.bss`.

The keys are worth one careful paragraph, because 479's document got this one wrong. Every run of this
image writes **24 `xnu_entry_*` keys before the OS console starts** — the payload's own boot report
(`xnu_entry_status = 0x90000001`, `xnu_entry_checks = 5`, `xnu_entry_failures = 0`, the layout words).
The trap path writes keys under the *same prefix*, one per line with no `MI4IOS6_STAGE90_XNU` prefix.
Counted separately:

    boot-report keys (MI4IOS6_STAGE90_XNU xnu_entry_…)   478: 24   479: 24   480: 24
    trap-report keys (leading space, xnu_entry_…)         478: 473  479: 0    480: 0

479's document says "**no ` xnu_entry_*` report key at all** (478: 473)". The 473 is right and the
*"at all"* is not: the 24 boot keys are in that run's log, as they are in this one. The claim it meant
is the one this table states — no *trap*-report key — and the difference matters only because a reader
checking the sentence with the obvious `grep` gets 24 lines and cannot tell whether the document is
wrong or the log is (defect 212).

The rest of the channel is the machine's state, unchanged from 479 and read as such: `xnu_live_block_seq`
runs to `0x46` (70 blocks entered) with `_returns = 0x0d` (13 returned), so **57 threads are still
parked on a wakeup that needs a clock**; `xnu_live_ostext_chars = 0x3ee`, `_tank = 0x262`, `_at =
0x00049cd0`, `_heals = 1` — every one of them the same value as 479's run.

## The three checks, and the four defects the step's own checks caught

`tools/check_sysent_table.py` grew a second slot and the munger that reads it. It now asserts, from the
linked image, `sysent[20] = 0x804733e0 = __wrap_getpid`, `sysent[197] = 0x80473428 = __wrap_mmap` (and
*not* `mmap` at `0x80291c20`), and `sysent[197].sy_arg_munge32 = 0x8027d7f0 = munge_wwwwwl` — the last
of which is the fact the whole argument section above depends on, and which no other check can see.
Nine witnesses spread from 0 to 197 keep the 16-byte stride a reading, `--selftest` refuses **all 12
mutations** (including `zero_the_munger`, `the_wrapper_in_the_munger_slot` and `each_wrapper_in_the_other_slot`),
and the `ok:` line is the wrapper slots rather than the last note (207).

`tools/host_ramdisk_macho_check.py` decodes the program as **27 words** instead of 5: `svc`, `udf`, `b`,
`mov`, `movw`, `mvn`, `cmp` against a register and against an immediate, `ldr`/`str`, with every branch
decoded to a **word index** and checked to stay inside the file range, every literal read back out of
the file it belongs to (`SYS_MMAP`/`SYS_GETPID` from `syscalls.master`, `MMAP_PROT`/`MMAP_FLAGS` from
`bsd/sys/mman.h`, `MMAP_LENGTH` from `osfmk/arm/proc_reg.h`'s `ARM_PGSHIFT`), and — the one that is not a
value — **word 8 (the `movw r5, #0x5a5a` marker) as a property**: it must be a `movw` into `r5`, and its
immediate is free. `--selftest` refuses all **61 mutations**, and the payload itself carries the
program once, at file offset `0x57a294`:

    800000ef 010050e3 1600001a 0000a0e3 001001e3 0320a0e3 023001e3 0040e0e3
    5a5a05e3 0060a0e3 0080a0e3 c5c0a0e3 800000ef 0b00002a 003090e5 000053e3
    0800001a 000080e5 001090e5 000051e1 0400001a 14c0a0e3 800000ef 010050e3
    0000001a faffffea f100f0e7

`tools/check_saved_state_offsets.py` now compares four numbers rather than two: the two frame offsets
and `STAGE90_ACT_MAP` (692) / `STAGE90_MAP_PMAP` (40) against this configuration's generated `assym.s`,
so the two dereferences the new instrument reads cannot drift from Apple's `struct thread`/`struct
vm_map` silently; 11 mutations, all refused.

Four defects, and as usual the interesting ones are in the checks:

  - **The fixture check called the image wrong for being right** (208). `decode_arm` classified `udf` by
    a mask (`word & 0x0F000000 == 0x07000000 and (word >> 4) & 0xFF == 0xF0`) that rejects `udf #1`
    (`0xe7f000f1`) — so the 27-word comparison failed at the last word of a *correct* program. Found by
    decoding the fixture's own bytes beside `describe()` in a scratch script, and fixed by matching the
    encoding rather than a remembered shape (`word & 0x0FF00000 == 0x07F00000 and word & 0xF0 == 0xF0`,
    immediate `((word >> 8) & 0xFFF) << 4 | (word & 0xF)`).
  - **The reporter raised while reporting** (209). `describe()` interpolated the operand with
    `#0x%x` and threw `TypeError` for the `("?", (0xe7f000f1,))` case — i.e. exactly in the situation a
    checker exists for, reporting a word it does not recognise. It now formats integers as `#0x…` and
    anything else as `repr`, because a checker that crashes on a wrong image is indistinguishable from a
    checker that crashes.
  - **The selftest found a hole and not a fault** (210). Word 8's `^ 1` mutation was *accepted*, because
    word 8's immediate is deliberately free (the marker is a property, not a value). Fixed by excluding
    word 8 from the per-word list *with the reason written down*, and adding four mutations of the
    property itself (`the pad word is never set (a nop)`, `the pad word is zero`, `the pad word is a
    rotated mov`, `the pad word repeats an argument`).
  - **A mutation that crashed instead of being refused** (211). `the_mmap_wrapper_elsewhere` moved the
    wrapper one slot past 197 — which is the last witness, so slot 198's bytes lie outside the span the
    check reads, and the mutation raised `struct.error` instead of being refused. Replaced with
    `each_wrapper_in_the_other_slot`, which swaps 20's and 197's wrappers and stays inside the span. The
    tell: **a mutation is a *test* only if it lands where the check looks**; a mutation built from an
    index instead of from the span is 205's family (a claim about a field the mask does not cover).

## What 481 has to do

**The frontier is still the absence of one, and the plan of record has not changed.** This run adds a
page of the process's own memory to what the OS has done — and the machine is otherwise exactly where
479 left it: 57 threads parked, `xnu_live_block_enter` at `0x800f7efc` = `Call_continuation+0x1c` among
them, and **not one of them parked on a missing symbol**. They are parked on a wakeup, and a wakeup
needs a clock. So, unchanged from 479's own agenda and now the only thing between this run and the
drivers moving:

  - **The timer.** `ml_init_timebase` plus an MSM8974 `tbd_ops_t` over the GPT at `0xf9020000`
    (19.2 MHz, IRQ 19), and `IOCPUInterruptController` behind it. That is a kernel-source step with an
    interrupt path in it — the first of this walk — and it is what "把基础驱动跑起来" needs. 480 was a
    fixture step and did not move it; saying so is part of the record.
  - **The owed list, which this step narrowed rather than emptied**: which of `arm_fast_fault`/`vm_fault`
    serviced the write fault (`--wrap=arm_fast_fault`); why the `VM_FLAGS_FIXED` stack allocation at
    `0x26E00000` is refused (`--wrap=mach_vm_allocate_kernel`); 474's `thread->map = 0` *moment*
    (narrowed: not the state of either thread on this path); 448's `_bad` slots as a pair of keys that
    names each one's sense; the pthread table's other ~34 slots; `osfmk/kperf/kperfbsd.c`; the untraced
    build's `entry_stubs.c` compile errors; `thread_bootstrap_return`.

**Measured:** gate passed, exit 0, device back on Android on its own, log 460167 bytes / 5031 lines /
1072 `xnu_live` records, `No errors detected`, no `pid 1 exited`, no `xnu_live_undef_*`, no
`xnu_entry_panic_*` and no *trap*-report ` xnu_entry_*` key (478: 473) with the same 24 boot keys every
run has; the SoC watchdog the only reset source (bite `0x000dffac` ticks at `0x00007ffd` Hz = 28.0 s,
bark `0x000c7fb5` = 25.0 s, nothing disarmed it), so every number in this document comes from the live
channel and the console tank. `.text` 5231040 (479: 5230176), `.data` 0x326b0, `.bss`
0x80533a80..0x8058b588 (0x57b08), image 5454452 (479: the same), headroom 1526392, boot_args
0x8058d000, payload `stage90-qcdt.img` 8474624 sha256
`379b67175428eba6df35fb96877b67d592240b3caea181ada224d2deaaa94a99`, undefined 26, **46 `--wrap`ped
symbols** (41 reached by a branch, 1 same-object-only, 1 never called here, 3 by address only: `vcputc
getpid mmap` — `mmap` joining `getpid` in the class whose only reference is a data initialiser, which is
the 479 census statement applied to the second table slot), and **the entry image rebuilt byte-identical
after the run** (`cmp` clean, `xnu_arm_entry.bin` sha256
`c8ca5c0fb00a712db606796cf6eaf9a498cad548e1760e31d13895788b7cb9f2`), so the run above is the run the
committed tree produces.
