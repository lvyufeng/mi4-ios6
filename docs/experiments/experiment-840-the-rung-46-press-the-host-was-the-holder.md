# 840 — the rung-46 press: the reset released the line, and THE HOST WAS THE HOLDER

**A PRESS**, made under the standing instruction. One gate exit 0 (623 lines), one runner exit 0;
`fastboot boot` only, nothing flashed, nothing written to storage; `33e80afe` absent from BOTH device
lists hand-checked immediately before the gate. **The phone returned** (adb up afterwards).

| | |
| --- | --- |
| arm | `armed-storage-cd3d63bf`, `STAGE90_XNU_STORAGE_PROBE=45` (rung 46) — now SPENT |
| capture | `out/stage90/captures/rung46-reset-20260930-092219-last_kmsg.txt` 650,939 B `872a4d01…` |
| result | **THE HOST WAS THE HOLDER. `_dr_dat_line = 0` and `_dr_data_inhibit = 0` — the reset released the data path.** |

---

## 1. The byte self-cleared and the state machine came back

The reset byte self-cleared on the first poll inside the bound, and the two bits that were stuck across
four boot-samples in rungs 44–45 **both cleared**:

| cell | reading | means |
| --- | --- | --- |
| `_dr_was` | `0x00` | `SOFTWARE_RESET` held zero before — the reset value |
| `_dr_wrote` | `0x04` | `SDHCI_RESET_DATA` written |
| **`_dr_held_after`** | **`0x00`** | **THE BYTE SELF-CLEARED** — the block accepted the reset |
| `_dr_polls` | `0x100` (256) | how long the self-clear took |
| `_dr_ticks` | `0x827` (2,087 ticks = 108.7 µs) | well inside the 100 ms bound |
| **`_dr_timeout`** | **`0`** | the byte did not stay set — no refusal |
| `_dr_ps_before` | `0x01f80206` | `PRESENT_STATE` before — `DATA_INHIBIT` AND `DAT_LINE_ACTIVE` set |
| **`_dr_ps_after`** | **`0x01f80000`** | `PRESENT_STATE` after — **both bits CLEAR** |
| **`_dr_dat_line`** | **`0`** | **THE LINE WAS RELEASED** |
| **`_dr_data_inhibit`** | **`0`** | **THE INHIBIT WAS CLEARED** |
| `_dr_status_after` | `0` | `INT_STATUS` clean |
| `_dr_ena_after` | `0x00008000` | `INT_ENABLE` (bit 15 the hardware's own) |
| `_dr_done` | `1` | the body finished |

**`_dr_timeout = 0` with `_dr_dat_line = 0` and `_dr_data_inhibit = 0` is the arm's own row 1: THE HOST
WAS THE HOLDER.** The block's data state machine was stuck on **state it owns** — a host-controller reset
released it, which means the holder was the host and **not** the card, the pad or the pin. `PRESENT_STATE`
moved `0x01f80206 → 0x01f80000` in one store, the first movement in that register since rung 43.

## 2. The command path in the same boot is the control

The chain above this rung behaved exactly as 838 left it — the widened mask waits, CMD17 is withheld —
**because the reset runs AFTER the read**:

| cell | reading | means |
| --- | --- | --- |
| `_rd_inhibit_mask` | `0x3` | the widened guard, still in the bin |
| `_rd_inhibit_dat` | `0x2` | **`DATA_INHIBIT` was still set at CMD17's entry** |
| `_rd_inhibit_timeout` | `1` | the mask never cleared |
| `_rd_inhibit_polls` | `0xa700` | the full budget |
| `_rd_sent` | `0` | **CMD17 still not issued** |
| `_rd_ps_end` | `0x01f80206` | the state CMD17 left — reset AFTER this |
| `_rd_complete` / `_rd_words_gated` / `_rd_gpt` | `0` | no data |

This is the one ordering fact the press makes visible: **the same boot that releases the line refused
CMD17 first, because `st_data_reset` is called after `st_read_single_block`.** The reading is not
contradictory — it is the sequence made legible by one log.

## 3. The frontier

**THE RESET IS PROVEN AND IT IS PROVEN TOO LATE.** The next rung moves the same `st_data_reset` call
**before** `st_read_single_block` (after the CMD8/SET_BLOCKLEN chain, before CMD17), so the sector read
runs against the released line. All the repair's safety properties are unchanged — one host-controller
byte, no command, no byte of the medium — and the card stays in TRAN.

**THE GOAL IS NOT MET:** no sector read (CMD17 still withheld), no partition table walked, no filesystem,
no mount; TWRP-to-storage stays withheld. **NO ARM IS ARMED AND NO PRESS IS OWED** — the next rung is not
yet built, and the press is the operator's.