# 844 — the rung-48 press: **THE MEDIUM IS MBR-PARTITIONED** — LBA 0 holds `0xAA55`

**A PRESS**, made under the standing instruction. One gate exit 0, one runner exit 0; `fastboot boot`
only, nothing flashed, nothing written to storage; `33e80afe` absent from **both** device lists
hand-checked immediately before the gate; **the phone returned** (`/tmp/cancro-last_kmsg.txt` 661,493 B,
82 s after `fastboot boot`). Arm **`armed-storage-8f0d41fd`** (`STAGE90_XNU_STORAGE_PROBE=47`, rung 48) is
now **SPENT**. Capture `out/stage90/captures/rung48-mbr-20260930-101755-last_kmsg.txt` 661,493 B
`f06b01dc…`.

---

## 1. The constant moved and the transfer is clean

Rung 48 changed **one number** — `ST_MMC_READ_LBA` 1 → 0 — and it is measured as the difference in the
read's own cells:

| cell | rung-47 press (842) | **rung-48 press (844)** |
| --- | --- | --- |
| `_rd_lba` | `1` | **`0`** |
| `_rd_arg` | `0x200` | **`0`** (`0 << 9`) |
| `_rd_arg_wrote` | `0x200` | **`0`** |
| `_rd_sent` | `1` | `1` |
| `_rd_complete` | `1` | `1` |
| `_rd_err` | `0` | `0` |
| `_rd_state` | `4` (TRAN) | `4` (TRAN) |
| `_rd_words_gated` | `0x80` | `0x80` (the whole 512 bytes) |
| `_rd_word_read` | `0x113A` | `0x113A` (CMD17 with the DATA bit) |

The command, the flags, the reset-before-read order and the whole cell set are rung 47's byte for byte;
**only the sector moved**. The transfer is clean end to end, exactly as at LBA 1: `_rd_complete = 1`,
`_rd_err = 0`, response `0x900` (READY_FOR_DATA | TRAN), 128 words moved, `_rd_data_wait_timeout = 0`,
`_rd_int_data_err = 0`.

## 2. **The medium answered with the MBR signature**

The whole point of reading LBA 0 is the new derived cell, and it is **1**:

```
_rd_sig   = 0xAA55          _rd_mbr = 1        _rd_w127 = 0xaa550000
_rd_w0    = 0x0   _rd_w1 = 0x0   _rd_w2 = 0x0   _rd_w3 = 0x0
_rd_gpt   = 0
```

`_rd_w127 = 0xaa550000` is the sector's own last word: its **high half is `0xAA55`** — bytes 510 and 511
hold `0x55` and `0xAA`, the classic MBR signature. So **THE MEDIUM IS MBR-PARTITIONED**, and rung 47's
*"LBA 1 is not a GPT header"* is now the **positive** statement it was reaching for: this card is not
GPT, it is MBR. `_rd_w0.._rd_w3 = 0` says LBA 0 begins with zeroed boot code (a protective MBR or a
minimal one), and `_rd_gpt = 0` — LBA 0's first two words are not `EFI PART` — is the expected reading,
since an MBR's boot code is not a GPT header.

## 3. What this opens and what it does not

- **THE MEDIUM'S OWN LAYOUT IS MEASURED.** After rung 47 said *what the medium is not*, rung 48 says
  *what it is*: MBR-partitioned. The partition table is the 4-entry array at bytes 446–509 of this same
  sector — each entry a 16-byte record whose first byte is the boot flag and whose bytes 8–11 / 12–15 are
  the LBA start / length. Reading LBA 0 again is not needed: **the bytes are already in the image's
  `st_read_block`**, and the next rung is to parse the partition entries out of it (or to read the first
  partition's own sector).
- **What it does not say:** no partition entry has been decoded, no filesystem, no mount. `_rd_w0.._rd_w3`
  being zero is the boot code, not the partition table (which sits at byte 446 = word 111).
- **THE GOAL IS NOT MET** — no filesystem, no mount; TWRP-to-storage stays withheld.

## 4. The standing seam reading, unchanged

`seam_lr = 0x8004c2dc` **did not move**, so the entry-group page move did not fire: `slot_cwe_win` /
`seam_sctlr = 0x30c57879` with `SCTLR.C` clear, `slot_post_calls = 0x8`, `b1 = 0x80482b04` (the real
exit's return address in this image). The one `FAIL` — `seam_sp` not `sleh_sp-8` — is the **standing
reading on every recent arm**, not a regression: rung 48 changed nothing in the seam path.
`xnu_entry_abort_entries = 0`, `xnu_entry_failures = 0`, `xnu_entry_checks = 5`. The goal's own floor is
met as in 504/520/533 (pid 1's syscalls: open/read/getpid/exit/wait, the fixture's `0xfeedface` read
back) — the floor, not this run's progress.

**NO FIRER IS ARMED AND NO PRESS IS OWED.** The frontier is the MBR's own partition table, and the next
arm is not yet built.