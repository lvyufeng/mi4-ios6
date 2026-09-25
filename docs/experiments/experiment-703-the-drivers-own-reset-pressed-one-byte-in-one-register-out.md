# 703: the driver's own reset, pressed — one byte in, one register out, and the byte was already clear

701 pre-registered rung 4 (`STAGE90_XNU_STORAGE_PROBE=4`): the vendor's own
`sdhci_reset(SDHCI_RESET_ALL)`, which out of `sdhci.c:229-279` is **one byte store and a bounded poll**,
and which is **this project's first store through `hc_mem`** — every device store before it was on
`core_mem`. This is its press. Every cell the pre-registration named is filled, and the one thing the
rung was for is now a measurement: **the reset moved exactly one register and that register returned to
zero inside 16 ticks of this machine's own counter.**

```
readiness  5/5, exit 0 - the live arm is armed-storage-reset-00b28262, 11/11 files, byte-identical to
           the park (tools/verify_revert_set.sh: VERIFIED, 11 files, 6 manifest-member checks agree)
gate       exit 0, 572 lines, under --allow-xnu-entry (derived from this arm's own switches)
runner     EXIT 0 - returned and captured, "the device came back 27s after this run called
           `fastboot boot`" (seen via: adb); capture 614,436 B, sha256 b966ecac914fd66d…
send       one non-persistent `fastboot boot`, serial 4a2fe00b, neighbour 33e80afe off both lists
fired by   /tmp/g668/press-on-clear.v5.sh (sha 7f23cae5…), armed by hand as
           `press-on-clear.v5.sh armed-storage-reset-00b28262 600`, set declared with no default;
           the launcher printed `done. gate=0 runner=0` and exited, so no firer is armed and 660's
           closure has released
```

Archived by hand under `out/stage90/captures/701-reset-pressed-2026-09-25-{last_kmsg.txt,run.log,gate.log,press.log}`
(`out/` is gitignored, and the runner does not archive the capture). 4,905 `xnu_live_` lines, 100 of them
`xnu_live_storage_*` — 79 distinct keys against the census arm's 62, the difference being exactly the
seventeen this rung adds.

## 1. The rung's own cells, in the order the function runs

| key | reading | the cell, against 701 §4's pre-registration |
| --- | --- | --- |
| `_rst_calls` | `0x1` | the function ran |
| `_rst_before` | **`0x00`** | `SOFTWARE_RESET` carried no reset bit at entry, so the self-clearing register's refusal path is **not** the path taken — which is what 699's `_reg_software_reset=0x00` said would happen, and the first time that before-value has been spent |
| `_rst_refused` | `0x0` | consistent with the above; the refusal cell exists and is empty |
| `_rst_wrote` | **`0x1`** | the store was reached |
| `_rst_stores` | **`0x00000001`** | **counted bottom-up from the artifact, not repeated from the record**: 701 §4 registered `_rst_stores=1` and the log's number is a second derivation of the same fact, on the same shape `_reg_loads` uses |
| `_rst_stage` | `0x1`, `0x2`, `0x3` | the three stages published in order, ending at done — so a run that stopped would have been localised by this key and not by where the log ends |
| `_rst_polls` | **`0x1`** | **the reset bit was already clear on the FIRST read.** 701 §1 cited 697's `_mode_rst_polls=1` on the vendor's `CORE_SW_RST` as evidence that this controller's reset bits behave self-clearingly; this is the second derivation, on the standard file's own bit, in a different block |
| `_rst_steps` | `0x0` | no extra pass through the outer loop |
| `_rst_ticks` | `0x00000010` | **16 ticks = 0.83 µs** at the 19,200,000 Hz this log's own `_post_cntfrq` carries — against the driver's bound of 1,920,000 ticks = 100 ms. The bound was there and was not consulted |
| `_rst_cleared` | `0x1` | the poll's terminal state, published rather than inferred from the count |
| `_rst_timeout` | `0x0` | the driver's own `Reset 0x%x never completed` path was not taken |
| `_rst_done` | `0x1` | the function completed |

## 2. The five after-values, and the sentence the reset was never allowed to make true

701 §4 registered these as the cells that turn "the reset did nothing else" from an argument into a
measurement, and the rung's most important one is the cell 698 §2 explicitly owed:

| register | before (699) | after (this press) | what the pair says |
| --- | --- | --- | --- |
| `SOFTWARE_RESET 0x2F` | `0x00` | **`0x00`** | the byte the poll tested, read after the poll: it is clear, so the poll's terminal state is a reading and not an inference |
| **`POWER_CONTROL 0x29`** | `0x00` | **`0x00`** | **the owed cell.** The standard reset did **not** turn the bus on: bit 0 (`BUS_POWER`) is still clear, so the block's belief about the bus is exactly what 699 found, and the driver would have nothing to reconcile with the vendor's own `CORE_PWRCTL`. A value with bit 0 set would have meant the reset changed that belief |
| `CORE_PWRCTL_STATUS 0xE4` | — | **`0x00`** | **the hazard's cell, and it is empty.** 696 §3 pre-registered that the reset may latch a power-IRQ status when the previous state was `BUS_ON`, and `_core_power` says the state was; nothing latched. And `_reg_pwrctl_mask=0x0000000f` is what makes that readable as *nothing latched while four bits were routed* rather than *nothing was listening* |
| `HOST_CONTROL 0x28` | `0x00` | **`0x00`** | identical to its before-value |
| `PRESENT_STATE 0x24` | `0x01f80000` | **`0x01f80000`** | identical to its before-value, both inhibit bits clear before and after — so no transfer was aborted, because there was none to abort |

**So "this reset moved exactly one register" is a measurement**: four registers the generic core's reset
path can touch on other platforms read the same word after the reset as before it, and the fifth — the
one the rung wrote — is back to the value that made writes to it safe.

## 3. The whole ladder reproduced under changed code

The rung is additive (the reset runs *after* the census), so this press is also a re-run of rungs 1–3 on
an image whose code grew under them:

| what | reading | the point |
| --- | --- | --- |
| the mode sequence | `_mode_bit=0x0` → `_mode_bit_after=0x1`, `_mode_refused=0`, `_writes` 0/1/2/3/4, `_mode_w0_read=0`, `_mode_w1_read=1`, `_mode_w2_read=0x00002001`, `_mode_rst_polls=1` with `_mode_rst_ticks=0x6` | every number 697 and 698 took, again — the controller this payload inherits is still the bootloader's state, so the vendor's sequence is a precondition on **every** boot |
| the census | `_reg_loads` reaches `0x0000000a` with `_regs_done=0x0000000a`, and `_reg_software_reset`/`_reg_power_control` both `0x00` before the reset | the ten reads and the count that derives them a second time |
| the vendor's own hazard reading | `_reg_pwrctl_mask=0x0000000f`, `_mode_pwrctl_status=0x0`, `_reg_pwrctl_status_after=0x0` | the three quarters of one fact: a status register cannot say whether anything was listening, the mask says it was, and the after-read says nothing latched again |
| `CLOCK_CONTROL 0x2C` | `0x0003` before and after | bit 0 internal-clock-enable and bit 1 stable set, **bit 2 `SD clock enable` clear** — so the reset's own `host->clock = 0` has no register to move and the clock is still genuinely later |

## 4. The ending, the panic, and the sixth return time

```
xnu_live_post_t0          0x075abe53     the baseline
xnu_live_post_cntfrq      0x0124f800     = 19,200,000 Hz, the machine's own rate (fifth press to carry it)
xnu_live_post_elapsed     0x06e0d551     = 115,397,969 ticks = 6,010.31 ms
xnu_live_post_end_calls   0x00000007     the ending FIRED, on the 7th return
xnu_live_slot_post_calls  0x00000004     published at 1, 2, 3, 4 - the count passed 4 and the run ended at 7
```

**The deadline was crossed 197,969 ticks (10.31 ms) before the returning pass** — beside 694's 11.011 ms,
697's 1,999.0 ms and 698's 15.49 ms, i.e. inside one ~2007 ms idle park every time. The ending's own
first store faulted into the same panic as 697's and 698's:

```
panic(cpu 0 caller 0x80455668): kernel abort type 4: fault_type=0x3, fault_addr=0xfa0065c
Attempting system restart...MACH Reboot
```

**And the return is a sixth measurement: 27 s**, against 8 (690), 17 (692), 24 (694), 27 (697) and 24
(698). It is still unattributed. What this run adds is only that the same interval appears on an arm
whose code changed by 576 bytes of `.text` and whose ending overshoot is 10.31 ms, while 697 and 698 share
24 s at 1,999.0 ms and 15.49 ms: **the interval is not a function of the ending's own clock, and it is not
a function of the image either.** The two candidates named in 690's §7 stay named — the watchdog bite
`0xf9017014` and `PS_HOLD 0xfc4ab000` — and the memory's own note that no arm has been observed to
complete a `PS_HOLD` store still stands.

The runner's seam block is the same block 697's and 698's runs printed, cell for cell: `PASS
seam_lr=0x800472dc` (the hook was entered at the exit's own call), the `FAIL seam_sp=0x80553ec8 is not
sleh_sp-8` and the `CLEAN LINE` pair `b=0x800b3648/0x8047db04` — all three are the
`STAGE90_XNU_SLOT_NULL=1` switch doing what 698 §3 said it does, so nothing here is new.

## 5. What was proven about the artifact before the press, and not by this log

The log is a reading of the *run*. Three of the claims this step makes are readings of the **artifact**,
and each was made twice from different directions:

* **the store census** — `build_entry.sh` refuses the build unless every store in `entry_storage_probe`
  lands on `core_mem` at `0x78/0x00/0x78/0x78` in that order *and* every store in `hc_mem` is exactly one,
  at offset 47 (0x2F), as a byte store. Its own line for this image reads
  `core_mem [120 0 120 120 ] on r5, hc_mem [47 ](strb ) on r5, image [r6:0:str:16804 r6:4:str:16804 ]`,
  and the live disassembly shows the same five stores at `0x8000d350`, `0x8000d38c`, `0x8000d47c`,
  `0x8000d4bc` and **`0x8000d728 strb r6, [r5, #47]`** — so `POWER_CONTROL 0x29`, four offsets away, is
  unreachable in the linked image and not by review.
* **the four mutations** that were refused (`POWER_CONTROL` added as a second `hc_mem` store → `[47 41 ]`;
  the reset compiled in on rung 3 → *"every rung below 4 is defined as reading that window and writing
  nothing in it"*), plus three that reached the **AMB** clause instead of the one they were written for —
  recorded in `revert-set.txt`'s rung-4 block, whose honest state is that **the width clause was never
  observed to fire**.
* **the build itself** — the rung-4 build is deterministic (the clean rebuild reproduces
  `00b28262…` byte for byte), and the step's **first** build failed inside the builder's own reader
  rather than in the image: `experiment-702` is that defect, and it is why this step has two records.

## 6. What this does not do

* **No storage.** One byte written to one register, and five reads beside it. No command, no sector, no
  partition table, no mount — and the eMMC *device* is not addressed at all.
* **No clock and no power state.** No `CLOCK_CONTROL` write, no `POWER_CONTROL` write, no `CORE_PWRCTL`
  write. The rung deliberately stops short of `check_power_status` (§701 1) because that call is an
  unbounded wait on a power IRQ.
* **No driver beyond the fixture.** The goal's floor is met and unchanged: pid 1, the driver's `open`
  answering `0x00000000` while the control open answers `0x00000002`, and the fixture's `0xfeedface` read
  into a user page. The runner's own note is right that this is the floor 520 and 533 also met.
* **TWRP-to-storage stays withheld**: the user's condition is that the OS can already be entered and
  **stays**, and the OS is not observed doing that.

## 7. Owed

* **The clock, now the named next step and named by a reading**: `CLOCK_CONTROL` reads `0x0003` before
  *and* after this reset (bit 2, `SD clock enable`, clear both times), so `sdhci_set_clock` is still ahead
  of the driver — and on this SoC that means `sdhci_msm_set_clock`'s `CORE_VENDOR_SPEC 0x10C` plus
  `clk_set_rate` on the GCC's SDCC clocks, i.e. **map the `0xfc400000` megabyte first**, which is where
  692's fault was and the block whose one gate the probe reads as an interlock.
* **The width clause of the store census has not been observed to fire** (§5).
* **What actually returns a run** (8/17/24/27/24/27 s): this run adds the negative fact that the image
  changed and the interval did not.
* **Carried, unchanged**: the ending's first store still faulting into a panic (`RESTART_REASON
  0x0fa0065c`); the 691 §5 `entry_note_wfi` readback; `entry_reset.h`'s false IMEM claim; 676 §6 / 677 §6;
  the 684-owed runner clause for the 678 arm; `tools/xnu_dt_requirements.py` and the `"master"` value; the
  two peer-lane tripwire repairs; the gate's narration for `STAGE90_XNU_STORAGE_PROBE` being two rungs
  short (reported by message, 700 §8); and the seam address pinned in two files.
