# 699: the census arm pressed — the standard register file answers, the bus-power bit is clear, and the mode sequence is the bootloader's state every time

698 built the read-only census of the SDHCI standard register file at the tail of `entry_storage_probe`
and pre-registered its cells before it was built. This is its press, and **every cell it pre-registered is
filled, including the one 697 left owed**: `hc_mem + 0xFE` answers with a spec version of 2 and a vendor
version of `0x11`, and the three registers the driver's own `sdhci_reset(SDHCI_RESET_ALL)` will touch all
have before-values now.

```
readiness  5/5, exit 0 - the live arm is armed-storage-census-4d66f871, 11/11 files, byte-identical to the park
gate       exit 0, 571 lines, under --allow-xnu-entry (derived from this arm's own switches)
runner     EXIT 0 - returned and captured, "the device came back 24s after this run called `fastboot boot`"
           (seen via: adb); capture 613,952 B, sha256 70f5fc924aec35cf75ea645e5ff57d00e223fbb0da1ff9994e8e77607500680f
send       one non-persistent `fastboot boot`, serial 4a2fe00b, neighbour 33e80afe off both lists
fired by   /tmp/g668/press-on-clear.v5.sh (sha 7f23cae5…), armed by hand as
           `press-on-clear.v5.sh armed-storage-census-4d66f871 600`, set declared with no default;
           the launcher printed `done. gate=0 runner=0` and exited, so no firer is armed and 660's
           closure has released
```

Capture archived by hand (the runner does not archive it) under
`out/stage90/captures/698-storage-census-pressed-2026-09-25-{last_kmsg.txt,run.log,gate.log,press.log}` —
`out/` is gitignored, so the record can carry the readings and not the files. 4,897 `xnu_live_` lines, 81 of
them `xnu_live_storage_*` (62 distinct keys), 25 × `persistent_write_attempted=0x00000000`, 87 ×
`failure_mask=0x00000000`.

## 1. The census, cell by cell: every pre-registered reading, in the direction it was registered for

698 §4 pre-registered ten keys and said what each direction would mean. The log fills all ten, and the
`_reg_loads` counter is the record's "ten reads" derived a second time:

| key | reading | the cell, against its pre-registration |
| --- | --- | --- |
| `_reg_power_control` | **`0x00`** | **the step's most important before-value, and it is the branch that makes the next act safe by construction**: bit 0 (`BUS_POWER`) is CLEAR, so the standard block does *not* believe the bus is powered. 698 §2's pre-registration: "a reading of `0x00` says it does not, and that reading would make the next step's `SDHCI_RESET_ALL` harmless by construction rather than by argument" |
| `_reg_clock_control` | `0x0003` | bit 0 internal-clock-enable and bit 1 internal-clock-**stable** set, **bit 2 `SD clock enable` clear** — the middle branch of the three pre-registered: the internal clock runs and the SD clock does not, so `sdhci_set_clock` is ahead of the driver on this block |
| `_reg_present_state` | `0x01f80000` | `CMD_INHIBIT` (bit 0) and `DATA_INHIBIT` (bit 1) both clear, as pre-registered for a block with no transfer in flight; **`CARD_PRESENT` (bit 16) is clear**, which 698 §4 pre-registered as the expected value and *not* evidence about the medium (`sdhci-msm.c:2896` sets `SDHCI_QUIRK_BROKEN_CARD_DETECTION`) — `_reg_card_present=0x00000000` is the same fact read out of the decoded word |
| `_reg_software_reset` | `0x00000000` | **the check the cell was named for**: zero, so no reset is stuck in progress — which is what 696's and 697's "no reset ran" implied and what this arm could finally measure |
| `_reg_host_control` | `0x00000000` | no bus width, no high-speed mode: the block's own control byte is at its power-on value |
| `_reg_slot_int_status` | `0x00000000` | no slot has a pending interrupt — the register `sdhci_reset`'s `SDHCI_QUIRK_RESTORE_IRQS_AFTER_RESET` path exists for |
| `_reg_capabilities_1` | `0x00008007` | the upper capability word, read for the first time in this project (its sibling `0x40` has now been read four times) |
| `_reg_max_current` | `0x00000000` | the identification set's last word |
| `_reg_pwrctl_mask` | `0x0000000f` | **the other half of 696 §3's hazard, and it answers the question a status register cannot**: a power IRQ **would** have been routed (four bits enabled in the mask), so 697's `_mode_pwrctl_status=0` means *nothing latched*, not *nothing was listening* |
| `_reg_pwrctl_ctl` | `0x00000000` | the before-value for the vendor's own acknowledge (`sdhci-msm.c:2879` reads it, `:2884` writes it back with the success bits) |
| `_reg_loads` | last value `0x0000000a`, and `_regs_done=0x0000000a` | **counted bottom-up, not written down**: the record's "ten reads" and the log's number are two derivations of one fact |

**The rename's read is in the same block and it is not a cell**: `_mode_host_version=0x00001102` — the
register 697 §3 proved the image was *not* reading, now read, at the width the vendor's own accessor uses.

## 2. `0xFE` answers: spec 2, vendor `0x11`, and the vendor driver's own branch would not be taken

698 §4 registered the two plausible directions for `_mode_host_version`: a plausible spec version in the low
byte would be the first statement this project can make that the block's *standard* register file is the one
the DT's `sdhci@f9824900` names; `0` or `0xffff` would move the whole question from the mode bit to the block.

```
xnu_live_storage_mode_host_version 0x00001102
```

`sdhci.h:242-245` splits it as `SDHCI_VENDOR_VER_MASK 0xFF00` / `SDHCI_SPEC_VER_SHIFT 8` over
`SDHCI_SPEC_VER_MASK 0x00FF`: **vendor version `0x11`, spec version `2`**. Neither `0` nor `0xffff`, so the
register answers, and it answers *in SDHCI mode* — 694 measured this block's mode bit clear and this arm
measures its register file readable after the vendor's sequence.

**And the value lands on a branch the vendor's driver actually takes**, which is what makes it a reading and
not a curiosity: `sdhci-msm.c:2909` reads this register with `readw_relaxed`, `:2913-2914` shifts the vendor
nibble out and compares it with `SDHCI_VER_100` (`:54`, `0x2B`). `0x11 != 0x2B`, so the
`SDHCI_QUIRK2_SLOW_INT_CLEAR` branch — the 40 µs delay in the interrupt handler at identification frequency —
is **not** the branch this hardware takes. The reading is exactly the input that decision needs, and it is the
first time this project has read it.

## 3. The mode sequence, re-measured on a boot that started from the bootloader: the inherited state is not the previous run's

This arm inherits rung 2's sequence, so the press re-runs it, and the pre-mode readings are the same numbers
694 took two presses earlier:

| key | 698's reading | what it says |
| --- | --- | --- |
| `_mode_bit=0x00000000`, `_hc_mode=0x00002000` | identical to 694 and 697 | **the controller the payload inherits is the bootloader's state, not the previous boot's**: every press starts from `HC_MODE_EN` clear, so the vendor's sequence is a precondition on every boot and not a one-time repair |
| `_core_power=0x00000441` | identical to 694 | the bus-on state the vendor's acknowledge logic was written for |
| `_mci_version=0x10000011` | identical | the guard that lets the arm write (`_mode_refused=0`) passed again |
| `_writes=4` (published 1/2/3/4), `_mode_w0_read=0`, `_mode_w1_read=1`, `_mode_w2_read=0x00002001`, `_mode_bit_after=1` | identical to 697 | the four stores landed in the vendor's order and `HC_MODE_EN` is set |
| `_mode_rst_polls=1`, `_mode_rst_steps=0`, `_mode_rst_ticks=0x6`, `_mode_rst_cleared=1`, `_mode_timeout=0` | identical to 697 (6 ticks) | `CORE_SW_RST` was already clear on the first read again |
| `_mode_pwrctl_status=0` with `_reg_pwrctl_mask=0x0000000f` | **0 both times, and now interpretable** | §1's mask reading is what turns this from "nothing happened" into "nothing latched while the mask was armed" |
| `_mode_dma_address=0x00000000`, `_mode_capabilities=0x742dc8b2` | identical to 697 | the soft register the reset clears stayed cleared and the strapping constant did not move |

**So 697's readings reproduce on a fresh boot**, which is the property that makes the next step's cells
comparable to these at all.

## 4. The ending, the panic, and the fifth return time

The arm's ending is unchanged from 690 (baseline on the wrapper's first return, `entry_seam_end_run` on the
return whose elapsed ticks have reached 115,200,000):

```
xnu_live_post_t0          0x052b38d3     the baseline
xnu_live_post_cntfrq      0x0124f800     = 19,200,000 Hz, the machine's own rate (fourth press to carry it)
xnu_live_post_elapsed     0x06e259fe     = 115,497,470 ticks = 6,015.49 ms
xnu_live_post_end_calls   0x00000007     the ending FIRED, on the 7th return
xnu_live_slot_post_calls  0x00000004     published at 1, 2, 4 - the count passed 4 and the run ended at 7
```

**The deadline was crossed 297,470 ticks (15.49 ms) before the returning pass** — the near side of the
resolution this arm's series has been measuring: 694 overshot by 11.011 ms and 697 by 1,999.0 ms because its
boundary landed just after a sleep began. **694, 697 and 698 together say the overshoot is where the boundary
fell inside one ~2007 ms idle park, and never more than one park.**

The ending's own first store faulted into the same panic, at the same addresses, as 697:

```
panic(cpu 0 caller 0x80455668): kernel abort type 4: fault_type=0x3, fault_addr=0xfa0065c
pc=0x8047c488 = entry_seam_end_run+0xc (the str to RESTART_REASON 0x0fa0065c)
lr=0x8047c5bc   fsr=0x00000805   far=0x0fa0065c   frame_ok=1   sleh_storm=9   sleh_seen=9
Attempting system restart...MACH Reboot
```

**And the return is a fifth measurement: 24 s**, against 8 (690), 17 (692), 24 (694) and 27 (697). It is
still unattributed, and what this run adds to the question is only that the same two return times can come
from different arms: **697 and 698 have the same 24 s and different ending overshoots**, so the interval is
not a function of the ending's own clock. The question stays owed with the same two candidates named
(watchdog `0xf9017014` / `PS_HOLD 0xfc4ab000`).

The runner's seam block is **the same block 697's run printed, cell for cell**: `PASS seam_lr=0x800472dc`
(the hook was entered at the exit's own call), the `FAIL seam_sp=0x80553ec8 is not sleh_sp-8` and the two
`UNREAD`s beside it — all three are the `STAGE90_XNU_SLOT_NULL=1` switch doing what 698 §3 said it does, on
both arms, so nothing here is new and nothing here is a regression.

## 5. What this does not do

* **No storage.** Ten reads of the register file, plus the four stores this arm inherits from 696. No command
  is issued, no sector is read, no partition table is parsed and nothing is mounted. `_writes=4` counts four
  stores to `CORE_HC_MODE`/`CORE_POWER`; nothing was written to the medium.
* **No driver beyond the fixture.** The goal's floor is met and unchanged: pid 1, the driver's `open`
  answering `0x00000000` while the control open answers `0x00000002`, and the fixture's `0xfeedface` read
  into a user page. The runner's own note says it plainly: this is the floor 520 and 533 also met, not this
  run's progress.
* **It does not perform the driver's reset or its clock set-up.** It measures the registers those two would
  change, which is what the next act needs.
* **TWRP-to-storage stays withheld.** The user's condition is that the OS can already be entered and stays;
  the OS is not observed doing that, so no write to storage is made.

## 6. Owed

* **The driver's own `sdhci_reset(SDHCI_RESET_ALL)` and the clock, now with before-values** (698 §7): the
  reset writes `0x01` to `SOFTWARE_RESET 0x2F` and polls it clear (`sdhci.c:246-266`), `sdhci.c:250` stops
  the clock (`host->clock = 0`) so `sdhci_set_clock` must follow, and **the reset's own publisher must read
  `POWER_CONTROL` back afterwards** — §1's `0x00` is the before-value that makes that question cheap.
* **What actually returns a run** (8/17/24/27/24 s): this run adds the one negative fact above (the interval
  is not the ending's overshoot) and nothing more.
* **Carried, unchanged**: the ending's first store still faulting into a panic (`RESTART_REASON 0x0fa0065c`
  unmapped); the 691 §5 `entry_note_wfi` readback; `entry_reset.h`'s false IMEM claim; 676 §6 / 677 §6; the
  684-owed runner clause for the 678 arm; `tools/xnu_dt_requirements.py` and the `"master"` value; the two
  peer-lane tripwire repairs; the gate's narration for `STAGE90_XNU_STORAGE_PROBE` (now **two** values
  short — rungs 2 and 3; reported to the peer lane by message, not by edit); and **the seam address pinned in
  two files** (`entry_trace.c`'s `STAGE90_XNU_SEAM_LR` and `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`),
  which this arm did not move — measured, not assumed: this image's seam clause reports `0x800472dc`.
* **The step after the reset**: the clock (`sdhci_set_clock` on `CLOCK_CONTROL`) and only then the bus power
  state (`CORE_PWRCTL_CTL`), because the first act the *medium* would see is a command, and 531's sequence
  puts the bus-power/IO-voltage state ahead of `CMD0`/`CMD8`.
