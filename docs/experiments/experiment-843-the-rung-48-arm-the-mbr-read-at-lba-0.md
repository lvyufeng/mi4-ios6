# 843 — the rung-48 arm: the PROTECTIVE MBR, read at LBA 0

**A BUILD AND A PARK, AND NOTHING ELSE.** Arm `armed-storage-8f0d41fd`, `STAGE90_XNU_STORAGE_PROBE=47`
(**VALUE 47 = ORDINAL RUNG 48**), entry bin `8f0d41fd…` 5,569,148 B (**the same size as the rung-47 arm's**
— one constant changed), entry elf `5c12f5e3…` 6,749,440 B (the same size — no function added), payload
`stage90-qcdt.img` 8,589,312 B; readiness 5 of 5, park 11 of 11; the entry-group page move did **not**
fire (`STAGE90_XNU_SEAM_LR` stays `0x8004c2dc`). **PARKED, NOT PRESSED, NOT ARMED.**

---

## 1. It is the rung-47 press's own frontier, and it changes ONE number

The rung-47 press (842) **opened the data path**: the whole chain CMD0…CMD17 completes on hardware —
`_dr_dat_line = 0` and `_dr_data_inhibit = 0` entering the read (the reset released the line), then
`_rd_sent = 1`, `_rd_complete = 1`, `_rd_err = 0`, `_rd_state = 4` (TRAN), `_rd_resp = 0x900`, the full 512
bytes moved (`_rd_words_gated = 0x80`, `_rd_data_wait_timeout = 0`, `_rd_int_data_err = 0`). **And the
medium answered with a sector that is neither `EFI PART` nor zero**: `_rd_gpt = 0`, `_rd_w0 = 0xe8f9decb`,
`_rd_w1 = 0xdee55679`, `_rd_w127 = 0xcb686a68`. So reading LBA 1 was the image's *self-verifying guess*,
and on this card it is not a GPT header. **The frontier is the medium's own layout.**

**Rung 48 is `ST_MMC_READ_LBA` 1 → 0 and nothing else.** The command, the flags, the block length CMD16
set, the reset-before-read order and the whole cell set are rung 47's byte for byte; **only the sector
moves**, so the byte address becomes `0 << 9 = 0`. A GPT disk keeps a **PROTECTIVE MBR** at LBA 0, and an
MBR's own signature is `0x55 0xAA` at bytes 510–511 — so LBA 0 turns "not a GPT" into a measurement.

## 2. The build clauses are the rung, and the FIRST clause draft was refused by its own build

The ladder's value-awareness is where the arm is enforced, in three places:

- **`xnu_entry_832`'s argument clause, split by value.** At values ≤ 46 the `st_read_single_block` body must
  load **512** into the argument register (LBA 1 << 9); **at value 47 it must load `0`** (LBA 0 << 9). The
  clause's own prose already explains why: a body that forgot the shift would produce a complete,
  error-free transfer of the **wrong** sector whose every cell read as success. Measured on this build:
  `r1 = #0`.
- **`xnu_entry_843` — the MBR signature, asserted as the half GCC emits.** The new derived cell `_rd_mbr`
  is `((w[127] >> 16) & 0xFFFF) == 0xAA55`, so the clause requires the immediate `#43605` (`0xAA55`, a
  `movw`) to be present and paired with the one `cmp r?, r?, lsr #16` (its high-half comparison), and
  requires **exactly one** such shift in the body. Measured: `movw r3, #43605` at the comparison.
- **The `xnu_entry_832` narration and the published-key list** now name `_rd_mbr`/`_rd_sig` at value 47.

**The first draft of `xnu_entry_843` looked for the literal `#127` and was REFUSED BY ITS OWN BUILD.**
An array index of 127 on a `uint32_t[128]` is not a `movw #127` — GCC folds it into a frame-relative
`ldr [r?, #1056]` (and its `+0`/`+1` siblings), so the index never appears as a literal. **The repair was
to assert the SHIFT** — the operation the source's `>> 16` must produce — **and the pairing** between the
`#43605` and that `cmp`, **never the index or its frame offset**: the offset is a property of the whole
function's stack layout and would move with any unrelated local, which would be the *one address pinned in
two files* defect stated a third time.

## 3. Read the rows

- `_rd_sent = 1` with `_rd_complete = 1`, `_rd_err = 0`, `_rd_state = 4` and `_rd_words_gated = 0x80` —
  **THE MBR READ COMPLETED**: the same clean transfer rung 47 measured, now at sector 0.
- **`_rd_mbr = 1` with `_rd_sig = 0xAA55` — THE MEDIUM IS MBR-PARTITIONED** (bytes 510–511 hold the
  signature: the boot sector this image can now walk).
- `_rd_mbr = 0` with `_rd_sig` anything else — **A SECTOR THAT IS NEITHER AN MBR NOR A GPT HEADER**, which
  would say the medium's layout is something this ladder has not yet named and the frontier moves again.
- `_rd_gpt` is still published and now reads LBA 0's first two words (expected 0 — an MBR's boot code is
  not `EFI PART`), so one log carries **both** layouts' signatures from the same 512 bytes.
- `_rd_err` carrying `SDHCI_INT_TIMEOUT` — **THE CARD NOT ANSWERING**; `_rd_int_data_err` nonzero — **THE
  TRANSFER failing**; `_rd_data_wait_timeout = 1` with `_rd_words_gated = 0` — **THE CARD SILENT**.
- **The negative cell is exact**: `_rd_lba = 1` with `_rd_arg = 0x200` would be **rung 46's arm wearing
  rung 48's name** — a clean, error-free read of the wrong sector whose `_rd_mbr` was a signature from LBA
  1's bytes 510–511. That is exactly what `xnu_entry_832`'s value-47 half refuses.

## 4. Safety

CMD17 reads, so the card stays in TRAN with its block length and its CSD/EXT_CSD exactly as rungs 42–47
left them. `MMC_DATA_READ` moves bytes card-to-host. `fastboot boot` only; nothing flashed; nothing written
to storage.

**NO PRESS HAS BEEN MADE AND NONE IS OWED: this arm is PARKED and NOT ARMED-FOR-PRESS, and the park is a
RECORD and not a queue. THE PRESS IS THE OPERATOR'S. THE GOAL IS NOT MET** — no partition table walked, no
filesystem, no mount; TWRP-to-storage stays withheld.