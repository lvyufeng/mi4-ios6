# 709: the rung-7 card-power byte pressed — the store took and its readback HELD, and the controller answered with the power IRQ this image cannot service

708 pre-registered the rung that makes the driver's **first power byte**: `mmc_power_up`'s PASS A, one
8-bit store to `POWER_CONTROL 0x29` with the value derived from `CAPABILITIES 0x40`, and **not** the
vendor's wait (`sdhci_msm_check_power_status` would wait on a threaded handler this image cannot
deliver). The arm was built at `STAGE90_XNU_STORAGE_PROBE=7`, parked as
**`armed-storage-power-7b1f3799`** (entry bin `7b1f3799…`), and pressed once. Every one of the nineteen
`_pwr_*` keys the pre-registration named is in the log and **every one reads what section 2 said it
would** — and the press answered a question the pre-registration did not ask: the controller's answer to
the power byte arrived as an **interrupt on the SDCC1 `pwr_irq` line**, which is the one line this image
hands to nobody, and that is what ended the run.

## 1. The press

| | |
| --- | --- |
| readiness | **5/5, exit 0** (22:50:06), the live arm found by hashing `stage90-qcdt.img` (`f0ad4405…`), 11 files vs the park; run twice in this step and green both times |
| gate | **exit 0, 575 stdout lines** (22:50:34), under `--allow-xnu-entry` — the flags readiness derived from the arm's own switches |
| runner | **EXIT 0** (22:51:37) — returned and captured, `--expect-arm=armed-storage-power-7b1f3799` |
| the device | back **18 s** after `fastboot boot` (seen via host log and port) — the ninth return-time measurement (8, 17, 24, 27, 24, 27, 25, 28, **18** s) |
| capture | 606,657 B, sha256 `c5bdeac77428ebbd9207366b36b8b57efb858383b6517fc12889a73c1e5349c6`, archived by hand under `out/stage90/captures/` as `rung7-pwr-20260925-224929-{last_kmsg.txt,gate.log,run.log,press.log}` (`out/` is gitignored and the runner does not archive the capture: 672's rule) |
| the firer | `/tmp/g668/press-on-clear.v5.sh` (sha `7f23cae5…`), `armed-storage-power-7b1f3799 600`, neighbour `33e80afe` off both lists (`fastboot: []`, `adb: [4a2fe00b device]`), `done. gate=0 runner=0` — **no firer is armed** |

The firer ran its own readiness before the gate (`readiness exit=0`, and it re-checked that the bytes
still hash to the armed set *after* the wait: `the arm survived the wait and is still
'armed-storage-power-7b1f3799'`), one gate, one runner, one non-persistent `fastboot boot`.

## 2. The nineteen keys, cell by cell

Every expectation below is 708 section 2's own number, and the read column is the log's.

| key | pre-registered | read |
| --- | --- | --- |
| `_pwr_calls` / `_pwr_done` | the act ran and completed | `0x1` / `0x1` |
| `_pwr_cap` | `0x742dc8b2` (705's `_mode_capabilities`, re-read) | **`0x742dc8b2`** |
| `_pwr_avail` | `MMC_VDD_165_195` | `0x00000080` |
| `_pwr_vdd` / `_pwr_vdd_ocr` | `fls(ocr_avail) - 1` = `7` | `0x7` / `0x7` |
| `_pwr_voltage_bits` | `0x0A` (`0x0E` would mean the caps' 3.3 V bit was set) | `0x0000000a` |
| `_pwr_refused` | `0` for the act to have happened | `0x00000000` |
| **`_pwr_wrote`** | **`0x0B`** = `POWER_180 \| POWER_ON` | **`0x0000000b`** |
| **`_pwr_before` → `_pwr_after`** | `0x00` → **`0x0B`** (`_after` is the readback that says the store took) | **`0x00000000` → `0x0000000b`** |
| `_pwr_status_before` → `_pwr_status_after` | `0` → "the controller's own answer" | `0x0` → **`0x00000002`** |
| `_pwr_mask` / `_pwr_ctl` | `0x0000000f` / `0` | `0x0000000f` / `0x00000000` |
| `_pwr_ps_before` / `_pwr_ps_after` | `0x01f80000`, `CARD_PRESENT` clear | `0x01f80000` / `0x01f80000` |
| **`_pwr_cc_before`** | **`0xE045`** — the cell that states the route | **`0x0000e045`** |
| `_pwr_cc_after` | unchanged at `0xE045` | `0x0000e045` |

Three of those deserve their own sentence:

* **THE STORE TOOK AND THE READBACK HELD.** `_pwr_before=0x00` → `_pwr_after=0x0B` is the first device
  write in this project whose own readback carries the value written. The cell it is measured against is
  706's: rung 6 wrote `0x200` to `CORE_VENDOR_SPEC 0x10C` and read back `0`, and that failure is why
  `_pwr_after` was pre-registered as the cell to read. A power byte that reads back is a *different
  kind* of object from a vendor field that does not.
* **THE CONTROLLER ANSWERED, AND WITH A REQUEST.** `_pwr_status_after = 0x00000002` is BIT(1):
  `CORE_PWRCTL_BUS_ON`. The mask is armed (`0x0f`, all four events), the status was `0` before the byte
  and asks for the bus after it, and **nothing cleared it** — `_pwr_status_before`/`_after` is the same
  register read either side of one store, so this is the store's own consequence and not a leftover.
* **THE ROUTE CELL READ AS PRE-REGISTERED.** `_pwr_cc_before = 0xE045` is rung 6's card-enable word
  still standing, read by this rung's pass A in the same boot: the probe's order puts the power byte
  *after* the clock set, where the driver's own pass A would see `0x0000`. `_pwr_cc_after` is the same
  word, so powering did not disturb the clock register — a coupling this ladder had not seen, and did
  not see.

## 3. The finding: the power IRQ arrived, and this image has no line for it

The rung-7 log carries four keys rung 6's does not, and they are not the `_pwr_*` ones:

```
 xnu_live_irq_other_count=0x00000001
 xnu_live_irq_other_iar=0x000000aa
 xnu_live_irq_other_cli=0x8055412c
 xnu_live_irq_timer_count_final=0x00000005
```

They are published by one path and only one — `entry_irq.c:546-557`, the arm the handler takes for
**a line this image handed out to nobody**: EOI it, count it, record the acknowledged intid and the
client pointer, publish the timer count as a parting reading, and stop the run through
`entry_epilogue("exception: irq line")`.

**`0xaa` = 170 = SPI 138 + 32, and SPI 138 is this controller's `pwr_irq`.**
`external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974.dtsi:502` is the node
`sdhc_1: sdhci@f9824900` — the very base this arm writes through (`hc_mem`, `0xf9824900`) — and it
declares:

```
 interrupts = <0 123 0>, <0 138 0>;
 interrupt-names = "hc_irq", "pwr_irq";
```

So the three readings are one event in the order they happened: **the byte was written
(`_pwr_after=0x0B`), the controller answered with a bus-on request (`_pwr_status_after` BIT(1)), and the
request arrived as an interrupt on intid 170** — the line the vendor's `sdhci_msm_check_power_status`
waits on (`_pwr_mask` armed with all four power events is what lets that status raise it).

**The control is the previous arm, and it is a clean one.** Rung 6's log has no `_irq_other_*` key at
all and no `_irq_timer_count_final`: it ran through sixteen idle windows and past a 6,000 ms deadline and
took **no unexpected interrupt**. The two arms differ in the rung-7 block and in nothing else but a
uniform `+0x1000` move of the kernel region's addresses (708, measured on 26,533 symbols), and nothing
in this project ties an address shift to IRQ delivery. So the *store* is what raised the line.

**This is 708 section 1.3's "the wait cannot be taken", measured from the other end.** The
pre-registration argued the wait is unportable because its completion comes from a threaded handler this
image cannot deliver. The press shows the stronger statement: **the request that handler would have
answered did arrive, and it ended the run.** And it shows the two halves of the vendor's sequence are
not separable the way a reader might assume — `CORE_PWRCTL_STATUS` latched at `0x02` with the mask
armed is a line that stays asserted until someone services or masks it, and the only thing this image
does with it is EOI and stop.

**What a faithful power path needs, and it is a decision rather than a reading** (owed to the next
rung, and named here rather than discovered there): either mask the power events before the store
(`CORE_PWRCTL_MASK <- 0`, which is a *write to the controller* and therefore its own arm) or hand intid
170 to a client that clears the latch (`CORE_PWRCTL_CLEAR 0xE4`, `sdhci-msm.c:64`) and completes what
`check_power_status` would have completed. The second is what the driver does and is what the ladder
owes the storage driver; the first is smaller and would answer whether the rest of the sequence is
reachable without it.

## 4. The ending, and why rung 7's differs from rung 6's

| cell | rung 6 (707) | rung 7 (this press) |
| --- | --- | --- |
| `_post_end_calls` | `0x00000007` (the ending fired) | **absent** |
| `_post_elapsed` | `0x06e0953c` = 6,006.2 ms | **`0x00000000`** = 0.0 ms |
| `_slot_post_calls` | `0x00000004` | `0x00000001` |
| panic | `xnu_entry_panic_entered=1`, `panic_len=0x13fa`, `fault_addr=0xfa0065c` | **`xnu_entry_panic_entered=0`**, `panic_len=0x24` |
| `_sleh_storm` | `0x00000009` | **absent** |

The trace published its zeroth sample and no more: **the machine stopped inside the first idle pass,
before the armed deadline** (`_seam_post_end_ticks=0x06ddd000` = 115,200,000 ticks = 6,000 ms at the
hardware's own `_post_cntfrq=0x0124f800` = 19,200,000 Hz). The count that would say the ending fired is
absent and the elapsed trace is 0.0 ms, which is the pair 690 built precisely so that "the machine
stopped before the clock ran out" could be told from "the ending never ran". Here it is the first one,
and the stop is section 3's: an interrupt line nobody owns.

So **the ending's own cells are UNREAD on this boot** — the 690 ending is a clock that never ran out,
and its first store's fault (rung 6's `0x0fa0065c`) was not reached again.

The runner's list, this capture (the previous press's log is printed through the same list beside it,
parked at `/tmp/cancro-last_kmsg.txt.prev.15`):

* `PASS no 'panic ... sleh_abort' in the log` — the idle exit retired its own epilogue;
* `PASS slot_cwe_win=0x30c57879 has SCTLR.C clear`, `PASS seam_sctlr=0x30c57879 has C=0`;
* `PASS slot_post_calls=0x00000001` — the exit returned through the wrapper;
* **`PASS seam_lr=0x800482dc is the exit's own call's return address`** — the re-pinned constant, read
  live: `xnu_live_seam_lr=0x800482dc` is this arm's own log line, and **the same criterion printed
  against the previous press's log FAILs it** (`seam_lr=0x800472dc is not the exit's own call's return
  address`), because the criterion is derived at run time from the arm in `out/`;
* `FAIL seam_sp=0x80553ec8 is not sleh_sp-8` — the `STAGE90_XNU_SLOT_NULL=1` fingerprint 697, 704, 705
  and 706 also printed, and an UNREAD pair (`_seam_other`, `_slot_pre_sp`) rides with it;
* `PASS b1=0x8047eb04` is the address the real exit returns to in *this* image, and `CLEAN LINE` the pair
  came back unchanged;
* the floor: pid 1, the driver's `open` answering `0` while the control open answers `ENOENT`, the
  fixture's `0xfeedface` read into a user page, and two AST records.

Eight abort records (`_slot_ab_calls` 1..8, `_ab_rej` 0..3) and no storm.

## 5. The return: 18 s, still unattributed

The ninth measurement of "what brings the phone back" (8, 17, 24, 27, 24, 27, 25, 28, **18** s) and it is
still not attributed. This press adds one thing to that question rather than answering it: the *run* now
demonstrably ends in the IRQ path's epilogue (section 3), so the epilogue is not what returns the device
— `out/`'s own `entry_epilogue` cannot write `PS_HOLD` in the context it runs in (that store faults,
`mi4-the-run-ends-at-entry-epilogue`) — and the two hardware candidates (the watchdog's bite at
`0xf9017014` and/or `PS_HOLD` at `0xfc4ab000`) remain unmeasured.

## 6. What rung 7 does not do, and the goal

`POWER_CONTROL` now carries a real 1.8 V byte and reports it, the controller has asked for the bus, and
the clock is enabled at the controller. **There is still no command, no sector, no partition table, no
mount, and no driver beyond the fixture**; the card has never been addressed, and what a card does with a
powered bus is unobserved because nothing on this bus has ever been read back from a card. The goal's
floor is met exactly as 520, 533, 699, 703 and 706 met it.

**TWRP-to-storage stays withheld: the OS is not observed entering and STAYING.** This press makes that
condition more clearly unmet rather than less — the boot now stops on an interrupt that the storage
controller itself raises, which is a fact about the image and not about the card.

## 7. Three things this step measured about itself

1. **The first payload build was made without `-DSTAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1'`** — a
   payload that never jumps into XNU, whose own switch record said `#define STAGE90_XNU_ENTRY 0u` while
   the build refused nothing, every hash it takes part in matched, and its embedded entry blob was the
   right image. Caught by comparing the dump against the record; the net that would have caught it is
   the gate's own clause (`preflight_boot_check.sh:1699-1719`) which readiness row 3 runs. Memory row
   **m720**.
2. **Two numbers in this project's record were wrong and are corrected in this step's commit**:
   `experiment-707`'s §5 and the rung-6 `set=` line both said the entry `.text` went `5315592 ->
   5317032` (the delta is right, +1440 B; both bases are wrong by `+3336`) and that `.data` is `206804`
   (that value is experiment-501's, for an image whose `.text` was `5280296`). Measured with three tools
   that agree: `.text` is `5309568` → `5310816` → `5312256` → `5316960` and `.data` is `206512` in all
   four parks. The `206804` was also quoted in the rung-6 `role=` line. Memory row **m719**.
3. **Two narration lists had outlived the thing they list.** `tools/verify_press_ready.sh`'s arm row
   refused this rung with "a rung with no paragraph here yet" — correctly — because its ladder stopped
   at 6 and, in the comment above it, at 3; rung 7's paragraph and the rung-7 clause are added here, and
   the comment now says the ladder is read from the branch rather than listed. The **gate's** narration of
   `STAGE90_XNU_STORAGE_PROBE` is now **seven rungs short** (2 through 7 are not described in it) — peer
   lane, by message and never by edit (708 sent the six-rung message; this step adds the seventh).

## 8. Owed

**New, and it is the next rung's subject:** the power IRQ — either mask the power events before the store
(`CORE_PWRCTL_MASK <- 0`) or hand intid 170 to a client that clears `CORE_PWRCTL_CLEAR 0xE4` and
completes the wait; and with it the decision of which of those two the driver's own sequence requires.
Also new: the store's *effect on the card* is still unobserved, and the status latch
(`_pwr_status_after=0x02`) has no clear in this image.

**Carried unchanged:** `CORE_VENDOR_SPEC 0x10C`'s MCLK select did not hold under a store (706/707); the
RCG rate write (the speed-change rung, a bounded poll of the update bit); the root-versus-branch
disagreement (a DECISION, not a reading); the width clause of the store census still never observed to
fire on a WIDENED DEVICE store; what actually returns a run; the ending's first store faulting (rung 6's
measurement — UNREAD here); the 691 §5 `entry_note_wfi` readback; `entry_reset.h`'s false IMEM claim;
676 §6 / 677 §6; the 684-owed runner clause for the 678 arm; `tools/xnu_dt_requirements.py` and the
`"master"` value; the two peer-lane tripwire repairs; BIT(29) of `_clk_ahb_cbcr` published and unnamed;
the seam address pinned in two files (this arm moved it, re-pinned both, and the build refuses a link
where they disagree); and the gate's narration, now seven rungs short.
