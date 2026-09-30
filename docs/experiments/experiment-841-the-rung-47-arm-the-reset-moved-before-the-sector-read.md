# 841 — the rung-47 arm: the block's data-state reset, MOVED BEFORE the sector read

**A BUILD AND A PARK, AND NOTHING ELSE.** Arm `armed-storage-6deb8308`, `STAGE90_XNU_STORAGE_PROBE=46`
(**VALUE 46 = ORDINAL RUNG 47**), entry bin `6deb8308…` 5,569,148 B (size unchanged — the move adds no
body), entry elf `40159a15…` 6,749,440 B (**unchanged** from the rung-46 arm's 6,749,440 — the move adds
no function), payload `stage90-qcdt.img` `d417285b…` 8,589,312 B; readiness 5 of 5, park 11 of 11; the
entry-group page move did **not** fire (`STAGE90_XNU_SEAM_LR` stays `0x8004c2dc`). **PARKED, NOT PRESSED,
NOT ARMED.**

---

## 1. It is the rung-46 press's own frontier, and it adds nothing

The rung-46 press (840) **proved the host was the holder** — the one-byte reset released both stuck bits,
`_dr_dat_line = 0` and `_dr_data_inhibit = 0`, with `_dr_held_after = 0` self-cleared in
`_dr_ticks = 0x827` (108.7 µs) — and in the same boot **proved the repair TOO LATE**: the widened guard
had already refused CMD17 (`_rd_inhibit_mask = 0x3`, `_rd_inhibit_dat = 0x2`, `_rd_sent = 0`), because
rung 46's `st_data_reset` call sits **after** `st_read_single_block`.

**So rung 47 is ONE CALL-SITE MOVE.** The SAME `st_data_reset` body issues after the CMD8/SET_BLOCKLEN
chain and **before** `st_read_single_block`, so CMD17 runs against the released line. No new body, no new
register, no new command, no new byte of the medium.

## 2. The build clause is the rung, and the first build refused

`xnu_entry_841` refuses the build unless `st_cmd_path`'s linked body holds **exactly one**
`bl <st_data_reset>` whose disassembly line falls **after** `bl <st_set_blocklen>` and **before**
`bl <st_read_single_block>`. Measured on this build: blocklen 520, **reset 521**, read 523. A build that
kept rung 46's after-read site (the reset issued on both sides of the read), or dropped the reset
entirely, is refused.

**And the value split is explicit.** Rung 43's own adjacency clause required the two instructions above
the sector read to be CMD16 then CMD17 with nothing between — which is exactly what the move deletes. It
is now scoped to values 43–45 and **superseded at 46** by an assertion that one of those two slots *is*
the reset, because the insertion IS the rung and a clause that kept the adjacency at 46 would refuse
every build of the arm the rung-46 press asked for. **The first build of this arm failed exactly there**,
which is the ladder's own value-awareness working rather than a sentence in a comment.

## 3. Read the rows

- `_dr_timeout = 0` with `_dr_dat_line = 0` and `_dr_data_inhibit = 0` — **THE LINE RELEASED BEFORE
  CMD17**: the reset did its work at the site that matters.
- Then the read's own cells, all of them reachable for the first time because no earlier rung got CMD17
  past the inhibit guard:
  - `_rd_sent = 1` with `_rd_complete = 1` and `_rd_err = 0` — **THE SECTOR READ ANSWERED THE GUARD THAT
    REFUSED IT AT RUNG 46**, and the frontier moves to the sector's content.
  - `_rd_gpt = 1` — **LBA 1 HOLDS A GPT HEADER**, the partition table this image can now walk;
    `_rd_gpt = 0` with non-zero words is a sector read that is not a GPT; `_rd_words_gated = 0x80` is the
    whole 512 bytes arrived.
  - `_rd_err` carrying `SDHCI_INT_TIMEOUT` — **THE CARD NOT ANSWERING** the command that now reaches it.
  - `_rd_int_data_err` nonzero — **THE TRANSFER failing** after the command.
  - `_rd_data_wait_timeout = 1` with `_rd_words_gated = 0` — **THE CARD SILENT** after a completed command.
- **The negative cell is exact**: `_rd_inhibit_dat = 0x2` with `_rd_sent = 0` **again** means
  `DATA_INHIBIT` was still set at CMD17's entry even though the reset ran above it — the inhibit
  re-asserted between the reset and the read, a different defect one call above, not this rung failing to
  build.

## 4. Safety

`RESET_DATA` has no effect on the card, so the card stays in TRAN with its block length and its
CSD/EXT_CSD exactly as rungs 42–46 left them. `fastboot boot` only; nothing flashed; `MMC_DATA_READ`
moves bytes card-to-host.

**NO PRESS HAS BEEN MADE AND NONE IS OWED: this arm is PARKED and NOT ARMED-FOR-PRESS, and the park is a
RECORD and not a queue. THE PRESS IS THE OPERATOR'S. THE GOAL IS NOT MET** — no sector read, no partition
table walked, no filesystem, no mount; TWRP-to-storage stays withheld.