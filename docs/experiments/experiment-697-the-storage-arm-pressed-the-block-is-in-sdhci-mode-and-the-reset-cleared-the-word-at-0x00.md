# 697: the storage arm pressed — the block is in SDHCI mode, the reset took 6 ticks, and the word at `0x00` is not the version register

694 answered 531 §6's open question in the direction that costs a write: the eMMC controller the payload
inherits is **not** in SDHCI mode (`_mode_bit=0`, `_hc_mode=0x2000`). 696 built the arm that performs the
vendor's own mode sequence (`sdhci-msm.c:2841-2868`) — the first arm in this project that **stores to a
device block** — and pre-registered its cells before it was built. This is its press, and **every cell it
pre-registered is filled in the positive direction**: the block is in SDHCI mode, and the two registers the
arm deliberately does not touch did not need touching.

```
readiness  5/5, exit 0 - the live arm is armed-storage-mode-57d55fc9, 11/11 files, byte-identical to the park
gate       exit 0, 570 lines, under --allow-xnu-entry (derived from this arm's own switches)
runner     EXIT 0 - returned and captured, "the device came back 27s after this run called `fastboot boot`"
           (seen via: adb); capture 615,362 B, sha256 ce54a0539371d44d933c976318af25711de29ea5b15f0f1d5809a1b324004008
send       one non-persistent `fastboot boot`, serial 4a2fe00b, neighbour 33e80afe off both lists
```

Capture archived by hand (the runner does not archive it) under
`out/stage90/captures/697-storage-mode-pressed-2026-09-25-{last_kmsg.txt,run.log,gate.log,press.log}` —
`out/` is gitignored, so the record can carry the readings and not the files. 4,944 `xnu_live_` lines, 58 of
them `xnu_live_storage_*`, 25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`.

## 1. The sequence, store by store, as the log publishes it

The eleven keys are written in the source's own order, and `_writes` is published as a **progress count**
(0, then 1/2/3/4), so a run that stopped mid-sequence is localised by `_mode_stage` rather than by where the
log ends:

| the log's order | reading | what it is |
| --- | --- | --- |
| `_mode_calls=1`, `_mode_refused=0`, `_mode_stage=1` | the sequence ran, once, on the probe's first call | §4's version guard passed: `_mci_version=0x10000011` is neither 0 nor `0xffffffff`, so `_mode_refused` stayed 0 and the arm was allowed to write |
| `_writes=1`, `_mode_w0_read=0x00000000` | store 1 landed | `CORE_HC_MODE <- 0` read back **0**, so `FF_CLK_SW_RST_DIS` is now **clear** on this block — the cell 696 §1 pre-registered, because the handed-over word (`0x2000`) had it set |
| `_writes=2`, `_mode_power_wr=0x000004C1` | store 2 landed | `CORE_POWER` wrote `0x441 \| (1<<7)` = `0x4C1` — **the `\|CORE_SW_RST` form and nothing else**, with `_core_power=0x441` the value 694 read |
| `_mode_rst_polls=1`, `_mode_rst_steps=0`, `_mode_rst_ticks=0x6`, `_mode_rst_cleared=1`, `_mode_timeout=0` | the poll returned immediately | `CORE_SW_RST` was **already clear on the first read**: 6 CNTVCT ticks at the device's own 19,200,000 Hz = **0.3125 µs** |
| `_mode_pwrctl_status=0x00000000` | §3's hazard, measured | the reset latched **no** power-IRQ status — see §4 |
| `_writes=3`, `_mode_w1_read=0x00000001` | store 3 landed | `CORE_HC_MODE <- HC_MODE_EN` read back `1` |
| `_writes=4`, `_mode_w2_read=0x00002001`, `_mode_bit_after=1` | store 4 landed | the final `HC_MODE` is `0x2001` = `FF_CLK_SW_RST_DIS \| HC_MODE_EN` — **the vendor's own end state, bit for bit** — and `HC_MODE_EN` is now SET where 694 measured it clear |
| `_mode_stage=7`, `_mode_hci_version=0x00000000`, `_mode_capabilities=0x742dc8b2` | the post-sequence pair | **neither branch of the pre-registered cell** — see §3 |
| `_mode_stage=8` | the whole sequence ran | `_writes=4`, no timeout, no refusal |

**So 531 §6's question is answered as a state and not as a prediction.** The vendor's mode sequence was a
**prerequisite** on this machine, and it has now been performed: the block the payload hands over is in
SDHCI mode, on the same boot, with the four writes the vendor's own driver makes at probe. **And the write
that cost the most thought is the one that did not happen**: `POWER_CONTROL 0x29` — in the *other* window
(`hc_mem`), where writing 0 **is** a bus-off request on this SoC — is untouched, not because this run
checked that it was (a run cannot) but because `build_entry.sh`'s store census refuses to link an image whose
probe stores anywhere but the four offsets on the controller's base, and this arm passed that clause.

## 2. The reset's own number, against the one the vendor wrote down

The vendor's comment says the SW reset "can take up to 10HCLK + 15MCLK cycles" and, "calculating based on
min clk rates … it comes to ~40us. Let's poll for max. 1ms". This arm's bound is deliberately 100 ms (100×
the vendor's) and the measurement is that **nothing was waited for at all**: `_mode_rst_polls=1` with
`_mode_rst_steps=0` and 6 ticks.

That is not a contradiction of the vendor's number, and the record says so rather than scoring it: their
figure is a *worst case derived from minimum clock rates*, and this block's clock was running. What the
reading buys is that the reset is not the slow part of bring-up on this hardware, and that a future arm can
keep a 100 ms ceiling without expecting to use it. `_mode_timeout=1` with `_writes=2` and `_mode_stage=4`
remains the failure path — pre-registered, and **not** this run's cell.

## 3. The one cell that came back as neither branch: the word at `hc_mem + 0x00` is not a version register

696 §5 pre-registered the post-sequence pair as two branches: *a differing pair is the mode change made
visible; an identical pair is the finding that the mode bit alone does not change these two words.* The run
gives **`(0x00000000, 0x742dc8b2)` against 694's pre-mode `(0x00000010, 0x742dc8b2)`** — one word moved and
the other did not. And the word that moved moved to the value this project's **own refusal guard treats as
"the block did not answer"** (`_mci_version` 0 or `0xffffffff` is how 696 §4 decides not to write at all), so
the reading cannot be left as "the register file now answers".

**The explanation is a header's, not this record's.** `entry_storage.c` cites the two standard registers as
"`HCI_VERSION 0x00` and `CAPABILITIES 0x40`". The vendor's own tree disagrees about the first:
`drivers/mmc/host/sdhci.h:27` is `#define SDHCI_DMA_ADDRESS 0x00`, `:181` is
`#define SDHCI_CAPABILITIES 0x40` and `:241` is `#define SDHCI_HOST_VERSION 0xFE` — and `sdhci-msm.c:2909`
reads the version through `SDHCI_HOST_VERSION`, i.e. at `0xFE`, not `0x00`. So:

* **`hc_mem + 0x00` is the SDMA system address register**, a *soft* register the core reset clears. The word
  reading `0x10` before the sequence and `0` after it is therefore **a second independent positive
  measurement that `CORE_SW_RST` did what the poll said** — the same event as `_mode_rst_cleared=1`, read at
  a different address.
* **`hc_mem + 0x40` is the capabilities register**, a strapping-derived constant, and it is unchanged at
  `0x742dc8b2` — which is what makes the pair's shape consistent rather than odd: a soft register cleared,
  a constant not.
* **The first spec-valid reading of the version register is still owed**, and it must be taken at `0xFE`.
  `_hci_version`/`_mode_hci_version` are *names* for a register the image does not read; the next arm that
  touches `entry_storage.c` renames them and adds the read (the repair is inside `xnu_arm_boot/`, which is
  content-hashed into this arm's identity — 673 — so it waits for the next edit in that directory, exactly
  as 695's `TEX[0]` comment did).

So the cell is answered in a third way, and the honest sentence is: **the mode bit did change one of the two
words, the word it changed is not the one the cell was named after, and the change is a measurement of the
reset rather than of the mode.** The standard register file's *validity* in the state the vendor's sequence
leaves is not established by this run either way — which is what the pre-registration's "identical pair"
branch was reaching for and what neither branch anticipated.

## 4. §3's hazard, measured: the power-IRQ status did not latch

696 §3 named one hazard it could not mitigate and would only measure: the vendor's sequence acknowledges
`CORE_PWRCTL_CLEAR 0xE4` after the reset because the reset "may trigger power irq if previous status of
PWRCTL was either BUS_ON or IO_HIGH_V", and the previous state on this machine **was** bus-on
(`_core_power=0x441`). This arm deliberately does not write the acknowledge — it registers no handler — and
reads the status instead:

```
xnu_live_storage_mode_pwrctl_status 0x00000000
```

**Nothing latched.** The specific cell the vendor's comment predicts did not occur on this hardware state,
so the acknowledge this arm declined to write was not owed, and the open question 696 §3 recorded is closed
for this state with a reading rather than a mitigation. (What it does not close: a *future* reset from a
different `CORE_PWRCTL_CTL` state, which is why the key stays in the log and not in a comment.)

## 5. The ending, the panic, and the fourth return time

The arm's ending is unchanged from 690: `__wrap_platform_cache_idle_exit` baselines `entry_counter()` on its
first return and tail-branches into `entry_post_clock`, which calls `entry_seam_end_run` on the return whose
elapsed ticks have reached 115,200,000.

```
xnu_live_post_t0               0x06fe9d47     the baseline
xnu_live_post_cntfrq           0x0124f800     = 19,200,000 Hz, the machine's own rate (the third press
                                              to carry it: 691, 694, 697 - one per clock-bearing arm)
xnu_live_post_elapsed          0x0927730f     = 153,580,303 ticks = 7,998.97 ms
xnu_live_post_end_calls        0x00000008     the ending FIRED, on the 8th return
xnu_live_slot_post_calls       0x00000008     the exit returned through the wrapper 8 times
```

**The deadline was crossed 1,999.0 ms before the return that ended the run** (153,580,303 − 115,200,000 =
38,380,303 ticks). 694's arm overshot by 11.011 ms, because its boundary landed just before a return; this
one landed just after a sleep began, so the ending waited out that sleep. **The two runs together measure
the arm's resolution: it is one idle sleep (`~2007 ms` of park), and a single run's overshoot says only
where the boundary fell inside it.** That is the same reading 691 took from the other side.

**Then the ending's own first store faulted, and this time the panic path is the one the log shows** — but
the log also shows that *nothing about that is new*:

```
panic(cpu 0 caller 0x80455668): kernel abort type 4: fault_type=0x3, fault_addr=0xfa0065c
r12: 0xde58febe  sp: 0x80553ec8  lr: 0x8047c5bc  pc: 0x8047c488
cpsr: 0x80000093 fsr: 0x00000805 far: 0x0fa0065c
Attempting system restart...MACH Reboot
```

`pc=0x8047c488` = `entry_seam_end_run + 0xc` (this image's `entry_seam_end_run` is at `0x8047c47c`), the
`str` to `RESTART_REASON` at `0x0fa0065c` — **the same store, at the same offset, that 684 measured on 678's
arm and 690/694 measured on theirs**, with the entry image's `+0x1000` shift applied to both `pc` and `lr`.
`frame_ok=1`, `sleh_storm=9`, `sleh_seen=9`.

**And 694's record's sentence "recovered rather than fatal (`frame_ok=1`, no `panic ... sleh_abort`)" is
corrected here, from 694's own parked capture**: that capture carries the same
`panic(cpu 0 caller 0x80454668): kernel abort type 4: fault_type=0x3, fault_addr=0xfa0065c` at its own line
4012 and the same `Attempting system restart...MACH Reboot` at line 4019. The frame was recovered
(`frame_ok=1` is true) and the abort **still took the panic path**. The distinction 694's record drew was
drawn between two things that did not differ — the correct form is **"the frame was recovered, and the
kernel panicked on the abort anyway"** — and this is the class
[[mi4-a-claim-in-a-comment-is-not-a-check]] names, where the claim was a record's rather than a comment's.

**And the return is a fourth measurement with still no attribution**: the device came back **27 s** after
`fastboot boot` (seen via adb), against 8 s (690), 17 s (692) and 24 s (694). What this run adds to the
question is that the *end of the log is a panic and not a stall*, so the machine reached XNU's own reboot
path — and that path cannot reset this device ([[mi4-xnu-reboot-path-cannot-reset]]: `PE_halt_restart` is an
unfilled `.bss` slot and `halt_all_cpus` ends in `b .`). So the reset is a hardware write, and the two
candidates remain the watchdog bite (`0xf9017014`) and `PS_HOLD` (`0xfc4ab000`). **This is the first arm in
the sequence whose own table maps `0xFC4`** — the megabyte `PS_HOLD` lives in — so `entry_epilogue`'s second
store was reachable in principle; the log cannot say whether it happened, because the fatal abort is the
*ending's* first store and the epilogue's own two stores are not published as keys.

## 6. What this does not do

* **No storage.** Four stores to the controller's own mode/power registers put the block in SDHCI mode. No
  command is issued, no sector is read, no partition table is parsed, no filesystem is mounted, and the eMMC
  *device* is not addressed at all. `_writes=4` counts four stores to `CORE_HC_MODE`/`CORE_POWER`; nothing
  was written to the medium.
* **No driver beyond the fixture**, and the goal's floor is unchanged: pid 1, a driver answering the first
  `open` (error 0), the fixture's `0xfeedface` read into a user page — a floor 520 and 533 also met.
* **It does not prove the mode change made the register file usable** (§3): the version register is not read
  by this image at all.
* **TWRP-to-storage stays withheld.** The user's condition is that the OS can already be entered and stays;
  the OS is not observed doing that, so no write to storage is made — and this step is the first one where
  the difference matters, because the controller now *is* in the mode a driver would use.

## 7. Owed

* **The register names, and the read that was never taken** (§3): `entry_storage.c`'s
  `ST_SDHCI_HCI_VERSION 0x00` and the two keys named after it should be the **SDMA address** register, and
  the version lives at `0xFE`. The repair is a rename plus the `0xFE` read, inside `xnu_arm_boot/` — so it
  waits for the next edit in that directory (673), which is the next arm below.
* **What actually returns a run** (§5): four measurements (8/17/24/27 s) and no attribution. This run narrows
  it to a hardware write and leaves the two candidates named; `entry_epilogue`'s own two stores are not
  published as keys, and the arm that answers it must publish them.
* **The ending's first store still faults, and the panic path is what follows it** (§5): `RESTART_REASON`
  `0x0fa0065c` is in a megabyte no table of this image maps, so the forced ending is a *panic* rather than a
  quiet stop, and 678/686/690/694/697 all read the same way. The repair is one more section (index `0xFA`
  does not collide with `0xFC4`/`0xF98`) or a decision that `RESTART_REASON` is not this project's to write.
* **Carried, unchanged**: the 691 §5 one-store `entry_note_wfi` readback; `entry_reset.h`'s false IMEM
  claim; the `RESTART_REASON` decision above; 676 §6 / 677 §6; the 684-owed runner clause for the 678 arm;
  `tools/xnu_dt_requirements.py` not encoding the `"master"` value; the two peer-lane tripwire repairs
  (`STORAGE_SYM_RE` and `tools/check_storage_refs.py`); the gate's narration for
  `STAGE90_XNU_STORAGE_PROBE` being one value short (reported to the peer lane by message in this step); and
  **the seam constant pinned in two places** — `entry_trace.c`'s `STAGE90_XNU_SEAM_LR` and
  `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`, which this step had to move to `0x800472dc` on the runner
  side after readiness row 3 refused the tree **before any press was spent**.
* **The step after this one, named so it is not re-derived**: with the block in SDHCI mode, the next act is
  the **driver's own reset path** — `sdhci_reset(SDHCI_RESET_ALL)` and the version read at `0xFE` — and only
  then a clock/power state (`CORE_PWRCTL_CTL`) before any `CMD0`/`CMD8`, because a command sequence against
  a bus whose power state the OS has not set is the first act the *medium* would see.
