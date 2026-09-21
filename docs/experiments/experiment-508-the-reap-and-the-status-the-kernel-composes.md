# Experiment 508 — the parent asks for its child, and the kernel answers with the status it composed

**One line.** `wait4` — `sysent[7]`, the third of the three calls a Unix process's life is — is wrapped,
the fixture's parent makes it, and **the call blocks**: `xnu_live_sleep_ent` carries a record whose
`site` is `wait4_nocancel + 0x19c` (the instruction after `bl __wrap_msleep0` at `0x802950a8`), whose
`pri` is `0x520` (`PWAIT | PCATCH | PDROP`), whose `chan` is the parent's own proc and whose `tmo` is 0 —
so the parent is parked while the child finishes dying, exactly as `kern_exit.c:1906` says. The child's
death records then appear **between** the call's two records (`_seq = 1` at 7837, `exit`/`pth_delete`/
`sigchld` at 7872–7890, `_seq = 2` at 7924) — the wakeup read out of the log's own order rather than
argued — the reap runs, and the kernel's `copyout` of the composed status takes a **kernel-mode write
fault on the fixture's own page** which the kernel services and retries (`sleh_seq = 8`, `far =
0x00102000`, `pc = 0x80016360` = `Lcopyout_bytewise`'s store, `recover = 0x8001644c` = `copyio_error`,
`redirect = 0`). The parent's `ldr r3, [r9]` / `cmp r3, #0x300` passes, and the **second** `wait4` on the
same pid answers **`ECHILD`** (`_error = 0x0a`, `_ret = 0`) while reading the page back as
**`_status = 0x00000300`** with `_copy_error = 0` — and that read-back is the measurement of the *first*
call's copyout, because the page held **`0xfeedface`** four keys earlier (504's `read` pair, this same
run) and the second call returns `ECHILD` without copying anything out at all. The fixture's program is
now complete: make a process, lose it, reap it, and be told what it exited with. One thing this step's
own prediction got wrong and it is recorded below: **the first call's `_done_seq` record was never
written** — six keys the writer emits unconditionally, absent, with `xnu_live_capped` absent too, so the
cap is not the cause.

    python3 tools/host_ramdisk_macho_check.py --selftest out/stage90/xnu_arm_entry.elf  # all 128 refused
    python3 tools/check_sysent_table.py --elf out/stage90/xnu_arm_entry.elf           # sysent[7] = __wrap_wait4, munge_wwww, 4/16
    python3 tools/check_pthread_table_slots.py --selftest                            # all 7 refused
    python3 tools/check_timer_sources.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 48 refused
    python3 tools/check_timer_line.py --selftest                                     # all 20 refused
    python3 tools/check_os_entry.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 30 refused
    python3 tools/check_irq_routing.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 57 refused
    python3 tools/check_driver_catalogue.py --selftest                               # all 131 refused
    python3 tools/check_experiment_index.py                                          # 485 rows

## What the step was for

507's run ended with the walk's first inter-process event and its first ending that is not a stop: the
child died, the OS told its parent (`psignal(pp, SIGCHLD)`), and **nothing ever asked for it**. A zombie
nothing reaps is the state `wait` exists to end, and it is also the only call in this fixture's program
whose *answer* is composed by the kernel out of things the program never sees: `exit` puts
`W_EXITCODE(uap->rval, 0)` into `p->p_xstat` (`kern_exit.c:682`, `:823`), `wait4` masks it
(`status = 0xffff & p->p_xstat;`, `:1782`) and copies it out, and the pid it returns is a process that
`reap_child_locked(q, p, 0, reparentedtoinit, 0, 0)` (`:1834`) has already freed. So the fixture's 3
becomes `0x300` one syscall away from the register that carried it, and the step is a reading rather
than a tautology: a zero-filled field, a stale register, or a wrapper publishing its own argument cannot
produce it.

## The instrument

Two files, and no change to the kernel:

  - **`entry_trace.c`'s `__wrap_wait4`** — a record before the real call (`xnu_live_wait_*`, seven keys:
    the call site, the waiter's pid, and the four argument words) and a record after it
    (`xnu_live_wait_done_*`, six keys: `error`, `ret`, the read-back's `copy_error`, the word read back,
    and the tick count). The split is deliberate and it is the *block* that makes it necessary: the
    duration of a blocking `wait4` is not a number any single record can hold, while the log's order can,
    and a pair that assumed the two records are adjacent would make the ordering unreadable.
  - **`entry_stubs.c`'s `entry_note_wait` / `entry_note_wait_returned`** — the two writers, with the
    before-writer returning its sequence number so the after-record can name the call it belongs to
    (a `wait4` can be entered once and return once, but the record pair is not necessarily adjacent).

**The four words are the four fields, and that is checked rather than read off the prototype.**
`sysent[7]`'s munger is `munge_wwww` and `wait4_nocancel` reads the argument struct at byte offsets 0,
4, 8 and 12 (`ldr r0, [r6]`, `ldr r1, [r6, #4]`, `ldrb r0, [r6, #8]`, `ldr r1, [r6, #12]` in
`out/xnu_kernel_obj/bsd_kern_kern_exit.o`), which is what makes `user_addr_t` one word on this target —
and `tools/check_sysent_table.py` gained an `ARGUMENT_WORDS` clause this step that reads `sy_narg` and
`sy_arg_bytes` out of the image and fails if the slot marshals anything but four words into sixteen
bytes. Without it, a slot with three marshalled words would leave `r3` holding whatever the last syscall
left there and the `_rusage` record would be a fact about that register.

**The fixture grew by seventeen instructions and one absence clause.** The program is 78 words (312
bytes) now, `entry_parent:` at `+220`, and `tools/host_ramdisk_macho_check.py` reads every one of them:
the table names the four argument registers, the two return checks, and the second call's arguments, and
two new structural clauses make the shape structural rather than described — **the parent's branch must
not land anywhere in the loop**, and **no conditional branch may appear between the second `svc` and the
loop**, because the second call's wrong answer must be *recorded* rather than faulted on (503's rule; a
`udf #1` here would kill `initproc` for a reason that has nothing to do with the call it stands behind).
Its `--selftest` is 128 mutations now, was 111.

**And one build check had to be told a fact it cannot see.** `wait4` is reached *only* through
`sysent[7].sy_call`, so the wrap census can find no branch to `__wrap_wait4` anywhere in the linked
image, and the build refused the step until `wait4` was added to the census's `by_address` list — the
list whose meaning is "this wrapper is reached through a table, and `tools/check_sysent_table.py` is the
thing that proves the table's word is the wrapper's address".

## The readings

In the order the log writes them (line numbers are `/tmp/cancro-last_kmsg.txt`, 573171 bytes / 8369
lines):

| line | record | what it says |
|---|---|---|
| 3994 | `load_init_program: attempting to load /sbin/launchd` | the console block's last line, unchanged from 505–507; no `failed loading` after it |
| 7817-7818 | `xnu_live_read_word_before = 0x00102000`, `_read_word_after = 0xfeedface` | **504's pair, in this same run** — the page the parent will hand to `wait4` holds the fixture's own address and then a driver's four bytes, so `0x300` cannot have been there before the reap |
| 7837-7843 | `xnu_live_wait_seq = 1`, `_caller = 0x80285898` (`unix_syscall + 0x100`), `_who = 1`, `_pid = 2`, **`_status_ptr = 0x00102000`**, `_options = 0`, `_rusage = 0` | the parent's first call, and the pointer is the address 504's `_read_buf` names |
| 7844-7850 | `xnu_live_sleep_site = 0x802950ac` (`wait4_nocancel + 0x19c`), `_thr = 0xc04a70d0`, `_chan = 0xc05453a8`, `_wmsg = 0x804ca4e2` (`"wait"`), **`_pri = 0x00000520`**, `_tmo = 0` | **the call blocked** — `PWAIT | PCATCH | PDROP` on the parent's own proc with no timeout, exactly `kern_exit.c:1906` |
| 7854 | `xnu_live_block_seq = 0x4b`, `_thr = 0xc04a70d0` | the same thread, parked — the dispatcher's own record of it |
| 7857-7871 | `sleh_seq = 7`: `_user = 1`, `_far = _pc = 0x000011a4`, `_sp = 0x00101efc`, `_thr = 0xc04a7df0` | the child's first dispatch, as 505–507 measured it — and `_thr` is *not* the sleeper's, so the two threads are told apart by the log |
| 7872 | `xnu_live_exit_seq = 1`, `_caller = 0x80285898`, `_pid = 2`, `_rval = 3` | the child's own `exit`, while the parent is parked |
| 7882-7890 | `xnu_live_pth_delete_seq = 1` (`p = 0xc05450f0`), `xnu_live_sigchld_seq = 1` (`_from = 0`, `_to = 1`, `_signal = 0x14`), `psignal_calls = 1` | 507's records, unchanged — the death finished and the OS told process 1, **between** the two `wait4` records |
| 7892 | `xnu_live_block_seq = 0x4c`, `_thr = 0xc04a7df0` | the child's thread, blocking as it is torn down |
| 7897-7914 | `sleh_seq = 8`: `_type = 4`, **`_fsr = 0x0000080f`**, **`_far = 0x00102000`**, `_thr = 0xc04a70d0`, **`_pc = 0x80016360`**, **`_lr = 0x80295114`**, `_sp = 0xc8103f14`, `_cpsr = 0x20000013`, **`_user = 0`**, `_frame_ok = 1`, `_recover = 0x8001644c`, `_back = 5`, **`_redirect = 0`**, `_at_back = 8` | **the kernel's own `copyout` of the status faulted on the fixture's page**, armed with its recovery address, and was serviced and retried instead |
| 7920-7923 | `xnu_live_block_seq = 0x4d`, `_thr = 0xc046eb10` | another thread's block — the ordering marker that separates the fault from the second call |
| 7924-7930 | `xnu_live_wait_seq = 2`, `_caller = 0x80285898`, `_who = 1`, `_pid = 2`, `_status_ptr = 0x00102000`, `_options = 0`, `_rusage = 0` | the same call on the same pid, by the process that has already reaped it |
| 7931-7936 | `xnu_live_wait_done_seq = 2`, **`_error = 0x0000000a`** (`ECHILD`), `_ret = 0`, **`_copy_error = 0`**, **`_status = 0x00000300`**, `_ticks = 0x0e` | the reap is proved by the errno, and the composed status is read back out of the page |
| 7937-8279 | `xnu_live_getpid_count` `0x2 … 0x800000` (23 records, `_last = 1`) | the parent is back in its loop: nothing is left to ask |
| — | `xnu_live_stub_hit_*`, `xnu_live_undef_*`, `xnu_live_osr_*`, `xnu_entry_abort_entries`, `xnu_entry_panic_entered`, `trap record` | **all absent** — no stand-in was reached, no trap was taken, and the epilogue never ran |

The counters at the end: `sleh_seen = 0x22` (34 entries), `sleh_armed = 0x1d` (29), `sleh_redirected =
0x1a` (26), `sleh_back = 5`, and 26 of the 34 are one repeated fault in `Lcopyin_wordwise_loop`
(`_pc = 0x80016254`, `_lr = 0x800aad34` = `telemetry_take_sample + 0x158`) whose `far` is **0** and whose
`fsr` is `0x807` — the kernel's telemetry timer copying 176 bytes from address 0 on a timer, getting
`EFAULT` and rearming, over and over. 507's run has the same three counters within one (33/28/26), so
this is not new here; it is named because the run names it.

## What the run's own order proves, and what had to be proved another way

**The reap happened inside the first call.** The parent's before-record is at 7837, the child's whole
death is at 7872–7890, and the before-record of the second call is at 7924 — so between the two calls
the child died, and a `wait4` on a pid with no zombie can only answer `ECHILD`. The second call answers
`ECHILD`; therefore the first call is what reaped it. No deduction from `_done_seq = 1` is needed, which
is fortunate, because that record does not exist.

**The status in the page is the kernel's, and it was written by the first call's `copyout`.** Three
readings, none of which needs the missing record: the page held `0xfeedface` at 7818; the second call
returns `ECHILD` before any `copyout` (the copy is on the success path, after `reap_child_locked`), so it
cannot have written the page; and the fixture's own `ldr r3, [r9]` / `cmp r3, #EXIT_STATUS` at words
62/63 passed — because the run reached the loop (words 72–76) and took no `udf`, and both of those
checks branch to `entry_failed` when they fail.

**And the kernel-mode abort is this copyout's, identified by address rather than by timing.** `0x80295110`
is `bl 800162ac <copyout>` inside `wait4_nocancel` and `0x80295114` is the instruction after it
(`mov r8, r0`, from the linked image's disassembly), so the frame's `lr` names the call and the faulting
instruction `0x80016360` is the `strb r3, [r1], #1` of `Lcopyout_bytewise`; `_recover = 0x8001644c` is
the symbol `copyio_error`, the shared `mov r0, #14` recovery the copy routines arm in `thread->recover`;
and `_redirect = 0` with a non-zero recovery address is, in this instrument's own words (490), the
signature of a fault the kernel *paged in and retried* rather than converted into an `EFAULT`. The
copyout then returned 0 — which is why `wait4` answered 2 and the page holds `0x300`.

## Two readings this step's own prediction got wrong

### The first call's `_done_seq` record is absent, and the cap is not the reason

The header predicted `xnu_live_wait_done_seq = 1 _error = 0 _ret = 2 _copy_error = 0 _status = 0x300`.
The log has **no `xnu_live_wait_done_seq = 1` at all** — and no `_error`, `_ret`, `_copy_error`,
`_status` or `_ticks` between the two calls either: the six keys the writer emits at
`entry_stubs.c`'s `entry_note_wait_returned` are simply not in the log, while the same six appear for
the second call. Three causes are ruled out by reading rather than by argument:

  - **the writer is not conditional** — `entry_note_wait_returned` writes all six keys on every call;
  - **the record cap was not reached** — `xnu_live_capped` is absent and the run's live channel carries
    4409 records against `ENTRY_LIVE_CAP` = 8192;
  - **the call did not fail to return** — which is the falsifier the prediction named, and this run is
    the case that separates the two: the record's absence and the call's absence have different causes.

What is left is the console's own two-writer exposure, which this image states in the comment above
`ENTRY_OS_BLOCK`: records append at the buffer's size field, every writer puts its characters down
*before* that field covers them, and a record whose window straddles another writer's can be written
over by it — the OS console capture's block reservation is self-healing for the *block*, and nothing is
self-healing for a record. The instrument's other writers were active at that instant: a
`xnu_live_tmr_src0..4` group (the timebase's timer-source dispatcher) is the five lines that sit exactly
where the six would have been (7915-7919). **508 does not claim the mechanism from one sample** — what
it claims is the absence, the three refuted causes, and the two facts that make the step readable
anyway. What it owes is a record that cannot be lost: claiming the size field before filling it, or a
cursor per writer.

### The block was predicted as a possibility, and the reading is stronger than the prediction

The header said the first call "may *block*", and named the wakeup as the reading: the child's records
between the two. Both are measured, and the sleep record adds the two numbers the header did not
predict — `site = wait4_nocancel + 0x19c` and `pri = 0x520`. `_ticks` exists only for the **second**
call (`0x0e` = 14, a zombie found on the first scan), so the first call's duration is the one thing the
run does not measure: the block is proved by the sleep record and by the log's order, and not by a tick
count.

## The image

  - **No kernel change.** `.text` 5287040 -> **5287616** (+576: the wrapper, the two writers and their key
    literals), and the entry image's file is **5503612 bytes** again — the same number 502 through 507's
    six builds had; `__bss_start 0x8053fa80` with 362888 bytes to `0x80598408`; 27 symbols undefined;
    the wrap census is **66 wrapped symbols: 54 reached by a branch, 1 same-object-only, 1 never called
    here, 10 by address only** (`vcputc getpid mmap poll open read fork exit wait4 thread_quantum_expire`).
  - the slot: **`sysent[7] = 0x8047b0f0 = __wrap_wait4`** (`wait4` is at `0x802952f0`, the wrapper is
    `0x8047b0f0`), `sy_arg_munge32 = 0x80284484 = munge_wwww`, `sy_narg = 4`, `sy_arg_bytes = 16`; the
    build's `xnu_entry_508` clause checks that `wait4` is defined by this image rather than stubbed for
    it, and the census's `by_address` list carries the reason it can find no branch.
  - the fixture: **78 words / 312 bytes** at file offset `0xe0` of the RAM disk blob, entered at
    `pc = 0x10e0`; `stage90_fixture.macho` is 1744 bytes, sha256
    `52bc9c357068b791f777bb9abd2e879fa5b0b27364267220b01215dc5faec97b`, and the build's length
    assertion fails if the program is not the 312 bytes the header describes.
  - the three files this step is: `stages/stage90/xnu_arm_boot/entry_ramdisk.s` (the parent's half, the
    prediction and its six falsifiers), `entry_trace.c` + `entry_stubs.c` (the wrapper and the two
    writers), and `tools/host_ramdisk_macho_check.py` + `tools/check_sysent_table.py` +
    `stages/stage90/xnu_arm_boot/build_entry.sh` (78 words, the two new structural clauses, the
    `ARGUMENT_WORDS` clause and the census's `by_address` entry), and this document.
  - the payload, hashed after the run's own artifacts and again after the comment-only edits below:
    `stage90.bin` 5999040 sha256
    `eed2c6b68f7009855fc923ad2bff5bf63b44ec14bb112f58f82be82041057682`, `stage90.img` 6002688 sha256
    `6f6ad93fe11b307fa0d90c8f764197b52f44e715550c198d0d06321539f12b49`, `stage90-qcdt.img` 8523776
    sha256 `537de92b642c2bac1953f03efc7fa416e543132ca4514afcda0bed61f9930fac`;
    `kernel_size=5999040 dt_size=2521088 page_size=2048`.
  - **one address reading worth keeping**: `xnu_live_wait_caller` is `0x80285898` in this run, and it is
    `unix_syscall + 0x100` in *this* build — as are `xnu_live_exit_caller`, `_open_caller` and
    `_fork_caller`. The offset is the reading; the address is not comparable across builds (507's
    `_exit_caller` was also `0x80285898`, 506's was `0x80285848`).

## What is owed

  - **A record that cannot be lost.** The first call's after-record is absent (above). The two candidate
    fixes are named: claim the size field (publish `size + n` before writing the `n` characters, so a
    concurrent writer cannot start inside the record), or give the payload's writer a cursor of its own
    beside the OS block's. Either is a change every reading in this walk depends on, so it wants its own
    step with the pre/post record counts as the reading.
  - **Why the PTE refused a write the same page had already accepted.** Entries 5 and 6 of the same run
    are the fixture's own two accesses to `0x00102000` (`_pc = 0x00001118` = the `ldr`, `_pc =
    0x00001124` = the `str`, both `_user = 1`), and the fixture's own comment says the read maps the page
    read-only and the write faults *again* — so the page's permission life is already a known shape. What
    is not known is why, after the write at word 17 and after the `read`'s own `copyout` succeeded, the
    *same* page refused the write in `wait4`'s `copyout` (`_fsr = 0x0000080f`, a write permission fault).
    The cheap next measurement is a reader of the L2 descriptor for the fixture's page, taken on both
    sides of the copy — the payload already maps the kernel's page tables and this is one word.
  - **The `_done_seq = 1` keys' values, as records.** `_error = 0` and `_ret = 2` are established by the
    fixture's own two checks and by the second call's `ECHILD`; publishing them as records needs the
    first item above.
  - **`wait4`'s block duration.** `_ticks` exists for the fast path only. A tick count around a call that
    blocked needs a record written *after* the wakeup with the counter's value from before it — the pair
    is already there, the arithmetic is not.
  - **The telemetry copy loop.** 26 of the run's 34 aborts are `telemetry_take_sample + 0x158` calling
    `copyin` with `far = 0`; every one is armed and redirected, so the kernel's answer is `EFAULT` and the
    timer rearms. Unchanged from 507 (26 there too), and named here because the counters now say what the
    repeated `pc` was.
  - **From 507**: `p->p_xstat` published by nothing (now measured from the other side: `wait4` is the
    reader that works); the report's empty `xnu_entry_why`; the `trap record:` gate; the watchdog as an
    ending, now measured twice. **From 506**: the report cannot name its own reason; 505's "six quantum
    expiries" against 506's five. **From 505**: the corpse-path slot `0x802933b4`, whose log no longer
    exists. **From 504**: `xnu_live_mdevadd_base` as a page number, an arrival record at `mdevopen`, and
    a second `read` at an offset. **From 503**: which row of `timer_compute_leeway`'s table the `poll`
    thread took, the ~1.8 ms `microuptime`-vs-`mach_absolute_time` difference, and a third ask.
    **From 502**: the frame's `0x038`/`0x03C` pair, intid 39, the `AckC`/EOI question, the registry's
    4-slot capacity stop, claim 16's one-level derivation reader, 497's conditional-clause mutation, the
    unregister guard's asymmetry, `/timer`'s second definition, `MSM8974RootResource`'s `state0 = 0`, the
    release as a reading, `vm_fault`, 488's flag-list, 490's frames band, `xnu_live_dec_same`.

## Safety

Every device touch went through `stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; the boot was a non-persistent `fastboot boot` and
nothing was flashed. **One run.** As in 507 it ended on the hardware watchdog's 25 s timeout and not on a
stop, so the epilogue never ran and there is no report; the device returned to Android on its own and the
capture is 573171 bytes over 8369 lines with `No errors detected`. Two processes were alive when it
ended and **neither is a zombie any more**: pid 2 was reaped by pid 1 inside the first `wait4`, which is
the one thing this run's log says that 507's could not.
