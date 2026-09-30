# 851 — the rung-52 arm: the GPT header, decoded out of the sector the sector-1 read already landed

**A BUILD AND A PARK.** Arm `armed-storage-2dbf8098`, `STAGE90_XNU_STORAGE_PROBE=51`
(**VALUE 51 = ORDINAL RUNG 52**), entry bin `2dbf8098…` 5,585,532 B, entry elf `36bfac0a…` 6,765,976 B,
payload `stage90-qcdt.img` `82e69b3a…` 8,605,696 B, `stage90.img` `3ec0025c…` 6,084,608 B; readiness 5 of 5,
park 11 of 11; the entry-group page move did **not** fire (`STAGE90_XNU_SEAM_LR` stays `0x8004c2dc`).
**PARKED, NOT PRESSED, NOT ARMED.**

---

## 1. It is the rung-51 press's own frontier, and it touches the medium not at all

The rung-51 press (850) measured `_rd_w0 = 0x20494645` ( **`"EFI "`** ) and `_rd_w1 = 0x54524150`
( **`"PART"`** ) with `_rd_gpt = 1` — **LBA 1 IS A GPT HEADER** — and the 512 bytes it read are **already in
`st_read_block`**. So rung 52 sends **no command, opens no window and touches no register**: it DECODES that
buffer, exactly as rung 49 decoded the MBR out of sector 0.

`st_gpt_parse`, called **once** by `st_cmd_path` after `bl <st_read_single_block>`, is the whole body. The
build clause makes its inertness **structural** rather than a sentence: **every `bl` in its linked body
resolves to `entry_live_write`** (the cell publisher) and **any other callee at all is refused by name
count**, so a later edit that let it issue a command would be a build refusal and not a silent extra sector
read. The body names the controller window **zero** times.

## 2. The one field the frontier names

**`PartitionEntryLBA`** — the GPT header's byte offset 72 = **word 18** (`72 / 4`), the **SECTOR the 128-byte
partition entry array lives in**. That number is the **next arm's CMD17 argument**, so **rung 52 has the
shape of 50 → 51: only the sector moves again.**

All the header's fields are 4-byte aligned, so — unlike the MBR — **there is no straddle here**, and the
decode is a plain word read at a fixed index:

| field | byte off | word | what it is |
| --- | --- | --- | --- |
| `Signature` | 0 | 0,1 | `EFI ` / `PART` (re-checked, §3) |
| `MyLBA` | 24 | 6 | the header's **own** self-check against the sector it was read from |
| `FirstUsableLBA` | 40 | 10 | lower bound of the region partitions may live in |
| `LastUsableLBA` | 48 | 12 | upper bound |
| **`PartitionEntryLBA`** | **72** | **18** | **THE FRONTIER** — the entry array's sector |
| `NumberOfPartitionEntries` | 80 | 20 | the array's own shape |
| `SizeOfPartitionEntry` | 84 | 21 | normally 128 |

## 3. The claims that are build refusals, not sentences

New clause **`xnu_entry_851`** reads `st_gpt_parse`'s **linked** disassembly and refuses unless:

- **every `bl` in its body is to `entry_live_write`** — 19 of 19 — so the body makes no device access, and
  **no `0xf9824xxx` controller-window reference appears at all** (the count is required to be 0);
- **all four halves of the two signature words** are materialized — `0x20494645`, `0x4649`/`0x2049`
  and `0x54524150`'s halves — i.e. the signature is **re-checked in this body** and not inherited from
  rung 51's derived cell;
- it is **called exactly once** by `st_cmd_path` and that call site is **after** the `bl
  <st_read_single_block>` (a parse above the read would publish a header one boot stale with no cell
  changing).

So *"a body that decodes the header and cannot touch the device"* is a build refusal, holding the call
order and the inertness to each other.

## 4. Read the rows

- **`_gp_sig_ok = 1`** — **THE SECTOR IS A GPT HEADER** and every number below is a header field. `_gp_sig_ok
  = 0` beside `_rd_complete = 1` is a sector the walk **refused to treat as a GPT header** — a stale or
  gated read left the buffer holding something else — and the numbers are then **not** a header.
- **`_gp_my_lba`** (word 6) is the header's **own self-check** against the sector this rung read (sector 1):
  **`_gp_self_ok = 1`** is **the header agrees with the address the read used** — a **second producer**
  agreeing with the decode, from a field the **header** carries rather than one the MBR carries.
  `_gp_self_ok = 0` with `_gp_sig_ok = 1` is a header whose own address word disagrees with where it was
  found — a medium whose layout is not what the protective MBR claimed.
- **`_gp_entry_lba` = `_gp_next_sector`** — **the sector the next arm reads**. `_gp_arg_sector` = `_rd_lba`
  = 1 beside it, with **`_gp_next_is_arg = 0`**, states the same tension rung 50 stated in reverse: the
  entry array is normally at LBA 2, so the table's number and this read's number **disagree on purpose**.
  The next arm sets the read's sector to `_gp_next_sector` and **this cell becomes 1**.
- **`_gp_first_usable` / `_gp_last_usable`** (words 10 / 12) bound the region partitions may live in, and
  both must be inside the card (`_gp_first_in_range`, `_gp_last_in_range`).
- **`_gp_nentries`** (word 20) and **`_gp_entry_size`** (word 21, normally 128) are **the array's own
  shape** — **a reader who walked the array without them would walk a fixed four (as the MBR has) and be
  right by luck on this disk and wrong on the next.**
- **`_gp_cap = 0x01d5a000`** and **`_gp_sector_addressed = 1`** — the addressing class re-measured.

## 5. Safety

**It moves no byte of the medium and touches no device register.** The medium is **not touched at all** —
this rung is a decode of bytes the previous rung's read already landed. The entry image grew (5,569,148 →
**5,585,532** bytes) because the new body is compiled in; the entry-group **page move did not fire**
(`STAGE90_XNU_SEAM_LR` stays `0x8004c2dc`). `STAGE90_XNU_PWR_WAIT_TICKS=384000` is inherited from rung 9
unchanged — 20 ms at this device's own 19,200,000 Hz. `fastboot boot` only; nothing flashed; nothing written
to storage.

**NO PRESS HAS BEEN MADE AND NONE IS OWED: this arm is PARKED and NOT ARMED-FOR-PRESS, and the park is a
RECORD and not a queue. THE PRESS IS THE OPERATOR'S. THE GOAL IS NOT MET** — no partition ENTRY read yet, no
filesystem, no mount; TWRP-to-storage stays withheld.