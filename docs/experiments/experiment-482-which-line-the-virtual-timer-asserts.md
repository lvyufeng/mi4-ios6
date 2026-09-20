# Experiment 482 — which line the virtual timer asserts, and where Apple's dispatcher would go

Date: 2026-09-20
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `stages/stage90/xnu_arm_boot/entry_gic.{h,c}` (new), `entry_timebase.{h,c}`,
`entry_stubs.c`, `build_entry.sh`, `tools/check_gic_routing.py` (new)

**Result: the virtual timer's interrupt is INTID 20 on this SoC, measured twice with a per-candidate
masked control, and neither of the two PPIs the payload's own device tree names (18 and 19) is the
line it asserts. The distributor is left exactly as the probe found it, the CPU interface is restored,
and the boot is unperturbed: the OS console is byte-identical to 481's, the block set is unchanged
(70 entered / 13 returned / 57 parked), there is no trap, no panic, and the device came back to Android
on its own.**

Three things are in the log that no previous run had: **which line** the countdown asserts
(`0x100000` appeared pending under the unmasked countdown, under a masked control that saw nothing,
and again on the second candidate), **what Apple's IRQ dispatch actually does** in this image
(`fleh_irq_handler` loads five `cpu_data` words into `r0..r3` and `r5` and `blx`es the fifth, at
0x80015abc — and that fifth word has never been written), and **the kernel's own page tables carrying
this probe's mapping** (`.bss`-side pmap checksums moved in all four places they are carried).

## What 481 left, and why the answer could not be guessed

481 gave the kernel a working decrementer and masked its interrupt on every write
(`CNTV_CTL = ENABLE|IMASK`, measured `0x00000003`). Two routes could deliver it, and neither could be
chosen from the source:

  - **The FIQ route.** `locore.s:147-151` picks slot 7 by `__ARM_TIME__`, which is defined nowhere, so
    XNU's timer is supposed to arrive on FIQ through `fleh_decirq_handler` → `bl rtclock_intr` — a path
    that needs no `cpu_data` handler at all. 143 measured this dead: with the physical timer's line
    (19, the number the payload's own IRQ path acknowledged as `IAR = 0x13`) in Group 0, enabled at
    both ends, `CPSR.F` clear and the timer measurably fired, **no FIQ is taken**.
  - **The IRQ route.** Slot 6. And in *this* image slot 6 does not hold Apple's handler: it holds
    `entry_stubs.c`'s `fleh_irq` (0x80007808), which sets a flag and calls
    `entry_epilogue("exception: irq")` — a report and a stop, which is what 308's run ended on.
    Apple's `locore_fleh_irq` is linked in at 0x80015954 and is in **no vector slot**.

Since the routing question is "which line", and both routes end at a line, the step that has to come
before any handler is a measurement of the line itself — with delivery still impossible. That is this
step. It changes no handler, no slot, and no mask: `STAGE90_CNTV_ARM_MASK` stays on every write, and
the only register the probe enables is one it disables and clears again before it returns.

## What the probe does, and the three guards

`entry_gic_probe()` runs from inside the timer's own owner — the first
`stage90_tbd_set_decrementer`'s guarded sample block, which is the one place in the boot where a
countdown is known to be armed, the live channel is up, and the kernel has already asked for a
deadline. It:

  1. **maps the distributor** into XNU's own L1 — `entry_mmio_section(0xf9000000, 0xf9000000, …)`, a
     one-megabyte section through L1 index `0xF90`, with the live channel's own attribute (`0xc` on
     this device: the first Strongly-ordered encoding read out of `PRRR`). This is a real precondition
     and 308 is why: XNU's page tables do not map the GIC, so without it every read below is a fault.
     `xnu_live_gic_slot_before = 0x00000000` is that fact measured again, and
     `xnu_live_gic_desc = 0xf901040e` is the descriptor installed.
  2. **reads the pre-state read-only** — `GICD_CTLR/TYPER/ISENABLER0/ISPENDR0/IGROUPR0`,
     `IPRIORITY0+16`, `ITARGETSR0+16`, `GICC_CTLR/PMR` — and every one of the eight is the same
     number the payload's own pre-XNU snapshot logged earlier in the same boot: `1`, `0x468`
     (`SecurityExtn` set, four CPU interfaces), `0x7fff`, `0x20400000`, `0`, `0xa0a0a0a0`,
     `0x01010101`, `1`, `0xf0`. Two independent readers, one machine — the check that the section
     this probe installed is the GIC and not an alias of something else.
  3. **guards three times, in order.** `cpsid if` for the whole window (`gic_irq_mask` / `gic_irq_restore`
     over `mrs`/`msr cpsr_c`, not `ml_set_interrupts_enabled`, which would set
     `cpu_data->interrupts_enabled` as a side effect of a probe that must not change what it measures);
     `GICC_CTLR` and `GICC_PMR` cleared at the CPU interface **and read back** (`0` and `0`, so the
     guard is a reading); and both candidate INTIDs disabled and cleared **before** the interface is
     restored, so there is no instruction in which a pending line and a live interface coexist.
     `GICC_IAR` is never read: a read would acknowledge, and this step acknowledges nothing.
  4. **runs each candidate twice** — once with the countdown masked (the negative control) and once
     with `IMASK` clear (the measurement) — and **clears the bits it raised** before returning.

## The measurement

Two candidate INTIDs — the payload's own two timer PPIs, 18 and 19, which the check re-reads out of
`stages/stage90/gic.c` — produce the same answer:

```
 xnu_live_gic_id18_isenabler=0x00047fff     bit 18 enabled: the candidate's own INTID
 xnu_live_gic_id18_pend_masked=0x20400000   masked window: no change from the pre-state
 xnu_live_gic_id18_ctl_masked=0x00000007    ENABLE|IMASK|ISTATUS - the countdown reached zero
 xnu_live_gic_id18_pend_unmasked=0x20500000 unmasked window: bit 20 appeared
 xnu_live_gic_id18_ctl_unmasked=0x00000005  ENABLE|ISTATUS
 xnu_live_gic_id18_pend_added=0x00100000    what this candidate raised
 xnu_live_gic_id18_pend_after=0x20400000    cleared again: back to the pre-state
 xnu_live_gic_id19_isenabler=0x00087fff     the second candidate, on its own bit
 xnu_live_gic_id19_pend_masked=0x20400000   its control is clean, because 18 cleared what it raised
 xnu_live_gic_id19_ctl_masked=0x00000007
 xnu_live_gic_id19_pend_unmasked=0x20500000 bit 20 again
 xnu_live_gic_id19_ctl_unmasked=0x00000005
 xnu_live_gic_id19_pend_added=0x00100000    the same line, measured independently
 xnu_live_gic_id19_pend_after=0x20400000
 xnu_live_gic_pend_added18=0x00100000
 xnu_live_gic_pend_added19=0x00100000
 xnu_live_gic_pend_added=0x00100000         one bit, raised twice
 xnu_live_gic_timer_intid=0x00000014        -> INTID 20
 xnu_live_gic_hits=0x00000000               and it is neither of the payload's two
 xnu_live_gic_ticks_elapsed=0x00027282      160386 counts across four windows of 400000 iterations
 xnu_live_gic_restore_isenabler0=0x00007fff
 xnu_live_gic_restore_ispendr0=0x20400000   == ispendr0: the distributor is left as found
 xnu_live_gic_restore_cpu_pmr=0x000000f0    == cpu_pmr
 xnu_live_gic_restore_cpu_ctlr=0x00000001   == cpu_ctlr
```

**The reading.** The only thing that differed between a window that saw nothing and a window that saw
bit 20 is `CNTV_CTL.IMASK` — same countdown (`0x1000` ticks), same spin (400000 iterations), same
distributor state. `CNTV_CTL.ISTATUS` is set at the end of both windows, so the countdown really did
reach zero with the line masked and with it unmasked, which is what makes the masked window a control
rather than a second negative. Therefore **the virtual timer's line is INTID 20**, and the pair the
payload's device tree names (`interrupts = <1 2 0 1 3 0>`, PPIs 2 and 3 → INTIDs 18 and 19) is wrong
for the virtual timer: 18 never went pending across two countdowns, and 19 is the *physical* timer's
line — `gic_timer_last_iar = 0x00000013` with `gic_timer_last_timer_ctl = 0x00000005`, taken by the
payload's own IRQ path earlier in this same boot. So what 483 has to install a handler on is 20, and
the fixture's timer node is a second thing to fix.

Two smaller facts come out of the same table. **`imask` is the only ingredient that mattered** — the
distributor marks a PPI pending whether or not it is enabled, so `ISENABLER0`'s bit for the candidate
was never the variable (both candidates' enables read back correctly and changed nothing else). And
**`pend_after` says the pending bit did not follow the line back down**: `0x100000` was still pending
after the countdown had been re-masked, which is what an edge-configured INTID looks like and what a
level-configured one does not. `GICD_ICFGR` was not read, so that is an *inference from two readings*
and not a measurement; 483 programs the line and should read the register.

## The second candidate, and the two corrections the first run's own numbers forced

The first 482 run (log `/tmp/cancro-482-last_kmsg.txt`, 462631 bytes) measured the same bit 20, and
its numbers are what found the probe's two defects:

```
 xnu_live_gic_id18_ctl_masked=0x00000003    <- read before the spin
 xnu_live_gic_id18_ctl_unmasked=0x00000005  <- read after the spin
 xnu_live_gic_id19_pend_masked=0x20500000   <- was already dirty: 18's bit 20 was still pending
 xnu_live_gic_restore_ispendr0=0x20500000   <- != ispendr0 (0x20400000): the probe leaked it
```

  - **The pair was two different moments, not two values of one bit.** `ctl_masked` was read before
    its spin and `ctl_unmasked` after it, so `0x3` against `0x5` reads as "IMASK clears ISTATUS" and
    was only "the first read happened earlier". Both are now read after their own spin, and the
    corrected pair (`0x7`, `0x5`) differs in `IMASK` and nothing else.
  - **Each candidate cleared its own INTID instead of the line it raised.** The countdown asserts 20,
    so `ICENABLER0/ICPENDR0 = 1 << 18` did nothing about it: the bit stayed pending into the second
    candidate's negative control (making its `pend_masked` dirty) and stayed pending in the machine
    after the probe returned. The candidate now clears `added` — the bits that appeared while its own
    countdown was the only variable — and the corrected run's `id19_pend_masked` and
    `restore_ispendr0` are both back at the pre-state.

The corrected run is `/tmp/cancro-482b-last_kmsg.txt` (462862 bytes), and the difference between the
two logs is exactly the six keys the fix added: 1139 → 1145 `xnu_live` records, 5098 → 5104 lines,
462631 → 462862 bytes (+231), with every other line either identical or a pointer value that varies
run to run.

## The mapping, and the four page-table checksums that carry it

Installing a section in XNU's L1 is not free of consequences, and the run shows the consequence rather
than asserting it: every pmap checksum this boot prints moved —

| key | 481 | 482 |
| --- | --- | --- |
| `stage90_xnu_arm_init_post_pe_bootstrap_checksum` | `0xfd71acd9` | `0xfd1d0164` |
| `stage90_xnu_arm_vm_init_full_pmap_checksum` | `0x7461ee72` | `0x740d43cf` |
| `stage90_xnu_arm_vm_init_high_va_code_exec_checksum` | `0x6543dd7b` | `0x652f70c6` |

— and the four carriers of each value (`stage90_xnu_*`, `loader_xnu_*`, `stage90_xnu_entry_stub_*`)
agree with each other inside the run, so this is one changed page table seen four times and not four
checksums. The probe's mapping is therefore in the kernel's *own* L1, which is also why the probe can
read the distributor at all: 308 measured that XNU mapped no GIC, `slot_before = 0` measures it again
on hardware, and the eight pre-state values then matching the payload's own snapshot is what says the
descriptor the probe installed points at the GIC.

## The console, the layout, and the keys

The OS console region (from the `[os-console-459]` marker to the first live record after it) is
**byte-identical to 481's and to 480's**: 26 lines, 131085 bytes. The block set is unchanged at
**70 entered / 13 returned / 57 parked** (`xnu_live_block_seq` ending `0x46`, `_returns` ending
`0x0d`), and the run has 0 `stub_hit=`, 0 `xnu_live_undef_*`, 0 `xnu_entry_panic_*`, no `pid 1
exited`, and `No errors detected` on the last line. The timer chain 481 closed is still closed:
`dec_written = setpop_returned = 0x7fffffff`, `dec_readback = 0x7ffffff9`, `dec_count == setpop_count`
at 2/4/8/`0x10`, and the countdown is still running through the sample (`CNTV_TVAL` `0x000ffffa` →
`0x000ffcce`).

One 481 number *does* move between runs and should not be read as a change: `xnu_live_dec_ctl` is
`0x3` in 481 and in the first 482 run and `0x7` in the second — a `CNTV_CTL` read taken *before* the
probe runs, whose `ISTATUS` depends on whether the payload's own earlier countdown had expired by that
instruction. It is a timing-dependent reading, not a state difference.

Build and layout (the entry image is unchanged in every size that the payload consumes):

| | 481 | 482 |
| --- | --- | --- |
| `.text` | 5232512 | **5237824** |
| `entry_gic.o`'s own two inputs | — | `.text` `0x5c4` + `.rodata.str1.4` `0x4be` |
| `.bss` | `0x80533a80 .. 0x8058b5c8` | unchanged (359240 bytes) |
| image bytes | 5454452 | **5454452** (unchanged) |
| headroom | 1526328 | 1526328 |
| entry base / point | `0x80000000` / `0x80000074` | unchanged |
| log | 461190 bytes / 5060 lines / 1101 records | 462862 / 5104 / 1145 |

`.text` moved by 5312 while the two input sections that changed account for 2690 of it (there was no
`entry_gic.o` at all in 481) and, between the step's own two builds, by 4256 while those same inputs
account for 344. The rest is the aligned-fill term, and the image says so: the largest `*fill*` entries
here are `0xfe8`, `0xfc4` and `0xf74` — three nearly-page-sized gaps — so a 172-byte input can move
the section total by thousands. The shift itself is measurable and exactly 0xAC: every address after
`entry_gic.o` moved by 172 between the two 482 builds (`__wrap_fiq_context_init` `0x80008704` →
`0x800087b0`), which is that object's own growth. The entry image is reproducible: two consecutive
builds give `xnu_arm_entry.bin` sha256 `d7d210b57b3482b6988fedd51e692d9de9e2569fedab368ffac2b995fb8f1a17`
and `xnu_arm_entry.elf` `a8f66c64c6ac95f094d9bf923d4fd4378cc5f1123d5ddfc7a6860f2e63b848c2`, and a
comment-only edit to `entry_gic.{h,c}` after the run left both byte-identical (`sha256sum -c`).

Undefined symbols: **26**; `--wrap`ped names: **49** (44 reached by a branch, 1 same-object-only, 1
never called here, 3 by address only). Payload: `stage90-qcdt.img` 8474624 bytes sha256
`a3bfeacad02a4f03a3ed2d099c164d6b4d1f42c61b7afcfd413d935b6a71dfe5`, `kernel_size = 5949716`.

## The check, and the defects this step's own work caught

`tools/check_gic_routing.py` is new: six claims (the register offsets against the payload's own copies
in `gic.c` and `xnu_msm8974_fiq_probe.c` — including `STAGE90_GICD_ITARGETSR0` against the payload's
`GICD_ITARGETS0`, one register with two spellings, recorded rather than silently mapped — the two
bases against both the payload's constants and its own `ok &= (distBase == …)` assertions, the two
candidate PPIs, the vector page's slots 6 and 7 against this image's own symbols, the shape of
Apple's dispatch, and the probe's guards in the order that makes them guards), **33 mutations refused
by `--selftest`**, four of them mutations of the linked image's own disassembly, and the whole thing
runs from `build_entry.sh` twice (verbose, then `--selftest`) so a broken one stops the build.

Nine defects were found while writing it and while reading the run's own numbers — this step's count
of *itself*, which is where most of the work went:

  1. **A parser whose `\s+` crossed a newline.** `^#define\s+(\w+)\s+([^\n]+)$` on the header, after a
     multi-line comment had been replaced by a space, matched the *next* line's `#define` as the
     value: `STAGE90_GIC_DIST_BASE` was absent from the parse and the claim printed
     "`0x-1` in `entry_gic.h`". The tell is that the failure named a value that is nowhere in any file.
  2. **A claim satisfied by a comment.** `fleh_irq`'s body *mentions* `entry_epilogue` in prose ("the
     MMU does not come off until `entry_epilogue` is inside"), so testing `"entry_epilogue" not in
     body` on the raw body passed even after the mutation deleted the call. Comments are stripped
     first, and the test is for a *call*.
  3. **A window that was a prefix, not the function.** The disassembly window was `0x60` = 24
     instructions and the `blx r5` is the 25th, so the claim failed while describing itself as a
     statement about the shape of the dispatch. Widened — and the widened window is stated as a
     *bound* rather than the function's extent, because `nm -S` carries no size for either handler and
     the assembler's `L_*` labels sit inside them, so "the next symbol" is not the end either.
  4. **A mutation that changed the facts and left the claim standing.** The mutation selected
     instructions with `LOAD_RE` alone, which matches `str` as well as `ldr`: it swapped the
     destination registers of a `str` and of a load that were not in the window, so the facts changed,
     the claim held, and `--selftest` reported ACCEPTED. The claim and the mutation now share one
     function (`dispatch_window`) so they cannot look at different instructions.
  5. **A mutation that was not a violation.** `guard_after_first_probe` re-inserted the guard after
     `t_a = stage90_cntvct_read()`, which is *before* every candidate — so the mutant satisfied the
     property and the selftest accepted it, which is the selftest being right and the mutation being
     useless. It now moves the guard past the probes.
  6. **One pair, two readings taken at different points** (the `ctl_masked`/`ctl_unmasked` pair above).
  7. **A probe that cleared the wrong bit** (above), which cost the second candidate its negative
     control *and* left a pending line in the machine.
  8. **A fingerprint that embedded a signature**: the guard block's needle was
     `gic_probe_candidate(STAGE90_GIC_TIMER_PPI0)`, with the closing parenthesis, so adding the
     pre-state argument made it stop matching and the build refused for the wrong reason — a refusal
     about a stale pattern rather than about the code.
  9. **An argument in a comment that a measurement contradicts.** `entry_gic.h` justified the spin with
     "481's 813 ticks over 20000 iterations, so ~24 ticks per 1000" — the division is 40.65, not 24 —
     and then used that rate for a loop that is *not* 481's: 481's counter is a register and this one
     is `volatile`, and this probe's own run measures ≈100 ticks per 1000 iterations
     (`ticks_elapsed` 160386 over four windows of 400000). Both numbers are now in the comment with
     their sources, the margin is the one the run measures (`ISTATUS` set in both windows, and the
     elapsed count bracketing them), and the check refuses the link if the probe stops bracketing its
     windows with `CNTVCT`. The comment also claimed "the new check requires the two of them to keep
     [the same shape]" — **there was no such check**, which is the same defect from the other side: a
     claim in a comment is not a check, and a claim *about* a check is worse.

## What 483 has to do

The routing decision is now made of measurements rather than of a quotation, and it has four parts:

  1. **Install the handler on INTID 20**, not 18 — and fix the fixture, whose timer node
     (`interrupts = <1 2 0 1 3 0>`) names the physical timer's line and one line nothing asserts. The
     payload's `gic.c` assertions should be re-derived from the measurement rather than the tree.
  2. **Give `cpu_data->interrupt_handler` a real value** before slot 6 can hold Apple's dispatcher:
     `fleh_irq_handler` at 0x80015abc loads `[r4, #180]` into `r5` and `blx r5`, and this image has
     never stored anything there — so moving Apple's handler into slot 6 without that is a branch to
     address 0. That is `IOCPUInterruptController`'s job, and it is the same shape as 481's
     registration: a table handed over at Apple's own point.
  3. **Read `GICD_ICFGR` for the line** before arming it, because the pending bit did not follow the
     line back down in this run — an inference that a register read can settle.
  4. **Lift `STAGE90_CNTV_ARM_MASK`** only after 1-3, and keep the ordering the probe's guards already
     have: candidates disabled and cleared before the CPU interface is restored.

**483 did all four, and one of them came back negative.** Items 1, 2 and 4 are as written: the handler
is installed on 20 through Apple's own `ml_install_interrupt_handler`, the line is enabled at the
distributor, and the countdown's mask comes off last — measured by an interrupt actually arriving
(`first_iar = 0x14`, `timer_count` past 2048, `setpop_count` 16 → 4096). Item 3's premise was that the
register could settle edge-versus-level, and it cannot: `GICD_ICFGR1` (INTID 20's word, `0xc04`) reads
**`0xffffffff`**, and no GICv2 configuration field is `0b11` — the bits are not storage on this SoC.
Also corrected here rather than there: the four GIC accessors this step put in `entry_gic.c` moved to
`entry_irq.c` in 483, so that the probe and the handler address the GIC through one definition, and the
claim this document's check made about slot 6 turned out to be about a *coupling* the machine does not
have — `entry_vectors.s` moves the slot unconditionally in 483, and the flag switches one distributor
bit. Still owed from earlier steps, unchanged: which of `arm_fast_fault`/`vm_fault` serviced 480's write
fault; why the `VM_FLAGS_FIXED` stack allocation at `0x26E00000` is refused; 474's `thread->map = 0`
moment; 448's `_bad` slots as a named pair; the pthread table's other ~34 slots;
`osfmk/kperf/kperfbsd.c`; the untraced build's `entry_stubs.c` compile errors; and
`thread_bootstrap_return`.
