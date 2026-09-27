# 775: the rung-27 arm — the rest of the CMD2 result, eleven fields that were already measured, and the reading 771 could not take

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER.** `out/` was
rebuilt and now holds **`armed-storage-8096cb2c`** (`STAGE90_XNU_STORAGE_PROBE=26`, ordinal rung 27),
**ARMED AND NOT PRESSED**. One press spent by this step: **none**. Rungs 26 (`armed-storage-a1378a48`), 25
(`armed-storage-f72e9f18`) and 23 (`armed-storage-3a92aa52`) are all intact and untouched.

## 1. What the arm is — eleven publishes, no new device access

`st_all_send_cid` was written at **rung 16**, before rungs 12 and 24 added the entrance state and the inhibit
gate to `struct st_cmd_result`. Its publish block therefore prints **ten of the struct's twenty-seven
fields**, and the fifteen it drops include every one of them. Rung 26 added two; this arm prints the other
eleven:

| key | what it is |
| --- | --- |
| `xnu_live_storage_cid_ps_before` / `_ps_after` | `PRESENT_STATE 0x24` at the command's entrance and exit |
| `xnu_live_storage_cid_inhibit_before` | the inhibit gate's first refusal |
| `xnu_live_storage_cid_inhibit_polls` / `_ticks` / `_timeout` | the driver's own bounded wait (`sdhci.c:1096`) |
| **`xnu_live_storage_cid_stale`** | **`INT_STATUS 0x30` as found at the entrance — see §2** |
| `xnu_live_storage_cid_clear_wrote` | the write-1-to-clear value |
| `xnu_live_storage_cid_arg` | `ARGUMENT 0x08` written |
| `xnu_live_storage_cid_rsp_present` / `_resp_read` | the driver's own condition (`sdhci.c:1169-1175`) and whether the register was read at all |

**No new device access, no new register, no new window, no new megabyte and no store of any kind.** Every
one of the eleven is already in the struct when the block runs, and the build's `st_send_command` clause
asserts that body's device set unchanged.

## 2. And one of the eleven decides something no other cell in the archive can

771 read the rung-24 press as one four-row table and found the four commands are not the same experiment.
The anomaly it could not read is this: **the block ACKNOWLEDGES CMD0** (inhibit rises, `INT_RESPONSE` latches)
**and CMD3** (inhibit held for all 1024 samples, `INT_TIMEOUT` latches at 665.2 µs) but **raises NOTHING for
CMD1 and CMD2** — `_cmd1_inhibit_seen = 0` and `_cid_inhibit_seen = 0`, with `_cmd1_any_polls = 0` over
`_cmd1_polls` = 5,090,304 reads that were **all zero**.

**And the press does carry `_nidx_status_pre = 0x00018000`** — `ERROR | TIMEOUT` latched somewhere between
CMD2's poll giving up and CMD3's entrance. **The only write in that interval is a store to `0x34`, which 736
measured sets bit 15 by itself.** So whether bit 16 (`INT_TIMEOUT`) is a real CMD2 timeout that arrived after
the poll gave up, or the enable store's own effect, is **unseparated by every capture on record.**

**`_cid_stale` is that reading taken one command earlier.** It is `INT_STATUS` as found at **CMD2's entrance**
— the first read of that register since CMD1's poll gave up 1.2 seconds earlier — and the interval it sits in
contains **no write to `0x34` at all**. A non-zero there is CMD1 doing something after its poll ended; a zero
is CMD1 doing nothing.

**Its zero is a reading only if something would have set it**, so it is read beside `_cid_clear_after = 0`
and `_cmd1_status_any = 0` — the same register at two other moments — and beside `_cid_status_any`, which is
what the poll itself saw over 5,088,256 reads.

**The second reading that matters is `_cid_inhibit_timeout`.** A `1` there would make CMD2 a **refusal at the
gate** — no command on the bus at all — rather than a command that ran and stalled, which is a different entry
in 771's answer space and would move the frontier off the card entirely. `_cmd1_inhibit_timeout = 0` is on
record; this is the first time that gate has been read for CMD2.

## 3. Containment is measured, not argued

Every new line sits inside `#if STAGE90_XNU_STORAGE_PROBE >= 26`, and then it was measured: the value-25
build **from this same source** came out **byte-identical** to the parked `armed-storage-a1378a48`
(`a1378a48…`, 5552764 bytes, exit 0).

**So one press answers the SDC1 pad question, rung 26's four cells, and these eleven — and rungs 23, 25 and
26 need never be pressed.** Three generations of arms now fold into one press.

## 4. What did not move, measured

`xnu_arm_entry.bin` is **5552764 bytes** — the same size as the arm before it, because the eleven keys fitted
the padding. 770 measured that an unchanged size is not the claim that nothing moved, so the claim rests on
the hash (`8096cb2c…`, different) and on the seam clause, which **accepted `STAGE90_XNU_SEAM_LR = 0x8004a2dc`
against the linked image** — so the entry text did **not** cross a page.

The payload's switch record is **byte-identical to rungs 23, 24, 25 and 26** (`6c2b6038…`, 682 bytes).

## 5. A build defect, recorded because a diff does not show it (m778)

The first build of this arm was **refused**:

```
src/entry/entry_storage.c:116: error: missing terminating ' character [-Werror]
```

The cause was an **odd number of apostrophes** in the appended ladder text. `#error`'s text is a single
double-quoted string, so an odd count leaves one character constant unterminated. Rewording three possessives
took the inserted segment from three apostrophes to none.

**The class is worth naming**: this file's ladder text is one 36 KB string that every rung appends a sentence
to, and the only reader that sees an odd apostrophe is the compiler. It is not a `make check` case (the
backtick check is about shell, not C), and it is invisible in a diff — so the defence is that the build
refuses, which it did.

## 6. What the press will cost and what it buys

The arm's hazards are **rung 25's own** — one read of a TLMM pad register in a megabyte whose section the arm
installs — and its new pubs add none: not one device access is added to any body.

**What it buys:** row 1 of rung 25's pad table, rung 26's four counts, and the completed CMD2 row of 771's
table — including the two cells that can move the frontier (`_cid_stale`, `_cid_inhibit_timeout`).

**What it does not say:**
- **Neither cell is a verdict about the card.** They are about what the *block* did, and 771 already retired
  `RESPONSE` in both directions — a non-zero here is not a response and a zero is not proof the card was
  never addressed.
- **It does not spend a press and it does not authorize one.**

## 7. Verification, and what is owed

| gate | reading |
| --- | --- |
| entry build | exit 0, `xnu_arm_entry.bin` `8096cb2c…`, 5552764 B, `STAGE90_XNU_STORAGE_PROBE=26` |
| containment | value-25 build from the same source → `a1378a48…`, **byte-identical** to the parked rung-26 arm |
| the eleven keys | each present once in the image's own strings; readiness reads them back against `xnu_arm_entry.elf` |
| payload build | exit 0, `stage90-qcdt.img` `c04d9c04…`, config `6c2b6038…` **byte-identical to rungs 23/24/25/26** |
| the park | `out/stage90/frozen/armed-storage-8096cb2c`, 11 members, `tools/verify_revert_set.sh` **11 ok / 0 failed** |
| `make check` | exit 0 — 36 sets |
| readiness | **5 of 5**, exit 0 |

**Owed, unchanged and named rather than left to be inferred:** the `rung_para` correction for values 12..23;
the seam-address class itself — *a kernel address pinned in an entry source* — whose repair is a link order;
`run_and_capture.sh`'s `EXIT_POP_LR_LITERAL` structural repair; the 737 window paragraph in
`entry_storage.c`, owed for a **cost** reason and not a lane reason; and **the rung-28 pad write, already
pre-registered in 773 §4** — read `0xFD512044`, compare against `0x00009F24`, write only if unconfigured,
read back.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist and 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
