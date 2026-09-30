# 848 — the rung-50 press: the MBR table decoded right, and the sector the read addresses

**A PRESS**, made under the standing instruction. One gate exit 0, one runner exit 0; `fastboot boot`
only, nothing flashed, nothing written to storage; `33e80afe` absent from **both** device lists
hand-checked immediately before the gate; **the phone returned** (`/tmp/cancro-last_kmsg.txt` 654,864 B,
runner exit 0). Arm **`armed-storage-28c82f79`** (`STAGE90_XNU_STORAGE_PROBE=49`, rung 50) is now
**SPENT**. Capture `out/stage90/captures/rung50-mbrstraddle-20260930-114357-last_kmsg.txt` 654,864 B
`b2ef6258…`.

---

## 1. The two owed readings, both delivered

The rung-49 press (846) published two cells that were not the fields they name. This press publishes
the **corrected** decode:

| cell | rung 49 (846, WRONG) | rung 50 (848, RIGHT) |
| --- | --- | --- |
| `_pm_p0_first` = `_pm_nonzero_first` | `0x0001FFFF` | **`1`** |
| `_pm_p0_len` = `_pm_nonzero_len` | `0xFFFF0000` | **`0xFFFFFFFF`** |

So the MBR's entry 0 is, unambiguously, **an EFI protective partition pointing at LBA 1** — status not
bootable, type `_pm_p0_type = 0xEE`, first LBA `1`, length `0xFFFFFFFF` sectors. `_pm_sig_ok = 1`,
`_pm_entries = 4`, `_pm_done = 1`, `_pm_index = 0`.

`_pm_next_sector = 1` beside `_pm_arg_sector = 0` is the tension stated in the log: the sector the
table names is **1**, and the sector this rung actually read is **0** — so `_pm_next_is_arg = 0`, and
the next arm sets the read's sector to `_pm_next_sector` (this cell then becomes 1).

## 2. The addressing the arm exists for

The read's own argument is now an explicit **sector**, and this press measures the class:

| cell | value | meaning |
| --- | --- | --- |
| `_ext_sec_count_carried` = `_rd_cap` = `_pm_cap` | `0x01d5a000` | 30,777,344 sectors |
| `_rd_cap_over` | `1` | above `mmc.c:344`'s 4,194,304-sector threshold |
| `_rd_shift` | `0` | `block.c:1777`'s `<< 9` is **NOT** applied |
| `_rd_blockaddr` = `_pm_sector_addressed` | `1` | **the card is SECTOR-addressed** |
| `_rd_lba` = `_rd_arg` | `0` | the CMD17 argument is the sector variable |
| `_rd_arg_lba_agree` | `1` | the argument is `st_read_lba` |
| `_pm_next_in_range` | `1` | sector 1 is inside the card's capacity |

**This is the rung-50 finding, measured**: rungs 43–49 sent byte addresses (`LBA << 9`) where the card
wanted sectors. `_rd_arg = 0x200` at value 46 was **sector 512**, not LBA 1 — which is why rung 47's
read of "LBA 1" found no `EFI PART`. The `0` of rungs 47–49 was accidentally correct.

The CSD cross-check cell `_rd_cap_csd_valid = 0` is the **honest negative**: the CSD carried from rung
34 is not in a form this arm could decode (its `CSD_STRUCTURE` is not v1.0/within this arm's reader), so
the **second independent producer did not answer** and `_rd_cap_agree` is **absent** rather than
asserted. The EXT_CSD count is the only producer that answered — which is exactly the single-producer
situation `mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one` warns about, published as such.

## 3. Read the counter-cells

The read is the **protective MBR again** (sector 0), so its own cells are unchanged and confirm the act
did not move: `_rd_complete = 1`, `_rd_mbr = 1`, `_rd_sig = 0xAA55`, `_rd_state = 4` (TRAN),
`_rd_w0 = 0`. The card is still in TRANSFER state across the press, exactly as rungs 42–49 left it.

## 4. The standing readings, unchanged

`seam_lr = 0x8004c2dc` **did not move**; `xnu_live_seam_sctlr = 0x30c57879`; `abort_entries = 0`; the
goal's own floor is met as in 504/520/533 (pid 1's syscalls, the fixture's `0xfeedface` read back). A
panic is present (`xnu_entry_panic_entered = 1`, the same forced-ending shape as before,
`fault_addr = 0xfa0065c`) — that is the epilogue's own store faulting in this context, not this rung's
doing.

## 5. What this press did and did not establish

- **ESTABLISHED**: the MBR's entry 0 is an EFI protective partition at **LBA 1** (corrected decode), and
  **this card is SECTOR-addressed** (`_rd_cap_over = 1`, `_rd_shift = 0`, `_rd_blockaddr = 1`) — so the
  read argument must be a sector, not a byte address.
- **NOT ESTABLISHED — the next arm's work**: the **partition's own sector**. This arm reads sector 0
  (the MBR), not sector 1. `_pm_next_is_arg = 0` says so. Reading sector 1 (or 2) — with the *corrected*
  sector addressing — is what settles whether the protective MBR's target is a real GPT header.
- **THE GOAL IS NOT MET** — no partition's own sector read, no filesystem, no mount; TWRP-to-storage
  stays withheld.

**NO FIRER IS ARMED AND NO PRESS IS OWED.** The frontier is **the first partition's own sector read
(LBA 1 or 2), now with the sector addressing the rung-50 arm proved in force**, and the next arm is not
yet built.