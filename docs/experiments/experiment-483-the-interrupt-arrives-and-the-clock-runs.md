# Experiment 483 — the interrupt arrives, and the kernel's clock starts running

Date: 2026-09-20
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `stages/stage90/xnu_arm_boot/entry_irq.c` (new), `entry_gic.{h,c}`, `entry_timebase.c`,
`entry_vectors.s`, `build_entry.sh`, `tools/check_irq_routing.py` (new), `tools/check_gic_routing.py`

**Result: the virtual timer's interrupt is delivered, dispatched through Apple's own entry, and
serviced by a handler this image installed — and the kernel's clock now runs on it.**
`xnu_live_irq_first_iar = 0x00000014` is the line 482 measured, arriving on hardware;
`xnu_live_irq_handler_entered = 1` with `target = 0x8053b2cc` (`g_irq_target`), `nub = 0x8050a000`
(`BootCpuData`) and `refCon = source = 0` says the four arguments are the four `fleh_irq_handler`
loaded; `xnu_live_irq_timer_count` passes 2048 serviced interrupts; `xnu_live_irq_late_count = 0` over
every sampled call says the acknowledgement always returned the line to the distributor; and
**`xnu_live_setpop_count` goes from 16 to 4096** — `rtclock_intr` → `setPop` →
`ml_set_decrementer` is now being driven by the interrupt rather than by the next call site.

The boot is unperturbed and the device came back on its own, so the never-brick half holds for the
first time on a build that takes interrupts. **The minimum bar is still not met**: the OS console is
byte-identical to 482's (27 lines, 131102 bytes by this run's own extractor, ending at
`load_init_program: attempting to load /sbin/launchd`), the block census moves by exactly two entries
(70 → 72 entered, 13 returns unchanged), and the two new entries are `thread_sched_call` and
`Call_continuation` — the scheduler spine, entered once more, with no new OS output. So what this step
proves is the *mechanism* the parked threads were waiting for, not their release.

## What 482 left, and why the slot could not move first

482 measured two facts that together made this step's order forced:

  - **The line is INTID 20.** Under a countdown it had driven itself, with a masked control, the
    pending bit that appeared at the distributor was `0x00100000`, and neither of the two PPIs the
    payload's device tree names (18, 19) produced anything. 19 is the *physical* timer's line, which
    the payload's own IRQ path acknowledged as `IAR = 0x13` earlier in the same boot.
  - **Apple's dispatch branches through a word nothing had written.** `fleh_irq_handler` at
    0x80016abc (0x80015abc in 482's image) loads `cpu_data`+192, +196, +184, +188, +180 into
    r0..r3 and r5 and `blx r5`. `INTERRUPT_HANDLER` (180) is written by exactly one function in the
    tree — `ml_install_interrupt_handler` (`osfmk/arm/machine_routines.c:396`) — and 482's image had
    never called it. Pointing slot 6 at `locore_fleh_irq` before that word exists is a branch to
    address 0 inside an exception entry.

So the order is not a style: **install, read back, configure the line, enable, and only then let the
countdown reach the CPU interface.** This step's own first run (`STAGE90_IRQ_ENABLE_LINE 0`) is the
measurement of everything before the enable; its second is the enable and nothing else.

## The chain, and the one switch

`entry_irq_arm()` runs at the end of the **first** `ml_set_decrementer` this image makes, after 481's
countdown sample, after 482's probe, after the kernel's own first deadline has been written to
`CNTV_TVAL`, and after `g_dec_writes++`. It:

  1. **reads the interrupt stack pointer as a precondition.** `fleh_irq_kernel` ends by loading `sp`
     from `[cpu_data, CPU_ISTACKPTR]` (`locore.s:1377`) and `fleh_irq_user` does the same through the
     CPU data (`:1304`), so a zero there is a load of address 0 in the middle of the exception entry.
     Measured `xnu_live_irq_istackptr = 0x80508000`, non-zero, and the run records the value either
     way, so "the machine was not ready" and "the step did not run" are different lines in the log.
  2. **records the state of the exception it runs in.** `xnu_live_irq_cpsr = 0x60000093` — `I` set,
     `F` clear. So the whole arming runs with IRQs masked by the CPSR, and an interrupt cannot be
     delivered into the middle of it whether or not anything is enabled yet. That is a measurement
     this step did not have to arrange and did not know in advance.
  3. **reads the five `cpu_data` interrupt words, installs, and reads them back.** The before-state is
     the step's premise measured on hardware: `handler_before = target_before = nub_before =
     source_before = 0`. The install is Apple's own `ml_install_interrupt_handler` and not five stores
     here, because a second writer is a second definition. The after-state gates the rest:
     `handler_after = 0x800085b0` (`entry_irq_handler`), `target_after = 0x8053b2cc` (`g_irq_target`),
     `nub_after = 0x8050a000` (`BootCpuData`), `refcon_after = source_after = 0`, and
     `install_ok = 1` only because the handler and the target both read back as the ones this file
     named. A disagreement returns 0 and the line is never enabled.
  4. **reads `GICD_ICFGR` for the line** — `icfgr_word = 0xc04`, `icfgr_shift = 8`, both derived from
     the intid (`GICD_ICFGR<n>` at `0xc00 + 4n` covers INTIDs `16n..16n+15`, so INTID 20 is in
     `ICFGR1`; the field is 2 bits at `(20 % 16) * 2`). It is *read* and not written, and the reading
     is the interesting part: **`xnu_live_irq_icfgr = 0xffffffff`**. No valid GICv2 configuration
     field is `0b11` — the architecture defines `0b00` as edge and `0b10` as level and leaves the
     other two reserved — so an all-ones word is not a set of configurations a writer chose: on this
     SoC the PPI bank's `ICFGR` bits read as ones because they are not implemented as storage. That is
     worth recording as a **negative result**: 482 inferred edge behaviour from a pending bit that did
     not follow the line back down, and this register is *not* the one that would settle that question
     here.
  5. **reads priority and targets** — `priority_word = 0xa0a0a0a0`, `targets_word = 0x01010101`, both
     byte-per-intid words indexed by the intid rounded down to a multiple of four.
  6. **clears the line's pending bit** and reads the register back: `pend_cleared = 0x20400000`, the
     same value 482's probe restored, so nothing the probe or the payload's timer left behind can be
     delivered as though it were this step's tick.
  7. **and, only in a build whose source says so, enables the line** —
     `gicd_write(STAGE90_GICD_ISENABLER0, 1 << 20)`, read back into
     `xnu_live_irq_line_enabled`. `GICC_CTLR` (1) and `GICC_PMR` (0xf0) are read and *not* written:
     the payload's own IRQ path already took an interrupt in this same boot with those two at exactly
     these values, so the one bit this step changes in the system is at the distributor.

`STAGE90_IRQ_ENABLE_LINE` is a `#define` in `entry_irq.c` and not a `-D` on either compile line,
because two checks read it (`tools/check_irq_routing.py` for the `#if` that contains the enable,
`tools/check_gic_routing.py` for the slot-6 claim) and a value the image and the source could disagree
about would make both of them reports rather than gates. It is **1** in the committed source, so a
plain build of this tree is the configuration that was measured taking interrupts.

## The handler, and the three cases

`entry_irq_handler(void *target, void *refCon, void *nub, int source)` is the function
`fleh_irq_handler`'s `blx r5` reaches. It reads `GICC_IAR`, masks with `0x3ff`, and splits three ways:

  - **INTID 20**: `GICC_EOIR` with the value it read, then `rtclock_intr(0)`. The EOI is deliberately
    *before* the service: `rtclock_intr` re-arms the countdown through `setPop` →
    `ml_set_decrementer`, so acknowledging first means a second expiry during that work is a line
    already back in the distributor's hands. The EOI itself is this project's decision and not
    Apple's — Apple's armv7 acknowledges from the FIQ bank out of `rtclock_timebase_addr`/`_val`
    (`fleh_fiq_s7002`'s `str r11, [r10, #PMGR_INTERVAL_TMR_CTL_OFFSET]`), which on this SoC is a
    private timer block, and there is no `GICC_EOIR` anywhere in Apple's armv7 tree. MSM8974's timers
    are on the GIC, so the shape is taken from the payload's own IRQ path instead
    (`stages/stage90/gic.c:246`, `:293`).
  - **0x3ff**: the spurious read. Counted and returned from, with **no** `EOIR`, because the
    architecture says a spurious read acknowledges nothing.
  - **anything else**: acknowledged, counted, and `entry_epilogue("exception: irq line")`. An
    unhandled line that returns is an interrupt that arrives again immediately — a hang with no
    message, which is what 308's run was.

**The third case never ran.** No `xnu_live_irq_other_count`, no `xnu_live_irq_other_iar`, no
`xnu_live_irq_spurious_count`, and no stop, in either run B. Nothing else on this machine asserted an
enabled line at the interface for the whole of the run.

## The measurement: three runs, two states

Run A is `STAGE90_IRQ_ENABLE_LINE 0`; runs B and C are `1` (B is the first build with the switch on, C
is the same source rebuilt after the defect below — `g_irq_late_count` was collected and never
published). All three go through `preflight_boot_check.sh --allow-xnu-entry` and
`run_and_capture.sh --allow-xnu-entry`, all three bring the device back with `No errors detected`.

| key | A (enable 0) | B / C (enable 1) |
| --- | --- | --- |
| `xnu_live_irq_enable_line_compiled` | `0` | `1` |
| `xnu_live_irq_istackptr` | `0x80508000` | `0x80508000` |
| `xnu_live_irq_cpsr` | `0x60000093` | `0x60000093` |
| `xnu_live_irq_handler_before` | `0` | `0` |
| `xnu_live_irq_handler_after` | `0x800085b0` | `0x800085b0` |
| `xnu_live_irq_target_after` | `0x8053b2cc` | `0x8053b2cc` |
| `xnu_live_irq_nub_after` | `0x8050a000` | `0x8050a000` |
| `xnu_live_irq_install_ok` | `1` | `1` |
| `xnu_live_irq_icfgr` | `0xffffffff` | `0xffffffff` |
| `xnu_live_irq_priority_word` | `0xa0a0a0a0` | `0xa0a0a0a0` |
| `xnu_live_irq_targets_word` | `0x01010101` | `0x01010101` |
| `xnu_live_irq_pend_cleared` | `0x20400000` | `0x20400000` |
| `xnu_live_irq_line_enabled` | `0` | `1` |
| `xnu_live_irq_cpu_ctlr` / `_pmr` | `1` / `0xf0` | `1` / `0xf0` |
| `xnu_live_irq_handler_entered` | — | `1` |
| `xnu_live_irq_first_iar` | — | `0x00000014` |
| `xnu_live_irq_target` / `_refcon` / `_nub` / `_source` | — | `0x8053b2cc` / `0` / `0x8050a000` / `0` |
| `xnu_live_irq_timer_count` | — | `0x800` (2048 serviced) |
| `xnu_live_irq_late_count` | — | `0` |
| `xnu_live_irq_pend_before` / `_after` | — | `0x20400000` / `0x20400000` |
| `xnu_live_irq_other_count` / `_spurious_count` | — | absent |
| `xnu_live_setpop_count` | `0x10` (16) | `0x1000` (4096) |
| `xnu_live_dec_count` | `0x10` | `0x1000` |
| `xnu_live_dec_min` | `0x4ab9` (call 15) | `0x4ab6`–`0x4ab8` |
| `xnu_live_block_seq` / `_returns` | `0x46` / `0x0d` | `0x48` / `0x0d` |
| `xnu_live_getpid_count` | `0x800000` | `0x800000` |
| log | 464226 bytes, 1184 `xnu_live` | B 470413/1373, C 470909/1387 |

Three things in that table are the step:

**`first_iar = 0x14` is 482's measurement arriving as an interrupt.** The value is read out of
`GICC_IAR` inside the second-level handler, i.e. *after* `locore_fleh_irq` split on the interrupted
mode, saved the context, swapped `sp` to `cpu_data->istackptr`, and fell into `fleh_irq_handler`,
which loaded five words and `blx`ed the fifth. Every link of that chain is exercised by one number.

**The four arguments are the four `fleh_irq_handler` loaded.** `target` is `0x8053b2cc` and it is
`g_irq_target` — an address this image chose, so it cannot be a coincidence of register ordering: the
word the dispatcher loads from `cpu_data`+192 is the first argument, `+196` the second, `+184` the
third, `+188` the fourth. The `r0`/`r1`/`r2`/`r3` order in `locore.s` and the parameter order in
`entry_irq.c` are one ABI, and it is measured rather than quoted.

**`setpop_count` 16 → 4096 is the clock.** Before this step the kernel re-armed the decrementer 16
times and never again: the log's last `xnu_live_dec_min` was `0x4ab9` (19129 ticks ≈ 1 ms) at call
15, and the machine then sat in process 1's `getpid` loop for 8.4 million iterations with a 1 ms
deadline armed and no way to notice. Now `rtclock_intr` re-enters `setPop` on every expiry, and
`dec_count` (this image's writer) agrees with it at 4096 in both schedules — so the 481 chain
(`ml_set_decrementer` → `CNTV_TVAL`/`CNTV_CTL`) is not merely closed, it is *being driven*.

## What moved, and what did not

**What did not move.** The OS console region is **byte-identical** across all three runs and to 482's:
27 lines by this run's extractor (131102 bytes from the `[os-console-459]` marker to the first live
record), ending at `load_init_program: attempting to load /sbin/launchd`. `stub_hit=` 0,
`xnu_live_undef_*` 0, `xnu_entry_panic_*` 0, `pid 1 exited` absent, `xnu_live_getpid_count` still
`0x800000`, and the last line `No errors detected`. So process 1 is still spinning on `getpid` and
nothing the kernel printed changed.

**What moved is a scheduler pass.** The block census goes from 70 entered / 13 returned to
**72 entered / 13 returned**, and the two new entries are, in both runs, the same two sites with one
more visit each:

| site (the `bl` is at `caller - 4`) | A | B | C |
| --- | --- | --- | --- |
| `0x8000f4c8` / `0x8000f4d8` — inside `thread_sched_call` | 31 | 32 | 32 |
| `0x800f8efc` — inside `Call_continuation` | 10 | 11 | 11 |

and the two sites' threads differ between runs (A: `0xc058f270`; B: `0xc05c5760`; C: `0xc0567df0`),
which is what says these are *new* blocks and not the same ones re-logged. `Call_continuation` is the
scheduler's own continuation call and `thread_sched_call` is the site above it: one more pass through
the scheduler spine, with no new return and no new OS output.

**And the parked set is the kernel's own workers.** Reading the sites the census has been reporting
all along through their containing symbols — `bl` at `caller - 4`, since the instrument publishes the
`lr` it was entered with — the 57–59 parked blocks are `vm_pageout`, `memorystatus_thread`,
`zone_replenish_thread`, `mbuf_worker_thread`, `aio_work_thread`, `mapping_replenish`,
`ipc_mqueue_receive`, `async_work_continue`, `lck_mtx_sleep`, `lck_mtx_sleep_deadline`,
`lck_mtx_lock_contended`, and the `thread_sched_call`/`Call_continuation` spine. Those threads are not
what the boot's next line of console output is waiting for; the boots' own progress stopped at
`load_init_program` before any of them was created. That is the frontier 484 has to name.

## The two checks, and the boundary between them

`tools/check_irq_routing.py` is new and makes **six** claims about this step's chain — the install
through Apple's own function (and the *absence* of a hand store through
`BootCpuData + STAGE90_CPU_INTERRUPT_*`), the read-back as a gate on both the handler word and the
target, the `ICFGR` word/field and both byte-per-intid indices derived from the intid, the handler's
three cases and their order (IAR read → intid masked and compared → EOI → `rtclock_intr`, with exactly
one `entry_epilogue` and exactly two `EOIR` writes, neither of them in the spurious case), the switch
(one `#define` in `entry_irq.c`, one `ISENABLER0` write and it inside the `#if`), and the order in
`entry_timebase.c` (probe → arm → the kernel's deadline → the unmask conditional on `g_irq_unmasked`,
with the arming after `g_dec_writes++`) — plus the two symbols in the linked image. Its `--selftest`
refuses **36 mutations**, and it runs twice from `build_entry.sh`.

It deliberately does **not** re-assert `tools/check_gic_routing.py`'s claims. The GIC register
offsets, the two candidate PPIs, the five `cpu_data` interrupt words against `assym.s`, Apple's
dispatch window and the vector page are 482's check, and this check's first draft duplicated three of
them; a claim in two files is a claim that can be updated in one. `tools/check_gic_routing.py` grew to
**36 mutations** as well, and what changed in it is the slot-6 claim:

**The vector page is not part of the switch.** The claim written for this step was the *coupling* —
slot 6 holds Apple's `locore_fleh_irq` when `STAGE90_IRQ_ENABLE_LINE` is 1 and this image's reporting
`fleh_irq` when it is 0 — and the build refused the image, correctly, because `entry_vectors.s` moves
slot 6 unconditionally. The coupling is wrong on the merits: the switch decides **one bit at the
distributor**, and the two runs of this step exist to differ in exactly that bit. A build that also
moved the vector page would differ from the measured run in two places, and "the interrupt arrived"
would have two candidate causes. So slot 6 is asserted unconditionally (Apple's entry, and *not* the
reporting stub, in both states) and the flag is only required to *exist* here, because what it
switches is `entry_irq.c`'s `#if`. `build_entry.sh`'s own vector-slot table needed the same correction
for the same reason, and it was one step behind `entry_vectors.s` until the build said so.

## Defects this step's own session found

Seven are recorded in `mi4-measurement-defects.md` (225–231) and every one of them is this step's own:

  - **225**: the two-state slot-6 coupling claim above — a claim about a coupling the machine does not
    have, caught by the build before any device run because the checks run from `build_entry.sh`.
  - **226**: `entry_timebase.c`'s comment said the arming was "at the end of the first call" and
    after `g_dec_writes++`; the code had it *before*. The re-entrancy argument in the comment was
    right — `ml_install_interrupt_handler` ends in `initialize_screen(NULL, kPEAcquireScreen)`, which
    can reach `setPop` and so this image's writer, and a re-entrant call with `g_dec_writes` still 0
    would run **482's probe** inside the arming — and the code did not implement it. The check caught
    it and the code moved, so the comment is now true.
  - **227**: four `--selftest` mutations were **accepted**, all four for the same reason — each tested
    a name or an element where the property is a position or a behaviour. `readback_not_a_gate`
    disabled the gate with `0 &&` and left the compared expression in place, so the claim's regex still
    matched; `no_zero_return` removed one `return 0u` while the interrupt-stack gate's own `return 0u`
    satisfied the claim; `istack_checked_after_install` moved the *gate* while the claim tested the
    position of the *read*; and `handler_eoir_first` swapped an `EOIR` with an `ISPENDR` read, which
    the ordering claim never covered (it was replaced with `handler_eoirs_after_rtclock`, which is the
    defect the claim is actually about). One more claim was simply about the wrong thing: the first
    version asserted `STAGE90_GIC_TIMER_INTID` appears *before* the `IAR` read, which is false by
    construction — the intid is compared after it is read — and is now a presence test on
    `intid = iar & MASK` and both comparisons.
  - **228**: `switch_removed` and `switch_is_a_third_value` were written against the literal
    `STAGE90_IRQ_ENABLE_LINE 0`, so both raised `AssertionError` the moment the flag became 1 — a
    mutation that is a claim about the flag's *current value* rather than about the line. They are
    regexes on the `#define` now, so the selftest refuses the same set in both states.
  - **229**: `g_irq_late_count` was incremented and never published. The direct evidence for the EOI
    claim was a count nobody could read, and the *sample* the claim rested on (`_pend_after`) is one
    call's reading on each power of two. Run C exists because of this: the count is now written on the
    same schedule, and `late_count = 0` is the claim stated as a number.
  - **230**: `build_entry.sh`'s vector-slot table still required slot 6 to hold `fleh_irq`, one step
    behind `entry_vectors.s`. The build refused the image and named it, which is the check working.
  - **231**: `tools/check_irq_routing.py`'s first draft duplicated `tools/check_gic_routing.py`'s
    `cpu_data`-offsets, candidates and dispatch-window claims. Two copies of a claim is two things to
    update; the draft was cut back to the boundary described above.

One more thing this step did not fix and should be named: **the fixture's timer node still names the
wrong lines.** `interrupts = <1 2 0 1 3 0>` maps to PPI 2 and 3 (INTID 18 and 19), and 482 measured
the virtual timer on 20. Nothing in this step reads that node — the handler is installed on the
measured line — but any driver that later binds the timer through the device tree will ask for the
line the run falsified.

## Build, layout, and artifacts

| | 482 | 483 |
| --- | --- | --- |
| `.text` | 5237824 | run A **5238944**, run C **5238976** |
| image bytes | 5454452 | **5454452** |
| `.bss` | `0x80533a80..0x8058b5c8` | `0x80533a80..0x8058b608` (+64) |
| headroom | 1526328 | 1526264 |
| undefined / wraps | 26 / 49 | 26 / 49 |

`.text` grows by 1152 bytes (run C) while the image file's length is **unchanged**: the added code
occupies space that was the aligned-fill term inside the existing sections, which is the same effect
that made 482's `.text` delta larger than the sum of its object's inputs. The lesson this project has
already recorded applies — a printed size is a reading band, not an exact sum — so the only numbers
quoted as deltas here are the two read from committed docs and this build's own output.

Addresses in the run C image: `entry_irq_handler` 0x800085b0, `entry_irq_arm` 0x800087c8,
`g_irq_target` 0x8053b2cc, `BootCpuData` 0x8050a000, `locore_fleh_irq` 0x80016954,
`fleh_irq` 0x800078a4 (still defined, still a report-and-stop, in no slot).
Entry image sha256: run A `bb2bfb9d7a14339e7406f2784ec6cbd18946345501c0b1a2d1fc0d02618666ae`,
run B/C `de457aa873fd25932fee333331acc7e262dbd2cb92471f63f512f513148058e9` (`.elf`
`9275a57e00b5e7925b813bc11b66f0aa1f32270358816cdddd2ba236ac2d1d55`).
Payload `stage90-qcdt.img` 8474624 bytes: run A `346f1ac5…`, run B `9c5b6ddb…`, run C
`5aa8edfc6f4c44012eafcddc8c30274ddc1d2cc1958d2d1f263873d2b20f7b97`;
`kernel_size = 5949716`, `dt_size = 2521088`, page size 2048.

Build lines, in order, all green: `check_undef_handler.py --split` (61 mutations refused),
`check_timebase_registration.py` (12), `check_gic_routing.py` (36), `check_irq_routing.py` (36).

## What 484 has to do

The interrupt works and the clock runs; the OS still stops at the same line. The two candidates for
what is actually blocking, and the measurement that separates them:

  1. **Nothing is asking the clock for anything.** `setpop_count` reaches 4096 because `rtclock_intr`
     re-arms for the *same* deadline: the rest of the kernel's timeouts are not registered, so the
     interrupt is a metronome and not a scheduler input. The test is whether any callout or
     `thread_call` reaches `ml_set_decrementer` with a deadline *other* than the repeating one —
     `xnu_live_dec_min` moved only within `0x4ab6`–`0x4ab9` across 4096 calls, which says it did not.
  2. **Process 1 is spinning on a syscall that never blocks.** `getpid` returns immediately 8.4 M
     times; `launchd` is loaded (`load_init_program: attempting to load /sbin/launchd`) but the
     fixture's Mach-O is a five-instruction loop. That is a fixture limit and not a kernel one, and
     484's first job is to decide which of the two it is looking at — a timer no one waits on, or a
     fixture that never asks to wait.

Either way the object is the same: **the first deadline that is not the repeating one**, and a way to
see it in the log. The 57–59 parked blocks list above is the set of threads that would move if
anything woke them, and `vm_pageout`'s and `memorystatus_thread`'s continuations are the two that
would produce OS console output if they ran.
