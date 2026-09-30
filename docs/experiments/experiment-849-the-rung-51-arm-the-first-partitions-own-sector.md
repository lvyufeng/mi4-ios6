# 849 — the rung-51 arm: the first partition's own sector, LBA 1, with the sector addressing proved in force

**A BUILD AND A PARK.** Arm `armed-storage-960c57c4`, `STAGE90_XNU_STORAGE_PROBE=50`
(**VALUE 50 = ORDINAL RUNG 51**), entry bin `960c57c4…` 5,569,148 B (**the same bytes** as the rung-50
arm's — the whole rung is one immediate), entry elf `176bbe58…` 6,749,560 B (**byte-identical in size**),
payload `stage90-qcdt.img` `6c9a50ad…` 8,589,312 B; readiness 5 of 5, park 11 of 11; the entry-group
page move did **not** fire (`STAGE90_XNU_SEAM_LR` stays `0x8004c2dc`).
**PARKED, NOT PRESSED, NOT ARMED.**

---

## 1. It is the rung-50 press's own frontier, one number wide

The rung-50 press (848) answered two questions in one boot:

- **the table decoded right** — `_pm_p0_first = _pm_nonzero_first = 1`, `_pm_p0_len = _pm_nonzero_len =
  0xFFFFFFFF`, `_pm_p0_type = _pm_nonzero_type = 0xEE`: entry 0 is an **EFI protective partition
  pointing at LBA 1**;
- **the addressing class** — `_rd_cap_over = 1`, `_rd_shift = 0`, `_rd_blockaddr = _pm_sector_addressed
  = 1`: this card is **SECTOR-addressed**, so `block.c:1777`'s `arg <<= 9` is **not** applied and rungs
  43–49's byte address was the defect (`_rd_arg = 0x200` at value 46 was **sector 512**, not LBA 1).

And it stated its own tension in the log: `_pm_next_sector = 1` (the sector the table names) beside
`_pm_arg_sector = 0` (the sector that rung read), so `_pm_next_is_arg = 0`.

**Rung 51 is `st_read_lba` moving 0 → 1.** The command, the flags (`181`), the block length CMD16 set,
the reset-before-read order and the whole cell set are rung 50's byte for byte. The read now addresses
the sector the rung-50 decode named — and under this card's sector addressing the argument *is* the
sector, so the `1` on the bus is **LBA 1** and not byte 512.

## 2. The cell the rung exists for

`_rd_gpt` — published at rung 43 and never yet read on the right sector — compares the sector's first
two words against `EFI ` / `PART`. On the right sector it finally means something:

- **`_rd_gpt = 1`** — **THE MEDIUM IS A GPT DISK**: the protective MBR was telling the truth, and LBA 1
  is a real GPT header.
- **`_rd_gpt = 0`** with the transfer clean — **A SECTOR THAT IS NOT A GPT HEADER**: the protective MBR
  is stale or the GPT was overwritten, and the frontier moves to LBA 2 (the first usable sector).

And the companion cell flips with it: **`_pm_next_is_arg` is 0 at value 49 and 1 at value 50** — the
reading that says the sector this rung read and the sector the rung-50 decode named are the **same
number**.

## 3. The number is a build refusal, not a sentence

The value-49 half of `xnu_entry_832` already proves the CMD17 argument is a **load** from
`st_read_lba`'s slot, and resolves that address to the **symbol**. **Neither says what number is in
it** — and rung 50 stores 0 while rung 51 stores 1, with no cell in the log distinguishing the two by
shape: both publish `_rd_arg = _rd_lba`, both complete, both sit in TRAN.

New clause **`xnu_entry_849`** reads, out of the linked disassembly, the instruction that **fills** the
slot — found by the **same `[base, #off]` the argument load used** (so it is that variable and not a
slot the compiler happened to place alike), and refuses unless:

- the store's **immediate is 1** (rung 50's 0 is the protective MBR wearing rung 51's name);
- the store is **above the load** in program order (an inverted order sends the previous value and
  rung 51's sector never reaches the bus);
- the stored register is **not** the base register of the load (else the value read is not the value
  stored).

So *"a variable, at the right address, holding the table's own sector, stored before it is read"* is
**a build refusal**, holding the two halves of `xnu_entry_832`/`xnu_entry_849` to each other.

## 4. Read the rows

- `_rd_lba = _rd_arg = 1`, `_rd_arg_lba_agree = 1` — **the command addresses LBA 1**, unshifted.
- `_rd_gpt` — **the rung's own cell**: 1 = `EFI PART` at LBA 1 (a GPT header); 0 = not.
- `_rd_complete = 1`, `_rd_err = 0`, `_rd_state = 4` (TRAN), `_rd_words_gated = 0x80`,
  `_rd_word_read = 0x113A` — a clean, error-free 512-byte read out of TRANSFER state, which is what
  makes `_rd_w0`/`_rd_w1`/`_rd_w127` the **card's** bytes and not the FIFO's.
- `_pm_next_is_arg = 1` — the read's sector and the table's number are the **same number**.
- `_rd_cap_over = 1`, `_rd_shift = 0`, `_rd_blockaddr = 1` — the addressing class re-measured.
- The negative: `_rd_lba = _rd_arg = 0` would be **rung 50's protective MBR wearing rung 51's name** —
  refused by the build, not left to the press.

## 5. Safety

**It moves no new byte of the medium and touches no device register it did not touch before.** CMD17
reads; the card stays in TRAN with its block length and its CSD/EXT_CSD exactly as rungs 42–50 left
them. The only change is the sector **number** in the existing store — the entry bin is the same
5,569,148 bytes. `fastboot boot` only; nothing flashed; nothing written to storage.

**NO PRESS HAS BEEN MADE AND NONE IS OWED: this arm is PARKED and NOT ARMED-FOR-PRESS, and the park is a
RECORD and not a queue. THE PRESS IS THE OPERATOR'S. THE GOAL IS NOT MET** — the partition's own sector
read is the arm, not yet a reading; no filesystem, no mount; TWRP-to-storage stays withheld.