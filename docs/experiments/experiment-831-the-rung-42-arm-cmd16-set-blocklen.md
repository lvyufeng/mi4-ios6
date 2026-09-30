# 831 — the rung-42 arm: CMD16 `SET_BLOCKLEN`, the first rung of the data path

**BUILT AND PARKED, NOT PRESSED, NOT ARMED.** No press has been made; none is owed by this record;
`frozen/` is a RECORD and not a queue. This arm adds the driver's own next command after the one the
rung-41 press proved — `core.c:2588`'s `mmc_set_blocklen(card, 512)` — the first command on this bus
whose argument is a **length**.

| | |
| --- | --- |
| arm | `armed-storage-3c6803f6` — named after its own entry bin's sha256 prefix |
| switch | **VALUE 41 = ordinal rung 42**, `STAGE90_XNU_STORAGE_PROBE=41` |
| the act | one command body `st_set_blocklen` (opcode 16, argument 512, flags 405, word `0x101A`), one ungated call to it in `st_cmd_path` — no data phase |
| entry bin | 5,569,148 B `3c6803f6…` |
| entry elf | 6,749,280 B `1d9d3706…` (+32 B over the rung-41 arm's: the new body is a new function) |
| payload | `stage90-qcdt.img` `bf3dbc78…`, 8,589,312 B |
| readiness | `tools/verify_press_ready.sh` **5 of 5**, exit 0; `make check` clean |

---

## 1. What the rung-41 press decided, and what this rung is

830's press was the whole premise, and it was the other half of 828's: rung 40's bound was right in
shape and wrong in size, and raising it from 20 ms to 1.25 s made the card's own 512 EXT_CSD bytes
arrive — `_ext_data_gated = 0x80` (all 128 words), `_ext_data_wait_timeout = 0`, `_ext_data_ticks =
0x67ba5` = 22.13 ms — beside `_ext_complete = 1` and `_ext_resp = 0x00000900`. **The data path works
end to end, and the press named the frontier: CMD16 `SET_BLOCKLEN`.**

**This rung is that command, and nothing else.** The driver's own next statement after CMD8 is
`core.c:2588`'s `mmc_set_blocklen(card, 512)`:

- the command is `mmc.h:50`'s `MMC_SET_BLOCKLEN`, **opcode 16**;
- the argument is `core.c:2594`'s `cmd.arg = blocklen` — **512**, the same number the transfer's own
  `ST_EXT_CSD_LEN` is, asserted equal in the source;
- the flags are `core.c:2597`'s `MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_AC` = `0x80 | 0x100 | 0x15` =
  **405**;
- `sdhci.c:1140-1143`'s five-bit mapping of 405 is `0x1A`, so the word the block receives is
  **`0x101A` = 4122**.

**It has NO data phase** — it is CMD13's shape (`st_send_status`) one opcode on, on a controller whose
command path this ladder has been running since rung 11.

## 2. Why the word is asserted as a whole and not as its mapping

`0x081A` is the bare five-bit mapping; `0x0D1A` is CMD13's own stored word (whose flags are 405 too).
A length and a state are two different commands that share a flag word, and only the **whole 32-bit
value** tells them apart. So the build clause does what rung 38's clause does one rung down — it
refuses **both directions**: the body must materialize `0x101A`, and `0x081A` and `0x0D1A` must be
**absent** from it. This is [[mi4-one-value-two-definitions]] (714's class) refused structurally rather
than described.

## 3. What the press would read

**Read the rows as the arm's own table:**

| reading | means |
| --- | --- |
| `_blk_complete = 1`, `_blk_err = 0`, `_blk_state = 4` (TRAN) | **CMD16 LANDED** — the frontier is the FIRST SECTOR READ (CMD17 at LBA 1, where the self-verifying eight bytes `EFI PART` or an MBR ending `55 aa` live) |
| `_blk_illegal = 1` | **THE CARD REFUSED `SET_BLOCKLEN`** — a block-address device that will not be told a length (read `_blk_switch_err` beside it) |
| `_blk_timeout = 1`, `_blk_complete = 0` | the bound expired on a card that did not answer |
| `_blk_inhibit_timeout = 1` | a controller that never came idle |
| `_blk_stale != 0` | a latch already dirty before the window opened |

**And the two cells that make the arm a reading rather than a comparison are the pre/post pair.**
`_blk_pre_state` is `RESPONSE`'s `R1_CURRENT_STATE` sampled **before** the command; `_blk_state_held`
is 1 exactly when it equals the response's own state. On the rung-37 press that comparison read
`_sta_state_held = 0` where the arm expected 1 — because **the field is sampled at response time**
(m823), so a `0` here is CMD16 having moved the card and a `1` is it having read a field the command
did not change.

## 4. The build clause, `xnu_entry_831`

It refuses the build unless the body at `0x8000fc74`:

- loads `r0 = 16`, `r1 = 512`, `r2 = 405` immediately before its `bl`;
- materializes `0x101A` (4122) and **not** `0x081A` (2074) or `0x0D1A` (3354);
- has the exact device-access set `[f9824910:ldr f9824924:ldr f982492e:ldrb f9824930:ldr f9824934:ldr
  f9824934:str]` with the asserted counts (`ldr=2`, `str=2`, `ldr=1`, `ldrb=1`, `ldr=1`);
- has the asserted program order (the `TIMEOUT_CONTROL` `ldrb` is **hoisted** above the window readback
  by the compiler, and that hoist is what the clause asserts — [[mi4-a-claim-in-a-comment-is-not-a-check]]);
- makes **no** store to `TIMEOUT_CONTROL` (`#46`);
- has an **EMPTY** image side;
- and `st_cmd_path` calls it exactly **once**, **UNGATED**, immediately after `bl <st_send_ext_csd>`.

`make check`'s `check_response_word_order` now reads **eleven** response-reading functions, this body
among them (`src/entry/entry_storage.c:6259 st_set_blocklen() reads the response register, 2
access(es)`), with **0 divergences** from the driver's word order.

**And the page-move hazard was cleared by the build, not by a run.** The build's own `xnu_entry_535`
clause printed `lr == STAGE90_XNU_SEAM_LR (0x8004b2dc)` and exited 0 — the entry group did **not**
cross a page boundary on this arm, so **neither** `entry_trace.c`'s `STAGE90_XNU_SEAM_LR` **nor**
`run_and_capture.sh`'s `EXIT_POP_LR_LITERAL` moved. That is the hazard that caught five earlier rungs
(696/708/724/770/822) before a press and was never caught by a run.

## 5. Safety

`fastboot boot` only; nothing flashed; no byte of the medium can change. CMD16 is `ac [31:16] 512 R1`
with no data phase, no storage write, and no register this image could not already reach. The bound on
the command poll is `st_send_command`'s own. The payload switch set is byte-identical to rungs 23
through 41 (`stage90-build-config.txt` `6c2b6038`), so no payload switch moved.

## 6. Where the ladder stands

The eMMC chain is CMD0 → CMD1 → CMD2 → CMD3 → CMD9 → CMD7 → CMD13 → CMD8 → **CMD16**. The card is in
TRAN (rung 37); the 512 EXT_CSD bytes arrived (rungs 38–41); this rung tells the card a block is 512
bytes so a sector becomes readable. The recommended next rung is **the first sector read (CMD17 at LBA
1)**, then filesystem → mount → TWRP-to-storage.

**THE GOAL IS NOT MET.** No sector read, no filesystem, no mount; TWRP-to-storage stays withheld.