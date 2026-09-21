# Experiment 502 — the compare values, and the counter that is not the core's

Date: 2026-09-21
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured once per run
Artifacts: `stages/stage90/xnu_platform/MSM8974Timer.cpp`, `tools/check_driver_catalogue.py`,
`tools/check_timer_line.py`

**Result: the register 501 could not identify is a countdown to an *unprogrammed* compare value, and the
comparison 501 drew from it - "the two routes agree" - was two different counters agreeing because the
slack it used was wider than their offset.** The step is read-only by construction and asked two
questions in two runs of one image family. The frame's `0x038` is `TVAL = CVAL - counter` with `CVAL = 0`:
the physical side, whose compare value is *this file's own write*, has a sum `TVAL + CNTPCT` that is
`CNTPCT_at_write + _arm_ticks` to the tick (`_cv_p_sum1` `0x06dc6dbe`, `_cv_p_arm_ct_lo` `0x06d97fbe`,
`0x2ee00` = 192000 exactly), the virtual side's sum is `5` and `4` - zero within the reads' own cost -
and its control word reads `0` while the core's reads `1`. And the frame's `0x008` is **not** the core's
`CNTVCT`: the two orders of the same comparison give `+70` and `+58` ticks, whose sum is `1022/16 = 63.9`
ticks per sample while the *physical* pair's is `9/16 = 0.6`, and the frame's own two windows - one read
path on both sides, so no latency difference can sit in it - measure the same `63.2` ticks with the two
orders agreeing to 2 ticks in 1011. The frame's `0x000` **is** the core's counter. Its `0x008` is a
second counter, ~63 ticks (3.3 us) behind it, and 501's slack of 104 was just wide enough to hide that.

## What the step was for

500 ended with a driver that owns a line the machine asserts, a frame it programs, and one register it
had written down and never explained. 501 read it - `0x038`, `QTIMER_CNTV_TVAL_REG`, the only one of
that kernel's eight timer offsets with no reader anywhere in it (`arch_timer.c:67`) - and found it
disagreeing with the coprocessor's `CNTV_TVAL` by 0x06f034ff ticks, recording what it could not settle:
*whether the frame's virtual countdown lives in the second view or behind a control word neither step
writes is a question that needs a **write** to answer.* It does not need one, and the reason is in the
architecture rather than in this driver. A generic-timer countdown is a *view* of a compare value:

    TVAL  =  CVAL - <the counter>

so `TVAL + counter` is invariant while the compare value is fixed - a prediction with no unknown in it -
and an unprogrammed compare value reads as *minus the counter*. Two consequences, one test each, and the
first one's control is the frame's *other* timer, which is why the block sits after the arming above it:
`0x028` has just been written by this file, so the physical side's compare value is this file's own
`_arm_ticks` and its sum must be `CNTPCT_at_write + _arm_ticks`, while the virtual side's is nobody's
write and its sum must be zero. One frame, two timers, the same arithmetic, one armed and one not.

The second question was 501's other half, and it is the half 501 knew was open. Its two-route comparison
found the frame's counter 71 ticks from the coprocessor's *inside a measured slack of 104*, and the doc
said of that bound: "two separate counters counting the same 19.2 MHz clock also read within a hundred
ticks of each other". The step's bracket was meant to settle it: read the frame's counter, then the
core's, then the frame's again, and require the core's reading to lie **between** the two. The first run
answered *outside* - `_cv_inside` 0 of `_cv_n` 8 - and not near the edge: `_cv_d0` was 70 while
`_cv_wmax` was 13, so the core's reading was past the bracket's **upper** end by `0x39` ticks, a case the
bracket's own comment had not named (it named "inside" and "before the low end"). That is a reading, not
an answer, because **one order cannot separate two counters from a late-returning path**: the frame's
counter is a device read and the core's is a coprocessor read of `counter_get_cntvct_cp15`
(`arch_timer.c:331`), and neither is ordered against the other. So the same step's second run reads the
same pairs **both ways round**.

## The compare values, both runs

| key | 502a | 502b | what it says |
| --- | --- | --- | --- |
| `_cv_p_tval1` / `_cv_p_tval2` | `0x0002eb6a` / `0x0002eb63` | `0x0002eb5f` / `0x0002eb57` | the countdown this file wrote into `0x028`, read twice |
| `_cv_p_sum1` / `_cv_p_sum2` | `0x06dc6dbe` / `0x06dc6dbf` | `0x04bae0f2` / `0x04bae0f2` | `TVAL + CNTPCT`: one difference of 1 tick, then of 0 |
| `_cv_p_inv_ok` | 1 | 1 | inside `_cv_slack` 0x68 / 0x6c both times |
| `_cv_p_cval_lo` | `0x06dc6dbe` | `0x04bae0f2` | the compare value the invariant implies |
| `_cv_p_arm_ct_lo` | `0x06d97fbe` | `0x04b7f2f2` | `cval - _arm_ticks`, i.e. the frame's counter at the write |
| `_cv_p_gt_now` / `_cv_p_lag` | 1 / `0x296` | 1 / `0x2a1` | the deadline is ahead of the counter, and 662/673 ticks have run |
| `_cv_v_tval1` / `_cv_v_tval2` | `0xf9267de0` / `0xf9267dd7` | `0xfb480aa0` / `0xfb480a98` | the virtual countdown, read twice |
| `_cv_v_sum1` / `_cv_v_sum2` | 5 / 4 | 4 / 6 | **zero**, to within the reads' own cost |
| `_cv_v_inv_ok` / `_cv_v_zero_ok` | 1 / 1 | 1 / 1 | a countdown to a fixed compare value, and it is zero |
| `_cv_v_ctl_fr` / `_cv_v_ctl_cpu` | 0 / 1 | 0 / 1 | the frame's virtual control word reads **stopped**, the core's enabled |
| `_cv_v_ctl_agree` / `_cv_v_ctl_mask` | 0 / 0 | 0 / 0 | they do not agree; the frame's mask bit is clear |

The physical side is the control and it came out exactly: `_cv_p_cval_lo - _cv_p_arm_ct_lo` is
`0x0002ee00` = 192000 = `_arm_ticks` in *both* runs, so the compare value the invariant recovers, minus
the counter the invariant recovers at the moment of the write, is the interval this file asked for - a
number with no reading in it, and it agrees with the reading. And `_cv_p_lag` is tens-then-hundreds of
ticks rather than thousands, which is what a countdown 662 ticks into a 192000-tick interval looks like.

Three things follow about `0x038`, and the first two are the answer to 501's owed question:

  - **It is a countdown.** Two sums taken a measured interval apart agree to 0-1 tick on both sides,
    where the counter has advanced ~13 ticks in between. A register that is not a countdown to a fixed
    compare value cannot do that twice.
  - **Its compare value is unprogrammed.** `_cv_v_sum1` is 5 and 4 in run 1 and 4 and 6 in run 2: zero
    within the cost of the reads that produced it. Nothing in this machine has ever written the frame's
    virtual compare value, which is why 501's `0xf912a3cc` was `2^32 - CNTVCT` and not a countdown like
    the core's `0x0002d8cb`.
  - **Its control word reads stopped**, and that is the register beside it - `0x03C`, defined by the
    derivation in the file rather than by a second literal - whose identity 501 confirmed on the
    physical pair (`_rt_ctl_fr` 2, `_rt_ctl_cpu` 2, `_rt_ctl_agree` 1). The frame's virtual timer is not
    enabled: with a compare value the counter passed long ago, an unmasked and enabled virtual timer
    would have to read `ISTATUS` set, and `0x03C` reads 0 with the mask bit clear. **Nothing will assert
    the frame's second line (intid 39) until someone programs `0x038` and `0x03C`** - which is the datum
    501's owed item about that line was missing.

## The bracket, and why it needed a second reading

The first run's bracket measured the frame's counter against the core's once per sample, three readings
each, and answered *outside* on all eight. That answer has three possible causes and one order cannot
tell them apart: **two counters with a fixed phase**, **one path returning its reading late**, or **two
counters at different rates**. Reading each pair both ways round separates them, and the arithmetic is
worth writing down because the step is designed around it - with `a` the spacing between two readings of
one sample, `K` the phase (the core's reading minus the frame's, at one instant) and `dL` the difference
between the two paths' latencies:

    order X (frame, core, frame)   core - frame  =  +a + K + dL
    order Y (core, frame, core)    core - frame  =  -a + K + dL

    the sum of the two orders  =  2K + 2dL      (the spacing cancels: +a once, -a once)
    the difference of the two  =  2a            (the phase cancels, and so does dL)

**So the sum holds the phase *and* a path-latency difference, and a pair of cross readings cannot tell
those two apart - which is why there are three.** The frame's own `0x000` against its own `0x008` takes
one path on both sides, so `dL` is zero there by construction and that sum needs no model. The physical
pair (`0x000` against the core's `CNTPCT`) is 501's own control. And `dL` is a property of the two
*paths*, so whatever part of a cross sum it is, it is the same in both cross pairs and cancels in their
difference - which is exactly the reading that closes the escape hatch.

| pair (all eight samples accepted, `_cv_*_n` = 8) | dX / sample | dY / sample | sum / 16 | dif / 16 |
| --- | --- | --- | --- | --- |
| virtual: frame `0x008` vs the core's `CNTVCT` | +70.1 | +57.6 | **+63.9** | 6.25 |
| physical: frame `0x000` vs the core's `CNTPCT` | +6.6 | -5.5 | **+0.6** | 6.06 |
| the frame's own `0x008` vs its own `0x000` | -59.5 | -66.9 | **-63.2** | 3.69 |

(`_cv_xv_*`, `_cv_xp_*`, `_cv_win_*`; the sums and differences are published as sums and differences of
the two orders' eight-sample accumulations, and the two's-complement readings above are the averages.)

Four readings of that table, in order of how little they assume:

  - **`_cv_xp_sum` is 9.** Sixteen samples of the physical pair, read through two different paths, sum to
    0.6 ticks each: the frame's `0x000` **is** the core's counter. 501's "7 ticks apart" was the cost of
    the reads, and its slack of 104 admitted a 64-tick offset that is not there. The mirror order's
    bracket agrees - `_cv_xp_m_in` is 8 of 8, the core's own two readings bracketing the frame's - and
    that bracket is 2a = 12 ticks wide against the virtual pair's 0 of 8.
  - **`_cv_win_sum` is -1011.** The frame's own two windows, both read through the device, differ by
    63.2 ticks per sample, with the two orders agreeing to 2 ticks in 1011 (`_cv_win_x0` -59,
    `_cv_win_y0` -66). This is the model-free reading: no path latency can sit in a difference between
    two adjacent reads of one window, so the frame's `0x008` reads **63 ticks below its own `0x000` at
    the same instant**, reproducibly, on both of the ways round it is read.
  - **`_cv_xv_sum` is 1022**, i.e. 63.9 per sample, and `_cv_xv_sum - _cv_xp_sum` = 1013 against
    `_cv_win_sum` = 1011: **the same quantity by two routes, one of which is path-free, agreeing to 2
    ticks in 1011 (0.2%).** That is what closes the escape hatch quantitatively rather than by argument.
    Whatever `dL` is, it is the same in both cross pairs and this difference does not contain it; what it
    does contain is the frame's own internal offset, and the frame measured that itself.
  - **`_cv_xv_dif` and `_cv_xp_dif` are 100 and 97**, i.e. 6.25 and 6.06 ticks per sample: the spacing
    `a`, which is what the first run's bracket width said too (`_cv_wmax` 13 = 2a). Two independent
    quantities in the design measuring the same 6 ticks is the cross-check that the model above is the
    right shape; the window pair's smaller 3.69 is the same thing measured with a device read at both
    ends instead of a device read and a coprocessor read.

So the machine's answer to the question 501's slack could not settle: **the frame carries two counters
that this driver reads through two names.** The one at `0x000` is the core's own counter - the physical
cross pair, two paths and sixteen samples, sum to 0.6 ticks and its mirror order brackets 8 of 8. The one
at `0x008` is not: it reads 63 ticks below the core's `CNTVCT` at the same instant, and the frame's own
two windows measure that same 63 ticks with one read path at both ends. What 501 read as "the two routes
agree" was these two counters compared inside a bound of 104, which is wider than their 63-tick offset -
the rival explanation 501's own doc named and could not exclude. One consequence reaches back into 501's
arithmetic: its `_rt_cntv_*` and every frame-against-core difference in this driver compares two
counters, so those readings carry a 63-tick bias. The compare-value arithmetic above is unaffected: it
never leaves one counter.

## The rate, measured rather than assumed

A rate difference between the two counters would make every frame-against-core difference in this driver
meaningless rather than merely offset, so it is read first and read directly: the frame's counter's
advance over an interval timed by `mach_absolute_time` over the same interval.

| key | value | what it says |
| --- | --- | --- |
| `_cv_r_fd` / `_cv_r_cd` | 32782 / 32781 | the frame advanced 32782 ticks, the core's clock 32781 |
| `_cv_r_ppm` | **1000030** | one part in a million = 1; thirty parts per million apart |
| `_cv_r_ok` / `_cv_r_n` | 1 / 1024 | the frame's counter reached the interval asked for, in 1024 readings |

32782 against 32781 over an interval of 1.7 ms is a 30 ppm difference, which is exactly one tick of that
interval (1/32781 = 30.5 ppm): the two counters tick at the same rate to the resolution of the
measurement. The loop is bounded twice - by the frame's counter and by `mach_absolute_time` - so a frame
whose counter is stopped ends it instead of hanging the boot in it, and `_cv_r_ok` says which bound
ended it: a frame counter that had stopped would have shown up as `_cv_r_ok` 0 with `_cv_r_n` at the
bound, which is a reading this step would have published rather than waited for.

## The claims: 18 claims / 131 mutations, and 44 citations

`tools/check_driver_catalogue.py` goes **18 claims / 120 mutations -> 18 / 131**. Claim 18's clauses grew
by the second half of the block: each of the three pairs publishes a *sum* that adds its two orders'
accumulations and a *difference* that subtracts them, the two accumulations may not be the same
expression (that mutation, `the_second_order_is_the_first`, is the one that would make the whole test a
sample taken twice), each cross pair reads the coprocessor at least three times per sample, the frame's
own two windows are read in both orders, every sample carries a cost witness that is a *difference of two
readings of the same side* compared against `MSM8974_CV_COST_MAX`, the rate is computed from two readings
of two clocks rather than written down, and the rate loop has both of its bounds. Eleven mutations were
added for this half and all are refused.

**Two clauses failed on our own correct file first, and both were the same defect in a new place.** The
bracket clause used to search "the region from the first `cv_i` to the first published key" for a
two-ended test, which was the whole block while the block was one loop; the second half's loops put three
more `msm8974_cpu_cntvct()` calls in that region, so **the mutation that replaced the bracket's own
coprocessor reading with a frame reading was accepted** - refused now by reading the loop's own braces
(`loop_body`) and by *counting* two-ended tests against the brackets the code runs rather than searching
for one anywhere. And the coprocessor-read clause had the same hole for the same reason. This is the
third step in a row where a check written for a new block failed the block, and this time it was the
*old* clause that went blind - the tell is the same one every time: **a needle that matches something
else, and the something else is the step's own new code.**

`tools/check_timer_line.py` passes with **44 citations checked** and 20 mutations refused, and the step
added to it as well: one clause, and the mutation that proves the clause can fail. The frame's virtual
control word is the *one* offset the driver derives rather than shares with the device's kernel - that
file writes and reads the physical timer's control word and never names the virtual one - so the clause
requires it to stay a **derivation** (`MSM8974_FRAME_CNTV_TVAL_OFF + 0x4`) and refuses a literal, because
the countdown's reading cannot be interpreted without that word and nothing else in the tree can catch it
being written down twice. `the_virtual_control_word_is_written_down` writes the literal `0x03Cu` and is
refused.

The citation count in this doc's first draft was wrong, and the correction is a measurement. The draft
said "44 citations checked (501: 42)". **501's tree says 37** - measured by re-running 501's own check in
a `git worktree` at `4767904` (with `external/` linked in, since the kernel trees are not tracked), where
it prints `37 checked` - and 37 is the number 501's own doc published, so the step added **seven**
citations and not two. The draft was also wrong that the file's clauses were unchanged. Both were
statements about an artifact written from memory, which is the defect this project counts: **the baseline
of a count is itself a number, and it has to come from the artifact that owns it.** The measurement takes
two minutes, and that is what makes the remembered number a defect rather than an accident. `tools/check_irq_routing.py` is unchanged at 9/57 and passes: the block adds no file-scope storage, which
is why the entry image's `.bss` does not move.

## The image, and a measured number that was not the one it was compared with

The four-step build is green. Artifacts (the ones that ran):

    out/stage90/xnu_arm_entry.bin  c0ed438ebe70c5783ffda80e15e74c4e20d44968951eede05e461e43e95f58dc
    out/stage90/stage90.bin        1e597693ff119000812fc15dfca654b12eb385ac7199ee2ccfbaf01f3a504ff3
    out/stage90/stage90.img        b4e3442fae68758c7de62a1177fb17e112188aff96343ac9a38adf5d126a48f2
    out/stage90/stage90-qcdt.img   319cfdcbe4c5ee1bfdfade15e80ca4a3c9954ac1106d56c4b8f1de25d848faea

against the first run's `816d6eb8…`, `dad51b24…`, `15931e34…`, `0fcd2da3…`. The entry image's `.text`
is **5280384** and was **5277600** after the first half and **5275520** in 501: +2080 and +2784, +4864
for the step. The *file* is 5503612 bytes and was 5503612 after the first half: the image is padded to a
16 KB boundary, 502's first half crossed one (5487228 -> 5503612) and the second half fitted inside the
band, which is why the image bytes repeat while the entries do not - the `.text` delta is the number
that moved. `.bss` is 362824, unchanged as designed; 27 undefined and 59 wraps unchanged; the payload's
`kernel_size` 5999040 with the qcdt image 8523776.

**501's doc published its `.text` as 5280296 with the delta "+8168 over 500".** 501's own step-3 log says
`text size 5275520 bytes (.text)`, and 5275520 + 4776 = 5280296: the recorded number was `.text` plus
`.sysctl_set`, and so was the 500 figure it was compared against, which is why the *delta* it published
is right and only the absolute is not. The chain read from the same line in both logs is 501 5275520 ->
502a 5277600 -> 502b 5280384, and the delta this doc quotes is that one. The defect is 461's exactly: **a
number compared against a remembered number that was not the same measurement** - and the difference
here is that both ends were inflated by the same constant, so the delta survived while the absolute did
not.

## The rest of the boot is unmoved

  - The OS console block is 1288 characters and **differs from 501's by exactly one line**: XNU prints
    the RAM disk's address in `Added memory device md0/rmd0 (...) at <va> for <size>` and that address is
    `STAGE90_XNU_RAMDISK_VA`, which this build moved from `0x80509000` to `0x8050D000`. With that line
    normalised the two blocks have the **same** fingerprint, `47c53d7558e1b2a6`, and the frontier line is
    the same one: `load_init_program: attempting to load /sbin/launchd`.
  - **The fingerprint this project has been quoting is a property of the build and not of the run.** The
    hash `a0593af0…` was recorded as byte-identical across 497, 498, 500 and 501 and used as evidence
    that the boot was unperturbed, but the block contains an address the linker chooses. It held while
    the address did; the honest fingerprint is the block with that line normalised, and the evidence that
    the boot is unmoved is the *diff* against the previous step, which here is one line long.
  - 500's three deliveries reproduce exactly: `_isr_calls` 1/2/3 on `_isr_intid` 0x28 with
    `_isr_ctl_after` 0x1 / 0x1 / 0x7, `_arm_rc`/`_arm_line_rc` 1 with their zeroes published before the
    registration, and the OS's own timer ran to `_irq_timer_count` 0x800 with `_irq_late_count` 0.
  - `xnu_live_sleh_storm` is 9, `getpid_count` 0x800000, the console block healed once and was not
    truncated (`_ostext_chars` 0x3ee, `_ostext_total` 0x3ee, `_ostext_heals` 1), the log is 564354 bytes,
    `No errors detected`, exit 0, and the device came back on its own inside the capture window.

## What is owed

  - **What the frame's `0x008` *is*.** The step says it is a second counter 63 ticks behind the frame's
    `0x000`, and it says that from timing alone. Whether the frame's virtual view simply carries the
    frame's own `CNTVOFF`-equivalent (which a *write* to the control frame could confirm and a read
    cannot) or is a counter of its own is not settled here, and the device's kernel cannot say either: it
    reads one route and never the other (`has_cp15`, `arch_timer.c:607`), so **nobody on this machine has
    compared the two routes except this driver** - which is 501's defect class with a magnitude, and the
    magnitude is 63 ticks.
  - **The frame's second line, intid 39, is still unarmed**, and the step now has a datum for it: the
    frame's virtual control word reads 0 with its mask clear and its compare value zero, so nothing will
    assert intid 39 until `0x038` and `0x03C` are programmed. A step that programs them needs a design
    that cannot move the kernel's deadline - the frame's virtual timer is not the OS's decrementer (`0x03C`
    reads 0 while the core's `CNTV_CTL` reads 1), but a *write* to a compare value is still an assertion
    about which counter it was written to, and the two counters now differ by a measured 63 ticks.
  - **The `AckC`/EOI question for a level line**, unchanged from 501: the dispatcher writes `GICC_EOIR`
    before it calls the client, the frame's line is level, and `_irq_late_count` is read for the OS's line
    and not for this one.
  - **The registry's capacity stop has still never been taken** (4 slots, `_irq_cli_capacity` 4, three
    used at the end of this boot).
  - Unchanged: claim 16's `_arm_ticks`-derivation clause reading one level of assignment; the citation
    rule pointed at the other files our sources cite (`IOService.cpp:6337`/`:6290`, the nine
    `IODeviceTreeSupport.cpp` sites, `msm8974.dtsi:153-167`, `IOCPU.cpp`); the build-time `out/xnu_asm_obj`
    unstamped with its configuration; 497's conditional-clause mutation needing two images; the unregister
    guard's asymmetry; a timeout that outlives its asker; `/timer`'s second definition; the other device
    nodes; the two services the catalogue answers with nothing; `MSM8974RootResource`'s `state0 = 0`; a
    name/class reader wider than eight characters; the release as a reading; `vm_fault`; 488's flag-list;
    490's frames band; `xnu_live_dec_same`.
  - **New from this step:** the frame's `0x000` and the core's counters are now known to be one counter,
    so the *frame's* window can be used to read the core's counter - worth knowing before any step that
    wants a timestamp from the frame; and the 63-tick offset is a number to re-measure whenever a step
    changes what runs before the driver, because it is asserted here from 16 samples of one boot.

## Safety

Non-persistent `fastboot boot` only, through `preflight_boot_check.sh --allow-xnu-entry` and then
`run_and_capture.sh --allow-xnu-entry`; nothing was flashed. Both runs of the step are read-only on the
device by construction: a build in which the block stores through either base does not build (claim 18's
region clause), and the register the arithmetic is about - the frame's virtual countdown - is a view of a
compare value that this step never writes, so the one write that could move the kernel's deadline is the
one the design refuses. The rate loop is bounded by the frame's own counter *and* by `mach_absolute_time`,
so a stopped counter ends it rather than spinning. Each run returned on its own inside the capture window
with `No errors detected`, and neither recovery net was needed.
