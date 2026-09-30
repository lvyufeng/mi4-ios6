# 852 — the rung-52 press: **the GPT header decoded — the entry array is at LBA 2**

**A PRESS**, made under the standing instruction. One gate exit 0, one runner exit 0; `fastboot boot`
only, nothing flashed, nothing written to storage; `33e80afe` absent from **both** device lists
hand-checked immediately before the gate; **the phone returned** (`/tmp/cancro-last_kmsg.txt` 655,874 B,
runner exit 0). Arm **`armed-storage-2dbf8098`** (`STAGE90_XNU_STORAGE_PROBE=51`, rung 52) is now
**SPENT**. Capture `out/stage90/captures/rung52-gptheader-20260930-122619-last_kmsg.txt` 655,874 B
`3a2f78e5…`. **Nothing about rung 52 touches the medium, and this run confirms it**: no command was sent,
no register was written; the whole rung is a decode of the sector the rung-51 read already landed.

---

## 1. The header decoded, and the field the frontier names

`_gp_called = 1`, `_gp_sig_ok = 1`, `_gp_w0 = 0x20494645` (**`"EFI "`**), `_gp_w1 = 0x54524150`
(**`"PART"`**) — the module re-checked the signature **itself** rather than trusting rung 51's derived
cell, and it agrees: **THE SECTOR IS A GPT HEADER AND THE NUMBERS BELOW ARE A HEADER.**

**THE ONE FIELD THE FRONTIER NAMES IS `PartitionEntryLBA` — and it reads `2`:**

| cell | value | meaning |
| --- | --- | --- |
| **`_gp_entry_lba` = `_gp_next_sector`** | **`2`** | **the 128-byte partition entry array lives at SECTOR 2 — the next arm's CMD17 argument** |
| `_gp_arg_sector` = `_rd_lba` | `1` | the sector **this** rung read |
| **`_gp_next_is_arg`** | **`0`** | names the tension **on purpose**: the entry array is at LBA 2, this read was sector 1, so the next arm sets the read's sector to `_gp_next_sector` and this cell becomes **1** |

That is **the same shape as 50 → 51**: the only thing that moves next is the sector number.

## 2. The header's own self-check — the second producer

`_gp_my_lba = 1` and **`_gp_self_ok = 1`**: the header's **own** `MyLBA` field (word 6) agrees with the
sector this rung read (sector 1). This is **a second producer agreeing with the decode from a field the
header carries** — not a value the MBR carries and not one this module computed. A GPT header placed at a
sector its own `MyLBA` disagrees with would be a medium whose layout is not what the protective MBR
claimed; it did not happen here.

## 3. The array's own shape, and the region it lives in

- **`_gp_nentries = 0x1c`** = **28** entries, **`_gp_entry_size = 0x80`** = **128** bytes each — the
  array's **own** declared shape (28 × 128 = 3,584 bytes = 7 sectors), read out of the header rather than
  assumed. **A reader who walked the array without them would walk a fixed four (as the MBR has) and be
  right by luck on this disk and wrong on the next.**
- **`_gp_first_usable = 0x22`** = 34 and **`_gp_last_usable = 0x01d59fde`** = 30,777,310, with
  **`_gp_first_in_range = 1`** and **`_gp_last_in_range = 1`**: the region partitions may live in is
  **inside the card**. The bounds are the standard ones — first usable just past the entry array, last
  usable just short of the backup structures at the end of the 30,777,344-sector medium.
- `_gp_cap = 0x01d5a000` and `_gp_sector_addressed = 1` re-measure the addressing class; `_gp_done = 1`.

## 4. The read this decode sits on — unchanged, and clean

Rung 52 sends no command, so the read cells are rung 51's, re-read here: `_rd_lba = _rd_arg = 1`,
`_rd_arg_lba_agree = 1`, `_rd_shift = 0`, `_rd_blockaddr = 1`, `_rd_gpt = 1`, `_rd_complete = 1`,
`_rd_err = 0`, `_rd_state = 4` (**TRAN**), `_rd_words_gated = 0x80` (128 words = a whole sector),
`_rd_w127 = 0`, `_rd_sig = 0`, `_rd_mbr = 0` — a GPT header carries no MBR signature. The card is still
in TRANSFER state.

**The honest negative is unchanged**: `_rd_cap_csd_valid = 0` — the CSD carried from rung 34 is not in a
form the reader decodes, so `_rd_cap_agree` is **absent** and the EXT_CSD count is the single producer.

## 5. The standing readings, unchanged

`xnu_live_seam_lr = 0x8004c2dc` **did not move** (the entry-group page move did not fire);
`xnu_live_seam_sctlr = 0x30c57879`; `xnu_entry_failures = 0x0`; `abort_entries = 0`. The goal's own floor
is met as in 504/520/533: pid 1's syscalls ran, and the fixture's `0xfeedface` came back into a user page
via a character device's `read`. The forced ending is present as before (`slot_post_calls` up to `0x8`),
not this rung's doing.

## 6. What this press established

- **ESTABLISHED — THE GPT HEADER DECODES**: `_gp_sig_ok = 1` with `_gp_w0`/`_gp_w1` = `EFI `/`PART`, and
  the header's own `MyLBA` **agrees** with the sector it was read from (`_gp_self_ok = 1`). The rung-51
  press's `EFI PART` at LBA 1 is confirmed as a **well-formed** GPT header, not four stray bytes.
- **ESTABLISHED — THE FRONTIER'S NUMBER**: the partition **entry array is at SECTOR 2** (`_gp_entry_lba =
  _gp_next_sector = 2`), holding **28 entries of 128 bytes**, inside a usable region `[34, 30,777,310]`.
  **The next arm's CMD17 argument is 2** — the same one-number move 50 → 51 was.
- **THE FRONTIER**: **the GPT partition entry array** at LBA 2. Each 128-byte entry carries a partition's
  **type GUID**, its **first LBA** and its **last LBA** — the numbers a filesystem mount eventually needs.
  Reading sector 2 and decoding the entries is the next arm, and it has the shape of rung 51: the read's
  sector variable moves 1 → 2 and `_gp_next_is_arg` becomes 1.
- **THE GOAL IS NOT MET** — no partition entry read yet, no filesystem, no mount; TWRP-to-storage stays
  withheld.

**NO FIRER IS ARMED AND NO PRESS IS OWED.** The frontier is **the GPT partition entry array at LBA 2**, and
the next arm is not yet built.