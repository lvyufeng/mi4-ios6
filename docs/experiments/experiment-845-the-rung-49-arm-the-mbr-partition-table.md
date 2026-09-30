# 845 — the rung-49 arm: the MBR's partition table, decoded out of the sector the read already moved

**A BUILD AND A PARK, AND NOTHING ELSE.** Arm `armed-storage-<newhash>`, `STAGE90_XNU_STORAGE_PROBE=48`
(**VALUE 48 = ORDINAL RUNG 49**), entry bin `<newhash>…`, entry elf, payload `stage90-qcdt.img`; readiness
5 of 5, park 11 of 11; the entry-group page move did **not** fire (`STAGE90_XNU_SEAM_LR` stays
`0x8004c2dc`). **PARKED, NOT PRESSED, NOT ARMED.**

---

## 1. It is the rung-48 press's own frontier, and it touches the device not at all

The rung-48 press (844) **measured the medium's layout**: `_rd_mbr = 1` with `_rd_sig = 0xAA55` — the
sector at LBA 0 ends with the MBR signature, so **THE MEDIUM IS MBR-PARTITIONED**. The partition table is
the **four 16-byte records at bytes 446–509 of that same sector**, and those bytes are **already in the
image's `st_read_block`** — the read rung 48 completed left them there. **So rung 49 sends no command, opens
no enable window, moves no byte off the medium and writes no register**: it decodes a buffer.

**THE ANIMAL IS A READER, AND ITS WHOLE SAFETY CLAIM IS A NEGATIVE.** Rung 48's act was a command on the
CMD line; rung 49's is a loop over 512 bytes of RAM. A claim of the form *"this body does nothing to the
device"* is only as strong as the check that refuses its violation — so the build clause makes it
**structural**: every `bl` in `st_mbr_parse`'s linked body must be to `entry_live_write`, the cell-publisher,
and a body containing **any other callee at all** is refused by name count. If a later edit let the parse
issue a command, no cell would change and no run would fail — the arm would quietly be rung 48 with a second
sector read, and the partition numbers would be decoded from a sector fetched by a step no document names.

## 2. The record is not word-aligned, and that is the one way this goes wrong silently

The MBR standard puts each partition record at **byte 446 + 16k**, and `446 = 4*111 + 2` — **the record
does not start on a word boundary.** Every field the standard names therefore sits at a byte offset
`(2 + 16k) mod 4 = 2` *within* a word:

| field | byte | word | extraction |
| --- | --- | --- | --- |
| boot/status flag | `446+16k` | `111+4k` | `(w >> 16) & 0xFF` — the **high** half |
| type code | `450+16k` | `112+4k` | `(w >> 16) & 0xFF` — the **high** half |
| first LBA (LE) | `452+16k` | `113+4k` | `w` — whole, **no swizzle** |
| length in LBA (LE) | `456+16k` | `114+4k` | `w` — whole, **no swizzle** |

The two 32-bit fields **are** word-aligned (`452 % 4 = 456 % 4 = 0`) — the standard's own gift — so they
read straight out of the FIFO's little-endian words. **A parser that took the type from a word's low byte,
or that byte-reversed the LBA word, would read a plausible but WRONG number and every cell would still read
as success** — the `one value, two definitions` class in its storage form.

## 3. The signature is re-checked, and the call site is after the read

The module **does not trust** rung 48's `_rd_mbr`: it re-checks `(st_read_block[127] >> 16) & 0xFFFF`
against `ST_MBR_SIG = 0xAA55` itself and publishes `_pm_sig_ok`. A boot where the read was gated or errored
leaves `st_read_block` holding whatever the previous rung left there, and four LBA-looking numbers decoded
from a stale buffer are four numbers with no provenance. `_pm_sig_ok = 0` beside `_rd_complete = 1` says
the walk refused to treat the sector as an MBR.

And the parse is called **once**, **after** `bl <st_read_single_block>` — the build clause asserts both the
count (exactly one) and the order (below the read). A parse above the read would decode the buffer's
previous contents and publish a table one boot stale, with no cell changing.

## 4. Read the rows

- `_pm_sig_ok = 1` — **THE SECTOR IS AN MBR AND THE FOUR CELLS BELOW ARE A TABLE.**
- `_pm_nonzero = 1` with `_pm_index`, `_pm_nonzero_type`, `_pm_nonzero_first`, `_pm_nonzero_len` — the
  **first** record whose type byte is not zero: the first real partition, which of the four it was, its type
  code, and **its first LBA — the sector the next rung reads.**
- `_pm_p0_*` — entry 0's record whatever it is (an all-zero entry 0 with a non-zero entry 1 is a protective
  or aligned table).
- `_pm_sig_ok = 0` — **THE SECTOR IS NOT AN MBR** (stale/gated read); the four records are not a table.
- `_pm_nonzero` **absent** with `_pm_done = 1` — **A TABLE WITH NO NON-ZERO TYPE** (a protective-only MBR,
  a GPT disk's single `0xEE` entry, or an empty one), which moves the frontier back to the medium's layout.

## 5. Safety

**It touches no device register and moves no byte of the medium** — the medium is not touched at all. The
card stays in TRAN with its block length and its CSD/EXT_CSD exactly as rungs 42–48 left them. `fastboot
boot` only; nothing flashed; nothing written to storage.

**NO PRESS HAS BEEN MADE AND NONE IS OWED: this arm is PARKED and NOT ARMED-FOR-PRESS, and the park is a
RECORD and not a queue. THE PRESS IS THE OPERATOR'S. THE GOAL IS NOT MET** — no partition's sector read, no
filesystem, no mount; TWRP-to-storage stays withheld.