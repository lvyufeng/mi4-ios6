# 850 — the rung-51 press: **LBA 1 BEGINS `EFI PART` — THE MEDIUM IS A GPT DISK**

**A PRESS**, made under the standing instruction. One gate exit 0, one runner exit 0; `fastboot boot`
only, nothing flashed, nothing written to storage; `33e80afe` absent from **both** device lists
hand-checked immediately before the gate; **the phone returned** (`/tmp/cancro-last_kmsg.txt` 655,146 B,
runner exit 0). Arm **`armed-storage-960c57c4`** (`STAGE90_XNU_STORAGE_PROBE=50`, rung 51) is now
**SPENT**. Capture `out/stage90/captures/rung51-lba1gpt-20260930-120713-last_kmsg.txt` 655,146 B
`364105d7…`.

---

## 1. The question rungs 43–49 all asked and none could answer

`_rd_w0 = 0x20494645` and `_rd_w1 = 0x54524150`. Those two words are **`"EFI "`** and **`"PART"`** —
and the arm's own derived cell agrees: **`_rd_gpt = 1`**.

**LBA 1 BEGINS `EFI PART`. THE MEDIUM IS A GPT DISK.** The protective MBR the rung-50 decode read was
telling the truth: its single `0xEE` entry pointed at LBA 1, and LBA 1 is a real GPT header.

That the sector is a GPT header is corroborated by what it is **not**: `_rd_w127 = 0`, so the sector
does **not** end in the MBR signature — `_rd_sig = 0` and `_rd_mbr = 0`, exactly as a GPT header
should read (the MBR signature lives only at LBA 0). And the re-run partition walk on *this* sector
found no non-empty record (`_pm_p0_type = 0` across all four entries), which is what a GPT header's
first 512 bytes look like to an MBR walk: `_pm_next_sector = 0` with `_pm_next_is_arg = 0`.

## 2. The read that finally got there

| cell | value | meaning |
| --- | --- | --- |
| `_rd_lba` = `_rd_arg` | **`1`** | the CMD17 argument is **sector 1**, unshifted |
| `_rd_arg_lba_agree` | `1` | the argument is `st_read_lba` |
| `_rd_shift` | `0` | `block.c:1777`'s `<< 9` is **NOT** applied |
| `_rd_blockaddr` = `_pm_sector_addressed` | `1` | **the card is SECTOR-addressed** |
| `_rd_cap` = `_pm_cap` | `0x01d5a000` | 30,777,344 sectors, above the threshold |
| `_rd_cap_over` | `1` | above `mmc.c:344`'s 4,194,304-sector threshold |

**This is why rungs 43–49 never answered it.** They sent `LBA << 9` as the argument — `_rd_arg = 0x200`
at value 46 was **sector 512**, not LBA 1 — and under this card's sector addressing that is the wrong
sector entirely. Rung 47's read of "LBA 1" found no `EFI PART` (`_rd_w0 = 0xe8f9decb`) because it read
sector 512; rung 50's sector 0 read the real MBR; **only now, with the sector variable carrying `1` and
the shift gone, does the read land on the GPT header**.

## 3. Read the counter-cells

The transfer is clean end to end: `_rd_complete = 1`, `_rd_err = 0`, `_rd_state = 4` (**TRAN**),
`_rd_words_gated = 0x80` (128 words = a whole sector), `_rd_word_read = 0x113A`. So `_rd_w0`/`_rd_w1`/
`_rd_w127` are the **card's** bytes and not the FIFO's. The card is still in TRANSFER state across the
press, exactly as rungs 42–50 left it.

The companion cell behaved as the arm's pre-registration said it would: `_pm_arg_sector = 1` (the sector
this rung read). `_pm_next_is_arg = 0` here is because the GPT header's first 512 bytes contain no MBR
record, so the walk named no sector — not because the read missed.

## 4. The honest negative, unchanged

`_rd_cap_csd_valid = 0` — the CSD carried from rung 34 is not in a form this arm's reader decodes, so
the **second independent producer did not answer** and `_rd_cap_agree` is **absent** rather than
asserted. The EXT_CSD count (`_rd_cap = 0x01d5a000`) is the only producer that answered — a
single-producer reading, published as such.

## 5. The standing readings, unchanged

`xnu_live_seam_lr = 0x8004c2dc` **did not move** (the entry-group page move did not fire);
`xnu_live_seam_sctlr = 0x30c57879`; `xnu_entry_failures = 0x0`; `abort_entries = 0`. The goal's own floor
is met as in 504/520/533 (pid 1's syscalls, the fixture's `0xfeedface` read back into a user page). A
panic is present (`fault_addr = 0xfa0065c`) — the epilogue's own store faulting in this context, the same
forced-ending shape as before, not this rung's doing.

## 6. What this press established

- **ESTABLISHED — THE MEDIUM'S LAYOUT**: LBA 0 is a **protective MBR** whose single entry points at
  **LBA 1** (rung 50, 848), and **LBA 1 is a GPT header** (`_rd_w0`/`_rd_w1` = `EFI `/`PART`,
  `_rd_gpt = 1`). The two agree: **this is a GPT disk with the protective MBR the standard requires.**
- **ESTABLISHED — THE ADDRESSING**: with the sector variable carrying `1` and no shift, the read lands
  on the sector the previous decode named — `_rd_lba = _rd_arg = 1`, `_rd_shift = 0`,
  `_rd_blockaddr = 1`. Rungs 43–49's byte address is confirmed as the reason they found no GPT header.
- **THE FRONTIER**: the **GPT partition entry array**. The GPT header's own `PartitionEntryLBA` field
  (at header byte offset 72) names the sector the partition entries live in, and those 128-byte entries
  carry each partition's first and last LBA — the numbers a filesystem mount eventually needs. Reading
  the GPT header's entry-array address and then the entries themselves is the next arm.
- **THE GOAL IS NOT MET** — no partition entry read yet, no filesystem, no mount; TWRP-to-storage stays
  withheld.

**NO FIRER IS ARMED AND NO PRESS IS OWED.** The frontier is **the GPT partition entry array**, and the
next arm is not yet built.