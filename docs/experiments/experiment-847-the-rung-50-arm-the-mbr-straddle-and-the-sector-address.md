# 847 — the rung-50 arm: the MBR table decoded from the word pairs it straddles, and the sector the read addresses

**A BUILD AND A PARK.** Arm `armed-storage-28c82f79`, `STAGE90_XNU_STORAGE_PROBE=49`
(**VALUE 49 = ORDINAL RUNG 50**), entry bin `28c82f79…` 5,569,148 B, entry elf `1b006fc1…` 6,749,560 B
(**88 B larger** than the rung-49 arm's: the addressing prelude, the CSD cross-check and the new cells),
payload `stage90-qcdt.img` `0141f1e0…` 8,589,312 B; readiness 5 of 5, park 11 of 11; the entry-group
page move did **not** fire (`STAGE90_XNU_SEAM_LR` stays `0x8004c2dc`).
**PARKED, NOT PRESSED, NOT ARMED.**

---

## 1. It is the rung-49 press's own two owed readings

The rung-49 press (846) did two things in one boot: it **measured a protective MBR**
(`_pm_nonzero_type = 0xEE`) and it **found its own two 32-bit cells were not the fields they name** —
`_pm_p0_first = 0x0001FFFF` and `_pm_p0_len = 0xFFFF0000`, published by a module whose own header
claimed the two fields *"ARE word-aligned … the standard's own gift"* (**[[mi4-a-claim-in-a-comment-is-not-a-check]]**).

The record's first-LBA field is at byte `454 + 16k` = `4·113 + 2` and its length at `458 + 16k` =
`4·114 + 2` — **neither word-aligned**, so each 32-bit field **straddles two words**:

```
first  = ((w[113+4k] >> 16) & 0xFFFF) | ((w[114+4k] & 0xFFFF) << 16)
length = ((w[114+4k] >> 16) & 0xFFFF) | ((w[115+4k] & 0xFFFF) << 16)
```

Rung 50 is **the same table decoded right** — `first = 1`, `length = 0xFFFFFFFF` — so entry 0 is an EFI
protective partition pointing at **LBA 1**, the sector rung 47 read and found is **not** `EFI PART`.
`_pm_next_sector = 1` beside `_pm_arg_sector = 0` states that tension in the log; `_pm_next_is_arg = 0`
is the reading that this arm's read sector is **not** the partition the table names — the next arm sets
the read's sector to `_pm_next_sector` and this cell becomes 1.

## 2. The addressing defect the read's argument hid

Rungs 43–49 sent an argument derived as `LBA << 9`. That is the **byte**-addressing rule, and on this
card it is not the rule in force:

- `mmc.c:344-345` — `if (card->ext_csd.sectors > (2u*1024*1024*1024)/512) mmc_card_set_blockaddr(card);`
  the threshold is **4,194,304** sectors.
- This card's `_ext_sec_count = 0x01d5a000` = 30,777,344 > threshold, so `mmc_card_blockaddr` is **SET**.
- `block.c:1777` — `if (!mmc_card_blockaddr(card)) brq->cmd.arg <<= 9;` — the shift is therefore **NOT**
  applied.

So **rungs 43–49 sent byte addresses where the card wanted sectors**: `_rd_arg = 0x200` at value 46 was
**sector 512**, not LBA 1. Rung 50 carries the sector as `st_read_lba` with **no shift**. The `0` of
rungs 47–49 was accidentally correct (`0 << 9 = 0`), which is why the rung-48 press found a real MBR at
"LBA 0" and rung 47's read of "LBA 1" found no GPT header — it read sector 512.

`_rd_cap_agree = 1` is the **two independent producers** agreeing on the class: the EXT_CSD sector count
rung 38 moved and the CSD capacity rung 34 carried, cross-checked in the arm itself.

## 3. The two claims are made structural in the disassembly

- **The decode.** New clause `xnu_entry_845` reads `st_mbr_parse`'s linked body: it requires **≥ 2**
  high-half extractions (`lsr … #16`) and **≥ 2** low-half placements (`lsl … #16`) across the loads —
  the straddle, not a whole-word read. Already-built clause `xnu_entry_844` remains and asserts the body
  touches **no** device.
- **The address.** `xnu_entry_832` is split: at `>= 49` it **refuses** a CMD17 argument that is an
  immediate, or a body that shifts `r1` left — the byte-address rule mistaken for this card's mode. The
  argument must be a load from `st_read_lba`; the clause resolves the base register's `movw`/`movt` pair
  and compares it against the ELF's `st_read_lba` symbol.

## 4. Read the rows

- `_rd_cap = 0x01d5a000`, `_rd_cap_over = 1`, `_rd_shift = 0`, `_rd_blockaddr = 1` — **the card is
  sector-addressed** and the argument is the sector, unshifted.
- `_rd_lba = _rd_arg = 0` with `_rd_arg_lba_agree = 1` — the CMD17 argument is the LBA variable.
- `_rd_cap_csd_valid / _rd_cap_csd / _rd_cap_csd_over / _rd_cap_agree = 1` — the CSD and EXT_CSD counts
  agree that the card is over the 4,194,304-sector threshold.
- `_pm_p0_first = _pm_nonzero_first = 1` and `_pm_p0_len = _pm_nonzero_len = 0xFFFFFFFF` — **the table
  decoded right**: entry 0 points at LBA 1.
- `_pm_next_sector = 1`, `_pm_arg_sector = 0`, `_pm_next_is_arg = 0` — the tension stated in the log.
- `_pm_cap`, `_pm_sector_addressed`, `_pm_next_in_range` — the table's own view of the card's class and
  whether the next sector is inside it.
- `_rd_shift = 512` with `_rd_arg = _rd_lba << 9` would be the byte-addressing mistake; the build now
  refuses it.

## 5. Safety

**It moves no new byte of the medium and touches no device register it did not touch before.** The read
is the **protective MBR again (sector 0)** — the only change is that the argument is now a variable rather
than a constant, and that the decode's two 32-bit fields are the fields they name. No new command, no new
MMIO, no new enable window, no new store. The card stays in TRAN with its block length and its CSD/EXT_CSD
exactly as rungs 42–49 left them. `fastboot boot` only; nothing flashed; nothing written to storage.

**NO PRESS HAS BEEN MADE AND NONE IS OWED: this arm is PARKED and NOT ARMED-FOR-PRESS, and the park is a
RECORD and not a queue. THE PRESS IS THE OPERATOR'S. THE GOAL IS NOT MET** — no partition's own sector
read, no filesystem, no mount; TWRP-to-storage stays withheld.