# 833 — the rung-43 arm: CMD17 `READ_SINGLE_BLOCK` at LBA 1, the first sector read

**BUILT AND PARKED, NOT PRESSED, NOT ARMED.** No press has been made; none is owed by this record;
`frozen/` is a RECORD and not a queue. This arm issues the driver's own first act on the medium after
the block length is fixed — `block.c:1770-1829`'s request — and it is the first rung of this ladder
that **moves bytes card-to-host into a buffer the press can read back**.

| | |
| --- | --- |
| arm | `armed-storage-7228e3f9` — named after its own entry bin's sha256 prefix |
| switch | **VALUE 42 = ordinal rung 43**, `STAGE90_XNU_STORAGE_PROBE=42` |
| the act | one command body `st_read_single_block` (opcode 17, argument 512 = LBA 1 << 9, flags 181, word `0x113A`), one ungated call to it in `st_cmd_path`, and one 512-byte `.bss` array |
| entry bin | 5,569,148 B `7228e3f9…` |
| entry elf | 6,749,376 B `80dec1cb…` (+96 B over the rung-42 arm's: the new body is a new function with a 128-iteration loop) |
| payload | `stage90-qcdt.img` `abd421e7…`, 8,589,312 B |
| readiness | `tools/verify_press_ready.sh` **5 of 5**, exit 0; `make check` clean |

---

## 1. What the rung-42 press decided, and what this rung is

832's press closed the data-path question in the direction that matters: **CMD16 landed.**
`_blk_complete = 1`, `_blk_err = 0`, `_blk_state = 4` (TRAN), `_blk_pre_state = 4`, `_blk_state_held
= 1` — a command whose whole content is a length read a field (state TRAN) that **it did not change**,
which is exactly what `SET_BLOCKLEN` is forbidden from doing. `_blk_resp_moved = 0`
(`_blk_resp_pre = _blk_resp_post = 0x00000900`) says the `RESPONSE` register was not rewritten by a
command whose answer is a 48-bit R1 — consistent with m823's negative that a 48-bit response moves
`RESPONSE + 0` alone. The chain CMD0 → … → CMD16 ran end to end in one boot with no fault, and the
frontier the press named was **the first sector read**.

**This rung is that read, and nothing else.** The driver's own next statement is a request in
`block.c:1770-1829`:

- the command is `mmc.h:51`'s `MMC_READ_SINGLE_BLOCK`, **opcode 17**, taken by `block.c:1821`'s
  `readcmd = MMC_READ_SINGLE_BLOCK` when `brq->data.blocks <= 1` (`:1810`);
- the argument is **LBA 1 shifted to a byte address**. `block.c:1776-1777` is
  `cmd.arg = blk_rq_pos(req)` followed by `if (!mmc_card_blockaddr(card)) cmd.arg <<= 9` — so for LBA
  1 the argument is **`1 << 9 = 512`**, the same number as rung 42's block length **and a different
  quantity**: one is a length, this is an address;
- the flags are `block.c:1779`'s `MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC` = `0x80 | 0x15 | 0x20`
  = **181 = 0xB5** — **the same word rung 38 asserts for CMD8**;
- `sdhci.c:1140-1143`'s five-bit mapping of 181 is `0x1A`, and `sdhci.c:1146-1149` ORs
  `SDHCI_CMD_DATA 0x20` in because the command carries data, so the word the block's `COMMAND`
  register receives is **`0x113A` = 4410**.

**Why LBA 1 and not LBA 0.** Sector 0 holds the MBR's boot code; the GPT header begins at LBA 1, and
its first eight bytes are the ASCII `EFI PART`. So the read is **self-verifying**: either the eight
bytes are `EFI PART` (a GPT), or they are an MBR-style sector ending `55 aa`, or the bytes are neither
— and each of those is a different fact about the medium, read off the returned words rather than
guessed. A body that read sector 0 would make the arm's own derived cell read 0 on a device that is
in fact GPT: a false negative that looks exactly like "not GPT".

## 2. Why the flags are asserted as 181 and not 413, and why the word is asserted whole

The first draft of this rung's clause asserted **413** (`0x80 | 0x100 | 0x15 | 0x1D`, reading
`MMC_RSP_R1` as if it were the ARM SDHCI driver's `0x100 | 0x15`). The compiler emitted **181**. The
driver's own `MMC_RSP_R1` (`core.h:51`) is `0x15` — PRESENT | CRC | OPCODE — so `MMC_CMD_ADTC`'s
`0x20` is what makes 181 ≠ CMD13's 405 despite the *same* five-bit mapping `0x1A`. **That is the whole
risk this rung carries**: with `MMC_CMD_ADTC` clear, `st_send_command` never ORs `SDHCI_CMD_DATA`
into the word (`entry_storage.c:3334`), the block never asserts `DATA_AVAILABLE`, and the 128-word
loop exits on its first gate — **a complete, error-free command whose every cell reads as a success
and which read no sector at all**. So the clause refuses **both directions**: r0 = 17 (not 16, not
18), r1 = 512 (not 0 — a forgotten shift), r2 = **181** (not 405, not 413, not 21), and the body must
materialize `0x111A` (4378) **and** the block's `0x113A` (4410) while **refusing** CMD16's `0x101A`
(4122), CMD8's `0x081A` (2074) and CMD13's `0x0D1A` (3354) by number. This is
[[mi4-one-value-two-definitions]] refused structurally: **four different flag words fold to the same
`0x1A`**, so the opcode is asserted separately from the two words.

## 3. The body is rung 38's command path with one opcode changed

`classify_body` reports the body's device accesses **in program order** as
`f9824910:ldr f9824928:ldrb f9824934:str f9824934:ldr f9824938:ldr f982492e:ldrb f982492e:strb
f9824904:strh f9824904:ldrh f9824906:strh f9824906:ldrh f982490c:strh f982490c:ldrh f9824924:ldr
f9824920:ldr f9824930:ldr` — **rung 38's order, instruction for instruction** — and its set is rung
38's **fifteen accesses exactly**, with `TIMEOUT_CONTROL 0x2e` **read then written** (the one store
rung 42's clause *forbids* and this one *requires*, because `sdhci.c:827-828` writes it exactly when
`data || (cmd->flags & MMC_RSP_BUSY)` and CMD17 is `adtc`). The order is load-bearing: `RESPONSE 0x10`
is read **first**, before anything in the block is written, which is what makes `_rd_gate_state` a
reading of the card's own R1 carried up from CMD16 and not of a register this body just moved. And
**no byte in the `0x13..0x1B` band** — those are the CRC bytes one below their own `RESPONSE` words
and belong to CMD9's 136-bit assembly alone; CMD17 answers with a 48-bit R1, so no word has a byte
below it ([[mi4-the-136-bit-response-has-one-word-order]]).

Three of the ten asserted access counts are the arm's own safety:
`f982492e:strb=2` says `TIMEOUT_CONTROL` is written **twice** — armed before the command and
**restored** at the end; a single `strb` leaves the controller with a data-phase timeout it did not
have. `f9824934:str=2` / `:ldr=2` say the `INT_ENABLE` window is opened, read back, closed and read
back. And `f9824910:ldr=1` says the pre-command `RESPONSE` is the **only** response this body reads —
the post-command R1 comes out of `st_send_command`'s own result struct, so a count of two would mean
`_rd_resp` had two producers.

## 4. The image side: why `st_ext_csd` is deliberately absent

The 128-word PIO loop fills a **new** file-scope object, `st_read_block`, one object over from
`st_ext_csd`. The clause resolves the body's image-side addresses to symbols and allows only
`[st_read_block st_csd_words st_csd_words_valid st_tacc_exp st_tacc_mant]` — **`st_ext_csd` is not in
that list**, because a sector written into it would silently change the meaning of the cells rung 38
publishes one rung later. `st_read_block` is **required** present, and an address no symbol covers is
refused: a sector read into some other object would satisfy every device-side assertion above while
leaving `_rd_w0` a reading of nothing.

The two GPT signature words are asserted as the **four `movw`/`movt` halves gcc actually emits** —
`EFI ` = `0x20494645` is `movw #17989` + `movt #8265`, `PART` = `0x54524150` is `movw #16720` +
`movt #21586`, all four required — because the derived cell `_rd_gpt` is the comparison
`(w[0] == 0x20494645) && (w[1] == 0x54524150)`; a body that published the words without comparing
them would leave the press to decide by eye.

## 5. The build clause, `xnu_entry_832`, and one prose defect it carried

The clause refuses the build unless the body at **`0x8000ffac`** loads `r0 = 17`, `r1 = 512`,
`r2 = 181` before its one `bl`; materializes `0x111A` and `0x113A` with `0x101A`/`0x081A`/`0x0D1A`
refused; has the exact set, order and counts above; has an image side resolving to the five allowed
symbols with `st_read_block` present; and `st_cmd_path` calls it **exactly once, UNGATED, immediately
after `bl <st_set_blocklen>`** — CMD16 then CMD17, adjacent, the driver's own order.

**Two sentences in the clause's own `echo` were wrong while the assertions beside them were right** —
m820's class. It printed `0x19D` (413) for the flags that the assertion enforces as **181**, and it
printed "the image side is EMPTY" where the final form uses symbol resolution. Both were corrected;
the bin is byte-identical across the rebuild, so the fix lives only in the build's narration.

`make check`'s `check_response_word_order` now reads **twelve** response-reading functions, this body
among them (`src/entry/entry_storage.c:6513 st_read_single_block() reads the response register, 1
access(es)`), with **0 divergences** from the driver's word order.

## 6. The sixth page move

The build's own `xnu_entry_535` clause **refused** at the arm's address: the entry group crossed a
page boundary on this rung, so **one address — the instruction after the idle exit's
`bl FlushPoU_Dcache` — moved in two files**: `src/entry/entry_trace.c`'s `STAGE90_XNU_SEAM_LR` and
`scripts/run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`, `0x8004b2dc` → **`0x8004c2dc`**, +0x1000
exactly. It was caught by the build, before a press and before readiness (696/708/724/770/822 are the
five before it, and 770 moved at a **byte-identical** bin), never by a run
([[mi4-entry-group-page-move-pins-two-copies]]).

## 7. What the press would read

**Read the rows as the arm's own table:**

| reading | means |
| --- | --- |
| `_rd_gpt = 1` (LBA 1 begins `EFI PART`) | **THE MEDIUM IS GPT** — the partition table's own header is on this bus, and the next rung walks it |
| `_rd_words_gated = 0x80`, `_rd_data_wait_timeout = 0`, `_rd_complete = 1` | **A WHOLE SECTOR ARRIVED** — 128 words, no gate timed out |
| `_rd_w0`/`_rd_w1` are a plausible first four bytes but `_rd_gpt = 0` | a sector arrived that is **not** an LBA-1 GPT header (an MBR-only device, or a wrong address) |
| `_rd_gated = 1`, `_rd_done = 0` | the precondition refused — the card was not in TRAN; CMD17 was not sent |
| `_rd_data_wait_timeout = 1`, `_rd_words_gated = 0` | the controller never asserted `DATA_AVAILABLE` — a card that took the command but delivered nothing |
| `_rd_int_data_err != 0` | the controller's own timeout / CRC / end-bit bits fired mid-transfer |
| `_rd_complete = 0`, `_rd_err = 1` | CMD17 itself was not answered (read `_rd_state` beside it) |
| `_rd_word_read != 0x113A` | the block's `COMMAND` register held a different word — the `SDHCI_CMD_DATA` OR did not happen |

## 8. Safety

`fastboot boot` only; nothing flashed; **no byte of the medium can change.** CMD17 is
`adtc [31:0] data addr R1` and `MMC_DATA_READ` (`core.h:41`) moves bytes **card-to-host**, so nothing
is written to storage; the sector read is one the card's own `EXT_CSD_SEC_CNT = 0x01d5a000` says
exists; the body touches no register this ladder has not reached since rung 38; and the data-phase
bound is rung 41's measured-sufficient 1.25 s. The payload switch set is byte-identical to rungs 23
through 42 (`stage90-build-config.txt` `6c2b6038`), so no payload switch moved.

## 9. Where the ladder stands

The eMMC chain is CMD0 → CMD1 → CMD2 → CMD3 → CMD9 → CMD7 → CMD13 → CMD8 → CMD16 → **CMD17**. The
card is in TRAN; the 512 EXT_CSD bytes arrived; the card has been told a block is 512 bytes; this rung
reads sector 1 and reads it back word by word. The recommended next rung is **the GPT partition-table
walk** (LBA 1's header, then the LBA-2 entry array), then filesystem → mount → TWRP-to-storage.

**THE GOAL IS NOT MET.** No partition table walked, no filesystem, no mount; TWRP-to-storage stays
withheld.