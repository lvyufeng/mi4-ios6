# 824 — the rung-38 arm: CMD8 `SEND_EXT_CSD`, the ladder's first data phase

**AN ARM, NOT A PRESS.** Rung 38 is built, parked and verified; **nothing has been pressed, no press is
owed and none is authorized**. This file is the record of *what was built and what the build refuses*.
Its park, `out/stage90/frozen/armed-storage-92b6c552/`, is a **record and not a queue** — and it is the
last park in this tree that should be pressed without the operator asking for it by name, because it
carries a **data path this ladder has never exercised**.

Rung 37's press (823) answered the state question: **`_sta_state = 4`, the card is in TRAN**. The
frontier is now the DATA PATH, and rung 38 is the driver's own next statement after
`mmc.c:1436`'s `mmc_select_card` — `mmc.c:1447`'s `err = mmc_get_ext_csd(card, &ext_csd)`. **CMD8
`SEND_EXT_CSD`, a 512-byte read.**

---

## 1. The central number is a data-phase bit, not a flag

CMD8's flag word is `mmc_ops.c:263`'s `MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC` = **181 = 0xB5**.
The block does not receive 181. It receives a word built in two steps:

| step | source | value |
| --- | --- | --- |
| opcode 8 shifted into place | `st_send_command` | `0x0800` |
| the five-bit mapping of 181 | `sdhci_cmd_to_flags`, `sdhci.c:1140-1143` | `0x1A` → `0x081A` |
| **OR `SDHCI_CMD_DATA 0x20`** | `sdhci.c:1146-1149`, taken when `cmd->data != NULL` (`core.c:338`) | **`0x083A` = 2106** |

**The build asserts both numbers on two otherwise identical clauses of the same body** — one requires
`0x083A` to be the word *stored*, the other requires `0x081A` never to be — so 714's class (one value,
two definitions) is refused in both directions rather than being a sentence in a comment. The
narration the build prints reads, in part:

> `st_send_ext_csd (at 0x8000f484) loads [r0=#8 (SEND_EXT_CSD), r1=#0 (no argument at all …), r2=#181
> …] immediately before its bl to st_send_command (disassembly line 235)`, and the word it STORES is
> `0x083A (2106, asserted)` … and `0x081A` is the same five bits without it.

## 2. The repair this rung forced in `st_send_command` itself

Until this rung the ladder's command-word helper took only `(opcode, mmc_flags)` and **never ORed
`SDHCI_CMD_DATA`**. A data command would therefore have gone out with `BLOCK_COUNT` armed and no DATA
bit — the silent failure `entry_storage.c`'s own header comment predicts. The OR is added under
`#if STAGE90_XNU_STORAGE_PROBE >= 37`, because `ST_MMC_CMD_ADTC` and `ST_SDHCI_CMD_DATA` are declared
only at this rung and above.

**Measured, and this is the point of the guard:** every rung below 37 passes flags whose ADTC bit is
clear — CMD0's `0`, CMD1's `ST_MMC_RSP_R3`, CMD2/CMD9's `ST_MMC_RSP_R2`, CMD3/CMD7's `ST_MMC_RSP_R1`,
CMD13's `ST_MMC_RSP_R1_SPI` — so below 37 the new branch is the identity and the body compiles to the
same bytes it did before.

- the **value-36** build after the guard is byte-identical to the spent rung-37 park `054f8269…`;
- the **value-37** build is byte-identical (`92b6c552…`) before and after the guard.

So every park below this rung is still reproducible from this tree — the property a spent park's
recheck rests on.

## 3. The gate: the first gated command in four rungs

The body re-reads `RESPONSE 0x10` at decision time — the same `RESPONSE`-gate shape
`st_send_command` reads directly (`entry_storage.c:3072`, one access) — and refuses with
`_ext_gated=1` / `_ext_done=0` unless `R1_CURRENT_STATE` (bits 12:9, `_ext_gate_state`) is **4 (TRAN)**.

That value is not assumed. **It is what the rung-37 press measured.** The gate/argument/word sequence
the three gates below use is repeated here against a value a press produced.

## 4. The timeout comes from rung 35's *carried* CSD, not from `RESPONSE`

**m814 rule 4** refuses a right shift of a *raw* response word, because the four `RESPONSE` registers
and the four words the driver assembles from them **are different values**. So the assembled CSD is
carried across the rungs — `st_csd_words[4]` plus `st_csd_words_valid` — and bound to rung 35's body by
a coverage assertion the build makes (four words by coverage, one validity byte). The timeout
arithmetic is the driver's own `sdhci_calc_timeout` over `TAAC`/`NSAC`, doubled to the first count that
covers it and capped at `_ext_tout_max`, because `SDHCI_QUIRK2_USE_RESERVED_MAX_TIMEOUT` is set at
`sdhci-msm.c:2904`.

## 5. The PIO path, in the vendor's own order

Read out of the *linked image*, in program order, distinct — the build's own narration:

```
RESPONSE gate read → HOST_CONTROL 0x28 → INT_ENABLE 0x34 (window open)
  → TIMEOUT_CONTROL 0x2E (BEFORE BLOCK_SIZE — sdhci.c:827-828 above :831)
  → BLOCK_SIZE 0x04 ← 0x7200 → BLOCK_COUNT 0x06 ← 1 → TRANSFER_MODE 0x0C ← 0x0012
  → ARGUMENT → COMMAND → 128 × 32-bit reads of BUFFER 0x20
  → window close → INT_STATUS 0x30 → PRESENT_STATE 0x24
```

Each of the three halfword setup registers (`BLOCK_SIZE`, `BLOCK_COUNT`, `TRANSFER_MODE`) is written
**and read back**. **It adds no megabyte and no device**: every register is the controller's own,
mapped since rung 14.

## 6. What this arm does *not* assert

It asserts the **word** the block receives and the **order** of the registers it touches. It does
**not** assert that a 512-byte read completes, because no press has been made. The cells a first press
would read:

- `_ext_words_read` (0 or 128) — did the 128 PIO reads happen;
- `_ext_w0` / `_ext_w127` — the first and last words;
- `_ext_sec_count` — the card's own capacity word out of the ext-CSD;
- `_ext_tout_count`, `_ext_timeout_us`, `_ext_timeout_step0` — whether the driver's arithmetic, run off
  a carried CSD, produces a count the block accepts.

And the negative cell: **`_ext_gated=1`** is the gate refusing on a card not in TRAN, which would say
the state rung 37 measured did not persist to the next boot and the frontier is *a state* again.

## 7. The build, the park, and the record

| | |
| --- | --- |
| entry image | `92b6c552829c2039187d482292762e3f2f89f95e8124636b2dfe476ed57e48b6`, 5,569,148 B |
| entry config | `91138384…`, 900 B — the spent rung-37 arm's with **exactly two** lines moved: the artifact hash and `STAGE90_XNU_STORAGE_PROBE` 36 → 37 |
| payload switches | `6c2b6038…`, 682 B — **byte-identical to rungs 23 through 37**; no payload switch moved |
| park | `out/stage90/frozen/armed-storage-92b6c552/`, **eleven** members, hashed by hand |
| record | `records/revert-set.txt`, the `# 824` block |
| readiness | `tools/verify_press_ready.sh` — **5 of 5, exit 0** |
| seam | **did not move** — the build's own `xnu_entry_535` clause still validates `0x8004b2dc`, so neither `entry_trace.c`'s `STAGE90_XNU_SEAM_LR` nor `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL` was edited |

**A size is not a value.** The new entry `.bin` is the *same size* as the spent arm's (5,569,148 B)
despite a larger `.text` (+2,432) and `.bss` (+512) — the alignment padding the rung-37 body's page
move introduced absorbs the growth, and the `.elf` grew by 244. The two images differ in **685,691 of
their 5,569,148 bytes**. This is `[[mi4-stand-in-size-is-not-value]]` turned on the artifact: the
identity is the hash and never the size.

## 8. Build clauses

Four clauses run at value 37 and all four pass: `xnu_entry_812` (which settled the RCA is the host's
own assignment), `xnu_entry_819` (rung 36's body), `xnu_entry_822` (rung 37's body) and the new
`xnu_entry_824` (rung 38's). Every ladder value — 0, 11, 12, 18, 20, 21, 30, 33, 34, 35, 36, 37 —
compiles `-fsyntax-only` exit 0. `make check` passes; `tools/check_response_word_order.py` now counts
**ten** response-reading functions, with `st_send_ext_csd` (`entry_storage.c:5692`) the tenth.

---

## The goal

`「把基础驱动跑起来」` is one command further along **in the source** — the first rung whose command
transfers bytes rather than only frames. But **no transfer has completed, no filesystem is reached and
no mount is made**, so `「让os可以正常启动并且挂载存储」` is not reached and **TWRP-TO-STORAGE STAYS
WITHHELD**: the storage it would be written to is the one thing this ladder has not got working. The
first clause, `「起码要能进入操作系统」`, remains met (818 §1).