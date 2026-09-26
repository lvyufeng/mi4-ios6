# 741: the rung-19 pre-registration — the driver's own CMD3, the ladder's first non-zero argument, and an answer that is a pair of readings

Host-side. **No device action of any kind**: no `fastboot`, no `adb` to the device, no press. The arm it
describes is **ARMED, NOT PRESSED**. The last press is 740's (2026-09-26 15:58:25–15:59:37 UTC, exit 0,
capture `rung18-rb-20260926-155937-last_kmsg.txt`, 642,375 B).

**The builds WERE run for this arm** — the entry image four times and the payload twice — and that is a
disclosure and not a routine step (`./scripts/build.sh` does not reproduce, 408). §7 is the account,
including the one build that was made wrong.

## 0. Two spellings of this rung's number

The ladder in `src/entry/entry_storage.c` counts a rung by the **value** of
`STAGE90_XNU_STORAGE_PROBE`, so this arm's `#error` clause reads `18 = 17 plus …`. The commit subjects and
the pre-registrations count **ordinal arms**, and the two part company at the ladder's value 9 (which has
two arms), so **this arm's ladder value is 18 and its ordinal is rung 19**. The record spells both. 739 §0
and 740 §0 are the same sentence for the two arms immediately below.

## 1. What 740 answered, and what it left

740 pressed rung 18 and took the reading that rung existed for:

    _rb_resp_zero = 0x00000001      RESPONSE 0x10..0x1C held NOTHING before any command

with the whole movement in the same log — zero before any command, `_cmd0_resp = 0x0` with
`_cmd0_resp_read = 1` after CMD0, `_cmd1_resp = 0x40ff8080` after CMD1. So the `0x40ff8080` this ladder has
read since 733 as *the card answered CMD1* was put there by CMD1; **733's reading survives and 738's doubt
about the ladder's one piece of evidence that a card exists is retired**. 739 §3 registered exactly this
outcome and what it would buy, and it bought it.

**What the answer does not do is produce a CID.** CMD2's 136-bit read returned CMD1's own 32-bit word in
the long-response positions rather than a 128-bit CID (`_cid_resp0 = 0x40ff8080`, `_cid_resp1..3 = 0`), so
`mmc_attach_mmc`'s next statement is what is left to walk. 740 §6 named it: CMD3 `mmc_set_relative_addr`,
CMD9 `SEND_CSD`, CMD7 `SELECT_CARD`, and then CMD17 at LBA 1, where 531 §9's self-verifying reading lives
(the eight bytes `EFI PART`, or an MBR ending `55 aa`) — **and the next rung should carry the inhibit
reading beside the response reading**, because `_cmd0_inhibit_seen = 0x219` says the block *started* CMD0
while CMD1's and CMD2's were both ZERO.

## 2. What the rung is

**The whole of rung 18, plus the driver's own next command.**

    #if STAGE90_XNU_STORAGE_PROBE >= 18
        if (cid_sent != 0u) {
            ST_LIVE("xnu_live_storage_rca_gated", 0u);
            st_set_relative_addr(int_enable);
        } else {
            ST_LIVE("xnu_live_storage_rca_gated", 1u);
            ST_LIVE("xnu_live_storage_rca_gated_reason", 1u);
        }
    #endif

called at the end of `st_cmd_path`, after rung 17's CMD2 publishes and inside the same `#if` that rung
already uses.

**The command is `mmc.c:1409`'s `mmc_set_relative_addr(card)`, transcribed from `mmc_ops.c:194-210`:**

| | |
| --- | --- |
| opcode | `MMC_SET_RELATIVE_ADDR` 3 (`mmc.h:32`) |
| argument | `card->rca << 16`, with `card->rca = 1` assigned eight lines earlier (`mmc.c:1400`) → **`0x00010000`** |
| flags | `MMC_RSP_R1 \| MMC_CMD_AC` = `PRESENT \| CRC \| OPCODE` (`core.h:51`, `core.h:35`) |
| the word | `RESP_SHORT 0x02 \| CRC 0x08 \| INDEX 0x10` = **`0x031A`** |

**Two firsts, and neither is decoration.** The argument is **the first non-zero argument this ladder has
ever put on the bus** — CMD0, CMD1 and CMD2 all carry zero — and the `INDEX` bit is **the ladder's first**,
which makes `0x031A` the first command word here that is not `opcode << 8 | small_flags`. It is R1's own
property: an R1 response carries the opcode back, and that is why `MMC_RSP_OPCODE` is in the flags.

**And the vendor ordering is the one this tree uses, again.** `sdhci.h:53` has `RESP_LONG 0x01` where
upstream Linux has `RESP_SHORT 0x02` at that position, so `MMC_RSP_R1` decodes to `0x02 | 0x08 | 0x10` and
not to upstream's `0x0A`. All three constants are `_Static_assert`ed immediately above the body:

    _Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_SET_RELATIVE_ADDR, ST_MMC_RSP_R1) == 0x031Au, …);
    _Static_assert(ST_MMC_RCA_1 == (1u << 16), …);

because the build's `st_send_command` clause asserts that body's **accesses** and never the value it
stores — the note 737 added when it found a source comment claiming the assertion existed.

**The gate is the one reading rung 17's body now RETURNS.** `st_all_send_cid` was `void`; it is now
`uint32_t` and returns the field it publishes as `_cid_sent`, so the gate and the cell in the same log are
**one reading with two consumers** and cannot disagree. `mmc.c` itself puts `mmc_set_relative_addr`
immediately after `mmc_all_send_cid` with no condition beyond `!mmc_host_is_spi(host)` — the driver does not
gate CMD3 on a completion, and this arm could not read one anyway (the latch is the thing this line of
rungs is about). So the gate is the weakest one that is still a reading: **CMD2 was SENT**. It is
deliberately *not* CMD2's response, because CMD2's response is the reading 740 has just re-established.

**And `st_send_command` still carries the driver's own two per-command guards** — the bounded wait for
`SDHCI_CMD_INHIBIT` to clear (`sdhci.c:1096`) and the refusal to issue when the write-1-to-clear left a
command bit latched — so nothing here can put a command on a bus another command still holds.

## 3. The new cell is a PAIR, and that is what rung 18's method made possible

`st_set_relative_addr` reads `RESPONSE 0x10` through **the driver's own word-0 derivation** —
`(readl(RESPONSE + 0x1C) << 8) | readb(RESPONSE + 0x1B)`, which is `sdhci.c:1164-1172`'s shift its own
comment calls *CRC is stripped* — **immediately before the window opens** and **again immediately after
the command's publishes**:

| step | reading | cell |
| --- | --- | --- |
| 1 | the driver's word-0 derivation, before the window | `_rca_resp_pre` |
| 2 | the window's store, the enable read back, `SIGNAL_ENABLE` | `_rca_ena_wrote`, `_rca_ena_held`, `_rca_sig_enable` |
| 3 | the opcode, the flags and the argument this body is about to send | `_rca_op`, `_rca_flags`, `_rca_arg` |
| 4 | CMD3, and the command's own returned cells | `_rca_sent`, `_rca_word`, `_rca_word_read`, `_rca_arg_wrote`, `_rca_complete`, `_rca_err`, `_rca_timeout`, `_rca_status_any`, `_rca_any_polls`, `_rca_inhibit_after`, `_rca_inhibit_seen`, `_rca_inhibit_last`, `_rca_polls`, `_rca_ticks`, `_rca_clear_after`, `_rca_resp`, `_rca_resp_read` |
| 5 | the R1 decode, the three fields that separate a card status from an OCR | `_rca_state`, `_rca_ready`, `_rca_illegal` |
| 6 | the SAME derivation again, after the command | `_rca_resp_post` |
| 7 | **the answer**, and the alternative producer named | **`_rca_resp_moved`**, `_rca_resp_is_arg` |
| 8 | the window's exit and the block's own state | `_rca_wrote_back`, `_rca_status_post`, `_rca_readback`, `_rca_ps_after`, `_rca_done` |

**`_rca_resp_pre` and `_rca_resp_post` are ONE ARITHMETIC TAKEN AT TWO TIMES, not two derivations that
agree** — the same reason 739's pre-command read used this derivation rather than a second one. So three
readings in one log are directly comparable: `_rb_resp0` (before any command, rung 18's body),
`_cid_resp0` (after CMD2, rung 17's) and this pair.

**`_rca_resp_is_arg` names the alternative producer explicitly**: 1 iff the word after CMD3 equals the
ARGUMENT `0x00010000`. A register echoing what was written to `ARGUMENT 0x08` is a reading this rung
*publishes* rather than one a reader has to think of.

**And the shape test is in the log too.** CMD1's answer `0x40ff8080` has an OCR's shape — bit 30 set, a
voltage window in 15:8 — which is why it convinced for eight rungs. An R1 CARD STATUS has different fields:
`CURRENT_STATE` in 12:9, `READY_FOR_DATA` in 8, `ILLEGAL_COMMAND` in 22. **A card that took CMD3 has
`_rca_illegal = 0` and answers with state 2 or 3; a register answering with its old content gives a
`_rca_resp` equal to `_cid_resp0` or to `_rca_resp_pre`.** The three fields are published beside the raw
word so no reader has to take a decode on trust.

**The inhibit triplet is published for CMD3 as it is for the three commands below it** —
`_rca_inhibit_after` (the bit immediately after the store), `_rca_inhibit_seen` (how many of the poll's
first 1024 samples showed it) and `_rca_inhibit_last` (whether it was still set at the last one). **One log
now holds four commands' own inhibit readings side by side**, which is 740 §6's next question made
answerable rather than asked: a CMD3 whose inhibit is seen beside a CMD3 whose register moved is the pair
that separates *the sequencer took it* from *the register file answered*.

## 4. The two outcomes, and why a zero answers

    _rca_resp_moved = 1  ->  CMD3 MOVED the register. The sequencer took it, the ladder's next statement
                             is the driver's own CMD9 SEND_CSD, and `_rca_resp` is a word to decode.
    _rca_resp_moved = 0  ->  it did not. Then `_rca_resp_is_arg` and the R1 decode say what came back
                             instead: an ARGUMENT echo, the previous command's word, or a value with
                             `_rca_illegal` set. The next rung is a different signal, not a different
                             command.

**A `0` is not a failed press.** It is this rung answering — and it answers with three cells rather than
one, because the pair and the decode are in the same log as the aggregate. 739 §3 made the same point about
its own one-bit cell; this rung's version of it is stronger because the *reason* is published beside the
answer.

**`_rca_gated = 1` is also a complete answer.** `_rca_gated_reason = 1` says CMD2 was never SENT, which is
the value `_cid_sent` in the same log publishes. A refusal appears as this rung's own cell with a cause
beside it, never as a set of `_rca_*` keys that are merely absent.

**And `_rca_complete = 0` with a moved response is not a failure either.** It is 726's sixth hypothesis
measured a third time — a command the block answers without latching a status bit — now on a command whose
flag word carries `INDEX` and whose answer is one word wide. It is there to be read beside `_rca_status_any`
and `_rca_timeout`, the pair that separates the card from the controller (`SDHCI_INT_TIMEOUT 0x00010000`
is the *card* not answering; any other command-error bit is the controller).

## 5. The safety contract

**Two stores, both to `INT_ENABLE 0x34`**, and the class of write is the one rungs 13, 14, 15 and 17
already make:

1. the one-bit window `(read | SDHCI_INT_RESPONSE)`, taken **immediately before CMD3 is put on the bus** —
   because 733's press measured that a command's status bit is latched only while its enable stands (CMD0
   completed inside rung 14's window; CMD1, sent with it closed, was answered and never latched);
2. its restore, on **one unconditional line** immediately after the publishes. The value written back is
   the value the gate read, not a constant.

`SIGNAL_ENABLE 0x38` is **read and never written, at every rung**. It is the register whose conjunction
with `INT_ENABLE` raises this block's SPI 123 → intid 155, a line nobody here owns. No new command class
beyond CMD3, no data path, no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word, and **no byte of the
medium**.

**The restore is a PARTIAL one and that is a measurement, not a bug.** Bit 15 (`SDHCI_INT_ERROR`) rides
along with any write to `0x34` and no write clears it, so by this point the register reads `0x00008000`;
the expected cells are `_rca_ena_held = 0x00008001` for a store of `0x00000001` and `_rca_readback =
0x00008000` for the restore. 730, 733, 736, 738 and 740 all measured that pair.

**The failure mode is a DIAGNOSIS and not a lost device.** The 1.2 s command bound is inside
`st_send_command`; a delivery on that line would end the run at the dispatcher as `_irq_other_count = 1` /
`iat = 155`, an ending this image already reads and survives (709 ended that way on intid 170).

**And rung 15's body is excluded, asserted on the symbol and at the call site.** `st_quiet_enable_probe` is
compiled for the value 15 and no other, because its store above `st_cmd_path`'s gate leaves bit 15 set and
refuses the whole command path (736's press). **A log from this arm carrying any `_quiet_*` key is a log
from an arm that is not this one.**

## 6. What the arm's own build holds it to

`src/entry/build_entry.sh` gains a clause for `STORAGE_PROBE -ge 18`, `xnu_entry_741`, whose clauses refuse
in this order. For `st_set_relative_addr`:

- the **symbol** is present — an unreferenced static is dropped, and a build without it publishes every
  `_rca_*` cell as absent while the record claims the rung (m720's shape);
- it has a **size** in `nm -S`;
- its device access **SET** is exactly
  `f982491b:ldrb f982491c:ldr f9824924:ldr f9824930:ldr f9824934:ldr f9824934:str f9824938:ldr`;
- its **COUNTS** are exactly
  `f982491b:ldrb=2 f982491c:ldr=2 f9824924:ldr=1 f9824930:ldr=1 f9824934:ldr=2 f9824934:str=2 f9824938:ldr=1`
  — **and the two 2s at `0x1b`/`0x1c` ARE the rung**: a count of 1 there is a body that took the baseline
  and never took the reading, and every cell it publishes would still read correctly;
- its **STORES** are exactly `f9824934:str`;
- its device accesses **IN PROGRAM ORDER, distinct** are
  `f982491c:ldr f982491b:ldrb f9824934:str f9824934:ldr f9824938:ldr f9824930:ldr f9824924:ldr` —
  **the freshness baseline first, before any store**, which is the half neither the set nor the counts can
  carry;
- its **image side is EMPTY**;
- `st_cmd_path` calls it **exactly once**;
- and that call is ordered **AFTER `st_all_send_cid`** and **after the command path's last
  `bl <st_send_command>`**;
- and the four `st_send_command` call sites are `[st_cmd_path 2, st_all_send_cid 1, st_set_relative_addr 1]`.

**What the clause does NOT assert, said here so it is not mistaken for it**: the ARGUMENT and the WORD. A
body's memory accesses and its `bl` sites carry no immediate. What carries them is the source's
`_Static_assert`s and **the run's own `_rca_op`, `_rca_flags`, `_rca_arg` and `_rca_word` cells**. A
perturbation that changed the argument to zero would build, and this record would then carry
`_rca_arg = 0x00000000` beside a document that says `0x00010000` — a disagreement between the log and the
record, and not one this build can see.

## 7. Eleven falsifications, and two of them refused for a reason that is the finding

Each perturbation was applied to a copy of the final source; every one rebuilt the entry image under the
arm's own switch set.

| # | perturbation | verdict |
| --- | --- | --- |
| 1 | the post-half byte read made a 32-bit `ldr` at `0x18` | **SET**, reporting `NODECL-f982-2328:ldr` |
| 2 | the post half taken from `before` instead of the device | **COUNTS**, `f982491b:ldrb=1 f982491c:ldr=1` |
| 3 | the window is never written back | **COUNTS**, `f9824934:str=1` |
| 4 | a write-1-to-clear store into `INT_STATUS 0x30` | **SET**, `f9824930:str` |
| 5 | a store to `SIGNAL_ENABLE 0x38` | **SET**, `f9824938:str` |
| 6 | the baseline moved below the window's store | **PROGRAM ORDER** |
| 7 | the whole gated block moved above `st_all_send_cid` | **SYMBOL** — see below |
| 8 | the call site duplicated | **CALL-COUNT**, 2 calls |
| 9 | the call site removed | the **compiler**: `-Werror=unused-function` |
| 10 | a fifth `st_send_command` call inside the body | the **four-call-site** clause, `[2, 1, 2]` |
| 11 | a **live** call placed above CMD2 | **ORDER** — *"calls st_set_relative_addr on disassembly line 447 and st_all_send_cid on line 474: CMD3 must come AFTER CMD2"* |

**Cell 9 is the control rather than the clause.** With the call site gone the body is an unreferenced
static and `-Werror=unused-function` stops the build before the symbol clause is reached; the symbol clause
stays reachable by a rename, so it is not dead — but a reader comparing this table to the mechanism should
know which of the two fired.

**Cell 7 is the interesting one, and its verdict is not the clause it was written for.** Moving the whole
gated block above `st_all_send_cid` puts the call *before* the assignment that produces `cid_sent`, so the
compiler can prove `cid_sent == 0` there, folds the branch, drops the call, and the static is no longer
referenced — **the SYMBOL clause refuses, one clause earlier, because the body is not in the linked image
at all.** The ORDER clause's own subject ("a body called before the command it gates") was therefore never
built, and **cell 11 exists because of that**: an *unconditional* live call placed above CMD2 reaches it,
and refuses there. The order clause is not dead; cell 7 simply answers a different question.

**And the two perturbations that were refused earlier by the OTHER body are the falsification harness's own
defect, recorded here rather than dropped.** The batch asserted that a perturbation's anchor was *present*
(`count >= 1`) and replaced the **first** occurrence. `st_set_relative_addr`'s restore anchor occurs three
times in the source, and the first is inside `st_quiet_enable_probe` — a body compiled for the ladder value
15 and no other, i.e. **dead code at value 18**. So:

- perturbation 4's first run edited that dead body and printed **`BUILT-OK (no clause fired)`** — a
  statement about code that is not in the image;
- perturbation 3's first run edited `st_all_send_cid`'s restore and was refused by **that** body's COUNTS
  clause.

Both were re-run with `assert t.count(anchor) == 1` on every edit, and both are refused by the rung-19
clause as cells 3 and 4 in the table above. **The class is m714's own point (2)** — *a mutation that cannot
reach its target has tested nothing* — one level earlier: there the wrong *clause* fired, here the wrong
*bytes* were edited. **And the rung-18 falsification table was checked for the same artifact rather than
assumed to be clean**: every one of its nine perturbations that produced a layout refusal names
`st_resp_before`, the body under test, so that table does not carry it.

## 8. Host-side work, and what verified it

| command | reading |
| --- | --- |
| `tools/verify_press_ready.sh` | **5 of 5, exit 0** — flags `--allow-xnu-entry`, `--expect-arm=armed-storage-rca-c333d09c`; the rung-19 arm paragraph and its own consequence paragraph both ran in the narration |
| `tools/rehearse_revert_set.sh` | **38 ok / 0 failed** |
| `tools/verify_revert_set.sh out/stage90/frozen/armed-storage-rca-c333d09c --set=…` | VERIFIED, 11 files, 6 manifest-member checks |
| the same against `/mnt/data/mi4-ios6-export/armed-storage-rca-c333d09c` | VERIFIED, 11 files — the park is exported |
| `tools/check_set_name_rule.sh` | exit 0 — 28 sets, the suffix is the entry bin's own sha256 prefix |
| `tools/check_arm_variant_pair.sh` | the pair agrees — `STAGE90_XNU_STORAGE_PROBE=18` against `STAGE90_XNU_ENTRY 1` |
| `make check` | exit 0 |
| `./scripts/preflight_boot_check.sh --allow-xnu-entry` | exit 0 — **587** lines, and **the tree in front of the gate is the tree the image was built from**, because the entry sources are verified **by content** |
| two clean entry builds of the final source | `c333d09c…` byte-for-byte, `cmp`-measured |
| the payload's config against the pressed arm's | **byte-identical** (`6c2b6038…`, `cmp`-measured) |

**The measurements the record keeps.** Entry image `c333d09c…`, **5,552,764 B — the same size as the
pressed arm's**, so the seam constant did not move: `platform_cache_idle_exit` at `0x800492d4`, its
`bl <__wrap_FlushPoU_Dcache>` at `0x800492d8` returning to `0x800492dc` = `entry_trace.c`'s
`STAGE90_XNU_SEAM_LR`, re-derived read-only from the fresh disassembly. `.text` 5,332,776 → **5,333,960**
(+1,184). `st_set_relative_addr` at **`0x8000df60`, size `0x2d8`** (728 B); `st_all_send_cid` at
`0x8000dcc0`, size `0x2a0`; `st_cmd_path` at `0x8000e238`, size `0x788`; `st_send_command` at `0x8000cff8`,
size `0x320`; `st_resp_before` at `0x8000d7d4`, size `0x17c`.

**The builds, stated as the disclosure they are.** The entry image was built four times — the rung-19
build, two reproducibility builds of the final source giving `c333d09c` byte-for-byte (both `cmp`-measured),
and one more after the falsification batch restored the source — and **the payload twice**. The first
payload build was made **without `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1'`**: `./scripts/build.sh`
takes its variant switches from that variable and its default leaves `STAGE90_XNU_ENTRY` at `0u`, where
every `armed-storage-*` arm carries `1`. The result was a payload with the entry path switched off and a
682-byte config that **differed from the arm's at line 5** — caught by `cmp` against the parked pressed
arm, never by a check, and rebuilt with the flag. **This is the same mistake the rung-15 step made and
recorded, so it is a known shape and still not a refusal**: `tools/gate_flags_for_arm.sh` derives the
*gate's* flags from the arm, and nothing in the tree compares a fresh payload's own config against the
arm's record line. A check that did would be one line of `cmp` in `make check`; it is owed and it is not
in this commit. The pressed arm's park (`armed-storage-rb-05645d1a`) was already exported and was not
touched; neither was any other park.

## 9. What the rung is not

It is not a storage driver: the command path is now four commands (CMD0, CMD1, CMD2, CMD3) with **no data
phase at all**, and this rung's new body sends exactly one command and reads one register pair twice. It
does not produce the CID — CMD2's long read is still not a 128-bit answer — so CMD9 `SEND_CSD` and CMD7
`SELECT_CARD` are still ahead, and CMD17 at LBA 1 (where 531 §9's `EFI PART` reading lives) is further
still. **It does not meet the goal**: 「把基础驱动跑起来」/「起码要能进入操作系统」 is still unmet — no
storage, no filesystem, the OS not observed reaching userland — so **TWRP-to-storage stays withheld** (the
clause is conditioned on 「如果os已经能进去了的话」).

**And it is not the last rung either way.** If `_rca_resp_moved = 1`, the driver's own continuation is
CMD9; if it is `0`, the next rung is a different signal and the cells that name it are already in this
log.

## 10. The arm, and the press

**ARMED, NOT PRESSED.** `out/` holds `armed-storage-rca-c333d09c` and nothing is owed. The two commands
carry the `scripts/` prefix from the repo root, and `33e80afe` must be absent from **both** device lists
before firing:

    sudo -n adb devices
    ./scripts/preflight_boot_check.sh --allow-xnu-entry
    ./scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-rca-c333d09c

**No firer is armed**, and the press path is those two commands run by hand once each — 730 §6's finding,
since the launcher's bytes are kept nowhere.
