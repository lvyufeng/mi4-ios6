# 854 — the rung-53 press: **the sector-2 read is clean, but the header decode was moved onto the wrong buffer**

**A PRESS**, made under the standing instruction. One gate exit 0, one runner exit 0; `fastboot boot`
only, nothing flashed, nothing written to storage; `33e80afe` absent from **both** device lists
hand-checked immediately before the gate; **the phone returned**. Arm **`armed-storage-e8597e3e`**
(`STAGE90_XNU_STORAGE_PROBE=52`, rung 53) is now **SPENT**. Capture
`out/stage90/captures/rung53-gptentries-20260930-125548-last_kmsg.txt` 654,954 B `2edc5584…`.

---

## 1. The read is correct; the decode is not

The CMD17 itself did exactly what rung 53 was built to do:

| cell | value | meaning |
| --- | --- | --- |
| **`_rd_lba = _rd_arg`** | **`2`** | the sector variable moved 1 → 2 and the command sent it |
| `_rd_arg_lba_agree` | `1` | the argument is the sector variable |
| `_rd_complete` / `_rd_err` / `_rd_state` | `1` / `0` / `4` | a clean 512-byte read out of **TRANSFER** |
| `_rd_words_gated` | `0x80` | 128 words = a whole sector |
| `_rd_shift` / `_rd_blockaddr` | `0` / `1` | sector-addressed, argument unshifted |

And the buffer rung 53 read is **not** a GPT header — which is the correct reading of sector 2:
`_ge_hdr_ok = 0` (the module re-tested `EFI PART` and it is absent), with `_rd_w0 = 0xdea0ba2c` and
`_rd_w1 = 0x4805cbdd` — the first partition's type GUID bytes, **not** `0x20494645`/`0x54524150` as
rung 52's sector-1 read was.

## 2. The defect: the header decode ran on the entry array

**`_gp_sig_ok = 0`**, `_gp_w0 = 0xdea0ba2c`, `_gp_w1 = 0x4805cbdd`, `_gp_my_lba = 0x4086f97b`,
`_gp_nentries = 0`, `_gp_entry_size = 0`, `_gp_entry_lba = 0`.

That is **rung 52's header decode reading sector 2's bytes as if they were a header.** The call order
in `st_cmd_path` is `st_read_single_block` → `st_mbr_parse` → **`st_gpt_parse`** → `st_gpt_entry_parse`,
and all three parses read the same `st_read_block`. In rung 52 the read's sector was **1**, so
`st_gpt_parse` decoded the header and filled the carries correctly. **Rung 53 moved the read's sector to
2, so `st_gpt_parse` now decodes the entry array** — and the header fields it reads out of that buffer
(`st_read_block[18]`, `[20]`, `[21]`) happen to be **zero**.

So the arm's pre-registration — *"it consumes `st_gp_entry_lba`/`st_gp_nentries`/`st_gp_entry_size`, the
header's own shape, **carried while the buffer still held the header**"* — was **false**, and
falsifiably so: **in rung 53 the buffer never held the header**, because the one CMD17 that runs lands
sector 2. The sentence assumed a carry the arm's own sector move destroyed
([[mi4-a-lower-rungs-side-effect-poisoned-the-rung-above]], the input-side form: not a durable device
side effect but a **buffer** the step above overwrote).

## 3. What the entry decode got, and why every cell is consistent

The carries came out zero, so:

| cell | value | why |
| --- | --- | --- |
| **`_ge_nentries_decl`** | **`0`** | `st_gpt_nentries` was filled from sector 2's word 20 = 0 |
| **`_ge_stride_words`** | **`0`** | `st_gpt_entry_size` = sector 2's word 21 = 0, `/ 4` = 0 |
| **`_ge_arr_sector`** | **`0`** | `st_gpt_entry_lba` = sector 2's word 18 = 0 |
| `_ge_entries_in_sector` | `0x80` = 128 | the guard `stride ? stride : 1` fell through to **1**, so `128 / 1` |
| **`_ge_nentries_used`** | **`0`** | `min(0, 128)` = 0, so the loop **never ran** |
| `_ge_ok` / `_ge_found` / `_ge_blank` | `0` / `0` / `1` | the aggregate guard refused; no entry was walked |
| **`_ge_arg_is_arr`** | **`0`** | `_rd_lba = 2` vs `_ge_arr_sector = 0` — the two derivations disagree |

**Every one of these is consistent with the defect and none is a device failure**: the entry decode
consumed a shape that the header decode had just zeroed. `_ge_arg_is_arr = 0` is the cell that names the
tension the rung was built to make read 1.

## 4. What is established, and what the run does not touch

- **ESTABLISHED — THE SECTOR MOVE WORKS**: `_rd_lba = _rd_arg = 2` with a clean transfer, so the read
  addresses sector 2 exactly as designed. **The mechanism of rung 53 is sound.**
- **ESTABLISHED — SECTOR 2 IS NOT A HEADER**: `_ge_hdr_ok = 0`, `_rd_w0 = 0xdea0ba2c` — the read landed a
  sector whose first bytes are a partition entry's type GUID, the correct shape for the entry array.
- **FALSIFIED — THE CARRY SURVIVES THE SECTOR MOVE**: the header decode's carries are zeroed because the
  read overwrote the buffer the header decode reads. **The frontier is not a new device act; it is the
  ORDER** — the header's shape must be captured from the header sector, and rung 53 reads only one sector.
- **NO BYTE OF THE MEDIUM MOVED AND NO DEVICE REGISTER WAS WRITTEN BEYOND RUNG 51's**: CMD17 reads, the
  card is in TRAN (`_rd_state = 4`, `_rd_gate_state = 4`), no panic (`runner: no 'panic … sleh_abort'`).
- **THE SEAM MOVED AND WAS CONFIRMED ON HARDWARE**: `seam_lr = 0x8004d2dc` — the moved constant, read out
  of a real log, so the page-move pin caught the move before the press and the press confirms it.
- Standing readings unchanged: `xnu_entry_failures = 0`, `abort_entries = 0`; the goal's own floor is met
  as in 504/520/533 (pid 1's syscalls ran, the fixture's `0xfeedface` came back into a user page).

## 5. The frontier

**The next arm must make the header's shape available to the entry decode without letting the read
overwrite it.** The natural step is **two reads in one rung**: read sector 1, capture the header's shape
(the carries), then read sector 2 and decode the entry array — the same one-number discipline, applied
to a second CMD17. And the build clause the correction needs is the one that would have refused *this*
build: **bind `st_gpt_parse`'s expected sector to the read's sector**, so a body whose header decode runs
on a buffer the read did not fill with sector 1 is refused rather than silently zeroing the carries.

**NO FIRER IS ARMED AND NO PRESS IS OWED.** **THE GOAL IS NOT MET** — the partition entry itself is still
not decoded, no filesystem, no mount; TWRP-to-storage stays withheld.