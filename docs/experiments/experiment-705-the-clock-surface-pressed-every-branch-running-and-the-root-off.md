# 705: the clock surface, pressed — every branch requested and running, the root's own enable clear, and nothing written

704 pre-registered rung 5 (`STAGE90_XNU_STORAGE_PROBE=5`): a **read-only census of the clock surface**, at
the tail of `entry_storage_probe` after rung 4's reset, guarded by the same `g_storage_mode_complete`
interlock every rung above 2 uses. This is its press. Every cell 704 §3 named is filled, and the rung's
whole claim — **a window this image has never written is now a window it has read, and the write set did
not move** — is a published number: `_clk_writes=0`.

```
readiness  5/5, exit 0 - the live arm is armed-storage-clock-6e7fbafa, 11/11 files, byte-identical to
           the park (tools/verify_revert_set.sh: VERIFIED, 11 files, 6 manifest-member checks agree)
gate       exit 0, 573 lines, under --allow-xnu-entry (derived from this arm's own switches)
runner     EXIT 0 - returned and captured, "the device came back 25s after this run called
           `fastboot boot`" (seen via: host log, serial); capture 615,815 B,
           sha256 b54dc3dd2b170b1939b9f928366d0744bd3b5c707dbb01ad641d8c1185eb01f0
send       one non-persistent `fastboot boot`, serial 4a2fe00b, neighbour 33e80afe off both lists
fired by   /tmp/g668/press-on-clear.v5.sh (sha 7f23cae5…), armed by hand as
           `press-on-clear.v5.sh armed-storage-clock-6e7fbafa 600`, set declared with no default;
           the launcher printed `done. gate=0 runner=0` and exited, so no firer is armed and 660's
           closure has released
```

Archived by hand under `out/stage90/captures/705-clock-pressed-2026-09-25-{last_kmsg.txt,run.log,gate.log,press.log}`
(`out/` is gitignored, and the runner does not archive the capture). 4,935 `xnu_live_` lines, 137 of them
`xnu_live_storage_*` — **116 distinct keys against the reset arm's 79, the difference being exactly the 37
this rung adds.**

## 1. The rung's own cells, in the order the function runs

The twelve words, then the bits each branch word carries. `_clk_loads` is counted bottom-up at each read
and published once, the way `_loads` and `_reg_loads` are:

| key | reading | the cell, against 704 §3's pre-registration |
| --- | --- | --- |
| `_clk_calls` | `0x1` | the census ran — and only after the block answered, because `g_storage_mode_complete` guards the call site |
| **`_clk_loads`** | **`0x0000000c`** | **twelve** — and the *count itself* arrived with two readings: 704 §3's prose calls its own pre-registration "the record's 'ten words'", while its table names twelve words' worth of keys and the shipped census takes twelve loads, one per word (the disassembly in §5 lists all twelve). The ten is a count of the **GCC** registers in that table; `CORE_VENDOR_SPEC` and `CLOCK_CONTROL` are two more and are in the same table. The log's number is the one the artifact confirms |
| **`_clk_writes`** | **`0x00000000`** | **the rung's whole claim, published rather than inferred from a comment** — and it is 0 on the path taken, not on a path argued |
| `_clk_bcr` | `0x00000000` | `SDCC1_BCR` unchanged from 694 |
| **`_clk_bcr_ares`** | **`0x00000000`** | **the bit that gives `_bcr` a meaning**: `BCR_BLK_ARES_BIT` (`clock-local2.c:66`) is **clear**, so the block is not held in reset — which is what makes every branch reading below a statement about a clock rather than about a block whose registers say nothing |
| `_clk_apps_cbcr` | `0x00004ff1` | `SDCC1_APPS_CBCR 0x04C4`, the word 694's gate reads |
| `_clk_apps_en` / `_off` / `_hw` | `0x1` / `0x0` / `0x0` | **bit 0 set, bit 31 clear, bit 1 clear.** The branch is requested **and running**, and it is not under hardware gating (so the framework does *not* skip its own halt check, `:346-347`) |
| **`_clk_ahb_cbcr`** | **`0x2000cff1`** | **`SDCC1_AHB_CBCR 0x04C8`, the register no run in this project had ever read** — 704 §3's genuinely open question |
| **`_clk_ahb_en` / `_off` / `_hw`** | **`0x1` / `0x0` / `0x0`** | **the answer**: the `pclk` branch `sdhci_msm_prepare_clocks` enables (`sdhci-msm.c:2315`) is **already requested and already running**, so the controller answering a version word is not a case of the register file being awake while its bus clock is not. The word also carries **bit 29**, which this census publishes and does not name — the whole word is published precisely so a reader can see that |
| `_clk_cdccal_sleep_cbcr` | `0x00000001` | `SDCC1_CDCCAL_SLEEP_CBCR 0x04E4` |
| `_clk_cdccal_ff_cbcr` | `0x00000001` | `SDCC1_CDCCAL_FF_CBCR 0x04E8` |
| the two CD-cal branches' `_en`/`_off`/`_hw` | `0x1` / `0x0` / `0x0` each | both calibration branches are requested and running. 704 §3 noted their parents are `cxo_clk_src` and not the apps root (`clock-8974.c:2362-2373`), so they were the two that could have read differently — and they do not |
| **`_clk_rcg_cmd`** | **`0x00000000`** | **`SDCC1_APPS_CMD_RCGR 0x04D0`, the apps root clock generator's command word — and all three of its bits are clear** |
| **`_clk_rcg_root_en`** | **`0x00000000`** | `CMD_RCGR_ROOT_ENABLE_BIT`, **clear** |
| **`_clk_rcg_update`** | **`0x00000000`** | `CMD_RCGR_CONFIG_UPDATE_BIT`, clear before anything writes it — **the premise `rcg_update_config`'s poll needs** (`clock-local2.c:90-108`, bound `UPDATE_CHECK_MAX_LOOPS 500`, `:44`) |
| `_clk_rcg_root_status` | `0x00000000` | `CMD_RCGR_ROOT_STATUS_BIT`, clear |
| `_clk_rcg_cfg` | `0x00000507` | `CFG_RCGR`: **src = 5**, **div = 7**, and bit 8 of the byte is nothing — the fields decode as `_clk_rcg_src`/`_clk_rcg_div` below |
| `_clk_rcg_src` / `_div` | `0x5` / `0x7` | the parent select and the divider — the two fields `set_rate_mnd` reads |
| `_clk_rcg_mnd_mode` | `0x00000000` | bits 13:12 clear, i.e. **not** dual-edge — consistent with `_clk_rcg_n = 0`, which is the condition `:144-145` states for dual-edge. The cross-check 704 §3 registered, and it holds |
| `_clk_rcg_m` / `_n` / `_d` | `0x0` / `0x0` / `0x000000ff` | the MND divider: `m = n = 0`, `d = 255`. **A D-only divider** — the arithmetic this names (a rate) is 704 §4's withheld cell |
| `_clk_vendor_spec` | `0x00000000` | **`CORE_VENDOR_SPEC 0x10C`, the fifth `core_mem` offset** — the register `sdhci_msm_set_clock` reads before it decides |
| `_clk_vendor_pwrsave` / `_mclk_sel` | `0x0` / `0x0` | `CBCR_PWRSAVE` (bit 1) and the MCLK select (bits 9:8) both clear — **the field the write rung will read-modify-write** |
| `_clk_clock_control_after` | `0x0003` | `CLOCK_CONTROL 0x2C` re-read **last**, via a halfword load: bit 0 and bit 1 set, **bit 2 `SD clock enable` still clear**. Read last so that "this census moved nothing" is a measurement; `0x0003` again, so it is |
| `_clk_done` | `0x1` | the census completed |

The twelve reads, from the shipped image's own disassembly — **eleven `ldr` and one `ldrh`**, at
`0x8000d8a4` `0x4c0`, `0x8000d8c8` `0x4c4`, `0x8000d90c` `0x4c8`, `0x8000d950` `0x4e4`, `0x8000d994` `0x4e8`,
`0x8000d9d8` `0x4d0`, `0x8000da1c` `0x4d4`, `0x8000da64` `0x4d8`, `0x8000da74` `0x4dc`, `0x8000da84` `0x4e0`,
`0x8000daa0` `0x10c`, and `0x8000dadc` **`ldrh` `0x2c`** — the last one a halfword because `CLOCK_CONTROL` is
read at its own width. That is `_clk_loads = 0xc` derived from the artifact, and the widths are the reason
the width census clause exists.

## 2. The two-bit correction, measured — and the root's own word disagrees with every branch

704 §2 is the reason this rung exists: since 692 the probe has published
`xnu_live_storage_gate` as `_cbcr & BIT(0)`, and **bit 0 of a CBCR is `CBCR_BRANCH_ENABLE_BIT` — the bit
the driver WRITES, i.e. the request** (`clock-local2.c:62`, written at `:380-382`) — while "the clock is
running" is `CBCR_BRANCH_OFF_BIT` = BIT(31) (`:63`), which the framework polls to a bound (`:386-387` →
`branch_clk_halt_check`, `:333-360`) and which its own handoff test reads **alone**
(`branch_clk_handoff`, `:490-497`). 694's `_cbcr = 0x00004ff1` happens to satisfy both halves, so nothing
measured before this press is overturned. What 704 §2 called a *possible* disagreement is now a measured
one, and it is not where the pre-registration guessed:

* **all four branches read `en = 1`, `off = 0`, `hw = 0`.** The state 704 §3 named as the one the old
  single-bit reading could not distinguish from "on" — `en = 1` with `off = 1` — **is not this boot's
  state.** The correction stands as a correction to the *reading* (the gate was reading half of a
  two-bit contract); it does not change this boot's verdict, which is the same verdict 694 reached.
* **and the disagreement is between the branches and the root, not between the two bits of one branch.**
  `_clk_rcg_root_en = 0` and `_clk_rcg_root_status = 0` — the apps root clock generator's own enable and
  its own status are **both clear** — while the apps branch, one register away, reports requested and
  running. So "the branch says the clock is on" and "the root says the clock is on" are **two readings of
  one quantity and they do not agree on this boot.** Which of them a given register read through the
  branch depends on is not something this census can settle; what it settles is that **neither bit is a
  proof on its own**, which is the same shape as the finding that produced the rung.

That is what makes rung 5 the premise of the write rung rather than decoration: a writer that enabled a
branch and then believed it had a clock would be trusting a reading this boot shows can be set while the
root above it says off. The driver's own order — rate first, then enable, then **poll**
(`clk_prepare_enable` → `branch_clk_halt_check`; `clk_set_rate` → `rcg_update_config`'s poll) — is the
answer to exactly this, and its before-values are the four words above.

## 3. The whole ladder reproduced under changed code

The rung is additive (the census runs after the reset), so this press is also a re-run of rungs 1–4 on an
image whose `.text` grew 1,248 bytes under them:

| what | reading | the point |
| --- | --- | --- |
| the gate and the two installs | `_gcc_map=1`, `_map=1`, `_gate_read=1`, `_bcr=0x0`, `_cbcr=0x00004ff1`, `_gate=0x1`, `_loads=0x6`, `_writes=0x0` at the gate | 694's readings again, on an image whose entry blob moved |
| the mode sequence | `_mode_bit=0x0` → `_mode_bit_after=0x1`, `_mci_version=0x10000011` | 697's and 699's: the controller this payload inherits is still the bootloader's state, so the vendor's sequence is a precondition on **every** boot |
| the census | `_reg_loads` counts 1…10, `_reg_power_control_after=0x0`, `_reg_present_state_after=0x01f80000` | 698's ten reads and the counts that derive them |
| the reset | `_rst_stores=0x1`, `_rst_polls=0x1`, `_rst_ticks=0x0000000f` | 703's press reproduced to the tick count within one: the reset bit was clear on the first read, in 15 ticks this time against 16 |
| the write set | `_writes` counts 1…4, `_rst_stores=1` | **unchanged by this rung.** Rung 5 adds no member: the image writes `core_mem` four times, `hc_mem` once, and the GCC never |

## 4. The ending, the panic, and the seventh return time

```
xnu_live_post_t0          0x07404b0a     the baseline
xnu_live_post_cntfrq      0x0124f800     = 19,200,000 Hz, the machine's own rate (sixth press to carry it)
xnu_live_post_elapsed     0x06df694a     = 115,304,778 ticks = 6,005.46 ms
xnu_live_post_end_calls   0x00000007     the ending FIRED, on the 7th return
xnu_live_slot_post_calls  0x00000004     published at 1, 2, 3, 4 - the count passed 4 and the run ended at 7
```

**The deadline was crossed 104,778 ticks (5.457 ms) before the returning pass** — beside 694's 11.011 ms,
697's 1,999.0 ms, 698's 15.49 ms and 703's 10.31 ms, i.e. inside one ~2007 ms idle park every time. The
ending's own first store faulted into the same panic as 697's, 698's and 703's, and the runner's seam block
printed the same cells as those three (`seam_lr=0x800472dc` PASS, the `FAIL seam_sp=0x80553ec8 is not
sleh_sp-8` and `CLEAN LINE b=0x800b3648/0x8047db04` both being `STAGE90_XNU_SLOT_NULL=1` doing what 698 §3
said it does).

**And the return is a seventh measurement: 25 s**, against 8 (690), 17 (692), 24 (694), 27 (697), 24 (698)
and 27 (703). The interval is still unattributed, and this run adds a negative fact to 703's: **the image
changed again (1,248 bytes of `.text`, and 608,984 bytes of payload) and the interval moved by one sample
inside a range it has already covered.** The two candidates named in 690 §7 stay named — the watchdog bite
`0xf9017014` and `PS_HOLD 0xfc4ab000` — and no arm has been observed to complete a `PS_HOLD` store.

## 5. What was proven about the artifact before the press, and not by this log

This step's first build found **two defects in a reader**, and both were repaired before the arm was
frozen. They are the reason this record has a §5 rather than a one-line note:

* **the store census was cumulative, and this rung is what exposed it.** The classifier counted *every*
  materialization of a high register half, so a probe that uses `r4` as a `core_mem` base, then as an
  `hc_mem` load base, then as a GCC base was **ambiguous by construction** — the first mutation written for
  this rung was refused by the `AMB` clause rather than by the new GCC clause, i.e. it was refused for a
  reason that had nothing to do with what it tested. Repaired by classifying **in program order**: the
  last device high half materialized *before* the store decides the window, and an *image* pair preceding
  the store classifies `IMG` first. **A build clause that cannot say which address a store uses is a
  reading about the clause's own scope, not about the image.**
* **the first repair misclassified image stores as GCC** (measured on a synthetic body as an `str` to
  `0x8055_41a4` classified `GCC`), because the device fallback paired a device high half with an image low
  half when both were true. Fixed with an explicit `IMG` arm, and then **controlled in both directions**:
  eleven synthetic bodies were classified by the new reader and by the reader taken from
  `git show HEAD:` — the true baseline, after the first attempt used the already-edited file as its own
  "old" column and so measured nothing — and the repair changes no verdict except the misread cases. **A
  control whose baseline was extracted from the thing under test is not a control.**
* **the new clause, exercised.** Three mutations: a store to `0xfc400000` → the new GCC clause
  (*"entry_storage_probe stores [] into the GCC megabyte … and no rung of this ladder writes the clock
  controller at any value"*); an unaligned 32-bit read → the width census; `stb`→`sth` → the per-window
  mnemonic clause. The build's own line for the shipped image reads
  `core_mem [120 0 120 120 ] on r5, hc_mem [47 ](strb ) on r5, gcc [], image [r6:0:str:16804 r6:4:str:16804 ]`,
  so the clock window's store set is empty **by a clause that knows the window by name** — before 704 such
  a store was refused anyway by the `DEVBAD` clause, whose stated subject is "not one of the two windows
  this controller declares": true, and a statement about the classifier's scope rather than about the
  clock.
* **the layout is stable, and for a reason worth naming.** `.text` grew 1,248 bytes, yet
  `entry_storage_probe` (8000cff8), the wrapper's `bl` (8047db2c), the seam (0x800472dc),
  `platform_cache_idle_enter/exit` and `early_random` are all where they were, and only `cpu_idle_wfi`
  moved (672 bytes). The growth was absorbed by alignment fill before `platform_cache_idle_enter` — so
  **every address the gate and the runner pin is stable because a fill contracted, not because nothing
  moved** (`mi4-linker-fill-term`).
* **one thing this step did *not* guard, and it is a naming one.** 704 §3's table names three whole-CBCR
  keys as `_clk_cbcr_ahb`, `_clk_cbcr_cdccal_sleep` and `_clk_cbcr_cdccal_ff`; the image publishes
  `_clk_ahb_cbcr`, `_clk_cdccal_sleep_cbcr` and `_clk_cdccal_ff_cbcr`. **A pre-registration's key names are
  prose.** The clause that checks names (`verify_press_ready.sh`'s narration sweep, 46 names and 2 globs
  against the ELF's own strings) checks the **narration**, not the doc — so this divergence was found by
  comparing the doc's names against the ELF's strings by hand, which is what §1 above is. The count above
  is the second instance of the same gap in the same section (§1's `_clk_loads` row).

## 6. What this does not do

* **No store to the clock surface, and none anywhere new.** `_clk_writes=0` on the path taken; the whole
  press's write set is the four `core_mem` stores and the one `hc_mem` byte that rungs 2 and 4 already
  made. The GCC is a window this image can now read and cannot write.
* **No rate.** The MND fields and the source select are published; **no frequency is.** Converting them
  needs the parent's rate (`gpll0`/`gpll4`/`cxo` in `ftbl_gcc_sdcc1_4_apps_clk`,
  `clock-8974.c:1564-1574`), which is a table this image does not carry — and a computed frequency beside
  the framework's own cached rate would be 704 §2's defect one level up.
* **No enable and no clock set.** `CLOCK_CONTROL` bit 2 is still clear after this press, so
  `sdhci_set_clock` is still entirely ahead of the driver.
* **No storage.** No command, no sector, no partition table, no mount; the eMMC device is not addressed.
* **No driver beyond the fixture.** The goal's floor is met and unchanged: pid 1, the driver's `open`
  answering `0x00000000` while the control open answers `0x00000002`, and the fixture's `0xfeedface` read
  into a user page — the floor 520 and 533 also met.
* **TWRP-to-storage stays withheld**: the user's condition is that the OS can already be entered and
  **stays**, and the OS is not observed doing that.

## 7. Owed

* **The write rung (6), and rung 5 is what lets it be bounded rather than experimental**:
  `CORE_VENDOR_SPEC 0x10C`'s MCLK select is a read-modify-write of a field the vendor also writes — the
  safest of the writes, and `_clk_vendor_mclk_sel = 0` is its before-value; `clk_prepare_enable` on the AHB
  branch is a `CBCR` read-modify-write **plus a halt handshake** on a branch with `has_sibling = 1` (whose
  `round_rate`/`list_rate` are `-EPERM` by the framework's own rule); and `clk_set_rate` on the apps RCG is
  `M`/`N`/`D`, then `CFG_RCGR`, then `CMD_RCGR`'s update bit **and a bounded poll of it**
  (`UPDATE_CHECK_MAX_LOOPS 500`, `clock-local2.c:44`, `:90-108`) — whose before-values are `_clk_rcg_update
  = 0` and `_clk_rcg_cmd = 0`, both now measured.
* **The root-versus-branch disagreement of §2 is owed a decision, not just a reading**: whether the driver's
  own order (rate, then enable, then poll) is what this SoC needs, or whether the apps RCG's clear
  `root_en` means a clock the branch reports as running is not reaching the block. The write rung's first
  poll is the experiment that distinguishes them.
* **Bit 29 of `_clk_ahb_cbcr`** is published and unnamed.
* **`CLOCK_CONTROL` bit 2 has never been set by this image** and is the cell the first `sdhci_set_clock`
  changes.
* **The width clause of the store census has still not been observed to fire on a widened device store**
  (703 §5's state, unchanged; the rung-5 mutations reached it on `hc_mem`).
* **What actually returns a run** (8/17/24/27/24/27/25 s).
* **The gate's narration of `STAGE90_XNU_STORAGE_PROBE` is four rungs short** — the peer lane owns that
  file (rungs 2, 3, 4 and 5 are not described in it), reported by message and not by edit (700 §8's
  carried item, now four rungs).
* **A pre-registration's key names are not a check** (§5): the three CBCR words above are the instance.
* **Carried, unchanged**: the ending's first store still faulting into a panic (`RESTART_REASON
  0x0fa0065c`); the 691 §5 `entry_note_wfi` readback; `entry_reset.h`'s false IMEM claim; 676 §6 / 677 §6;
  the 684-owed runner clause for the 678 arm; `tools/xnu_dt_requirements.py` and the `"master"` value; the
  two peer-lane tripwire repairs; and the seam address pinned in two files (`entry_trace.c`'s
  `STAGE90_XNU_SEAM_LR` and `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`).
