# 568: 533 ran and came back - 547 §4's first row, measured

The armed arm ran on hardware for the first time, on 2026-09-23 at 00:41, and the reading 547 §4
pre-registered arrived **verbatim**: the boot dies at the idle exit's `pop {fp, pc}`, the log is present,
and the device returns. That is row (i) of that table, whose own words are:

> the enable is confirmed as the difference between a recovered death and a hang, and 546's mechanism
> stands for the enable-off cells

So **535 is to be built as designed** (547 §5: a PoC invalidate of the L2, in the window, after the push
and before the pop), and the third row - "returns with no pop death at all", the one that would have
stopped 535 - did not happen.

This is also the phase's **first returning configuration**: the SoC took the boot, ran the payload all the
way into XNU, reached pid 1, died at the idle exit, and came back on XNU's own `MACH Reboot` - so this arm
can be re-run, and its log can be read without a power press.

## 1. The run, and the two artifacts

```
gate:   ./preflight_boot_check.sh --allow-xnu-entry          exit 0
run:    ./run_and_capture.sh --allow-xnu-entry               exit 0
boot:   Sending 'boot.img' (8340 KB)   OKAY [  0.262s]
        Booting                        OKAY [  0.011s]
device: found 4a2fe00b in fastboot      (fastboot by keypress, never flashed)
```

| artifact | before the run | after the run |
| --- | --- | --- |
| `/tmp/cancro-last_kmsg.txt` (this run's target) | 596665 B, `f0285b0f…` | **597641 B, `734062592d6f6a4e4f252d2587ab4ac9e49311f8565417226b3f2c6d`** |
| `/tmp/cancro-last_kmsg.txt.prev` (parked by step 2b) | absent | **596665 B, `f0285b0f…` - byte-identical to what the name held** |

Step 2b (564) did what it was built for: the previous log was moved aside *before* the boot, and it is
still there, unchanged, under a name that says what it is. **This is the first time that path ran with a
device attached**, and it leaves the 2026-09-22 death intact while the new log occupies the name the
reader is told to read.

## 2. Proof that the log is this run's, and not a re-read of the parked one

The one thing 562/563/564 were all built to prevent is a log read as this run's that is really the
previous one, so this is checked three ways rather than assumed:

- **The arm's own instrument is in it.** `xnu_live_slot_cwe_*` has **3 records** here and **0** in the
  parked log - and that pair is published by `entry_window_note`, which exists only in images built since
  522. The parked log's image predates it.
- **The run-specific values differ.** `hw_watchdog_countdown_second`, `hw_watchdog_checksum`,
  `timebase_boot_ticks_lo` and `ml_timebase_start` all differ, as they must between two boots.
- **Everything else is identical, which is the payload being deterministic**, not a stale read: 36 of
  3931 payload lines differ in total (the four run-specific keys, the three new `cwe` records, and their
  follow-on checksums).

## 3. The arm is 533's, read out of the log rather than asserted

| key | value | reading |
| --- | --- | --- |
| `xnu_live_slot_pre_calls` | `0x00000001` | the wrapper was entered |
| `xnu_live_slot_rtcpre_calls` | `0x00000001` | one note later - the same pass |
| `xnu_live_slot_post_calls` | **absent** | it did not come back through the call |
| `xnu_live_slot_cwe_calls` | `0x00000001` | `entry_window_note` ran once |
| `xnu_live_slot_cwe_win` | `0x30c57879` | SCTLR as Apple's enter left it, C **clear** |
| `xnu_live_slot_cwe_set` | `0x30c57879` | the same register a few bytes later - **equal** |

`_win == _set` with C clear is exactly 533's arm's expected reading (the gate's derivation: with
`IDLE_CACHE_ENABLE=0` the `SCTLR.C` write is not in the image, so the pair can only agree), and the
absence of `post_calls` beside one each of `pre`/`rtcpre` is the localization 540 §3 built: **the death is
inside `platform_cache_idle_exit`.**

## 4. The death, and 547 §1's reading confirmed on real register values

```
panic(cpu 0 caller 0x80454584): sleh_abort: prefetch abort in kernel mode: fault_addr=0x33f1c1b4
r4:  0x33f1c1b5   r11: 0x33f1c1b5
sp:  0x8054fed0   lr: 0x800462dc   pc: 0x33f1c1b4
cpsr: 0x800000b3  fsr: 0x00000005  far: 0x33f1c1b4
```

- `lr = 0x800462dc` is the exit's own `bl FlushPoU_Dcache` return address - the criterion, and it is the
  address the reader **derives from the entry ELF at read time** (556), not a literal.
- `pc = 0x33f1c1b4` is the **popped word**, and 547 §1's reading - `pc = r11 & ~1` - is confirmed against
  the registers on this run: `r11 = 0x33f1c1b5`, `0x33f1c1b5 & ~1 = 0x33f1c1b4`. The `far` is the same
  value, so the aborting *address* is the popped data and not a code address; `fsr = 0x5` is the
  prefetch-abort-on-translation-fault encoding.
- `sp = 0x8054fed0` is where 520's was, and `storm = 0x9`, `episodes_seen = 0x9` match 520's count.

The reader's own summary of all of this:

```
  PREDICTED  1 'panic ... sleh_abort' record(s) and xnu_live_sleh_lr=0x800462dc is the
        return address of the exit's own bl FlushPoU_Dcache - i.e. the death is at its pop {fp, pc}
  PASS  slot_cwe_win=0x30c57879 has SCTLR.C clear
  ARM   533's arm: slot_cwe_set=0x30c57879 has C clear
  DIED IN THE EXIT  pre_calls=... and rtcpre_calls=... both published and slot_post_calls did not
  => the arm's prediction arrived
```

## 5. What did *not* change, said plainly

**The boot still does not survive the idle pass.** The OS reached pid 1 in this run - `/sbin/launchd`,
"the OS starts the process at 0x10e0" - and so did 520's; the payload's own frontier line is the same
`real XNU entry: 47`, and the death is at the same instruction. So nothing about the *frontier* advanced:
**this run answered a question about the mechanism, not about the boot.** The honest one-line status is
that the enable-off cell is now a **returning** configuration with a log, which is what makes 535 a
development step rather than another blind attempt.

And the second thing that did not change is that TWRP stays withheld: the OS does not get in
(「如果os已经能进去了的话」 is still unmet), because the pass dies at the idle exit and the device only
returns by rebooting.

## 6. What 535 now is, and the one thing this run cost

547 §5 is unchanged and now unblocked: a **PoC invalidate of the L2, in the window, after the push and
before the pop**, at the exit's own `bl FlushPoU_Dcache` (`0x800462d8`, identified by the return address
`0x800462dc`), and it must **not** re-enable allocation outside the coherency domain. 565 §2's warning
stands: `STAGE90_XNU_EXIT_POC_FLUSH` is *before* the push and is not this.

**The cost, named because it is real: `out/stage90/stage90-qcdt.img` and `out/stage90/xnu_arm_entry.bin`
are not tracked by git** (`git ls-files` on both returns nothing), and the payload link does not reproduce
(408). So 533's arm was a **one-shot artifact**, it has now been spent, and building 535 means `./build.sh`
- after which `1daaf44e…` cannot be re-made. That is payable now, and only because the run returned and
was read: the ordering 565 §1 established (run, read, then build) has been satisfied rather than
skipped.

## 7. Safety

One gated device action, and it is the run this phase was armed for: `preflight_boot_check.sh
--allow-xnu-entry` (**exit 0**) then `run_and_capture.sh --allow-xnu-entry` (**exit 0**). `fastboot boot`
only - the runner printed `booted, never flashed, so no outcome of this run can write to storage`, and
nothing was written to storage or flashed. No build was run and no file under `out/` was touched in this
step; the frozen pair still reads `1daaf44e624563694e…` / `f202f2465886aba6…`. The previous capture at
`/tmp/cancro-last_kmsg.txt.prev` is intact (`f0285b0f…`, 596665 B).
