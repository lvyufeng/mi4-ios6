# Experiment 94 — Stage90 Phase 0: the recovery net proved on hardware, and the two bugs the baseline run found

Date: 2026-09-17 (runs 00:27–01:43 UTC)
Commit under test: `a683c9d` and the two commits before it (`3b32ee4`, `07ea5a5`)
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`

Three builds were used, in this order. `STAGE90_HANDOFF_MODE = HARD_SKIP` and
`STAGE90_ENTRY_LADDER_LEVEL = FULL` throughout; the only varying switch is named per run.

| # | Captured | Build switches | Result |
| --- | --- | --- | --- |
| 1 | 00:27 | `-DSTAGE90_HW_WATCHDOG_SELFTEST=1` | reported **NOT armed**; device returned unaided |
| 2 | 00:32 | same, after the truncation fix | reported **NOT armed**; device returned unaided |
| 3 | 00:35 | same, after the direction fix | **armed and counting**; device returned unaided |
| 4 | 00:37 | `-DSTAGE90_EXCLUSIVE_PROBE=1` | `kernel_entry returned failure` — device-tree child count |
| 5 | 00:42 | same build as 4, re-run | identical failure |
| 6 | 00:46 | same, after the child-count fix | **hang** in the Mach-O loader; hardware watchdog reset at ~28 s |
| 7 | 00:52 | same, after the loader fix | `kernel_entry returned success` |
| 8 | 01:43 | `-DSTAGE90_DEADMAN_SELFTEST=1 -DSTAGE90_HW_WATCHDOG=0` | **dead-man fired**, dumped the interrupted PC, rebooted; device returned unaided |

Runs 1–7 are the ones the section headings below describe. Run 8 is a later addendum: it was
run after this log was first written, and it closes the last Phase 0 item — see the addendum at
the end.

Raw captures are `/tmp/kmsg-selftest{,2,3}.txt`, `/tmp/kmsg-baseline{,2,3,4}.txt` and
`/tmp/kmsg-deadman1.txt`. Rebuilding run 7's configuration reproduces `stage90-qcdt.img` sha256
`9eea0d49c51b5fd6807a410aef978d7ad440850313b63c92bef64f2b9acc5dea`.

## What was being tested

The three Phase 0 exit criteria in `docs/status/roadmap.md` §5:

- **(a)** an induced hang self-recovers to Android without a manual power-cycle;
- **(b)** a jump to the Stage90 fixture header produces a logged undef/abort with PC/LR, not a hang;
- **(c)** a real Stage-owned function executes via the candidate L1 at high VA and returns, with
  the IRQ handler still live afterwards.

Runs 1–3 were for (a). Runs 4–7 were the queued baseline run, which covers (c) as a side effect
and closes out the F-AM1, device-tree, dead-man, watchdog and harness changes at once.

## Part 1 — criterion (a): the hardware watchdog, three runs

Runs 1 and 2 both **returned the device to Android on their own**, so the reset path works, but
both reported the watchdog as not armed. Two independent causes, one per run.

**Run 1 — the bark/bite registers are 20 bits and a 30 s timeout does not fit.** The hardware
showed the truncation in its own readback, with `0x00107f9d` losing bit 20:

```
hw_watchdog_timeout_s=0x0000001e         30 s
hw_watchdog_bark_ticks_written=0x000effa6
hw_watchdog_bite_ticks_written=0x00107f9d
hw_watchdog_bite_after         =0x00007f9d      <- bit 20 gone
hw_watchdog_countdown_second   =0x00001034
hw_watchdog_countdown_plausible=0x00000000
hw_watchdog_readback_ok        =0x00000000
stage90 hw_watchdog: NOT confirmed armed; no hardware reset net
```

The bite still happened — it landed 1.0 s after the bark instead of 3 s — but the readback
mismatch was reported as a dead register. Fixed three ways: the timeout is 25 s (bite 28 s,
which fits), a `#error` rejects any timeout that cannot fit, and the readback compares masked
to the register width so a real truncation is reported as `bite_truncated` instead of as a
failed readback.

**Run 2 — `WDT0_STS` counts *up* from zero, not down from the bark value.** The truncation is
gone (`bite_truncated=0x00000000`, `readback_ok=0x00000001`) and the only remaining failure is
the plausibility window, which was looking for a down-counter near the bark value:

```
hw_watchdog_timeout_s=0x00000019          25 s
hw_watchdog_bite_after=0x000dffac
hw_watchdog_bite_truncated=0x00000000
hw_watchdog_max_ticks=0x000fffff
hw_watchdog_countdown_second=0x00000b6e
hw_watchdog_countdown_plausible=0x00000000
```

The vendor driver confirms the direction in its own pet path: `slack = (bark_time * WDT_HZ / 1000)
- count`, which is only a remaining-time figure if `count` rises toward `bark_ticks`. The window
now asks for near-zero and rising, which still rejects aboot's own 20 s arming (that sits near
655300 ticks, not near zero).

**Run 3 — green.**

```
hw_watchdog_enabled=0x00000001        hw_watchdog_readback_ok=0x00000001
hw_watchdog_counter_running=0x00000001
hw_watchdog_countdown_plausible=0x00000001
hw_watchdog_bite_truncated=0x00000000
stage90 hw_watchdog: armed and counting; the SoC will reset itself if the payload stops
```

The selftest then spins, and the log ends there — no `deadline reached` line, so the 90 s bounded
spin never completed and the SoC's own counter is what reset the device. It returned to Android
unattended in ~59 s wall clock (~28 s of payload plus Android's own boot).

**Criterion (a) is met**, and it is met twice: once here as an induced hang, and again in run 6
on a hang that was not induced at all (Part 2).

## Part 2 — runs 4–7: the baseline run, and two bugs it found

The queued run 2 in the roadmap — the default `HARD_SKIP` ladder with `STAGE90_EXCLUSIVE_PROBE=1`
— took four attempts, each failing at a different layer.

### Runs 4 and 5: the device tree is validated in two places, and only one was fixed

```
high_root_dt_root_children=0x00000014        <- 20
...
mmu high bootstrap selftest failed: validation status
kernel_entry bad: MMU high bootstrap selftest
kernel_entry returned failure
```

`mmu.c` compared the device tree's root child count against a literal `19u` **in two places**
(`mmu.c:2358` and `mmu.c:6310`). Adding the `/arm-io` node made the count 20, so both checks
failed — and a failure four steps away, in the MMU high-bootstrap selftest, is how it surfaced.
Both sites now use a shared `STAGE90_APPLE_DT_ROOT_CHILDREN`, and the builder uses it too.

Runs 4 and 5 are the same build: their logs are identical except for the timebase readings.
The second run's purpose is not recorded; the most likely reading is that it confirmed the
failure was deterministic before the cause was chased.

The gap this exposed is worth keeping: `host_dt_selftest_probe` verified the tree is internally
consistent and that XNU's own reader can walk it, but nothing checked `mmu.c`'s separate
hardcoded expectation of the same number. A host-side check now covers the count itself (the
builder prints `nChildren 20+1 / 20-1 detected` in the corruption table).

### Run 6: the Mach-O loader wrote through an unmapped VA, and hung

```
stage90_xnu_macho_loader: loading Mach-O kernel
copying segment data
dest_va=0x80008000  copy_size=0x000003e0
data abort: dfar=0x80008000 dfsr=0x00000805 lr=0x00008254
data abort: dfar=0x80008000 dfsr=0x00000805 lr=0x00008254
data abort: dfar=0x80008000 dfsr=0x00000805 lr=0x00008254
zeroing BSS
zero_size=0x00001c20
<- and the log stops here
```

`dfsr = 0x805` is a translation fault, i.e. `0x80008000` is genuinely unmapped.
`xnu_macho_loader.c` said "we assume segments are already mapped by the candidate L1. The
candidate L1 maps 0x80000000-0x800fffff" and copied anyway. In a `HARD_SKIP` build the candidate
L1 is built and verified but **not installed** — the identity table is live and it maps nothing
at `0x80000000` — so every store in the copy faulted.

It hung rather than failing visibly because the data-abort handler resumes by skipping the
faulting instruction, and a byte copy is a post-indexed store: skipping the store skips the
pointer increment with it, so the loop re-executed the same address forever. That is a silent
infinite loop by construction, and it is now the best explanation for the original Stage90
"handoff hang" as well — same address, same fault pattern, same absence of output.

The hardware watchdog reset the device ~28 s in, with no manual power-cycle.

### Run 7: fixed fail-closed, and green end to end

The loader now reads `TTBR0` and refuses to copy unless the live L1 is the candidate L1 that
provides the high-VA window. The check is on the page table, because what decides whether a VA is
mapped is the page table — nothing about the segment tells you.

```
segment __TEXT destination VA is NOT mapped (candidate L1 not live); refusing to copy
  segment_vmaddr=0x80008000  live_l1_from_ttbr0=0x000b8000  candidate_l1_base=0x0010c000
  (repeated for __DATA, __LINKEDIT, __PRELINK_TEXT, __PRELINK_INFO, __PRELINK_STATE)
stage90_xnu_macho_loader_segments_loaded=0x00000000
stage90_xnu_macho_loader_segments_unmapped=0x00000006
stage90_xnu_macho_loader_ready_for_handoff=0x00000001
stage90_xnu_handoff: HARD_SKIP active - no L1 switch, no loop, no IRQ, no jump
...
loader_status=0x90000001
Stage84 Mach-O/XNU loader preflight ok
kernel_entry ok
kernel_entry returned success
stage90_main final platform_reboot call
```

All six segments refused, nothing written, the ladder completed, and the payload rebooted the
device itself.

## What run 7 also confirms

Run 7 is the first `HARD_SKIP` baseline to reach `kernel_entry returned success`, so it also
settles the other pending changes in the audit's live list, in one boot:

- **Criterion (c) is met.** `high_va_code_exec`: `fn_called=1`, `fn_high_va=0x80048098`,
  `fn_result_correct=1`, `public_xnu_executed=0` — a real Stage-owned function ran through the
  candidate L1 at high VA and returned.
- **The IRQ handler is still live afterwards**: `irq_count 1 → 3`, `timer_irq_count 1 → 3`,
  `irq_delivered=1`, `vbar_restored=1` with the restored VBAR back at `0x000080a0`.
- The high-VA data-abort and undef handlers both fired and were handled
  (`dfar=0xdeadc000` / `undef_last_addr=0x00049018`), each restoring VBAR.
- The device tree walked cleanly (`high root dt summary ok` and the whole high-root cascade ok),
  and `/arm-io` is present — that is what made the count 20 in the first place.
- The dead-man armed at the end of `kernel_entry`'s GIC validation and never fired, as designed.

## Part 3 — the Phase 1 baseline, measured

`exclusive_probe` ran in every one of runs 4–7 with identical results, so this is a stable
measurement of what exclusives do under the current Strongly-Ordered mapping:

```
exclusive_probe_ldrex_reads_word        =0x00000001
exclusive_probe_undisrupted_strex_status=0x00000000     <- success
exclusive_probe_disrupted_strex_status  =0x00000000     <- success, and it should have FAILED
exclusive_probe_iterations              =0x000003e8     1000
exclusive_probe_success_count           =0x000003e8
exclusive_probe_fail_count              =0x00000000
exclusive_probe_monitor_tracks          =0x00000000
exclusive_probe_exclusives_usable       =0x00000000
```

The discriminating sub-test is the third line. A plain store between `LDREX` and `STREX` clears
the exclusive monitor, so a working monitor must make that `STREX` fail; here it succeeded, and
`monitor_tracks = 0` says the monitor is not tracking at all. An implementation that always
reports success is indistinguishable from a working one unless T3 is checked — which is why the
probe exists, and this is the number Phase 1a's `NORMAL_NC` change has to beat.

## What changed in response

- `hw_watchdog.c`: 25 s timeout (bite 28 s), a `#error` on a timeout that cannot fit the register
  width, a masked readback comparison, and a plausibility window that expects a counter rising
  from near zero. The selftest's message prints its deadline from the macros rather than a literal.
- `STAGE90_APPLE_DT_ROOT_CHILDREN` in `stage90.h`: the root child count now has one definition,
  used by `mmu.c`'s two checks and by the builder.
- `xnu_macho_loader.c`: reads `TTBR0` and refuses to copy unless the candidate L1 is live;
  logs the segment, the live L1 and the candidate base; counts refusals in a new
  `segments_unmapped` field.

## What is still outstanding

- **Criterion (b)** — a jump that produces a logged abort rather than a hang — has not been run.
  `STAGE90_HANDOFF_FAULT_INJECT_VA` exists for it and is gated behind `--allow-fault-inject`.
- **The handoff mode has not been stepped past `HARD_SKIP`.** `PREFLIGHT_WATCHDOG_ONLY` and
  `FULL` are its own queued run.
- **Phase 1a** (`STAGE90_PMAP_ATTR_MODE = NORMAL_NC`) has not been on hardware; the run above is
  the `SO_ONLY` baseline it will be compared against.

## A note on the record

`a683c9d`'s message describes run 6's hang and the fix, and it was written while run 7 was still
in flight — so it reads as if the fix were unvalidated and the baseline still failing. Run 7
completed at 00:52 and validated both fixes. This log is the record that the commit message
could not be.

---

# Addendum — run 8: the software dead-man, proved on its own

Run 8 is the queued run 3 from the roadmap, and it was run after the log above was written.
Build: `STAGE90_EXTRA_CFLAGS='-DSTAGE90_DEADMAN_SELFTEST=1 -DSTAGE90_HW_WATCHDOG=0'`, gate
`--allow-selftest`. The hardware watchdog is **off**, on purpose: it is the only other route
back to Android, and if it were armed a success could not be attributed to the dead-man. The
gate warns about exactly this and lets the run through anyway.

```
stage90_build_hw_watchdog=0x00000000
deadman_gicd_ctlr_before=0x00000001     deadman_gicc_ctlr_before=0x00000001
deadman_gicd_ctlr_after =0x00000001     deadman_gicc_ctlr_after =0x00000001
stage90 pc-sampling watchdog: arming
watchdog_interval_us=0x000186a0         100 ms
watchdog_max_samples=0x00000258         600  -> a 60 s budget
watchdog_interval_ticks=0x001d4c00      1920000 ticks at 19.2 MHz = 100 ms
watchdog_cntp_ctl_armed=0x00000001
deadman: armed (dump + PS_HOLD reboot if the payload stops making progress)
deadman SELFTEST: spinning; the dead-man should dump and reboot us at ~60s
irq handler iar=0x00000013 id=0x00000013 count=0x1 ... count=0x8
stage90 pc-samples: begin
pc_sample_total=0x00000258
pc_sample_budget_used=0x00000258
pc_sample_watchdog_fired=0x00000001
pc_sample_timer_irq_count=0x00000258
pc_sample_last_pc=0x0004a2e8
...
stage90 pc-sampling watchdog: rebooting after sample dump
platform_reboot entered ... writing PS_HOLD=0 ... PS_HOLD write returned
platform_reboot entering WFE loop
```

The device returned to Android on its own, `run_and_capture.sh` exit 0.

**Why this is the direct proof.** The selftest spins with no exit of its own, and the hardware
watchdog is disabled, so the only thing that can return the device is the dead-man: timer IRQ
(`id 0x13` is the ARM generic timer PPI, and `timer_irq_count` tracks `count` exactly) →
interrupt the spin → dump the interrupted PC → `platform_reboot()`. It fired after the full
600-sample budget, 60 s, as armed.

**The dumped PC is real, not filler.** The samples alternate between two addresses, and
`arm-none-eabi-addr2line` on the built ELF resolves them:

| Sample | Symbol |
| --- | --- |
| `0x0004a2e8`, `0x0004a300` | `stage90_main` — the selftest's own spin loop |
| `0x000098d0` | `timebase_ticks` |

That is exactly the call site and callee of a `while (timebase_ticks() < deadline);` loop, which
is what the selftest runs. A dump that returns plausible-but-wrong PCs is the failure mode worth
ruling out, and this rules it out.

**One honest limit.** This is evidence about the dead-man *as a mechanism*. It is not evidence
that the dead-man would fire on the hangs that actually matter — it needs the GIC, the timer,
IRQ delivery, the vector table and unmasked IRQs, and this run had all five healthy by
construction. The hardware watchdog remains the net for a hang that takes any of them out, which
is why both exist and why the loader hang of run 6 was recovered by the watchdog rather than by
the dead-man.

Phase 0's remaining item is now criterion (b) alone.
