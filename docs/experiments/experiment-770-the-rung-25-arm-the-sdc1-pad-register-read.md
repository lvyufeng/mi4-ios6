# 770: the rung-25 arm — the SDC1 pad register read, and the seam constant's fourth move

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER.**
`out/` was rebuilt and now holds `armed-storage-f72e9f18` (`STAGE90_XNU_STORAGE_PROBE=24`),
**ARMED AND NOT PRESSED**. One press spent by this step: **none**. The live arm 768 spent
(`armed-storage-94de2ede`) and rung 23's superseded park (`armed-storage-3a92aa52`) are both intact
and untouched.

## 1. What the arm is — one read, one new megabyte, zero stores

769 §6's pre-registration, built. `st_pad_census` installs the `0xFD5` megabyte through
`entry_mmio_section` (693's design; 692's press is why the install cannot be skipped — an
uninstalled megabyte faults with `fsr = 0x5`), then takes **ONE 32-bit read of `0xFD512044`**, the
SDC1 dedicated-pad drive/pull register, and stores nothing anywhere. It publishes the word raw
(`xnu_live_storage_pad_raw`) beside seven field decodes — the three hdrive fields at bits 6/3/0 and
the four pull fields at 13/11/9/15 — plus `_pad_expect`, a one-bit `_pad_match`, the install's own
outcome (`_pad_map`, `_pad_slot_before`, `_pad_desc`, `_pad_section`), `_pad_read` and
**`xnu_live_storage_pad_writes = 0`, a count that never leaves zero.**

**The build is what says so, not this document.** The rung-25 clause in `src/entry/build_entry.sh`
reports, verbatim from the linked image:

```
st_pad_census's device accesses are [fd512044:ldr ] with counts [fd512044:ldr=1 ]
  - ONE READ of 0xfd512044 and NO STORE ANYWHERE - in program order [fd512044:ldr ]
  with an EMPTY image side; entry_storage_probe calls it once, on disassembly line 1277,
  BEFORE st_cmd_path on line 1281
```

Four refusals sit behind that line: a count of `str` in the body must be **zero** (first, before the
set and count clauses, so a perturbed build that adds a store is refused by the sentence that
explains why a store here is the one thing this rung must not have), the device set must be exactly
`[fd512044:ldr ]`, the counts exactly `[fd512044:ldr=1 ]`, the image side **empty**, and the call
site exactly one and ordered before `st_cmd_path`.

**Reading a pad-control register is safe by the vendor's own code**: `msm_tlmm_set_field` opens with
`__raw_readl(reg)` on exactly this address (`gpio-msm-common.c:487`), and the register is not
write-one-to-clear and not a FIFO. The ladder's handler acks the power latch rather than performing
the vendor's `CORE_PWRCTL_BUS_ON` work, so **the pads' drive and pull have been attempted by no
rung** — this arm reads their state, and does not write it.

## 2. The field the mechanism rests on

`PULL SDC1_CMD` = **pull-up**. MMC's CMD line is **open-drain during identification**: both sides
pull it LOW and it returns HIGH only through a pull-up, which on this SoC is this pad register and
not a discrete resistor the ladder can assume. A board whose pads sit at reset reads `0x00000000` —
no pull-up on CMD, none on DATA, drive strength at minimum.

## 3. The expectation is derived, not typed

`qcom,pad-pull-on = <0x0 0x3 0x3 0x1>` and `qcom,pad-drv-on = <0x4 0x4 0x4>`
(`msm8974pro-ac-pm8941-mtp-v5.dts:25-29`) through `msm_tlmm_set_field`'s
`reg &= ~(mask << off); reg |= (val & mask) << off;` (hdrive width 3, pull width 2,
`gpio-msm-common.c:481-496`) evaluate field by field to **`0x00009F24`**. Six `_Static_assert`s in
`src/entry/entry_storage.c` fix the address (`== 0xfd512044`), its megabyte (`>> 20 == 0xFD5`), its
alignment, the seven masks' disjointness (their sum equals their OR) and the CMD-pull mask's overlap
with the expectation — so the value the arm compares against is computed from the board's own arrays
on every build. `xnu_live_storage_pad_expect` publishes it so no reader has to take the arithmetic
on trust.

## 4. The seam constant moved a FOURTH time, by the same page, and the build refused it

`st_pad_census` and its call site pushed `entry_storage.o` past the entry group's remaining
alignment slack, so the whole kernel text run moved by one page and the exit's `bl FlushPoU_Dcache`
is now at **`0x8004a2d8` returning to `0x8004a2dc`** — where every arm from 724 to 768 carried
`0x800492d8` / `0x800492dc`. The build refused with its own clause:

```
the exit's call to FlushPoU_Dcache is at 2147787480 and returns to 2147787484, while
entry_trace.c's STAGE90_XNU_SEAM_LR is 0x800492dc: the arm's identification is a comparison
with that constant, so a disagreement means the wrapper would let this very site through
(or hook a different instruction) and every reading the run publishes would be about the wrong call
```

**Two copies of that address have to move together and the gate is the place that compares them**
(`[[mi4-one-value-two-definitions]]`): `src/entry/entry_trace.c`'s `STAGE90_XNU_SEAM_LR` and
`scripts/run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`, which
`preflight_boot_check.sh` compares against the live ELF. Both moved. Verified from the linked image,
not from the build's message alone:

| symbol | 724–768 | 770 |
| --- | --- | --- |
| `platform_cache_idle_enter` | `0x80049238` | **`0x8004a238`** |
| `platform_cache_idle_exit` | `0x800492d4` | **`0x8004a2d4`** |
| exit's `bl FlushPoU_Dcache` | `0x800492d8` | **`0x8004a2d8`** |
| its return address (the constant) | `0x800492dc` | **`0x8004a2dc`** |
| the four call sites | `0x80048d08`, `0x80049284`, `0x800492d8`, `0x800493bc` | `0x80049d08`, `0x8004a284`, `0x8004a2d8`, `0x8004a3bc` |

**And the fourth move shows what a size comparison cannot see.** `xnu_arm_entry.bin` came out at
**5552764 bytes — byte-identical in size to the arm before it** — because the growth fitted the
padding while the page boundary *inside* it was still crossed. So "the bin did not change size" is
not the same claim as "nothing moved", and the only defence is the pair of clauses that compare the
constant with the linked image.

This is the fourth page move in this ladder (696, 708, 724, 770), and the mechanism has been the
same every time: `LINK_OBJS` puts the entry objects before the kernel's, so every byte they add
moves Apple's text, and the move happens a whole page at a time.

## 5. What a refused entry build leaves in `out/`

The entry build writes `xnu_arm_entry.bin` **before** its clause family runs. So the refused rung-25
build left `out/stage90/` with a **new entry bin** (`594d539ae41f28da…`) sitting beside the rung-24
arm's own switch record (`94de2ede…`, written 08:54) — a pair the record does not describe and which
the gate's blob clause refuses. The live **arm** was never in doubt (`stage90-qcdt.img` was
untouched, still 94de2ede's, and readiness resolves the arm by hashing exactly that file), but the
lesson is worth naming: **a refused entry build must be finished, or `out/` re-armed from a park,
before any press.** `out/` is not left as it was. This is why the rung-25 edit had to be completed in
one build rather than parked half-way.

## 6. Two prose debts paid, and one `make check` refusal that was invisible until it fired

**`entry_trace.c`'s seam paragraph closed with "The value is the one every arm from 724 on carries",
and this build makes that FALSE.** It now records the fourth move with the addresses read out of the
linked image and states the rule that resolves the class: **the absolute addresses in the prose
around the define are era-stamped** — each is the address of the build in which its sentence was
written and they are *not* kept in step. That is stated rather than repaired because a paragraph that
must be edited on every move is a second pin wearing the name of a record; a reader who needs the
address reads the define, the build's clause (which prints both numbers), or the live ELF.

**`build_entry.sh`'s seam header and its four-caller refusal message still named the 696-era
addresses (`0x800462xx`).** The refusal now names the four callers **by function** and prints no
addresses at all: a refusal whose explanation names four stale addresses sends a reader to the wrong
instructions, which is **m776's shape in the most load-bearing place a wrong address can sit**.

**Both edits are comment-only, and the artifact proves it**: the entry build re-ran and
`xnu_arm_entry.bin` came out at `f72e9f18…`, **byte-identical** to the arm built before them — so the
set's name did not move and only `xnu_arm_entry-sources.txt` changed (`9c6abedd…` → `47325aa4…`),
which is the one member that records source *content*.

**And `make check` refused this step's own new text before it was green.**
`tools/check_backtick_messages.sh` found unescaped backticks inside double-quoted `layout_fail`
messages at `build_entry.sh:31285`, `:31289` and `:31298` (6, 6 and 10 of them) and inside the
rung-25 readiness narration (2). **The class is invisible until the refusal fires**: a double-quoted
string is expanded only when the call runs, so the build itself exited 0 with all three present, and
a refusal that did fire would have printed a sentence with the register's name replaced by the
output of a command named after it. All four sites are escaped. That is **m730**'s shape and the
reason the check exists.

## 7. The two-row answer, and what this arm does not say

| `_pad_raw` | reads as | the next act |
| --- | --- | --- |
| **`0x00009F24`** (all seven fields as the DT declares) | **the pads are configured** — 768 §5's other half retired | the subject moves to the PMIC rails (`sdhci_msm_setup_vreg`, `sdhci-msm.c:2019`), a bus the ladder cannot reach from `hc_mem` |
| **`0x00000000`**, or any field at odds with §3 | **the vendor act was never applied on this path** | a single read-modify-write of one register in a megabyte whose section this arm has already installed |

- **It does not say the arm's read proves this image applied the pads.** The read is taken at the
  arm's own moment, *before* the command path, so row 1 is a reading about the pads' **state** — the
  boot chain may have left exactly the same values. **Only row 2 carries an act.**
- **It does not say a matching pad register means the card will answer.** `PULL SDC1_CMD` is
  necessary and not sufficient: the clock at the pins, the bus width and the card's own power-on
  state are all still unmeasured, and 768 §6 already noted that the idle line reads HIGH on every arm
  on record — which is what a pull-up produces **and** what a floating line that happens to sit high
  looks like. **Only the register can tell those two apart**, which is why this rung reads it.
- **It does not re-open 764 §1 or 768 §4.** The DLL path stays closed and the safety contract is
  unmoved.
- **It does not spend a press and it does not authorize one.** Building and parking are host-side.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount, no driver.

**And the rung-25 readiness narration carries a correction the file has owed since rung 12.** The
shared storage consequence paragraph (692/693 era) closes with "a `_loads=6` with `_writes=0` is the
run this arm exists for". A guard corrects that for values 2..11 and rungs 3 and 4 correct it in
their own paragraphs, and **every value from 12 on carries it uncorrected** — `rung_para 24` now
states the correction for this arm and *names* the omission for 12..23 rather than papering over it
with a range guard this file has not re-read arm by arm. Those are narrations of arms already spent,
so no press is bought or lost by the gap.

## 8. Verification, and what is owed

| gate | reading |
| --- | --- |
| entry build | exit 0, `xnu_arm_entry.bin` `f72e9f18…`, 5552764 B |
| payload build | exit 0, `stage90-qcdt.img` `54f88bba…`, `stage90-build-config.txt` `6c2b6038…` — **byte-identical to rung 23's and rung 24's** |
| the park | `out/stage90/frozen/armed-storage-f72e9f18`, 11 members, `tools/verify_revert_set.sh` **11 ok / 0 failed** |
| `make check` | exit 0 |
| readiness | **5 of 5**, exit 0 — the live image hashes to exactly one recorded set, the park verifies, the gate accepts the tree, and a press would be caught |

**Owed, and named rather than left to be inferred**: the `rung_para` correction for values 12..23
(§7); the seam address class itself — *a kernel address pinned in an entry source* — whose repair is
a link order and not this step's question; and `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`, whose
structural repair (a fallback that must be edited per arm is a pin wearing the name of a fallback)
is unchanged from 724.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block issues a
response-demanding command, drives the CMD line, arms and fires its own response timeout at 665 µs,
and **the card does not answer** — and 「让os可以正常启动并且挂载存储」 is not reached, so
**TWRP-to-storage stays withheld.**
