# 772: the rung-26 arm — four cells that were already measured, printed at last, and rung 25 folded into it

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER.** `out/` was
rebuilt and now holds **`armed-storage-a1378a48`** (`STAGE90_XNU_STORAGE_PROBE=25`, ordinal rung 26),
**ARMED AND NOT PRESSED**. One press spent by this step: **none**. Rung 25's park
(`armed-storage-f72e9f18`) and rung 23's (`armed-storage-3a92aa52`) are both intact and untouched.

## 1. What the arm is — four keys, zero new device accesses

All four values were already in a register when the arm runs:

| key | what it is | who filled it before |
| --- | --- | --- |
| `xnu_live_storage_cmd0_cmdlow_seen` | of CMD0's first 1024 samples, how many read `PRESENT_STATE 0x24` bit 24 (`ST_SDHCI_CMD_LINE_LEVEL`, the CMD line's own level) **LOW** | the 724 sampler, for every command — published by rung 24 for `nidx` alone |
| `xnu_live_storage_cmd1_cmdlow_seen` | the same, for CMD1 | idem |
| `xnu_live_storage_cid_cmdlow_seen` | the same, for CMD2 | idem |
| `xnu_live_storage_cid_inhibit_after` | `CMD_INHIBIT` one read after the store to `COMMAND 0x0E` | `st_send_command` fills it for **every** command; **no publish block has ever printed it** |

**No new device access, no new register, no new window, no new megabyte and no store of any kind.** The
build's own `st_send_command` clause asserts that body's device set, and it is unchanged from rung 24.

## 2. Why it exists — 771's one-sample problem

771 read the rung-24 press as one four-row table, and the rows are not the same experiment:
`_cmd0_inhibit_seen = 0x218` beside a command that completed and latched `INT_RESPONSE`;
`_cmd1_inhibit_seen = 0` and `_cid_inhibit_seen = 0` beside 5.09 M polls each with nothing latched at all;
`_nidx_inhibit_seen = 0x400` beside a block that armed and fired its own response timeout at 665.2 µs.

**And the only trace of what the CMD line was doing during CMD1 or CMD2 is one sample.** `inhibit_last`
bit 24 reads LOW for CMD1 and HIGH for CMD2 on **every** capture in the archive (10 cmd0, 8 cmd1, 7 cid).
`_cmd1_inhibit_last = 0x00F80000` with `_cmd1_inhibit_seen = 0` is a contradiction the archive already
carries: on this block the line can sit LOW with `CMD_INHIBIT` never rising, so *the block never started
this command* and *the block drove the line and the inhibit bit is not a reliable witness* are
**indistinguishable from one sample**. **The count separates them** — 0 says the line never moved at all,
anything else says it did.

A single sample taken at the end of a window is exactly the reading rung 24 replaced with a count for
CMD3. This arm finishes that repair for the other three.

## 3. Containment is measured, not argued

Every new line sits inside `#if STAGE90_XNU_STORAGE_PROBE >= 25`, so value 24 is untouched *by
construction*. That was then **measured**: the value-24 build from this same source came out
**byte-identical** to the parked `armed-storage-f72e9f18` (`f72e9f18…`, 5552764 bytes, exit 0).

So one press answers the SDC1 pad question **and** these four cells, and **rung 25's park need never be
pressed**. That is the relation 767 established between rungs 23 and 24, and it is stated here as a
comparison of two artifacts rather than as a reading of four `#if`s.

## 4. What did not move, measured

`xnu_arm_entry.bin` is **5552764 bytes — the same size as the arm before it**, because the four new keys
fitted the padding. **770 measured that an unchanged size is not the claim that nothing moved**, so the
claim here rests on two other readings: the hash (`a1378a48…`, different from `f72e9f18…`), and
`STAGE90_XNU_SEAM_LR`, which is **still `0x8004a2dc`** and which the build accepted against the linked
image — so the entry text did **not** cross a page this time.

The payload's switch record is **byte-identical to rung 23's, rung 24's and rung 25's**
(`6c2b6038…`, 682 bytes) — the **763 §5** trap, checked by `cmp` and not by eye.

## 5. The ladder text and the bound

`src/entry/entry_storage.c`'s ladder bound widened from `> 24` to `> 25`, and its one `#error` sentence
gained the rung-25 paragraph. The bound is **parsed out of that file** by `build_entry.sh` (732's
repair), so the two readings of the ladder cannot disagree.

**And the bound and the text are both inert for every valid value** — the `#error` is inside the `#if`
that fires only for an out-of-range value — which is why widening them cannot move a byte of any arm
below 25, and the byte-identical value-24 build is the measurement that says so.

## 6. The ready file

`tools/verify_press_ready.sh` gained a value-25 branch and a `rung_para 25`, because this file refuses a
`rung_para` written for the armed value that did not run (the `RUNG_HIT` mechanism) and refuses an armed
rung it does not narrate. **Readiness is what caught the omission**: with the arm built and parked but
unnarrated, the row read *"the arm named above does not quote the rung the entry record carries"* —
which is the file working, not a defect.

## 7. What the press will cost and what it buys

The arm's hazards are **rung 25's own** — one read of a TLMM pad register in a megabyte whose section
the arm installs — and its new cells add none.

**What it buys:** row 1 of rung 25's pad table (`_pad_raw = 0x00009F24` retires 768 §5's other half and
sends the next rung to the PMIC rails; `0x00000000` makes it a single read-modify-write of one register
whose section is already installed) **plus** three counts and one after-bit.

**What it does not say:**
- **Neither cell is a verdict about the card.** They say what the *block* did with the line, and 771
  already retired `RESPONSE` in both directions — a nonzero count is not a response, and a zero is not
  proof the card was never addressed.
- **A zero count is a reading only if the sampler outran the bus.** A sampler slower than one bit period
  could miss a transmission and report a zero, so each count is read **beside its own** `_polls`/`_ticks`
  and never alone.
- **It does not spend a press and it does not authorize one.**

## 8. Verification, and what is owed

| gate | reading |
| --- | --- |
| entry build | exit 0, `xnu_arm_entry.bin` `a1378a48…`, 5552764 B, `STAGE90_XNU_STORAGE_PROBE=25` |
| containment | value-24 build from the same source → `f72e9f18…`, **byte-identical** to the parked rung-25 arm |
| the four new keys | present once each in the image's own strings; readiness reads them back against `xnu_arm_entry.elf` |
| payload build | exit 0, `stage90-qcdt.img` `b782cc1a…`, config `6c2b6038…` **byte-identical to rungs 23/24/25** |
| the park | `out/stage90/frozen/armed-storage-a1378a48`, 11 members, `tools/verify_revert_set.sh` **11 ok / 0 failed** |
| `make check` | exit 0 — 35 sets, 33 named by `xnu_arm_entry.bin` |
| readiness | **5 of 5**, exit 0 |

**Owed, unchanged and named rather than left to be inferred:** the `rung_para` correction for values
12..23 (771 §7 of 770); the seam-address class itself — *a kernel address pinned in an entry source* —
whose repair is a link order; `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL` structural repair; and the
737 window paragraph in `src/entry/entry_storage.c`, which **this build carried forward unchanged** and
which remains owed for a **cost** reason and not a lane reason — correcting it forces a build, and the
next build should be an arm that carries the correction rather than a comment that spends one.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist and 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
