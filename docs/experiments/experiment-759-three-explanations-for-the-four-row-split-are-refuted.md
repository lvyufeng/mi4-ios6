# 759: three candidate explanations for the four-row split are eliminated by the press's own cells — and what remains is the command word

**Read out of the rung-21 capture and the payload source. No device action, no press, no gate, no runner, no
build, `out/` untouched.** This is a reading of `out/stage90/captures/rung21-noidx-20260927-last_kmsg.txt`
(639,147 B, sha `25512586ae087d17…`), the capture 758 landed, plus one function of `entry_storage.c` read in
full.

**Why this exists.** 758 ended by naming a question and refusing to answer it: four command words in one boot
split into `{0x0000, 0x030A}` started and `{0x0102, 0x0209}` never started, the opcode column is the only
field that separates them, and *a clean split is not an explanation*. **Three explanations a reader would
reach for first are refuted by cells the same capture already carries**, and each refutation is cheap and
one of them contradicts the codebase's own comment. That is what this document is for: it does not answer the
question, it removes three answers, and it states exactly what is left.

## 1. The enable window does not predict "started" — and the capture says so in four cells

The source's own prose leans toward a **state** explanation. `entry_storage.c:3833-3836`, in rung 21's call
site:

> Rung 19's own press measured that its CMD3 left `CMD_INHIBIT` set at the end of a 1.2 s window, so a second
> command in the same boot would meet the driver's own inhibit guard inside `st_send_command` and either wait
> out its 10 ms bound or be refused

**The rung-21 press is the first boot in this ladder that sent four commands, so it is the first that can
test that.** It does not hold:

| word | what the interrupt-enable window was | started? |
| --- | --- | --- |
| `0x0000` (CMD0) | **OPEN** — `_int_enable_wrote = 1`, `_int_enable_held = 0x8001` (rung 14's window, opened before this command) | **yes** |
| `0x0102` (CMD1) | **CLOSED** — that window is restored on one unconditional line after CMD0's publishes, `_int_enable_readback = 0x8000`, bit 0 clear (m756's partial restore, bit 15 is the artifact) | **no** |
| `0x0209` (CMD2) | **OPEN** — `st_all_send_cid` opens its own window at `:2791` (`_cid_ena_wrote = 1`), reads it back `0x8001`, and restores at `:2838` (`_cid_readback = 0x8000`); `_cid_sig_enable = 0` | **no** |
| `0x030A` (CMD3) | **OPEN** — `st_cmd3_noidx` opens its own window immediately before its store: `_nidx_ena_wrote = 1`, `_nidx_ena_held = 0x8001`, `_nidx_readback = 0x8000`, `_nidx_sig_enable = 0` | **yes** |

**CMD2 and CMD3 carry the identical five window cells and the identical held value `0x8001`, and one started
while the other did not.** An open window is present in one of the two rows that failed and in both rows that
succeeded; a closed window is present in the other failing row. **The window state takes two values and the
outcome takes both values under each.** It is not the discriminator, and no amount of prose about the
between-commands gate can make it one.

**And the driver's own inhibit guard was not reached either.** `_cmd1_inhibit_before = 0x00000000`,
`_cmd1_inhibit_polls = 1`, `_cmd1_inhibit_ticks = 8`, `_cmd1_inhibit_timeout = 0` — the loop at
`entry_storage.c:2246-2258` read `PRESENT_STATE` once, found bit 0 clear, and proceeded; CMD0 the same
(`_cmd0_inhibit_before = 0`, `_cmd0_inhibit_polls = 1`, `_cmd0_inhibit_ticks = 9`). **The `return` at
`:2263` that would leave `sent = 0` was not taken by any command that reached the store**, which is why
every row carries `_*_sent = 1` and a `_*_word_read` equal to its own word. **CMD2 and CMD3 are the two
rows where the guard's own before-cell does not exist** — `st_all_send_cid` and `st_cmd3_noidx` are their
own bodies and neither publishes `inhibit_before` or `ps_before` (checked in the capture: 0 occurrences of
`_cid_inhibit_before`, `_cid_ps_before`, `_cid_inhibit_after`) — so for those two the evidence is
`_*_sent = 1` and the word read back, and **the distinction is stated rather than smoothed over**.

## 2. `st_send_command` has no per-opcode branch

Read in full. It clears its result, reads `PRESENT_STATE` (`:2239`), runs the driver's inhibit loop with the
driver's own bound (`:2246-2260`), clears the latch (read / write-back / read, `:2267-2270`), writes
`ARGUMENT` (`:2277`), builds the word with `ST_SDHCI_CMD_WORD(opcode, mmc_flags)` (`:2299`), stores it as a
**16-bit** write to `COMMAND 0x0E` (`:2301`), sets `r->sent = 1u` (`:2302`), and reads the word and the
inhibit bit back (`:2322-2323`) before the poll. **The only use of `opcode` anywhere in the function is as
an argument to the word-building macro at `:2299`.** There is no branch, no lookup, no special case.

**So CMD0 and CMD1 take identical code, with identical pre-state, and land identically:**

| | `_cmd0_*` | `_cmd1_*` |
| --- | --- | --- |
| `ps_before` | `0x01f80000` | `0x01f80000` |
| `inhibit_before` | `0x00000000` | `0x00000000` |
| `stale` | `0x00000000` | `0x00000000` |
| `clear_wrote` / `clear_after` | `0` / `0` | `0` / `0` |
| `arg` | `0` | `0` |
| `word` = `word_read` | `0x0000` = `0x0000` | `0x0102` = `0x0102` |
| `sent` | `1` | `1` |
| `inhibit_after` | **`1`** | **`0`** |
| `inhibit_seen` | `0x219` | **`0`** |

**Seven cells agree and two differ, and the two that differ are the two that are the answer.** The word is in
the register in both cases (`word_read` equals `word`), and in one case the block raises `CMD_INHIBIT` and in
the other it does not. **There is nothing between the store and the block that this image can see.**

## 3. Order alone does not explain it either

Started: **1st and 4th**. Never started: **2nd and 3rd**. A block that stopped accepting after its first
command would not start the fourth; a block that had wedged on a stalled command (CMD3 stalled for 1.2 s in
rung 19's press) would not start on the next one. **The row order is not monotonic in the outcome**, so no
"the block is done accepting" story fits, and neither does a simple bus-wedge story — and §1's window table
is the same argument from the other side.

## 4. The absences are readings, because the instrument that would see them ran

This is the discipline the record paid for in 749 and 760, and it is checked rather than asserted. **Three
different commands ran the same poll and two saw no non-zero sample at all:**

    _cmd0_polls = 0x0000021a = 538          _cmd0_any_polls = 0x0000021a   (a completion was seen)
    _cmd1_polls = 0x004da000 = 5,087,232    _cmd1_any_polls = 0x00000000   (nothing, over the whole bound)
    _cid_polls  = 0x004da400 = 5,088,256    _cid_any_polls  = 0x00000000
    _nidx_polls = 0x004da000 = 5,087,232    _nidx_any_polls = 0x00000000

> **CORRECTED BY 778.** The decimal attached to this hexadecimal was not the hexadecimal's value: it
> was transcribed rather than computed. The hexadecimal is the reading and it is unmoved.


**5,088,256 samples is not zero samples.** The poll ran to its full 1.2 s bound in three commands and
`any_polls` distinguishes "ran and saw nothing" from "did not run" — they are different cells, and the one
that says nothing ran is the one that would be the trap. `_cmd1_resp_read = 1` and `_nidx_resp_read = 1` say
the same for the response reads.

## 5. What is left, stated exactly

> **CORRECTED 2026-09-27 by `experiment-761-the-merged-class-and-the-vendor-write-census.md`, and the text
> below is left standing as the record of what this step concluded.** The two sets are `inhibit_seen`-defined,
> and **this project's own tool gives the four words four shape names, the second set's two members among
> them** (`0x0102` is `LINE MOVED INSIDE THE WINDOW`, `0x0209` is `LINE NEVER MOVED` — 749 §3 measured the
> cell that separates them). **The opcode claim below does not survive that**: under the only split a measured
> shape supports, the field of the `COMMAND` register that separates the words that completed from the words
> that did not is **bit 1, `RESP_PRESENT`** — 746's headline — and the opcode column is **not** clean (`3`
> appears in both sets). See 761 §3 for the field-by-field table and 761 §8 for the defect instance.

With §1 (not the enable window), §2 (not a code path — they take the same one) and §3 (not order) removed,
the split is **in the command word**, and within the word:

    started      0x0000  0x0300  0x030A  0x031A
    not started  0x0102  0x0209

and among all six words the only field of the `COMMAND` register that separates those two sets is the
**opcode** — `{0, 3}` against `{1, 2}`. Every other field fails on at least one row:

| field | value | fails on |
| --- | --- | --- |
| `RESP_TYPE_SELECT` (bits 1:0) | `00` / `10` / `01` | `0x0102` is `10` and did not start; `0x030A` is `10` and did |
| `CRC_CHECK` (bit 3) | on / off | `0x0102` is off, `0x0209` is on, both did not start |
| `CMD_INDEX` (bit 4) | on / off | `0x031A` is the only word with it set |
| `CMD_TYPE` (bits 7:6) | `00` for all six | separates nothing |
| argument | `0` / `0x00010000` | `0x0102` is `0` and did not start; `0x0000` is `0` and did |
| total value | — | any threshold is the opcode statement in another spelling, since opcode is bits 13:8 |

**And an opcode is not a mechanism.** Nothing in the payload branches on it (§2), nothing in the SDHCI
`COMMAND` register's own field definitions makes `1` and `2` a family, and **the block is the only thing that
could be reading it**. The honest statement is the one 758 made and this document narrows: **four words in one
boot separate into two behaviours by a field that no other evidence in this project names, and the three
explanations that are not the word have now been measured away.**

## 6. What this document does not say

- **It does not claim the opcode is the mechanism, or that a mechanism exists.** §5 removes three
  alternatives and states the remainder; it does not name a cause. **No rung is designed from it.**
- **It does not re-open 758's finding.** The `INDEX` rule is still refuted; nothing here touches that.
- **It does not contradict the source comment it quotes.** `:3833-3836` is a *prediction* about a second
  command meeting the inhibit guard, written before this ladder had ever sent four in one boot. **The
  prediction is now measured and it did not hold** — which is the comment doing its job, and is why it is
  quoted rather than corrected. The comment is not edited.
- **It does not move the arm, the payload, `out/`, or the entry image.** Nothing here is compiled. `out/`
  still holds the arm 758's press spent.
- **It does not arm anything, and no press may be spent without the operator's authorization.** A new rung
  costs **one press**, and it costs the arm in `out/` its **rebuildability** — once `src/` moves, a past arm's
  bytes can still be sent and still restored from its park, but cannot be produced from the tree again
  (666 §5). **It does not cost the live payload and it does not cost the park**: 666 §5 measured that
  `./build.sh` *does* reproduce (the 48 bytes 408 read as nondeterminism are the `STAGE90_XNU_ENTRY` switch),
  and the park is bytes on disk outside the build path. **This bullet said the opposite before 760 corrected
  it** — see `experiment-760-a-cost-asserted-from-a-citation.md`.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. It spends no press, adds no
  reading, and buys exactly one thing: **the next reader does not have to spend a press ruling out the three
  things this capture already rules out.**

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block starts a command and never
completes one — and 「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage stays withheld.**
