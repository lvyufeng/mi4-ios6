# Experiment 501 — the other route agrees, and the offset nobody read is not the register

Date: 2026-09-21
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured once per run
Artifacts: `stages/stage90/xnu_platform/MSM8974Timer.cpp`, `tools/check_driver_catalogue.py`,
`tools/check_timer_line.py`

**Result: the frame's counters were read a second way for the first time, and the second way agrees with
the first on everything except the one register the device's own kernel declares and never reads.** The
step is read-only by construction — no store to any device in either new block, and a build-time clause
that refuses one — and it asked three questions. The frame's **second `reg` region**, carried in our tree
since 492 and never read, maps (`_v2_phys` 0xf9022000, `_v2_va` 0xc218d000) and reads **zero everywhere**:
`_v2_freq` 0, `_v2_cntv_lo` 0, `_v2_cntp_lo` 0, `_v2_tval` 0, so the difference between the two views is
the frame's own counter (`_v2_cntv_d` 0x06ed5bca) and not a few ticks. The frame's counters read
**through the coprocessor** agree with the frame: `CNTPCT` 7 ticks apart, `CNTVCT` 71 ticks apart, both
inside a measured `_rt_slack` of 104, and `CNTP_CTL` equal *exactly* (`_rt_ctl_fr` 2, `_rt_ctl_cpu` 2) —
the offset the whole driver is built on, measured against a route that does not use the frame at all. And
the frame's `0x038` — `QTIMER_CNTV_TVAL_REG`, the only one of that kernel's eight timer offsets with no
reader anywhere in it — reads `0xf912a3cc` where the coprocessor's `CNTV_TVAL` reads `0x0002d8cb`.
`_rt_tval_ok` is 0, and that is the step's finding rather than its defect.

## What the step was for

500 ended with a driver that owns a line the machine asserts and a device it programs, and with two
things it had never done: it had read the frame's registers *only* through the frame, and it had never
looked at the region the frame's `reg` calls its second view. Neither gap is exotic. The frame's
description in the device's own tree is

    frame@f9021000 {
        frame-number = <0>;
        interrupts = <0 8 0x4>, <0 7 0x4>;
        reg = <0xf9021000 0x1000>, <0xf9022000 0x1000>;
    };

and the binding beside that kernel says of a frame's `reg`: "The first and second view base addresses in
that order. The second view base address is optional" (`arch_timer.txt:43-44`). Our tree has carried both
regions since 492, flattened onto the node the driver matched; the device's kernel maps the first
(`of_iomap(frame, 0)`, `arch_timer.c:623`) and nothing in it ever maps the second. So the second view is a
region this project has published for nine steps and never looked at, and the step's first question was
whether it is another window onto the same frame.

The second question has the same shape but a sharper reason. That kernel has **two routes to every
counter** —

    :297  counter_get_cntpct_mem      (reads the frame's 0x000/0x004)
    :310  counter_get_cntpct_cp15     (`mrrc p15, 0, ..., c14`)
    :318  counter_get_cntvct_mem      (reads the frame's 0x008/0x00C)
    :331  counter_get_cntvct_cp15     (`mrrc p15, 1, ..., c14`)
    :607  if (!has_cp15) {            (the memory route is taken *only* when the CPU has no coprocessor)

— and chooses one of the two, once, at boot. On a machine that has both, which is this one, the loser of
that switch is never read again by anything. **Two definitions of one value in one file, with the choice
made and never compared**: this project's oldest defect family, sitting inside the evidence for the frame
rather than in our own code. A driver that reads the frame's counters through the coprocessor is making
the comparison that kernel cannot, and it is the strongest reading of the offset table available here — an
offset that is off by one register answers as a difference of ~10^7 ticks instead of as an argument.

The step's whole risk is therefore a wrong *offset*, not a wrong write, and its design follows: nothing
in either new block stores to a device, the four slacks it compares against are measured on this machine
around the reads they bound, and `tools/check_driver_catalogue.py`'s claim 17 refuses a build in which the
region writes at all.

## The second view, and the zeros

The view maps, and everything behind it reads zero:

| key | value | what it says |
| --- | --- | --- |
| `_v2_phys` / `_v2_len` | `0xf9022000` / `0x1000` | the tree's third `reg` entry, resolved by the OS's own route |
| `_v2_objkind` / `_v2_va` / `_v2_map` | `2` / `0xc218d000` / `0xc0658fc0` | an `IOMemoryDescriptor`, mapped, at an address the OS chose |
| `_v2_freq` / `_v2_freq_agree` | `0` / `0` | not the frame's frequency register: `0x0124f800` is what view 1 reads |
| `_v2_cntv_lo` / `_v2_cntp_lo` | `0` / `0` | both counters read zero, where the frame's own read 0x06ed5bca-ish |
| `_v2_tval` | `0` | and so does the register at `0x038` in that region |
| `_v2_cntv_d` / `_v2_cntp_d` | `0x06ed5bca` / `0x06ed5c11` | the *frame's* counters, i.e. the whole count and not a read's worth |
| `_v2_slack` | `0x78` (120 ticks) | measured around a view-2 read, as `_frame_slack` is (498's rule) |
| `_v2_cntv_ok` / `_v2_cntp_ok` | `0` / `0` | the comparison can fail, and it did |

So the second view is **not** a window onto this frame. It reads like the container at `0xf9020000` that
494 mapped and read a zero from — a region the tree declares and the hardware does not populate — and the
binding's own word for that region is *optional*. The step records the answer and does not argue with it:
this is a fact about the machine, and both `_v2_*_ok` bits are 0 in a run whose driver is correct.

Two details are worth keeping because they are what make the zero readable rather than merely present.
The *slack* is measured (`_v2_slack` 0x78, the cost of one view-2 read times four), so "the two views
agree" is a statement about this machine's reads and not about how fast the CPU is. And `_v2_d` is
published beside `_v2_ok`, so a zero answer and a block that never ran are different records: `_v2_va`
non-zero says the view was mapped, and `_v2_cntv_d` of 0x06ed5bca says what the two readings actually
were.

## The two routes, and the three readings

| register | frame | coprocessor | Δ | bound | verdict |
| --- | --- | --- | --- | --- | --- |
| `CNTPCT` | `0x06ed5c6b` | `0x06ed5c72` | 7 | `_rt_slack` `0x68` (104) | `_rt_cntp_ok` 1 |
| `CNTVCT` | `0x06ed5c1f` | `0x06ed5c66` | `0x47` (71) | `_rt_slack` 104 | `_rt_cntv_ok` 1 |
| `CNTP_CTL` | `0x00000002` | `0x00000002` | — | exact | `_rt_ctl_agree` 1 |
| `CNTV_TVAL` | `0xf912a3cc` | `0x0002d8cb` | `0x06f034ff` | `_rt_slack` 104 | `_rt_tval_ok` **0** |

The first three are the step's proof of the driver's own table. `0x000`/`0x004`, `0x008`/`0x00C` and
`0x02C` are now the device's offsets by *two* independent readings of one set of registers, one of which
does not go through the frame at all — and the 64-bit reads are the tearing-safe shape the device's kernel
uses for the same reason (`:318`'s loop: read the high word, the low word, the high word again, take the
pair only if the high word did not change).

The last one is the finding. `CNTV_TVAL` **through the frame** is not the virtual countdown the
coprocessor reads: 0xf912a3cc against 0x0002d8cb, a difference of 0x06f034ff ticks. Two things make that
worth a step of its own rather than a footnote:

  - **It is the one offset in that file with no reader.** `QTIMER_CNTV_TVAL_REG` is defined at
    `arch_timer.c:67` and appears exactly once in the whole kernel tree — on its own `#define`. The
    frame route that kernel does use writes `QTIMER_CNTP_TVAL_REG` (the `ARCH_TIMER_REG_TVAL` arm of
    `timer_reg_write_mem`) and reads the same. So the offset is a name with no reader, **and this machine
    contradicts it** — which is why nothing anywhere noticed, and why no amount of re-reading that kernel
    would have found it. Only a reading of the register could.
  - **It cannot be repaired from here.** Whether the frame's virtual countdown lives in the second view
    (which reads zero through the offsets we have), or behind a control word that neither this step nor
    that kernel writes, is a question that needs a *write* to answer — and writing is exactly what this
    step refuses to do, because the virtual countdown on this machine is XNU's own decrementer: this
    image's `entry_timebase.c` writes `CNTV_TVAL` (`c14, c3, 0`) and `CNTV_CTL` (`c14, c3, 1`) for the
    kernel's `tbd_set_decrementer`. The step can say what the register is not. It says so and stops.

The fourth control reading is the other half of that safety argument and it came out as the shim's own
state: `_rt_cntv_ctl_cpu` is 1 — `ENABLE`, with the mask off — which is what `entry_timebase.c` leaves the
decrementer in when its arming succeeded. A driver reading the OS's timer from outside and finding it
enabled and unmasked is the reading that says this step did not disturb it.

## The claims: a seventeenth claim, and the anchors that read a second block

`tools/check_driver_catalogue.py` goes **16 claims / 99 mutations → 17 / 110**. Claim 17 is "a driver that
reads a device reads every way the device can be read, and holds them together", and its clauses are the
step's own ways of reading like a proof: the second view is the `reg` entry the tree names (a name in the
driver's defines, and *not* the frame's entry — a second view read at the frame's own index is the same
device read twice, the one comparison that cannot fail); both views are compared with a slack that is
*measured* (`_v2_slack`/`_rt_slack` may not be literals); the high words are compared exactly, because a
32-bit difference means nothing across a `2^32` boundary; every counter is read both ways and the
coprocessor side is the architected encoding; the frame side is the same offset *symbol* the certified
block uses, never a literal; both readings, both differences and both verdicts are published, so a
disagreement cannot be mistaken for a block that never ran; and the region between the second view's
mapping and the last coprocessor read **stores to nothing**.

Eleven mutations were added and all are refused. Three of them were *accepted* on the first run, and all
three were the same defect in a new shape — a reader that matched something other than its object:

  - **A needle that matched the prose.** `the_coprocessor_read_is_not_the_architected_one` replaced
    `mrrc p15, 1, ...` in the accessor with `lo = 0u; hi = 0u;` and the clause still found `mrrc p15, 1`
    — in the *comment* three lines above the code, which names the device's kernel's own instruction. The
    encoding searches now read the source with its comments removed. 269/272/297/496/500's family
    (`a needle that is a claim about the file's bytes`), in its newest shape: here the needle was in the
    file twice, once as code and once as prose *about* the code.
  - **A literal clause that could not see a literal.** The mutation wrote `0x000u` where the clause's
    pattern expected `0x000`, so the `u` suffix made a written-down offset invisible: `the_route_offsets
    _are_written_again` was accepted. The pattern takes the suffix now.
  - **A pair clause that omitted half the pairs.** `the_route_words_are_not_published` removed
    `_rt_cntv_lo_cpu`, and the clause that requires "the inputs of every comparison" only listed the
    *frame* side's words — so the coprocessor's side could have gone missing silently, which is precisely
    the reading that makes `_rt_cntv_d` a difference between two routes rather than between a route and
    nothing.

`tools/check_timer_line.py` needed a change for a reason of the same family, and it was found by the check
refusing **our own correct file** twice before it was found by a mutation:

  - The 501 block cites the device's kernel's counter functions and the switch that chooses between them
    (`:297`/`:310`/`:318`/`:331`/`:339`/`:340`/`:577`/`:581`/`:607`-`:609`). A citation in that check is
    read only when a *name the check knows* is written beside it, so those names are now a third anchor
    list beside the frame's offsets and the two calls: **37 citations are checked**, up from 499's 17, and
    the count is published as a note because a clause that can match nothing reads exactly like one that
    matched everything. The new mutation — the route block citing `:296` for `counter_get_cntpct_mem`,
    which is the blank line above it — is refused by the anchor clause.
  - **And the clause caught its own author.** The first version of the block wrote two citations and two
    names on one line (`` `counter_get_cntpct_mem` (`:297`) / `counter_get_cntvct_mem` (`:318`) ``), which
    the clause reads as each name needing to be on the *other* citation's line — so it failed a file whose
    numbers were all right. The fix is the clause's own rule applied to prose: one citation per line. That
    is the second time in two steps that a check written for the new block failed the block first, and the
    lesson is the same one: **a citation is read with the name beside it, so a line must carry one of
    each.**

`tools/check_irq_routing.py` is unchanged at **9 claims / 57 mutations** and passes with the same note as
500 — every file-scope record the three files declare is in the image — which is also why the entry
image's `.bss` does not move in this step: 501 adds no file-scope storage, only locals that are published
where they are read.

## The image, and the arithmetic that says it moved

The four-step build is green. Artifacts (the ones that ran):

    out/stage90/xnu_arm_entry.bin  0352455abbb66b9ab5f36da5045d0a4ed884e6cc167f164a46042142d569dc05
    out/stage90/stage90.bin        d4915fcf6c1b271f29f77918ca64aa36daf744cf6ccbb0268ee926b45951a6cd
    out/stage90/stage90.img        75af37970afe3923faf34d302abf6a87aca42790610ffde5502087f347f96556
    out/stage90/stage90-qcdt.img   38ba7e278bcd4f13f77b7f51fa7b9ae20bc8c5f21a61b9ae5c8c559271433934

against 500's `c0a466cd…`, `8c91ce7d…`, `f3e988b8…`, `2f39e8e5…`. The entry image's `.text` is
**5280296** (500: 5272128, +8168 — 501's two blocks are inline code with forty published keys), `.data`
206804, `.bss` **362824 unchanged** for the reason above, 27 undefined and 59 wraps unchanged, and the
payload's `kernel_size` 5982656 with the qcdt image 8507392 as in 500.

The edit that corrected the comments after the run (four places in the driver: the define block, the two
clauses of the read block, and the accessors' header) is proved comment-only by rebuilding: all four
artifacts are **byte-identical** to the ones above, and the check counts in that build are the ones
quoted here.

## The rest of the boot is unmoved

  - The OS console block is **byte-identical** to 497's, 498's and 500's: 1288 characters, sha256
    `a0593af02867eac3ddf107a6523292ea9e5016861188faa0a2c796d3ce54cc8d`.
  - 500's three deliveries reproduce exactly in shape: `_isr_calls` 1/2/3 on `_isr_intid` 0x28 with
    `_isr_agree` 1, `_isr_rearmed` 1 → 2, `_isr_masked` 1, `_isr_ctl_after` 0x1 / 0x1 / 0x7,
    `_isr_tval` −145 / −83 / −81 (500: −141 / −82 / −90), and `_arm_rc`/`_arm_line_rc` 1 with their
    zeroes published before the registration.
  - The OS's own timer ran to `_irq_timer_count = 0x800` again, `_irq_late_count` 0, `_irq_other_count`
    never taken — and `_rt_cntv_ctl_cpu` is the independent reading that the decrementer was enabled and
    unmasked while all of this was going on.
  - The log is 561874 bytes, `No errors detected`, and the device came back on its own inside the capture
    window.

## What is owed

  - **The frame's second line, intid 39, is unarmed, and this step changed what arming it would mean.** The
    tree's `<0 7 0x4>` is the frame's second interrupt and the binding calls it the *virtual* timer's; the
    machine's virtual timer is the one XNU's own decrementer uses, on intid 20 as a PPI (measured, 2048
    deliveries in every run since 498). Whether intid 39 is a second signal for the same frame is now a
    question with a datum attached: the frame's second *view* is empty, so a step that armed 39 would have
    to say which device it expects to assert it.
  - **What the frame's `0x038` actually is.** The step says what it is not. A step that writes is the only
    one that could say more, and the register is the OS's own decrementer's, so that step needs a design
    that cannot move the kernel's deadline — a masked write and a read-back, or a test on a different
    frame number.
  - **The `AckC`/EOI question for a level line** is still answerable and unasked: the dispatcher writes
    `GICC_EOIR` before it calls the client, the frame's line is level, and `_irq_late_count` is read for
    the OS's line and not for this one.
  - **The registry's capacity stop has still never been taken** (4 slots, `_irq_cli_capacity` = 4, three
    used at the end of this boot).
  - Unchanged from 500: claim 16's `_arm_ticks`-derivation clause reading one level of assignment; the same
    citation rule pointed at the other files our sources cite (`IOService.cpp:6337`/`:6290`, the nine
    `IODeviceTreeSupport.cpp` sites, `msm8974.dtsi:153-167`, `IOCPU.cpp`); the build-time `out/xnu_asm_obj`
    still unstamped with its configuration; 497's conditional-clause mutation that needs two images; the
    unregister guard's asymmetry; a timeout that outlives its asker; from 498: `/timer`'s second
    definition, the other device nodes, the two services the catalogue answers with nothing,
    `MSM8974RootResource`'s `state0 = 0`, a name/class reader wider than eight characters, the release as
    a reading, `vm_fault`, 488's flag-list, 490's frames band, `xnu_live_dec_same`.

## Safety

Non-persistent `fastboot boot` only, through `preflight_boot_check.sh --allow-xnu-entry` and then
`run_and_capture.sh --allow-xnu-entry`; nothing was flashed. The step is read-only on the device: it maps
two of the node's `resource` objects through the OS's own route and reads their registers, and the one
register it reads that belongs to the OS — the virtual countdown — is XNU's own decrementer, so a *write*
to it is the thing that could move the kernel's deadline and a write is the thing the step's claim
refuses. The property that makes that structural rather than intended is the region clause: a build in
which the 501 block stores through either mapped base does not build. The run returned on its own inside
the capture window with `No errors detected`, and neither recovery net was needed.
