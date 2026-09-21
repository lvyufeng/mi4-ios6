# Experiment 500 — the piece that was missing was the deadline, not the line

Date: 2026-09-21
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured once per run
Artifacts: `stages/stage90/xnu_platform/MSM8974Timer.cpp`, `stages/stage90/xnu_arm_boot/entry_irq.c`,
`stages/stage90/xnu_arm_boot/entry_gic.h`, `stages/stage90/xnu_arm_boot/entry_stubs.c`,
`tools/check_driver_catalogue.py`, `tools/check_timer_line.py`

**Result: the boot asked a driver's line to arrive — three times, on the intid the device tree gave it —
and it did, on a boot whose OS-side record is byte-identical to 498's.** The step planned to arm two
things: the frame's countdown, and the distributor's per-line state on intid 40. It armed both, and the
run says **the second was already done** — `_line_isen_before = 0x00000100`, `_line_target_before =
0x01010101`, `_line_group_before = 0` — so every distributor write this step makes was a write of a
value that was already there, and what had been missing in 498 was the **device**: nothing in the boot
had put a deadline in the frame, and a line with no device behind it asserts nothing. The mechanism is
now a driver's: `MSM8974Timer::start` programs `CNTP_TVAL` with a count derived from the frame's own
frequency, starts `CNTP`, enables the line, and the payload's dispatcher calls `msm8974_timer_isr`
(`_isr_calls` 3, `_isr_intid` 0x28, `_isr_agree` 1) — twice reloading the deadline from a file-scope
record and once masking the frame, which is the end state 498 measured, after which there is no fourth
call.

## What the plan said, and what the machine said

The plan for this step was the shape 496's and 498's runs had left it: a registration makes an intid
*serviceable*, and only the distributor makes it *delivered*. Concretely, that an SPI at reset has
`GICD_ITARGETSR` 0 — no CPU targeted — and its enable bit clear, so 498's `_isr_calls` of 0 was a
distributor that had never been asked. That rests on the *reset* values of two registers.

This machine's GIC is not at reset when a driver runs. `fastboot boot` does not clear it, and the boot
before this image was Android's own kernel — which takes **this frame's** deadline on **this SPI**, so
the line it needs is a line Android enables at its own boot. The step's own record is what turned that
from an argument into a reading, and every number in it was added for a different reason (to keep "the
line needed configuring" and "it was configured" apart):

| key | value | what it says |
| --- | --- | --- |
| `_line_intid` | `0x00000028` | the tree's line is intid 40, as 498 resolved |
| `_line_word` / `_line_bit` / `_line_isaddr` | `1` / `0x100` / `0x104` | `ISENABLER1` bit 8 — derived from the intid, not written down |
| `_line_isen_before` / `_line_isen_after` | `0x00000100` / `0x00000100` | **the line was already enabled**, and the write did not change it |
| `_line_target_before` / `_line_target_after` | `0x01010101` / `0x01010101` | already CPU 0 — the byte the plan called "the write without which nothing else matters" |
| `_line_group_before` / `_line_group_after` | `0` / `0` | already Group 0, which is the group `GICC_CTLR = 1` delivers |
| `_line_pend_before` / `_line_pend_after` | `0` / `0` | nothing was latched on the line, so clear-pending had nothing to clear |
| `_line_prio` | `0xa0a0a0a0` | priority 0xa0, inside `_dist_pmr = 0xf0` |
| `_line_icfgr` / `_line_icfgr_field` | `0xfffffcff` / `0` | field 0 = level-sensitive, which is what the tree declares |
| `_line_rc` | `1` | the enable bit read back set — true, of a bit that was already set |

So the four writes are the *architecture's* requirement for any machine whose predecessor left the line
unconfigured, and on *this* machine they are idempotent. The claim that read them was written believing
the other half was missing; the record is what corrected it, and the correction is in the comments
(`entry_gic.h`, `entry_irq.c`, the driver's arming block) rather than only here.

## The device, and the three deliveries

The half that was missing is the frame. `start` derives the deadline from the frequency the frame
itself reports and writes it, in this order, all of it gated:

    arm_ticks = frame_freq * MSM8974_FRAME_ARM_MS / 1000      (0x2ee00 = 192000 at 19.2 MHz)
    g_timer_arm_ticks = arm_ticks                             (the record, before the device)
    *(frame + 0x028) = arm_ticks                              (CNTP_TVAL)
    *(frame + 0x02C) = ENABLE                                 (CNTP_CTL: unmask and start)
    arm_ctl   = *(frame + 0x02C)                              (read back)
    arm_rc    = (arm_ctl & ENABLE) != 0
    if arm_rc: arm_line_rc = entry_irq_enable_line( line, MSM8974_TIMER_LINE_TARGET )

and the run reads it back whole: `_arm_ms` 10, `_arm_ticks_max` 0x7fffffff, `_arm_ticks` 0x2ee00,
`_arm_ctl_before` 0x00000002 (the frame arrived masked and disabled — the architected reset value),
`_arm_ctl_after` 0x00000001 (ENABLE, unmasked: the device took the write), `_arm_tval_after` 0x0002ede5
— the count **27 ticks down** from 192000, so the frame is running, `_arm_rc` 1, `_arm_target` 1,
`_arm_line_rc` 1.

Three deliveries follow, ten milliseconds apart, and each one is a reading of a different part of the
handler:

| call | `_isr_ctl` | `_isr_tval` | `_isr_rearmed` | `_isr_masked` | `_isr_ctl_after` |
| --- | --- | --- | --- | --- | --- |
| 1 | `0x5` (ENABLE\|IT_STAT) | `0xffffff73` (−141) | 1 | 0 | `0x1` (ENABLE, ISTAT clear) |
| 2 | `0x5` | `0xffffffae` (−82) | 2 | 0 | `0x1` |
| 3 | `0x5` | `0xffffffa6` (−90) | 2 | **1** | `0x7` (ENABLE\|IT_MASK\|IT_STAT) |

  - `_isr_ctl` 0x5 is the state 498's handler existed for: the deadline has passed and the frame's
    output is asserted, unmasked. `_isr_agree` is 1 on all three — the dispatcher called *this*
    function for the line it was registered under — and `_isr_intid` is 0x28, the tree's line, which is
    now a measurement rather than a resolution.
  - `_isr_tval` is *negative*: `CNTP_TVAL` keeps counting below zero, so the register reads how long ago
    the deadline was. −141, −82 and −90 ticks are 7.3 µs, 4.3 µs and 4.7 µs — the delivery latency, and
    the first two are ten milliseconds apart because the handler reloaded 192000 in the first.
  - `_isr_ctl_after` 0x1 on calls 1 and 2 is the acknowledgement *working*: writing `CNTP_TVAL` cleared
    `IT_STAT`, which is what drops a level-sensitive line. That is why the re-arm branch is a legal
    interrupt handler and not a storm.
  - Call 3 masks instead, and `_isr_ctl_after` 0x7 — latched and silenced — is byte for byte the end
    state 498's run measured. There is no fourth call, and the cap that stops the sequence is
    `MSM8974_TIMER_FIRES` = 3, published as `_arm_ms`'s neighbour rather than written into the branch.

## The order, and what actually makes it safe

The claim this step wrote insists that the device is programmed **before** the line is enabled, and the
draft of its comment called that order "the safety argument": the frame has nothing to assert until its
control word is written, so there should be no window in which an enabled line has no deadline behind
it. The run shows the window was **already open before this driver ran** — the line was enabled at
boot, and the frame was masked and disabled — and that nothing came of it. The answer to "why not" is
the mask, not the order: `CNTP_CTL.IMASK` with `ENABLE` clear cannot assert, whatever the distributor
thinks, and the invariant the boot actually holds is **"the frame is masked except while a deadline is
being serviced"** — `_arm_ctl_before` 2, then 0x1 on the two re-arms, then 0x7.

The order is still checked (two mutations: the enable before the device, and `CTRL` before `CNTP_TVAL`)
and it is still the right discipline — a driver that enabled a line before starting its device would be
relying on a state it did not create. What is corrected is the claim that the order is what kept the
machine out of trouble, and the correction is the run's.

## The claims: the catalogue check at sixteen claims and ninety-nine mutations

`tools/check_driver_catalogue.py` goes **15 claims / 81 mutations → 16 / 99**. The new claim 16 is
"the driver that owns a machine line arms it, and programs its device before it does", and its clauses
are the chain: the subject is a driver whose line came from the tree (identified by the `_line_intid`
record, the same variable the registration uses); every arming call arms *that* line; the target is a
name whose value is in `[1, 0xff]` and not a literal in the call; both failing halves are tested (the
registration's return, and the driver's own read-back of `ENABLE`); the device is programmed before the
line is enabled and the deadline before the enable; the count is derived from the frame's frequency and
not written down; `_arm_rc`/`_arm_line_rc` are published *and* pre-published as zeroes before the
registration; and the handler reloads `CNTP_TVAL` from the file-scope record, refuses a zero, unmasks
before it reloads, and counts both paths.

The eighteen new mutations are refused, and each was checked to be refused *by the clause it is for*
rather than by an accident (the one that matters most, `the_line_is_armed_before_the_device`, is refused
by three clauses at once, which is the order relation being visible from more than one place). Two
things had to be fixed in the claim's readers while writing it, and both are the family this walk keeps
meeting:

  - **A declaration is not a call.** `driver_arming_calls` read the driver's own
    `extern "C" uint32_t entry_irq_enable_line(uint32_t intid, uint32_t target);` as the first arming —
    so the "device before line" clause compared the *declaration's* offset with the registration's and
    reported the driver as arming the line before the registration that precedes it. Same test as
    `method_body`'s: a definition has its return type before the name.
  - **A comparison is not an assignment.** `value_expression` matched `\s*=\s*` on the first of
    `arm_ticks == want`'s two `=`, and the capture then ran through a comment — no `;` in it — to the
    next statement's semicolon, so the "deadline" it answered with was a paragraph of prose and the
    `the_tick_count_is_written_down` mutation was **accepted**: the clause that exists to refuse a
    literal deadline passed a literal deadline. `=(?!=)` is the whole fix.

**Claim 15 also had to be repaired, and its defect was a false statement rather than a missed one.**
Its reader required the registration's `refCon` to be spelled `(uint32_t)(uintptr_t)this` — Apple's own
route and the shape the interrupt-controller driver writes — so `MSM8974Timer.cpp`'s
`entry_irq_register_client( line, ..., 0u )` did not match, and the claim printed, about the one file
in the image that had just been given a line: **"`MSM8974Timer.cpp` owns no interrupt line"**. 498's
registration was real and measured (`_line_cli_rc = 1`). The reader now takes the argument's text
whatever its shape, and the claim's subject is stated instead of inferred: the driver whose *own*
device is the distributor is read by claim 15, the driver whose line the *machine* asserts is read by
claim 16, and the note names which half a file is in. A claim whose gate is "registers a client or
defines the switch" was silently choosing its subjects by a reader's narrowness; it now chooses by a
property of the driver.

`tools/check_timer_line.py`'s claim 5 needed the same treatment for the same reason. It read the
handler's device write by looking 120 characters past `MSM8974_FRAME_CTRL_OFF ) =` for `IT_MASK` —
a window measured on 498's one-store handler. 500's handler has two paths, so the first store found was
the re-arm's, whose window holds `ENABLE`, a counter's increment and an `else`, and the mask was outside
it: the check **failed a handler that still masks on its last call**. The question is now asked of every
store in the body ("at least one carries `IT_MASK`"), which is the same statement about the same bit
with no assumption about how many paths the writer left, and the claim's own note says so.

## The image, and the arithmetic that says it moved

The four-step build is green. Artifacts (the ones that ran):

    out/stage90/xnu_arm_entry.bin   c0a466cdf42a603ae4343a9f56ac505a200a1f5b515eed587b6eedc8b59a3798
    out/stage90/stage90.bin         8c91ce7de3ebadfab2bfed474a96a2eb661e6c1dcb499d21873ecaeefe85ddf9
    out/stage90/stage90.img         f3e988b87d39c8ce968bdb2d62d8f8d28c95e0b291aa5ef1aa428ace291359a6
    out/stage90/stage90-qcdt.img    2f39e8e5fd8f537ae2f1c1c656da52961fb200ac67b7207a6362515bcbb51c2b

against 499's `7c3dbe7c…`, `13a1ee83…`, `c0545590…`, `39e0d1c6…`. The entry image's `.text` is **5272128**
(499: 5270560, +1568) and its `.bss` runs to `0x805943c8` — 362824 bytes, **+32** for the file-scope
records 500 adds. 27 undefined and 59 wraps are unchanged, and `check_irq_routing.py`'s claim 8 reads
**18** file-scope records in `MSM8974Timer.cpp` — all of them in the linked image, which is that
claim's whole question: a declared record the optimizer removed publishes a copy of a value and still
reads correctly, and the handler's two counters are exactly the shape it removes when nothing reads
them.

The edit that corrected the comments after the run (three files, prose only) is proved comment-only by
rebuilding: all four artifacts are **byte-identical** to the ones above.

## The live channel, and the cap that had to move

498's run wrote **3967** of the old 4096 records — 97%, with no `xnu_live_capped` to say so. 500's
arming and handler add 69 more, and the cap is now `ENTRY_LIVE_CAP` = **8192**, published as
`xnu_live_cap = 0x2000` in the header of the channel. This run wrote 4036 records and hit no cap;
against 4096 it would have been at 98.5%. The lesson is not the number: a buffer whose occupancy is not
published is a buffer nobody can see filling, and the key that publishes the cap is what makes the next
raise a measurement rather than a guess.

## The rest of the boot is unmoved

  - The OS console block is **byte-identical** to 497's and 498's: 1288 characters, sha256
    `a0593af02867eac3ddf107a6523292ea9e5016861188faa0a2c796d3ce54cc8d`.
  - The OS's own timer ran to `_irq_timer_count = 0x800` exactly as in 498, `_irq_late_count = 0`,
    `_irq_spurious_*` unchanged, `_irq_other_count` never taken: **the frame's line and the OS's line
    are both live in the same boot and neither disturbs the other** — intid 40 and intid 20, three
    deliveries against two thousand and forty-eight.
  - `_arm_ctl_before` is 2 and `_isr_ctl_after` is 0x7: the frame is masked at both ends of the
    sequence, so the deadline this step leaves behind is a masked one.
  - The log is 559990 bytes, `No errors detected`, and the device came back on its own inside the
    capture window.

## What is owed

  - **The physical timer's *other* line was never armed.** The frame's `interrupts` is
    `<0 8 0x4>, <0 7 0x4>` — intid 40 and 39 — and the driver registers for the first only. The measure
    that would settle whether the second is a second signal or the same one is entry 1 of the node's
    `reg`; 498's plan for it is still unstarted.
  - **The `AckC`/EOI question for a level line is now answerable and unasked.** The dispatcher writes
    `GICC_EOIR` before it calls the client; the frame's line is level, and `_irq_late_count` (0 on the
    OS's own line, never read for this one) is the key that would say whether an EOI re-pends it. The
    frame's line does not appear in it at all.
  - **The registry's capacity stop has still never been taken** (4 slots, `_irq_cli_capacity` = 4,
    three used at the end of this boot).
  - Claim 16's `_arm_ticks`-derivation clause reads one level of assignment; a driver that computed the
    count in three steps would be refused for the wrong reason, and the reader says so rather than
    looking deeper.
  - Unchanged from 499: the same citation rule pointed at the other files our sources cite
    (`IOService.cpp:6337`/`:6290`, the nine `IODeviceTreeSupport.cpp` sites, `msm8974.dtsi:153-167`,
    `IOCPU.cpp`); the build-time `out/xnu_asm_obj` still unstamped with its configuration; 497's
    conditional-clause mutation that needs two images; the unregister guard's asymmetry; a timeout that
    outlives its asker. From 498: `/timer`'s second definition; the other device nodes; the two services
    the catalogue answers with nothing; `MSM8974RootResource`'s `state0 = 0`; a name/class reader wider
    than eight characters; the release as a reading; `vm_fault`; 488's flag-list; 490's frames band;
    `xnu_live_dec_same`.

## Safety

Non-persistent `fastboot boot` only, through `preflight_boot_check.sh --allow-xnu-entry` and then
`run_and_capture.sh --allow-xnu-entry`; nothing was flashed. The step enables an interrupt line and
starts a device the image never touched before, and the two properties that bound it were designed in
rather than discovered: the device is masked on every path that returns to the boot (`_arm_ctl_before`
2, `_isr_ctl_after` 0x7), and the handler's sequence is capped at three deliveries with the cap
published — so the worst case this step could have produced is a nameless line reaching the dispatcher,
which is the documented stop, on a machine whose hardware watchdog and software dead-man have both been
proved to bring it back. Neither was needed: the run returned on its own, with `No errors detected`.
