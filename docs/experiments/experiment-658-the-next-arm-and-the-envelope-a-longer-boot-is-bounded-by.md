# 658: the next arm, designed before the press — and the envelope a boot that goes further is bounded by

The owed press is blocked on one physical thing: the neighbour `33e80afe` is still parked in fastboot, so a
run fired now is refused at the ambiguity guard and boots nothing (readiness row 4, every check since
2026-09-24 19:06). The watcher is armed and gated on both conditions, so the press will fire on its own.

That leaves exactly two things worth doing without the device, and this step does both:

1. **Pre-register the step from the press's reading to the next arm** (§1–§2), so that no arm is designed
   after the fact from its own result — the discipline 651/653 established for the pre-registration and
   654 for the rehearsal.
2. **Measure the envelope that makes 「不能变砖」 hold as the frontier advances** (§3). This is the clause
   that gets *weaker* with progress, and the user's constraint on it is absolute, so it is the one to
   spend host-side time on while the press waits.

Nothing was built, nothing was run, no device was touched, and **no file the press fires was edited** — the
watcher fires the runner *by path* and calls the readiness tool by path, so both are closed to edits while
it is armed.

## 1. The press's reading → the next arm

Both readings come back in the same log, so the decision is a two-dimensional cell and not a single guess.
The reader prints the pair (657 §4); the frontier is read from `slot_post_calls` and `poll_seq`
(`run_and_capture.sh:1669` and `:1087`, both of which have a **PASS** row, so the success direction is
read rather than inferred from silence — the 630 rule):

| the pair | the frontier | what it establishes | the next arm |
| --- | --- | --- | --- |
| `a1 == rtcpre_pop` ⇒ `STALE LINE, WRITTEN OUT` | **past** the `pop` | the operation acted: one `mcr` wrote the dirty line out **and** invalidated it (657 §4.2), and the `pop` missed | **none for this death.** The frontier moves to whatever XNU hits next, and the next step is to read *that* — the arm in `out/` is unchanged |
| `a1 == rtcpre_pop` ⇒ `STALE LINE, WRITTEN OUT` | **dies at** the `pop` | the clean reached the line, so the invalidation happened at **L1** — and the `pop`'s word still came from below it | **the level walk** (§2): an L2 copy answered the refill |
| `a1 == b1` ⇒ `CLEAN LINE` | **past** the `pop` | the line was already **clean**, so there was nothing to write out and the invalidate was the whole repair | none for this death; but the prediction to re-read is *why* the line was clean, because `a1 == rtcpre_pop` would then never fire on any arm |
| `a1 == b1` ⇒ `CLEAN LINE` | **dies at** the `pop` | the operation was **inert** with `SCTLR.C` clear (638 §3's second row), or the line's contents were re-established before the step-5 read | **do the maintenance with the cache set**, or the level walk with `CSSELR` written (§2) — both are maintenance-*reach* arms, and this cell is the only one where reach is the suspect |
| `a1` is neither ⇒ `CHANGED` | either | the word at that offset is not this boot's deadline | compare `a1` by hand against the death's `pc`/`r11`, as 652 did for `0x05006e74` |

**The two rows that need a new arm are the two that share `dies at the pop`,** and they need *different*
arms — which is the point of reading the pair and the frontier together rather than either alone. The
envelope's own `slot_post_calls` row is a PASS, so a success cannot arrive as an `UNREAD`.

## 2. The arm the L2 cell needs, designed here and **not** built

If the `pop`'s word came from below the L1, the missing step is not a different routine: it is the step
534 found missing from **every set/way loop in this kernel** — *the level is selected, not assumed*.

The defect 534 measured, restated because this arm is its fix:

* The whole XNU tree has **exactly one `CSSELR` writer**, `osfmk/arm/machine_cpuid.c:88`, reached only from
  `cpuid.c`'s `do_cacheid` (`:214`, which reaches it for L1, L3 and L2 in turn) — and it **never selects L1 back**.
* Every set/way sweep (`caches_asm.s:245/257/280`, `start.s:285/298`) writes `mcr p15,0,r0,c7,c14,2`
  assuming a level's geometry in the operand, with no `CSSELR` write of its own. The loops differ only in
  which level's geometry is baked in — the same assumption in two spellings.
* The ARMv7 answer is in the same kernel tree as the device: `arch/arm/mm/cache-v7.S`'s
  `v7_flush_dcache_all` walks `CLIDR` and **writes `CSSELR` before each level's `CCSIDR` read and its
  sweep**, switching back to level 0 at the end.

So the arm is: at the seam, **save `CSSELR`**; for each level `CLIDR` reports a data cache at, write
`CSSELR` = that level, read `CCSIDR` for that level's geometry, and sweep it with clean-and-invalidate;
then the existing restore of the two words; then **restore `CSSELR`**. Two design notes, both of which are
this project's own defect classes and both of which are cheap to get right now and expensive later:

* **Restoring `CSSELR` is not tidiness.** `do_cacheid` leaves it at the L2 and the rest of the kernel's
  cache code assumes whatever it left. An arm that fixes the `pop` by leaving `CSSELR` moved fixes one
  quantity by redefining another — the one-value-two-definitions shape, in a register.
* **The arm must not be built until the press has read.** Building replaces the payload in `out/`, and an
  armed watcher fires the runner by path: a build under an armed watcher would send bytes that no
  pre-registration describes, which is 654's `TREE` verdict and its reason for existing.

The new cells this arm needs published — the saved and restored `CSSELR`, and the level count `CLIDR`
reported — are **not** in the current arm, so this arm is a new pre-registration (a new number), not an
edit to 653.

## 3. The envelope: what makes 「不能变砖」 hold, and how it changes as the boot advances

The user's constraint is absolute, and it is the one clause that gets *weaker* as the frontier moves: the
further the boot gets, the more of XNU's own code runs, and the more of the tree's device handling becomes
reachable. So it is measured here rather than assumed. Five pieces, each a reading:

**(a) The host half, and it is a rule rather than a check.** `fastboot boot` only, never `flash`. Nothing
this project sends is ever written to storage by the host, so no outcome of any run can change what the
phone's storage holds. That held for the 2026-09-24 press and it is the reason bricking is *impossible by
construction* rather than merely unobserved.

**(b) The payload's static tripwire, re-run by hand on the payload this press will send.** Both halves, on
`out/stage90/stage90.elf` (6077388 B, the arm's own build):

```
arm-none-eabi-nm -a out/stage90/stage90.elf | grep -icE 'sdcc|emmc|\bmmc\b|ufs|partition|flash_|nand'
  -> 0                                    (no storage symbol)
tools/check_storage_refs.py out/stage90/stage90.elf
  -> movt high halves checked: 0xf980-0xf98f ; literal words checked: 0xf9800000-0xf98fffff ; none
  -> exit 0
```

**(c) The frames the run actually touched, measured from the 574 capture.** Every device frame this run
ever published is one of two, and the whole set is four keys:

```
xnu_live_gicdrv_phys0        = 0xf9000000      the GIC distributor
xnu_live_timerdrv_phys0      = 0xf9020000      the timer
xnu_live_timerdrv_frame_phys = 0xf9021000
xnu_live_timerdrv_v2_phys    = 0xf9022000
```

**(d) The window the kernel may address at all.** `xnu_entry_args_memSize = 0x01000000` — **16 MB** —
with `physBase == virtBase == 0x80000000`, so the identity window is `[0x80000000, 0x81000000)`. Everything
outside it is a fault entry.

**(e) So where storage is, and what a store to it would do.** On MSM8974 the whole `0xf9800000`–`0xf98fffff`
megabyte contains only storage controllers (`sdcc@f9824000` eMMC, `sdcc@f98a4000`, `sdcc@f9864000`,
`sdcc@f98e4000`, and their `sdhci` companions). It is outside the 16 MB kernel window and outside both
frames the run mapped. A store there therefore takes a **translation fault**, which the payload's vectors
route to `sleh_abort(regs, T_DATA_ABT)`, which in kernel mode panics — and a panic ends in
`MACH Reboot`. 652's run measured that path end to end: the phone **returned to Android by itself** and was
not hung, altered, or bricked.

**Two honest gaps, named because the constraint is absolute and because a guarantee that is stated more
broadly than it is checked is this project's most expensive defect class.**

1. **The symbol half of the tripwire is scoped to `stage90.elf` and is blind to the entry image.** The
   payload jumps into `xnu_arm_entry.elf` — 6.7 MB of XNU, and the thing that actually runs. `nm` on that
   image matches the same pattern **25 times**. And the 25 are **all false positives**: the pattern's
   `ufs` matches `bufsize`, `bpf_bufsize`, `nkdbufs`, `ubc_upl_maxbufsize` … and its `partition` matches
   `IODTNVRAM::getNVRAMPartitions`/`readNVRAMPartition`/`writeNVRAMPartition`. **So the scope cannot simply
   be widened — the check would refuse a clean payload**, which is worse than a narrow one: it would train
   its reader to pass over it. The patterns need anchoring (a word boundary and a name shape), and that is
   a change to the gate, so it waits for a window with no watcher (654's rule, and the same reason 654 §6
   is still open).
2. **The mapping half is checked by nothing.** `check_storage_refs.py`'s own docstring says so: *"The real
   guarantee is not this tool … the mapping is what actually prevents the write."* That mapping claim rests
   on `arm_vm_init`'s arithmetic (`osfmk/arm/arm_vm_init.c:381-382`, the V==P window's extent) and on
   `entry_mmio_section` having **exactly one call site** (`entry_gic.c:377`, `0xf9000000`, one 1 MB
   section). Both are true of this payload. **Neither is re-read by any tool before a press**, and that is
   exactly where progress will press: `xnu_entry_jump.c:161-162` hands XNU the **device tree**
   (`a->deviceTreeP`, copied in, and the payload is the `-qcdt` image precisely because the tree goes with
   it). The day a boot gets far enough for IOKit to match a node and call `ml_io_map` on a tree address, a
   *new* mapping appears that no check in this chain would see before the press — the early-warning tool
   reads code and literals, not what a runtime loop maps.

**The conclusion for the goal, stated as narrowly as the measurements support it:** for *this* payload the
envelope holds by construction, and the two ways it could weaken are named with the check that would catch
each — one of which (the symbol patterns) is already a defect, and one of which (the mapping) does not yet
exist.

## 4. What this does not do

* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** No boot was observed. `poll_seq`
  still stops at 2 and `slot_post_calls` is still absent since 520.
* **It does not fire, arm, or touch the device.** The press is still blocked on the operator and nothing
  here changes its state.
* **It does not build the §2 arm, and deliberately not** — building replaces the payload under an armed
  watcher (§2).
* **It does not claim the L2 candidate is the likely one.** §1 registers it as one of four cells; the
  measurement that would rank them is the press.
* **It does not edit the gate, the runner, or the readiness tool.** Every item in §3's two gaps is a
  change to one of those, and all three are closed while the watcher is armed.
* **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet, and nothing here meets it.

## 5. Safety

No device action: no `fastboot`, no `adb`, nothing sent anywhere, **nothing written to storage**. Every
reading is a host-side read — `nm`/`objdump` on the frozen arm's images, `check_storage_refs.py` in its
read-only mode on the same two images, greps and the runner's own extractor over the archived 652 capture,
and the sources. The only file written is this document. **No source, no image, no byte of `out/`** — and
that is a constraint here rather than a preference: a press watcher is armed
(`/tmp/r654/press-on-clear.sh`, waiting for the neighbour `33e80afe` to leave the bus), it fires the runner
by path and calls `tools/verify_press_ready.sh` by path, so both are closed to edits while it is armed.
`fastboot boot` only, never `flash`.
