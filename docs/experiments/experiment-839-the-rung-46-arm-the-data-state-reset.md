# 839 — the rung-46 arm: RESET the block's data state machine

**A BUILD AND A PARK, AND NOTHING ELSE.** Arm `armed-storage-cd3d63bf`, `STAGE90_XNU_STORAGE_PROBE=45`
(**VALUE 45 = ORDINAL RUNG 46**), entry bin `cd3d63bf…` 5,569,148 B (size unchanged — the new body is
absorbed by page padding), entry elf `2148c60a…` 6,749,440 B (+28 from the rung-45 arm's 6,749,412),
payload `stage90-qcdt.img` `800ad99b…` 8,589,312 B; readiness pending, park 11 of 11; the entry-group
page move did **not** fire (`STAGE90_XNU_SEAM_LR` stays `0x8004c2dc`). **PARKED, NOT PRESSED, NOT ARMED.**

---

## 1. It is the rung-44 press's candidate (b), and now the one left by elimination

Rung 45's press (838) **refuted candidate (a)**: the missing `DATA_END` enable was not the cause — the
store took (`_de_ena_wrote = _de_ena_held = 0x00008002`) and the completion still did not latch
(`_de_data_end = 0`), with the line still held (`_de_dat_line = 1`). So the block's data state machine is
**genuinely stuck**, and the only repair left is the one the standard defines for exactly this state:
**reset it.**

## 2. The act is one byte to a register rung 4 already writes

`st_data_reset` (gated `#if STAGE90_XNU_STORAGE_PROBE >= 45`) writes **`SDHCI_RESET_DATA` (0x04)** into
`SOFTWARE_RESET 0x2F` and polls the byte for self-clear within `ST_RESET_DATA_TICK_BUDGET` (1,920,000
ticks = 100 ms at 19.2 MHz — the vendor's own bound, `sdhci.c:250-260`), then reads `PRESENT_STATE`,
`INT_STATUS` and `INT_ENABLE`.

- **`0x04` is `RESET_DATA` ALONE** — NOT `RESET_ALL` (0x01, which rung 4 writes and which resets the whole
  host including the clock) and NOT `RESET_CMD` (0x02). Two `_Static_assert`s in the source pin that
  distinction (`(ST_SDHCI_RESET_DATA & ST_SDHCI_RESET_ALL) == 0`, and against `RESET_CMD`).
- **It moves no byte of the medium.** `RESET_DATA` has no effect on the card, so the card stays in TRAN
  with its block length and its CSD/EXT_CSD exactly as rungs 42–45 left them.

The thirteen `_dr_*` cells it publishes:

| cell | meaning |
| --- | --- |
| `_dr_calls` | the body ran (1) |
| `_dr_was` | `SOFTWARE_RESET` before the store |
| `_dr_wrote` | `SDHCI_RESET_DATA` (0x04) — the byte written |
| `_dr_ps_before` | `PRESENT_STATE` before |
| `_dr_held_after` | the byte re-read after the store — 0 = self-cleared |
| `_dr_polls` / `_dr_ticks` | how long the self-clear took |
| `_dr_timeout` | 1 = the byte never self-cleared (the block refused the reset) |
| `_dr_ps_after` | `PRESENT_STATE` after |
| `_dr_dat_line` | `DAT_LINE_ACTIVE` (bit 2) after — **the decisive bit** |
| `_dr_data_inhibit` | `DATA_INHIBIT` (bit 1) after |
| `_dr_status_after` / `_dr_ena_after` | `INT_STATUS` / `INT_ENABLE` after |
| `_dr_done` | the body finished (1) |

## 3. The rows

- **`_dr_timeout = 0` with `_dr_dat_line = 0` and `_dr_data_inhibit = 0`** — **THE HOST WAS THE HOLDER.**
  The block was stuck on state it owns, the reset released it, and the data path is usable: the next rung
  is CMD17 again and the sector read.
- **`_dr_timeout = 0` with `_dr_dat_line = 1`** — **THE LINE IS HELD BY SOMETHING THE HOST DOES NOT OWN**:
  the card, the pad, or the pin. A host-controller reset that does not clear `DAT_LINE_ACTIVE` is a reading
  about the card, and it moves the frontier off the block entirely.
- **`_dr_timeout = 1`** — **THE BLOCK REFUSED THE RESET** (`_dr_held_after` is the byte that would not
  clear). A block state, not a card state.

## 4. The build clause `xnu_entry_838`

It refuses the build unless `st_data_reset`'s linked body holds the `mov rN, #4` (the `RESET_DATA` byte),
the `strb [rN, #47]` store to `SOFTWARE_RESET 0x2F` (base `0xf9824000`), and the six `xnu_live_storage_dr_*`
keys — so a reset of the wrong bit, or to the wrong register, cannot be built. It was already present and
inert at value 44 (rung 45); this build makes it live.

## 5. Safety

`fastboot boot` only, nothing flashed; the only store is one host-controller byte in a register rung 4
already writes, and it moves no byte of the medium — the card stays in TRAN. **NO PRESS HAS BEEN MADE AND
NONE IS OWED: this arm is PARKED, and the park is a RECORD and not a queue. THE PRESS IS THE OPERATOR'S.**

**THE GOAL IS NOT MET.** No sector read, no partition table walked, no filesystem, no mount;
TWRP-to-storage stays withheld.