# Experiment 512 — the process parks, and the kernel's own idle path runs

**One line.** The fixture's `getpid` loop becomes `poll(NULL, 0, PARK_MS)`, so pid 1 *waits* instead of
asking, and `machine_idle` — the call `processor_idle` makes when every run queue is empty — is wrapped,
so the run can see the OS in the one state no earlier run could reach: nothing to do. The OS's own
console block gains one line, and it is the first line in this project that is the kernel *idling*
rather than the fixture asking:

    load_init_program: attempting to load /sbin/launchd
    mini4: the OS starts the process at 0x10e0 (the thread's user pc was 0x10e0)
    mini4: the OS's own init load returned, so pid 1 has the init image (caller 0x80048eb8)
    mini4: the AST is done -- pid 1's thread is at 0x10e0 for user mode (sp 0x101efc)
    mini4: the OS has nothing to run -- pid 1 parked in poll for 2000 ms (caller 0x80285898), and the
    kernel's own idle path was entered 3325332 time(s) while it did (ticks 0x24b80d2)

`machine_idle` is called from exactly one place (`processor_idle`, `osfmk/kern/sched_prim.c:4532`) and
only when the processor has decided to stay idle, so an `xnu_live_idle_*` record is not a report about
the instrument — it is the kernel saying it had nothing to run. **The run answers with 33,554,432 of
them in 21 s, once every 12.0 ticks (0.625 µs), all on `processor_idle+0x110`, all on the idle thread
(`_pid` 0), with the live CPSR's low byte `0x93` — SVC mode with I masked, the state `machine_idle`'s own
`cpsid if` anticipates.** It also answers a question this step did not ask and could not have asked
before: **that idle is a spin.** The decrementer's write census, which 483 built and which is the only
way the kernel can reach the countdown, reads **at most 127 writes for the whole boot** — so the `wfi` at
the bottom of `cpu_idle` was reached at most 127 times against 33,554,432 passes of the path above it.
The OS runs, its deadlines fire, and it never stops the CPU.

    bash stages/stage90/xnu_arm_boot/build_entry.sh          # prints xnu_entry_512's clause
    python3 tools/check_saved_state_offsets.py --selftest    # all 21 mutations were refused
    python3 tools/check_saved_state_offsets.py               # ok: ACT_PCBDATA/SS_PC/SS_SP/SS_CPSR
    python3 tools/host_ramdisk_macho_check.py --selftest out/stage90/xnu_arm_entry.elf  # all 129 refused
    python3 tools/check_sysent_table.py --elf out/stage90/xnu_arm_entry.elf
    python3 tools/check_pthread_table_slots.py --selftest    # all 7 refused
    python3 tools/check_timer_sources.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 48 refused
    python3 tools/check_os_entry.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 30 refused
    python3 tools/check_fault_recovery.py --image out/stage90/xnu_arm_entry.elf --selftest  # all 31 refused
    python3 tools/check_driver_catalogue.py --selftest       # all 131 refused
    python3 tools/check_experiment_index.py                  # 489 rows

## What the step was for

Every run from 506 to 511 ended the same way — on the hardware watchdog's 25 s timeout — and 511's log,
read to its end, says why: `xnu_live_getpid_count` was still climbing — its last published record was
`0x00800000` — when the watchdog fired, the OS's own service threads were all parked, and the only process in the system was this fixture,
busy in a loop that never blocks. **The OS was healthy, had nothing to do, and the one thing it could not
be measured doing is the thing an operating system does most of its life: idling.** So the change is to
one block — the `getpid` loop becomes a park — and the reading is `machine_idle`'s own entry.

The park is `poll(NULL, 0, PARK_MS)`: no descriptor, so the only thing that can wake it is the timeout,
which makes it a *pure deadline* and puts the kernel's own timer path on the fixture's critical path for
the first time. It is also the kernel's own idiom for this — `bsd/kern/sys_generic.c:1788` calls exactly
this call "an extremely inefficient sleep". The fixture grows from 78 words (312 bytes) to **79 words
(316 bytes)**, and the answer is deliberately not tested: `poll`'s return value is never loaded, because
a `udf` there would kill `initproc` and take the boot with it.

Two things this step is *not*: it is not the first run with an idle path to read (the instrument is new,
which is why 511's log has no `xnu_live_idle_*` record at all — the kernel may well have idled during
511's two short asks, and nothing was watching), and it is not a claim that the kernel *should* idle
here. It is the difference between a machine running a program and a machine with nothing to run.

## What the build checks

Four properties, in the shape 455 and 510 established, and the design choice they protect is the same:
this wrapper must not be able to damage the path it measures.

  - **membership** — `machine_idle` is not in pass 1's undefined set, so it is XNU's own function and
    `__real_machine_idle` is not a stand-in. The clause reads: *machine_idle (0x80016224) is the kernel's
    own (defined in osfmk/arm/machine_routines_asm.s, not in pass 1's undefined set)*.
  - **the caller is a different object, and the branch reaches the wrapper** — `bl <__wrap_machine_idle>`
    (0x8047afc4) is inside `processor_idle` (0x800b68d4..0x800b6d8c), and the clause requires *all*
    branches to the wrapper to be inside it and none elsewhere. Measured: **all 1 branch(es)** are, which
    is the structural answer to 455's hazard — a wrapper whose callers share its object links, defines its
    symbol and never runs. `machine_idle` is in `osfmk/arm/machine_routines_asm.s`; `processor_idle` is in
    `osfmk/kern/sched_prim.c`. Two objects.
  - **every reference to it in the pool is a call** — the relocation records of all 708 objects must
    contain nothing but `R_ARM_CALL` for this symbol. This is what keeps the wrap safe for a function on
    the scheduler's idle path: an `R_ARM_ABS32`/`MOVW`/`MOVT` would be an address taken, `--wrap` rewrites
    those too, and a wrapper's address could then end up somewhere it is jumped to and never returns from.
  - **the reader has no pipe in it** — 511's own defect, and this clause reuses its fix: the `objdump`
    output is captured into a variable and the status is `grep`'s, not the pipeline's. Under
    `set -o pipefail` a `| grep -q` reports a successful match as exit 141 whenever the left side dies of
    SIGPIPE.

And the fixture's own three, which are the part that makes the *park* a property rather than five
pinned instructions:

  - **the loop closes on its own first word** — the `b` at word 77 must target word 72 (`PARK_WORD`),
    not the ask word and not anything else, so every turn reloads all three arguments (`poll`'s return
    writes `r0`, and a loop that kept it would ask for a body-count-sized timeout on turn two).
  - **the timeout is not zero** — `poll` with a zero timeout returns without arming a wait timer, i.e. a
    spin with the same shape as the park; the clause says so in words, because the failure mode is an
    image that looks like this step and is 511 again.
  - **the threshold relation, read out of the C file** — the console line prints only when a timeout is at
    least `ENTRY_PARK_MIN_MS` (1000), and the two asks (5 ms, 40 ms) must both be *below* it while the
    park is not. `park_threshold()` parses the `#define` out of `entry_trace.c` and exits if it is gone,
    so the fixture and the wrapper cannot drift apart: an ask at or above the threshold would make the
    durable artifact claim the OS had nothing to run when it was only waiting on a deadline the program
    asked for.

## The readings

Line numbers are `/tmp/cancro-last_kmsg.txt` (569602 bytes, 8278 lines, 3930 of them the payload's own,
this run), and the log's own end is `No errors detected`.

| line | record | what it says |
|---|---|---|
| 3995-3997 | the three lines 509, 510 and 511 added, byte for byte | the exec, its return, and the AST, unchanged by this step |
| **3998** | **`mini4: the OS has nothing to run -- pid 1 parked in poll for 2000 ms (caller 0x80285898), and the kernel's own idle path was entered 3325332 time(s) while it did (ticks 0x24b80d2)`** | **this step: the process parked, and the kernel entered its own idle path while it did** |
| 7562-7695 | `xnu_live_ast_seq = 1`, `_thread = 0xc05afa40`, `_before = _after = _sp = 0`, `_cpsr = 0x10`, `_pid = 0` | 511's first AST, unchanged: the bootstrap thread, zeroed state, `kernproc` |
| 7587-7690 | `xnu_live_exec_seq = 1`, `_caller = 0x80048eb8`, `_who = 1`, `_proc = 0xc0687b80`; `_done_*` ending `_ast_done_seq = 1` | **the exec is inside the first AST's window, by the instrument's own order** (see below) |
| 7700-7707 | `_ast_seq = 2`, `_thread = 0xc05ea760`, `_before = _after = 0x000010e0`, `_sp = 0x00101efc`, `_pid = 1` | 511's second AST, unchanged: the thread the entry point was written to |
| 7757, 7772, 8034 | `xnu_live_sleh_seq = 5`, `_user = 1`, `_pc = 0x1118`, `_fsr = 0x07`; `_seq = 6`, `_pc = 0x1124`, `_fsr = 0x080f`; `_seq = 7`, `_type = 3` (prefetch), `_pc = 0x11a4`, `_thr = 0xc05eb480` | the fixture's own two page faults and the forked child's prefetch abort, unchanged in kind |
| 7900-7905 | `xnu_live_poll_seq = 1`, `_timeout_ms = 5`, `_error = 0`, `_retval = 0`, `_ticks = 0x000582c2` | 503's first ask |
| 7938-7947 | `_seq = 2`, `_timeout_ms = 40`, `_ticks = 0x00102e1c` | 503's second ask |
| **8174-8183** | **`_seq = 3`, `_fd = _nfds = 0`, `_timeout_ms = 0x7d0`, `_error = _retval = 0`, `_ticks = 0x024b80d2`** | **the park's first turn: 2000 ms asked, 38,502,610 ticks** |
| **8198-8207** | **`_seq = 4`, the same five fields, `_ticks = 0x024b84d0`** | **and its second turn** |
| 8238-8270 | `xnu_live_poll_over = 1, 2, 4, 8` | eight further park turns after the fourth record — twelve calls in all, ten of them parks, ~20 s |
| **7775-8278** | **26 `xnu_live_idle_*` records, at powers of two: `_seq` 1 … `0x02000000`** | **the kernel's own idle path, censused** |
| 8247, 8248, 8265 | `xnu_live_dec_count = 0x40`, `_setpop_count = 0x40`, `_irq_timer_count = 0x10` | 483/484's instruments: the decrementer, the deadline machinery and the timer interrupt, censused |
| — | `xnu_live_stub_hit_*`, `xnu_live_undef_*`, `xnu_live_osr_*`, `xnu_entry_abort_entries`, `xnu_entry_panic_entered`, `trap record`, no report, and no `pid 1 exited` | **all absent** |

**The idle records, in full.** Five keys are published together, 26 times:

| field | value, in all 26 records |
|---|---|
| `_seq` | `0x00000001, 2, 4, 8, … 0x02000000` — the wrapper's own call count, published at powers of two |
| `_caller` | **`0x800b69e8`** = `processor_idle+0x114`; the `bl` is at `0x800b69e4` = `processor_idle+0x110`, and `processor_idle` is `0x800b68d4..0x800b6d8c` |
| `_thread` | **`0xc05b1480`** — the idle thread, the same pointer in every record |
| `_pid` | **`0`** — `current_proc()` on it is `kernproc`, which is what the idle thread belongs to |
| `_cpsr` | **`0x60000093`** — read live with `mrs`, so the low byte is the mode `machine_idle` was called in: `0x93` = SVC (`0x13`) with **I masked** and F clear, which is what `processor_idle` leaves and what `machine_idle`'s own `cpsid if` anticipates. The upper nibble is the flags at that instant and means nothing |
| `_now` | `0x06ced6b1` first, `0x1ecfe458` last: **402,722,215 ticks for 2^25 − 1 entries — 12.00 ticks each, 0.625 µs at 19.2 MHz, 1.6 million entries per second** |

The first record is the interesting one for what this step changed: `_now = 0x06ced6b1` is **131 µs after**
`xnu_live_poll_before` of the fixture's first ask (`0x06ceccdb`). The first time this machine ever idled
in a log is inside the fixture's first `poll`.

**The four asks, at the 19.2 MHz timebase the port registers** (the rate is itself a reading: the 2000 ms
park measures 38,502,610 ticks, +0.27%, so the timebase the OS is running on is the one the port armed):

| seq | asked | measured ticks | measured time | difference |
|---|---|---|---|---|
| 1 | 5 ms | 361,154 | 18.810 ms | **+13.81 ms** |
| 2 | 40 ms | 1,060,380 | 55.228 ms | **+15.23 ms** |
| 3 | 2000 ms | 38,502,610 | 2005.344 ms | +5.34 ms |
| 4 | 2000 ms | 38,503,632 | 2005.398 ms | +5.40 ms |

**The ordering evidence, stated because 511's route claim rested on an ordering that this log does not
actually provide.** The live records are appended to the ram console in event order, and they can be
checked against each other: `_ast_seq = 1` (byte 546006) < `_exec_seq = 1` (546809) < `_exec_done_seq = 1`
(550043) < `_ast_done_seq = 1` (550152) — so **the exec runs inside the first AST's window**, which is 511's
conclusion on evidence 511 did not have, all four records coming from one instrument and one append-only
buffer. The second ordering check is independent: `_sleh_seq = 5` (551373) < `_idle_seq = 1` (552848) <
`_poll_seq = 3` (566146) < `_idle_seq = 0x02000000` (569396), which is the order their own values require.
**But the OS's own console text is not in that stream**: it is captured into a reserved 128 KB block
(`xnu_live_ostext_block = 0x00020000`, `_heals = 1`, opened by the payload's own `[os-console-459]`
marker) that sits *before* the live records: the console block's last nine lines are at bytes 296982..297691,
and the live records that describe the same two seconds start 250 KB later in the buffer. A line's position
relative to that block says nothing about when it was printed; 511's reading of the console block was a
reading of the *printing* order, which is all the block can carry.

## The reading the step was not looking for: this idle does not sleep

`machine_idle` was wrapped to answer "does the kernel ever have nothing to run". The census answers a
second question as well, and the answer is in the instruments that were already there:

  - **`cpu_idle` has exactly one `wfi` on this path and a loop that is not its own.** `machine_idle`
    (`osfmk/arm/machine_routines_asm.s:54`) does `cpsid if` / `bl Idle_context` / `cpsie if`. `Idle_context`
    saves the thread and branches to `cpu_idle` (`osfmk/arm/cpu.c:118`), whose body is: **two early
    returns** — `if ((!idle_enable) || (cpu_data_ptr->cpu_signal & SIGPdisabled)) Idle_load_context();` and
    `if (!SetIdlePop()) Idle_load_context();` — then the timer/idle-notify work, `cpu_idle_wfi(wfi_fast)`
    (one `wfi`, `machine_routines_asm.s:69`), `ClearIdlePop`, `cpu_idle_exit`, and `Idle_load_context()`
    (which restores the PCB and `bx lr`s back into `machine_idle`). There is no loop in `cpu_idle`; the
    loop is `processor_idle`'s, and it calls `machine_idle` again whenever it still has nothing to run.
    So **33,554,432 `machine_idle` entries are 33,554,432 trips round that whole path**, and 12.0 ticks —
    0.625 µs — is what a trip costs.
  - **The decrementer says the `wfi` was not reached.** 483's instrument reads `ml_set_decrementer`, and on
    this configuration that is the only way the kernel can program the countdown: arm32's
    `ml_set_decrementer` (`machine_routines_asm.s:1025`) loads `cpu_data->cpu_set_decrementer_func` and
    `bxne`s it — set by `cpu.c:461` to the timebase registration — and *returns without touching the
    hardware* if it is null. Every decrementer write the kernel makes therefore passes through 483's
    counter, and the counter reads **at most 127 for the whole boot** (`xnu_live_dec_count` published at
    2, 4, 8, 16, 32, 64 and never at 128). `SetIdlePop` writes the decrementer on its `TRUE` path and not
    on its `FALSE` path, so the `wfi` at the bottom of `cpu_idle` was reached at most ~127 times: **one
    pass in 264,000 gets past the early returns.** `xnu_live_setpop_count` (≤127) and
    `xnu_live_irq_timer_count` (16..31) agree: the timer interrupts happen, the deadlines fire, and the
    idle path runs 1.6 million times a second *between* them.
  - **What that costs and what it does not.** It does not cost the run: the fixture's parks return, the
    timebase is right to 0.27%, and the child, the reap and the `SIGCHLD` all still happen. It costs
    power and, at 5 ms and 40 ms, accuracy — both short asks come back 14-15 ms late (table above), which
    is the size of a wake that goes through the idle path rather than a countdown that is wrong. **And it
    is not this step's subject**: which of the three early exits is taken is a question with an exact
    answer and three candidate readings, and 513 is where it is asked (see below).

**And the first AST's thread, which 511 left unnamed, is named by an instrument that was already
running.** 511's owed list carried "`_thread = 0xc0485d20` has not been identified beyond `_pid = 0`".
This run's first AST is on `0xc05afa40` and the same pointer appears in `xnu_live_sleep_*` as a thread
that parked with `_site = 0x8040c25c` = `ux_handler_init+0x70` and `_wmsg = 0x804eddd8`. What is at that
address in this image is the string **`ux_handler_wait`**, and the only maker of it is
`bsd/uxkern/ux_exception.c:200` — `msleep(&ux_exception_port, proc_list_mlock, 0, "ux_handler_wait", 0)`
**inside `ux_handler_init`**, which `bsdinit_task` calls (`bsd/kern/bsd_init.c:1059`) on its way to
`load_init_program(p)` at :1079. So the thread that carries the first AST, with its zeroed saved state and
`kernproc` as its process, is **the thread running `bsdinit_task`** — the thread the exec is performed by,
a fact 511 could only state as "a `kernproc` thread with no user-mode history". The pointer itself is
per-boot (511's was `0xc0485d20`); the identification is the wait message, and this run has both halves on
the same pointer.

## Two readings this step's own prediction got wrong

### The ratio the two asks are supposed to produce is not the ratio the device produces

`tools/host_ramdisk_macho_check.py` carried, from 503 until now, the sentence "their property is an 8:1
ratio and not a value" about the words 5 ms and 40 ms — the words *are* 8:1, and **no run has ever
produced it**. What the wrapper publishes is the kernel's tick count across the call, which is the
deadline **plus** the wake, so the ratio is diluted by a per-wake cost that does not scale with the ask:
511's run measured 160755 : 898180 (5.6:1) and this one 361154 : 1060380 (2.9:1). The prediction in the
fixture's own header — `_poll_seq = 3 then 4 … _ticks of about 2000 ms` — is right; the *ratio* clause
was a claim about a difference made from an instrument that measures a sum, which is 503's defect class
arriving inside a comment rather than inside a check. The clause that actually runs was already the safe
one (`long_ms > short_ms`), and both comments now say what the two runs measured, with the ordering as
the property and the ratio as a reading of the run.

### `_cpsr_live = 0x13` was a mode written as a word

The fixture's header predicts `_cpsr_live = 0x13`, and the record publishes `0x60000093`. Both are about
the same thing — the low byte ends in `0x13`, SVC mode — but the key is a live `mrs`, so it carries the
flags of the instant as well, and a reader comparing against `0x13` would call a correct record wrong.
This is 511's `_cpsr = 0x10` note with the sign flipped: there the key never varied, here it varies in a
field nobody is reading. The honest form is the one used above: the *mode* is the reading, the word is
reported.

## The image

  - **One flag, one wrapper, one writer, one print, and a four-byte change to the fixture.** `.text`
    5,288,480 (510) → 5,289,056 (511) → **5,289,568** (+512), the entry image's file is **5503612 bytes**
    again, `.bss` is `__bss_start 0x8053fa80` with **362896 bytes to 0x80598410** — **the same extent 511
    had**, even though this step adds two words of `.bss` (`g_idle_calls` at `0x80541c48`, and the poll
    wrapper's `park_printed.2` at `0x805983f0`): both land inside the section's existing tail, which is the
    linker-fill term's cousin — the printed extent is a section boundary, not a sum of the objects that
    live in it. **27 symbols undefined**, and the wrap census is **70 wrapped symbols: 58 reached by a
    branch, 1 same-object-only** (`_ZN9IOService12matchPassiveEP12OSDictionaryj`), **1 never called here**
    (`sleep`), **10 by address only** (`vcputc getpid mmap poll open read fork exit wait4
    thread_quantum_expire`) — 511's census with `machine_idle` added to the first category, which is the
    census's own confirmation of the call-site clause.
  - the wrapper is at **`0x8047afc4`** (`__wrap_machine_idle`), delivered from `processor_idle+0x110`
    (`0x800b69e4`); `machine_idle` is at `0x80016224`, `processor_idle` is `0x800b68d4..0x800b6d8c`, the
    writer `entry_note_idle` is at `0x80006b84` and `g_idle_calls` at `0x80541c48`.
  - the six files this step is: `stages/stage90/xnu_arm_boot/entry_ramdisk.s` (the park: the `b` at word
    71 re-pointed from the old `getpid` loop to `park`, the six words at 72..77 rewritten as
    `mov r0, #0` / `mov r1, #0` / `movw r2, #PARK_MS` / `mov r12, #SYS_POLL` / `svc #0x80` / `b park`,
    the program's one added word putting `udf #1` at 78 instead of 77, the length guard at 316, and the
    prediction and falsifiers the header now carries), `stages/stage90/xnu_arm_boot/entry_trace.c` (the
    wrapper,
    `ENTRY_PARK_MIN_MS`, and the console line), `stages/stage90/xnu_arm_boot/entry_stubs.c`
    (`entry_note_idle`, the powers-of-two publishing), `stages/stage90/xnu_arm_boot/build_entry.sh` (the
    flag, the clause, the wrap count 69 → 70), `tools/host_ramdisk_macho_check.py` (`PROGRAM_WORDS` 79,
    the five park words, `park_timeout`/`park_threshold`, the threshold clause, and the ratio correction
    above), and this document. No header change: `SS_PC`, `SS_SP`, `SS_CPSR`, `ACT_PCBDATA` and
    `CPU_DECREMENTER`/`SET_DECREMENTER_FUNC` are all already checked.
  - **A correction to 511's image row.** 511's document says the entry image's `.text` went "5288480 →
    **5288960** (+480)". Its own build log (`/tmp/511-run2-build.log:863`) says `text size 5289056 bytes
    (.text)`, and 510's log says 5288480 — so the true pair is 5,288,480 → **5,289,056, +576**, and the
    after-value and the delta in that row are both 96 low. The row is corrected in this commit; the
    before-value was a reading and the after-value was not.
  - the payload, from the build that ran: `stage90.bin` 5999040 sha256
    `bf9f42d149add5a9b000eb75eb3e1c2a7e2a7948b5dc5be866e04583e7a93298`, `stage90.img` 6002688 sha256
    `110471d1528af23b1063751332ee312eb8d995959ef5ec8097aaf2da34deb2aa`, `stage90-qcdt.img` 8523776 sha256
    `efad670f7c2916e3c734ed9c68d8d893637f9cdc4c105c686f10306de716da1a` — all three different from 511's,
    and the first two *the same size*, which is why a size is never the check.
  - **the capture script had the mirror image of 511's failure, and it is fixed.** 511 could not reopen a
    log owned by the invoking user; 512 could not *remove* one owned by root — `rm: cannot remove
    '/tmp/cancro-last_kmsg.txt': Operation not permitted`, exit 1, after the boot had already been spent.
    Both are the same sticky-`/tmp` rule with the identities swapped, so the removal is now tried as both:
    `rm -f "$LOGFILE" 2>/dev/null || sudo rm -f "$LOGFILE" || die …`, verified against a root-owned file, a
    user-owned file and a missing one. **This run's log was recovered by hand** (`sudo rm -f` plus the
    redirect, while `/proc/last_kmsg` still held the payload's boot) and its readings are what this
    document quotes.

## What is owed

  - **513's object, and it is one question with three candidate answers.** `cpu_idle` returns before its
    `wfi` on at least 264,000 of every 264,001 passes, and the three ways that can happen are all readable
    from a wrapper that does no more than 512's does:
    - `idle_enable` — `osfmk/arm/cpu_common.c:67` initialises it **FALSE** and only
      `cpu_machine_idle_init(TRUE)` (`cpu.c:533`, called from `arm_init.c:376`, which is on the path the
      boot demonstrably took because the console's own lines come from below it) sets it TRUE, and only
      when the boot arguments carry no `jtag`.
    - `cpu_data->cpu_signal & SIGPdisabled` — set to `SIGPdisabled` unconditionally by `cpu_init`
      (`cpu.c:365`) and cleared only by `cpu_signal_handler_internal(FALSE)`, whose only caller is
      `cpu_signal_handler`, whose only caller is the platform's IPI handler slot
      (`machine_routines.c:605`). On a machine that takes no interprocessor interrupt, this stays set.
    - `SetIdlePop()` — returns FALSE when `cpu_data->rtcPop < mach_absolute_time()` or the remainder is
      inside `cpu_idle_latency` (`rtclock.c:365`), and `rtcPop` is only ever set to a *future* time by
      `setPop` — which `timer_resync_deadlines` (`arm_timer.c`) calls only when a deadline is queued or
      is `EndOfAllTime`, so a machine whose last deadline has expired can leave `rtcPop` in the past
      indefinitely.
    Five readings settle it — `idle_enable`, `cpu_signal`, `rtcPop - now`, `cpu_idle_latency`, and
    `SetIdlePop`'s return — and each is a word in `struct cpu_data` or a return the wrapper can see. The
    companion reading is 483/484's own: `xnu_live_timerdrv_arm_ms = 10` with `_fire_lat_ns ≈ 101 ms` and
    `_gap_ns ≈ 101 ms`, i.e. the OS's own timer driver asks for 10 ms and is called back ~101 ms later,
    which is that instrument's finding and not this step's.
  - **The idle path's cost is now measured, and its upper bound is the instrument's own.** 12.0 ticks per
    pass includes the wrapper's `current_proc()`/`proc_pid()` calls and `entry_note_idle`, so it is a bound
    on `machine_idle`+`Idle_context`+`cpu_idle` and not a measurement of them; a run whose wrapper did
    nothing would be the only way to separate them.
  - **511's carried items, still open**: the telemetry copy loop on pid 1's own thread (`_pc =
    0x80016574` = `Lcopyin_wordwise_loop`, `_lr = 0x800aad34` = `telemetry_take_sample + 0x158`, `_seen =
    0x22`, `_redirected = 0x1a`); `_cpsr` = `0x10` on both AST records, which has never distinguished
    anything; 510's `_entry_hi` never non-zero; `_before`'s other source (a wrapper on
    `thread_setstatus`); 508's a-record-that-cannot-be-lost and the L2 descriptor around a `copyout`;
    507/506's `p->p_xstat`, the empty `xnu_entry_why`, the `trap record:` gate; the watchdog as an ending;
    505's corpse-path slot `0x802933b4`; 504's `mdevadd_base`/`mdevopen`/second `read`; 503's leeway row
    and third ask; 502's long list.
  - **And the captured console's own census**, new here: `xnu_live_ostext_chars` and `_total` both read
    1006 and `_block` reads 0x20000, while the block the payload emits holds **1740 bytes** — the marker's
    own `\n[os-console-459]\n` and then **1722 characters** of the OS's text, from `Darwin Kernel Version`
    through this step's park line to the space-fill that ends it. The count stops at 1006, which is about
    where `BSD root: md0` begins, so the census is of one of the two sinks the OS's text can arrive by and
    the block holds both. Which is which is one step's reading rather than a guess.
  - **The driver clause, stated where it now stands.** The OS's own data paths that this project has
    exercised end to end are the console, the device tree, the GIC (a line enabled, an interrupt taken,
    acked and serviced, `ISR` counted) and the timer as *delivered* — the OS arms a deadline, the
    interrupt arrives, `poll` returns. What this step adds is that the timer's *deadline machinery* is not
    idle-safe on this machine, which is the first time a driver's failure to do its job shows up as a CPU
    that never sleeps rather than as a boot that stops. 513 is where that is read.

## Safety

Every device touch went through `stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; **one run**, non-persistent `fastboot boot`,
nothing flashed. The device returned to Android on its own and the run ended on the hardware watchdog's
25 s timeout, as 507's through 511's did, with `No errors detected` and no stop of any kind. The
script exited 1 *after* the boot — on the root-owned log above, not on the boot — and the log was
recovered by hand while `/proc/last_kmsg` still held it; that failure is fixed in the same commit, and
the fix is a `rm` tried as both identities rather than one.
