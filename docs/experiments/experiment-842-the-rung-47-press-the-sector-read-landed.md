# 842 — the rung-47 press: **THE SECTOR READ LANDED** — the data path is open

**A PRESS**, made under the standing instruction. One gate exit 0 (**624 lines**), one runner exit 0;
`fastboot boot` only, nothing flashed, nothing written to storage; `33e80afe` absent from **both** device
lists hand-checked immediately before the gate; **the phone returned** (`/tmp/cancro-last_kmsg.txt`
653,974 B, 26 s after `fastboot boot`). Arm **`armed-storage-6deb8308`** (`STAGE90_XNU_STORAGE_PROBE=46`,
rung 47) is now **SPENT**. Capture `out/stage90/captures/rung47-readmoved-20260930-094924-last_kmsg.txt`
653,974 B `35d9a979…`.

---

## 1. The move worked: CMD17 got past the guard and the medium answered

Rung 46's press (840) left the reset proving itself but too late. Moving the *same* `st_data_reset` call
above `st_read_single_block` was the whole of rung 47, and it is **measured as the difference in the
read's own cells**:

| cell | rung 46 press (840) | **rung 47 press (842)** |
| --- | --- | --- |
| `_rd_inhibit_dat` | `0x2` | **`0x0`** |
| `_rd_inhibit_timeout` | `1` | **`0`** |
| `_rd_inhibit_polls` | `0xa700` (expired) | **`0x1`** (clear on the first test) |
| `_rd_sent` | `0` | **`1`** |
| `_rd_complete` | `0` | **`1`** |
| `_rd_err` | `0` | **`0`** |
| `_rd_state` | `0` | **`4` (TRAN)** |
| `_rd_words_gated` | `0` | **`0x80` (the whole 512 bytes)** |
| `_rd_data_wait_timeout` | `1` | **`0`** |
| `_rd_int_data_err` | `0` | **`0`** |

**The negative cell did not fire.** `_rd_inhibit_dat = 0` is the arm's own exact falsifier — the one that
would have said the inhibit re-asserted between the reset and the read. It did not.

## 2. The medium reads as a random block, and that is not a failure

`_rd_word_read = 0x113A` — the block's own COMMAND register held the five-bit `0x111A` opcode 17 word plus
the DATA bit, i.e. CMD17 as written. `_rd_arg = 0x200` (LBA 1 << 9), `_rd_lba = 1`. All 128 words arrived
and the sector is **not** a GPT header:

```
_rd_w0   = 0xe8f9decb      _rd_w1   = 0xdee55679
_rd_w2   = 0x4147cf37      _rd_w3   = 0x17b14f89      _rd_w127 = 0xcb686a68
_rd_gpt  = 0
```

`_rd_gpt = 0` is the arm's own **A SECTOR READ THAT IS NOT A GPT**, and the words are the shape of
encrypted or otherwise non-header content — **which is a reading about the medium, not about the
transfer**. The transfer itself is clean end to end: command sent, response `0x900` (READY_FOR_DATA |
CURRENT_STATE = TRAN), 128 words moved, no data error, no timeout.

## 3. The reset at its new position

`_dr_was = 0`, `_dr_wrote = 0x04`, `_dr_held_after = 0` (self-cleared, `_dr_polls = 0x101`,
`_dr_ticks = 0x831` = 109.4 µs, `_dr_timeout = 0`), `_dr_dat_line = 0`, `_dr_data_inhibit = 0`,
`_dr_ps_after = 0x01f80000` against `_dr_ps_before = 0x01f80206` — **identical to 840's answer**, so the
rung-46 verdict (THE HOST WAS THE HOLDER) reproduces at the position that makes it load-bearing.

## 4. What this opens and what it does not

- **THE DATA PATH IS OPEN.** The whole command chain from CMD0 through CMD17 now completes on hardware:
  IDENT → STBY → TRAN → an EXT_CSD read → SET_BLOCKLEN → **a 512-byte sector read**.
- **What it does not say:** no partition table was found at LBA 1, no filesystem, no mount. Reading LBA 1
  was the image's self-verifying choice, not a claim the boot sector is a GPT; the answer is that on this
  card it is not. The next frontier is the medium's own layout — an MBR scan, other LBAs, or the EXT_CSD's
  sector count and partition attributes (rung 38 already carried `_ext_sec_count`).
- **THE GOAL IS NOT MET** — no filesystem, no mount; TWRP-to-storage stays withheld.

## 5. The standing seam reading, unchanged

The runner's seam criteria read as on every recent arm (`seam_lr=0x8004c2dc` **did not move**, so the
entry-group page move did not fire): `slot_cwe_win`/`seam_sctlr = 0x30c57879` with `SCTLR.C` clear,
`slot_post_calls = 0x8`, the line pair CLEAN. The one `FAIL` — `seam_sp` not `sleh_sp-8` — is the
**standing reading on every recent arm**, not a regression: rung 47 changed nothing in the seam path and
the arm's clause is about the storage chain. `xnu_entry_abort_entries = 0`, `xnu_entry_failures = 0`,
`xnu_entry_checks = 5`.

**NO FIRER IS ARMED AND NO PRESS IS OWED.** The frontier is the medium's layout, and the next arm is not
yet built.