# 707: the rung-6 clock set pressed — eight stores and no rate, the readback that did not hold the field, and four defects the arm's own build found first

706 pre-registered the **first rung that writes the clock controller** and read the vendor's own source
for it: `sdhci_msm_set_clock` at the 400000 Hz `mmc_rescan_try_freq` asks for, six writes and five
bounded polls, and — the finding that made the rung small — **no rate write at all**, because
`sup_clock == msm_host->clk_rate` on the first call. The arm was built at `STAGE90_XNU_STORAGE_PROBE=6`,
parked as **`armed-storage-clkset-b4b696d9`** (entry bin `b4b696d9…`), and pressed once. Every key the
pre-registration named is in the log, `_clk_set_rate_writes` reads **0**, and the one cell whose
alternative the pre-registration spelled out — `_clk_set_vendor_after` — came back as the alternative.

## 1. The press

| | |
| --- | --- |
| readiness | **5/5, exit 0** (22:06:10), the live arm found by hashing `stage90-qcdt.img` (`c2e34cda…`), 11 files vs the park |
| gate | **exit 0, 574 stdout lines** (22:06:37), under `--allow-xnu-entry` — the flags readiness derived from the arm's own switches |
| runner | **EXIT 0** (22:07:42) — returned and captured, `--expect-arm=armed-storage-clkset-b4b696d9` |
| the device | back **28 s** after `fastboot boot` (seen via adb) — the eighth return-time measurement (8, 17, 24, 27, 24, 27, 25, **28** s) |
| capture | 618,572 B, sha256 `498fa36dc11c650f376de75fb126c9cd713ad029773fa19c21dd0e5538ff0aa2`, archived by hand under `out/stage90/captures/` as `rung6-clkset-20260925-220856-{last_kmsg.txt,gate.log,run.log,press.log}` (`out/` is gitignored, and the runner does not archive the capture: 672's rule) |
| the firer | `/tmp/g668/press-on-clear.v5.sh` (sha `7f23cae5…`), `armed-storage-clkset-b4b696d9 600`, neighbour `33e80afe` off both lists (`fastboot: []`, `adb: [4a2fe00b device]`), `done. gate=0 runner=0` — **no firer is armed** |

The runner's own verdict list is the one every arm since 690 prints, with the same single FAIL:
`PASS no 'panic ... sleh_abort' in the log`, `PASS slot_cwe_win=0x30c57879 has SCTLR.C clear`,
`PASS slot_post_calls=0x00000004`, `PASS seam_lr=0x800472dc is the exit's own call's return address`,
`FAIL seam_sp=0x80553ec8 is not sleh_sp-8` (the `STAGE90_XNU_SLOT_NULL=1` fingerprint 697, 704 and 705
also printed), `PASS b1=0x8047db04`, `CLEAN LINE the pair came back unchanged`, and the floor
(pid 1, the driver's `open` answering 0, the control open answering `ENOENT`, the fixture's `0xfeedface`
read into a user page).

## 2. The forty keys, cell by cell

`_clk_set_calls=1`, `_clk_set_done=1` — the sequence ran and completed. **`_clk_set_writes` reaches
`0x8`** and **`_clk_set_gcc_writes` reaches `0x4`**, and §3 is about the first of those numbers.

**The four branch enables, and every one of them wrote back the word it read** — which is what 705's
readings predicted and what makes the four stores unable to gate a clock:

| branch | `_before` | `_after` | `_polls` | `_ticks` | `_halted` |
| --- | --- | --- | --- | --- | --- |
| `ahb` (`0x4C8`) | `0x2000cff1` | `0x2000cff1` | 1 | 10 | 1 |
| `apps` (`0x4C4`, the gate's own word) | `0x00004ff1` | `0x00004ff1` | 1 | 11 | 1 |
| `ff` (`0x4E8`) | `0x00000001` | `0x00000001` | 1 | 10 | 1 |
| `sleep` (`0x4E4`) | `0x00000001` | `0x00000001` | 1 | 10 | 1 |

Every halt check passed on its **first** read (`_polls = 1` on all four, and `_ticks` = 10/11/10/10 at
19,200,000 Hz, i.e. ≈0.5 µs each) — 706 §2's prediction, made from `branch_clk_halt_check`'s
accept-both rule (`BRANCH_ON_VAL` or `BRANCH_NOC_FSM_ON_VAL`) plus 705's own
`_clk_ahb_cbcr = 0x2000cff1`, whose bits 31:28 are the `0x2` that only the second value accepts. **The
AHB branch is the one whose `ON` is the NoC-FSM value and this arm is the measurement of it.**

**The vendor spec's two read-modify-writes, and the cell that came back the other way**:
`_clk_set_vendor_before=0x00000000` (705's reading), **`_clk_set_vendor_w1=0x00000200`** — MCLK select
written as `CORE_HC_MCLK_SEL_DFLT` = `2 << 8` exactly as `sdhci-msm.c:2497-2499` writes it —
**`_clk_set_vendor_w2=0x00000000`** (a read-modify-write of the field the first store did not touch, so
the pair is a sequence and not one store), and **`_clk_set_vendor_after=0x00000000`**. 706 §3 wrote the
alternative out in advance: *"`0` here would mean the field did not take"*. It did not take: the register
read back `0` after a store the artifact shows writing `0x00000200`, and the second store's own word
being `0` follows from the readback, so **two stores were spent and the register did not change.** This
is a new question about `CORE_VENDOR_SPEC 0x10C` and it is owed (§8): the block's register file answers
reads (this byte has been read since 698), so the field is either write-through-to-nowhere in this
state, or needs something this arm did not do.

**The divisor arithmetic, and the two candidate device trees told apart in the log**:
`_clk_set_max_clk=0x16e36000` = 384,000,000 — the **`msm8974pro.dtsi:1768` table**, not the 200 MHz one —
so `_clk_set_divisor_alt=0`, `_clk_set_real_div=0x3c0` (960) and `_clk_set_div=0x1e0` (480), which packs
to the pre-registered word.

**CORRECTED 2026-09-25 (710): the two `CORE_VENDOR_SPEC` stores above went to the wrong window, and the
sentence "the field did not take" is an address error rather than a device finding.** `#define
CORE_VENDOR_SPEC 0x10C` (`sdhci-msm.c:92`) is used **23 times in that file and every one is through
`host->ioaddr`** — `:587`, `:597`, `:646`, `:653`, `:2079-2085`, `:2416-2433`, and the two read-modify-
writes this rung read out (`:2490-2512`) — and **zero times through `msm_host->core_mem`**.
`host->ioaddr` is `hc_mem` (`0xf9824900`), so the register the driver's MCLK select lives in is
**`0xF9824A0C`**, and this arm wrote `core_mem`'s `0x10C` = `0xF982410C`, 0x900 bytes away. The
measurements stand as measurements — `_clk_set_vendor_w1=0x200` was *computed* and `_clk_set_vendor_after=0`
was *read* — but they are readings of a register the driver never addresses for this field, so
**whether the MCLK field took is UNMEASURED**, and this document's conclusion (*"it ran and the
register says no"*) is withdrawn. Rung 8 (`docs/experiments/experiment-710-…`) reads both addresses in
one run and settles whether they are two registers or one; until then no rung moves the store. This is
[[mi4-one-value-two-definitions]] again — one offset, two windows — and the census could not catch it
because the census names windows by the `movt`/`movw` pair the *arm* materializes, not by the base the
*vendor's source* uses.

**The standard's two halfwords and the poll between them, and this is the rung's acting cell**:

| key | reading | what it means |
| --- | --- | --- |
| `_clk_set_cc_before` | `0x00000003` | `CLOCK_CONTROL` before, exactly 705's own `_clk_clock_control_after` and 699's `_reg_clock_control` |
| `_clk_set_cc_int` | **`0x0000e041`** | divisor 960 packed, `SDHCI_CLOCK_INT_EN` set, **`SDHCI_CLOCK_CARD_EN` still clear** — the pre-registered word |
| `_clk_set_cc_stable` | `0x00000001` | `CLOCK_INT_STABLE` asserted |
| `_clk_set_cc_polls` / `_cc_ticks` | `0x12` (18) / `0xbb` (187) | 18 reads and 187 counter ticks (≈9.7 µs) inside the driver's 20 ms bound — **not one read**, so the stability poll is a real poll |
| **`_clk_set_cc_card`** | **`0x0000e045`** | **`INT_EN \| CARD_EN` — the SD clock enable bit, set for the first time by any image in this project** |
| `_clk_set_cc_after` | `0x0000e045` | read back, so the word took |

**`_clk_set_rate_writes=0x00000000`** — §2 of 706 measured on hardware. The RCG's before-values are
unchanged and were never a *rate* this arm wrote: 705's `_clk_rcg_update=0` and `_clk_rcg_cmd=0` are
re-read in this log (the census runs before the clock set), and the count is what says the set did not
go near them.

The whole ladder reproduced under the new code: `_gate_read=1`, `_loads=6`, `_clk_loads=0x0000000c` (the
twelve clock-surface words), `_reg_loads` reaching 6, `_mode_bit=0` → **`_mode_bit_after=1`** with
`_mode_w2_read=0x00002001` (the vendor's mode sequence is still a *prerequisite* and still ends in the
same word), `_rst_cleared=1` on the reset's first poll, and the panicking end of §4.

## 3. The count was eight, and the pre-registration said six

706 §1 called rung 6 "six device writes and five bounded polls" and §3's table said
`_clk_set_writes` is "expected **six**: four CBCR read-modify-writes and two `CORE_VENDOR_SPEC` ones".
The log says **eight**. Both numbers are honest readings of a differently-drawn line: *six* is the count
of the vendor's `sdhci_msm_set_clock` stores in `CORE_VENDOR_SPEC` and the GCC, and *eight* is the count
of stores the sequence **makes**, because the two `CLOCK_CONTROL 0x2C` halfwords are stores of the clock
set too — the same function, the same call, the same rung. The artifact's own counter is the one that is
right about the artifact (`_clk_set_writes` is published after every store, bottom-up, which is what the
key says it is), and the pre-registration wrote the narrower definition beside the number. **This is
[[mi4-one-value-two-definitions]] again, and this time the two definitions were both in one document**:
§1's prose counted the vendor's two paths and §3's expected value inherited that count while the key it
named counts all four. The reading to keep: `_clk_set_writes=8`, `_clk_set_gcc_writes=4`, and the
difference — four — is the vendor spec's two plus the standard's two.

## 4. The ending, and the trace's own shape

`_post_t0=0x050cfa9f`, `_post_cntfrq=0x0124f800` (19,200,000 Hz), the deadline
`_seam_post_end_ticks=0x06ddd000` = 115,200,000 ticks = 6000.0 ms, and **`_post_end_calls=0x00000007`**
— the ending fired on the **7th** return, at `_post_elapsed=0x06e0953c` = 115,318,588 ticks =
**6006.2 ms**, i.e. **6.2 ms past the deadline**, inside one ~860 ms idle window. `_slot_post_calls`
reaches 4 and `_seam_calls` 4, with the pair `a0/b0 = 0x800b3648`, `a1/b1 = 0x8047db04` — the CLEAN LINE
the 686 correction says to read *beside* those counts and never alone.

**The trace's last published sample is not the end, and a reader (this one, twice) can take it for one.**
`_post_elapsed` is published on the powers of two of the pass count *and again* on the pass that ends the
run, so a log holding `0x00000000`, `0x001dae66` (101.3 ms) and `0x010dafe5` (921.2 ms) beside
`_post_end_calls=7` has its answer in the last line, not in the largest of the middle ones. **The
negative cell 689 pre-registered is the absence of `_post_end_calls`, and this log does not have it.**

The run's end is the same one 684 measured and every arm since has printed:
`panic(cpu 0 caller 0x80455668): kernel abort type 4: fault_type=0x3, fault_addr=0xfa0065c` — the
`entry_epilogue`'s first store to `RESTART_REASON`, with `pc=0x8047c488` (`entry_seam_end_run+0xc`),
`r2=0x0fa00000` and `r3=0x78665501` the value being written. **So this arm's ending "fired and did not
end the run" in exactly the sense 678/686/690 need the qualifier for: the deadline was REACHED, the
store faulted, and the boot went on until the panic.** What actually returns a run (8/17/24/27/24/27/25/**28**
s) is still unattributed.

## 5. Four defects the arm's own build found before the press could happen

The rung's first build refused to produce the arm the record describes, four times, and each refusal was
a real defect in code this file's authors wrote in this session. They are recorded here because they are
what the arm cost, and because three of them are the project's own defect classes arriving in its own
tools:

1. **`st_branch_enable` came out as a symbol, so the four GCC stores were not the probe's.** GCC does not
   inline a helper with four call sites (it does inline the single-call `st_clock_census` and
   `st_clock_set`), so the probe was pushed to `0x8000d09c`, the census read `core_mem [120 0 120 120 ]`
   — the two `0x10C` stores *were* in the inlined `st_clock_set` and did land — and the **GCC window came
   out EMPTY**. Fixed by making the helper `always_inline`: the stores now land in the body the clause
   reads, `entry_storage_probe` is back at **`0x8000cff8`**, and the growth (entry text
   **5310816 → 5312256**, exactly +1440 B) moved **two** symbols, `cpu_idle_wfi` (+1568) and `arm_init`
   (+1560), while **the seam `0x800472dc`, `platform_cache_entry/exit`, the wrappers and the probe's own
   `bl` did not move at all** — an alignment fill contracted by the same amount, which is
   [[mi4-linker-fill-term]] in the direction this project depends on. **CORRECTED 2026-09-25 (708):** this
   pair read `5315592 → 5317032` when this document was written, and both bases were wrong by exactly
   `+3336` while the delta was right. Measured with three tools that agree (`arm-none-eabi-size -A`,
   `objdump -h`, `readelf -S`) against the four parks: `.text` is `5309568` (rung 4) → `5310816` (rung 5)
   → `5312256` (rung 6) → `5316960` (rung 7), and `.data` is **`206512`** in all four, not the `206804`
   this document and the record below also carried — that value is experiment-501's `.data`, for an image
   whose `.text` was `5280296` and whose `.bss` was `362824`. The lesson is the pair's shape and not the
   arithmetic: **a correct delta applied to a stale base passes every check either number takes.**
2. **The window's store set was being measured per base register.** The census collected each window's
   offsets from *the base register carrying most of them* (`c[k] > m`), correct only while each window
   happened to be written through one register. Rung 6 broke that twice: the two `CORE_VENDOR_SPEC`
   read-modify-writes use a register the mode sequence's four do not, and `hc_mem`'s three stores use
   **three** different registers (`sl`, `r4`, `r3`). Worse, **with the counts tied the old expression's
   answer was decided by `awk`'s `for (k in c)` hash order** — a verdict that could change under an
   unrelated edit. The offsets and mnemonics are now taken from every store in the window in program
   order, the registers are reported as a set, and the refusal that used to stand here (`stb_other`,
   "this arm's pre-registration names one register carrying the four") is retired: **the register is
   GCC's business, the record names stores and offsets, and a proxy may decide when to ask and never
   whether to act** ([[mi4-one-value-two-definitions]], m697).
3. **The GCC clause could never have passed.** It compared `stb_gcc` built as `$2, $3` of the
   classifier's `<base>:<offset>:<mnemonic>:<low-half>` tuple — so its first reading of a non-empty GCC
   set was `[r4:1224:str:0: …]` against a record of `1224:str …`. **A comparison that cannot pass, in a
   clause whose subject had been empty on every rung below 6**, caught by the build it was written for.
4. **`sth` is not what `objdump` prints for this image.** The record and the clause both said `strb sth
   sth` for `hc_mem`'s three stores; the halfway store's ARM mnemonic is `strh`, and the first reading of
   a non-empty `hc_mem` set was `[strb strh strh ]`. Both spellings are `strh` now (706 §5 carries the
   correction).

The census the gate and the build now agree on for the pressed artifact — from `build_entry.sh`'s own
report, on the bytes that were sent:

```
xnu_entry_698: the probe's stores, classified by window and offset -
  core_mem [120 0 120 120 268 268 ] through [r4 ],
  hc_mem   [47 44 44 ](strb strh strh ) through [sl r4 r3 ],
  gcc      [1224:str 1220:str 1256:str 1252:str ],
  image    [r5:0:str:16804 r5:4:str:16804 ], ambiguous [], unknown [], unnamed-device []
```

## 6. Readiness's narration census was a false-refusal generator

`tools/verify_press_ready.sh`'s fourth row checks every `` `_name `` its narration writes against the
keys the image publishes, and it refused this press:

```
FAIL  the arm is named by a reading   the arm named above refers to [_clk_set_cc_int _loads _mode_bit] …
```

All three names are suffixes of keys this image publishes (`xnu_live_storage_clk_set_cc_int`,
`…_loads`, `…_mode_bit`). The row's test was

```sh
printf '%s\n' "$nar_keys" | grep -q -- "$_t\$" || printf '%s ' "$_t"
```

and under `set -o pipefail` (line 126) that pipeline's status is the **producer's** when the producer
fails: `grep -q` exits at the first match, and if that happens before the producer's write is through,
the write gets `EPIPE`, `printf` dies of `SIGPIPE` with 141, pipefail hands 141 to the `||`, and a name
whose key **is** published is reported missing. **Measured, not inferred**: the same six lines on the
same inputs, three runs, returned `[]`, `[_cbcr _clk_set_cc_card _clk_set_writes _gate ]` and
`[_mode_dma_address ]` — and a second readiness run of the same arm refused with nine *different* names.
A verdict that depends on **when the reader stopped** is the defect class this whole file exists to
catch, one level up, and its direction is the expensive one: a refused press costs the window, not the
run. The test is one `awk` process now (both inputs are in memory; no pipe, no race), the same false
refusal was removed from the device-list row (`grep -q … <<<"$fb"`), and the row reads 5/5 deterministically.

## 7. What rung 6 does not do

* **No rate** — `_clk_set_rate_writes=0`, and no `M`/`N`/`D`, no `CFG_RCGR`, no `CMD_RCGR` update bit.
  The RCG write is the **speed-change rung's**, and it is now a bounded act with measured before-values.
* **No block reset** — `BCR 0x04C0` (`BCR_BLK_ARES_BIT`) is in neither the store set nor the reads; no
  CBCR bit is cleared anywhere.
* **No card power.** The card is unpowered and `POWER_CONTROL 0x29` is still unreachable in the linked
  image: the `hc_mem` store set is `47 44 44` and offset 41 does not appear. The same
  `sdhci_do_set_ios` reaches `sdhci_set_power` — and **708 §1.2 corrects this bullet**: on this host
  `SDHCI_QUIRK_SINGLE_POWER_WRITE` is set (`sdhci-msm.c:2897`), so the zero write is **not** on the path
  and the act is one store (`pwr | SDHCI_POWER_ON`) followed by one
  `sdhci_msm_check_power_status(REQ_BUS_ON)`, whose **unbounded `wait_for_completion`** is the hazard and
  the next rung's subject, named and refused rather than attempted.
* **No command, no sector, no partition table, no mount**, and no driver beyond the fixture: the floor
  (pid 1, `open` answering 0, the control open `ENOENT`, the fixture's `0xfeedface` in a user page) is
  met exactly as 520, 533, 699, 703 and 705 met it.
* **TWRP-to-storage stays withheld**: the OS is not observed entering and **staying**.

## 8. Owed

* **`CORE_VENDOR_SPEC 0x10C`'s MCLK select did not take** (`_clk_set_vendor_after=0` after a store the
  artifact shows writing `0x00000200`). Is the field write-through-to-nowhere in this state, does it need
  the block's clock or a reset released, or is the readback of that byte not the field's? A rung that
  writes one field and reads it back at two addresses is the cheapest next step, and until it is
  answered, "the vendor's non-HS400 path ran" is half a claim: **it ran and the register says no.**
* **The RCG write** (the speed-change rung): `M`/`N`/`D`, then `CFG_RCGR`, then `CMD_RCGR`'s update bit
  and a bounded poll of it (`UPDATE_CHECK_MAX_LOOPS 500`), with `_clk_rcg_update=0`/`_clk_rcg_cmd=0` as
  before-values — unchanged by this press, as `_clk_set_rate_writes=0` says.
* **`CLOCK_CONTROL` bit 2 is set for the first time** (`0xE045`) and its consequences are entirely
  unmeasured: what the unpowered card does with an enabled SD clock, and whether the block raises
  anything the kernel's SDCC handler would see.
* **The root-versus-branch disagreement** (704 §2, 705 §2) is still owed a decision, not a reading.
* **What actually returns a run** (8/17/24/27/24/27/25/28 s), the ending's first store still faulting
  (`RESTART_ADDRESS 0x0fa0065c`), the width clause of the store census still never observed firing on a
  widened device store, `_clk_set_writes`' **8-vs-6** now corrected in the pre-registration's own terms
  and in the narration's, `BIT(29)` of `_clk_ahb_cbcr` published and unnamed, the 691 §5
  `entry_note_wfi` readback, `entry_reset.h`'s false IMEM claim, 676 §6 / 677 §6, the 684-owed runner
  clause, `tools/xnu_dt_requirements.py` and the `"master"` value, and the seam address pinned in two
  files.
* **Peer lane, by message and never by edit**: `stages/stage90/preflight_boot_check.sh` still narrates
  `STAGE90_XNU_STORAGE_PROBE` as a two-valued switch — **rungs 2 through 6 are not described in the gate
  at all** (six rungs short now, not five).

The pre-registration this press fills is
`docs/experiments/experiment-706-the-rung-6-pre-registration-the-first-clock-set-and-the-rate-write-that-is-not-on-this-path.md`.
