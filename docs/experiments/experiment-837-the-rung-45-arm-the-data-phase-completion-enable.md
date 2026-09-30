# 837 — the rung-45 arm: the data-phase completion enable, issued alone

**A BUILD AND A PARK, AND NOTHING ELSE.** The rung-45 arm is the rung-44 press's own **candidate (a)** —
the diagnosis and not the workaround. It is **PARKED, NOT PRESSED, NOT ARMED**.

| | |
| --- | --- |
| arm | `armed-storage-f8d91170`, `STAGE90_XNU_STORAGE_PROBE=44` (**VALUE 44 = ORDINAL RUNG 45**) |
| entry bin | `f8d91170…` 5,569,148 B (unchanged size — the new body is absorbed by page padding) |
| payload | `stage90-qcdt.img` `bd43c052…` 8,589,312 B |
| readiness | **5 of 5**, exit 0 — and the park verifies **11 of 11** |
| `.text`/seam | the entry-group page move did **not** fire: `STAGE90_XNU_SEAM_LR` stays `0x8004c2dc` |

---

## 1. Why rung 45 alone, and not rungs 45+46

The rung-44 press (836) ended with the data path **stuck**: `PRESENT_STATE = 0x01f80206` —
`DATA_INHIBIT` (bit 1) **and** `DAT_LINE_ACTIVE` (bit 2) both set and **unmoving across three
samplings in one boot** — CMD8's `DATA_END` never latched (`_ext_int_data_end = 0`), and the transfer
window's own enable `_ext_ena_wrote = 0x000f0031` had **bit 1 (`SDHCI_INT_DATA_END`) clear**. The press
named the missing enable a **hypothesis**, not a conclusion, and left **two candidate repairs**:

- **(a) enable `DATA_END`** and re-read `INT_STATUS` — *the diagnosis*;
- **(b) reset the data state machine** (`SOFTWARE_RESET`, `SDHCI_RESET_DATA` 0x04) — *the workaround*.

**This arm is (a) ALONE, deliberately.** A single boot that ran *both* would be unable to say which
repair released the line: if the enable is issued and then the reset is issued, a cleared
`DATA_INHIBIT` is consistent with either being the cause. **The reset is the NEXT arm (rung 46, value
45, admitted by the same ladder) and it is NOT built here.** One rung per value, as every arm before it:
833 was rung 43 = value 42, 831 rung 42 = value 41, 835 rung 44 = value 43.

## 2. What the body does, and the one store it makes

`st_data_end_probe` (`entry_storage.c`, gated `#if STAGE90_XNU_STORAGE_PROBE >= 44`) is one `bl` in
`st_cmd_path`, after the chain, outside every window:

| act | register | cells |
| --- | --- | --- |
| read `INT_ENABLE 0x34` | — | `_de_ena_before` |
| read `PRESENT_STATE 0x24` *(before any store)* | — | `_de_ps`, `_de_dat_line`, `_de_data_inhibit`, `_de_doing_read` |
| **OR in `SDHCI_INT_DATA_END`, write, READ BACK** | `INT_ENABLE 0x34` | `_de_ena_wrote`, `_de_ena_added`, `_de_ena_held` |
| read `INT_STATUS 0x30` | — | `_de_int_status`, `_de_data_end`, `_de_data_avail` |
| restore the window, read back | `INT_ENABLE 0x34` | `_de_ena_restore`, `_de_ena_readback` |
| read `PRESENT_STATE 0x24` *(after)* | — | `_de_ps_end` |

**It sends no command and re-arms no transfer.** `DATA_END` is latched by the block at a transfer's own
end; if the enable is the reason it never latched, then the latch is a **LEVEL the block is still
asserting** — so the reading is taken on the state rung 44 left behind, in the same boot, with **no new
transfer**. **It adds no device, no megabyte, and no new immediate**: the only store is one
`INT_ENABLE` bit, a register this ladder has written on every command rung since 23, restored to
`_de_ena_before` at the exit.

## 3. How to read the press, and the cell no enable can gate

**Read the rows as the arm's own table:**

- **`_de_ena_held = _de_ena_wrote = _de_ena_before | 0x2`** — THE ENABLE ACCEPTED, and bit 1 the bit
  added. The **precondition** for either verdict; a `_de_ena_held` unequal to it is the store being
  **DROPPED** (a block-side reading), not a verdict about the transfer.
- **`_de_data_end = 1`** — **THE HYPOTHESIS CONFIRMED.** The completion *had* latched and was invisible
  only because its enable was off — the same shape rung 38's `INT_RESPONSE` repair measured on the
  **command** side, one command up.
- **`_de_data_end = 0` with `_de_dat_line = 1`** — **THE HYPOTHESIS REFUTED, THE LINE STILL HELD.** The
  enable was not the explanation; the transfer really is in flight or stalled. **The next arm is the
  data state machine's own reset (rung 46).**
- **`_de_data_end = 0` with `_de_dat_line = 0`** — the block having **released** the line since the
  rung-44 press: a reading about the medium, not about the enable.

**`_de_dat_line` is published beside `_de_data_end` because it is the one reading no enable can gate.**
`DATA_END` is a **latch** that can be set and be cleared, so a clear bit is *ambiguous* — it may never
have latched, or it may have latched and been consumed. `DAT_LINE_ACTIVE` is the line's own **level** —
the same kind of quantity `CMD_LINE_LEVEL` (bit 24) is, which this image already samples as evidence
about a line rather than about a latch. So the pair separates *"the enable was off"* from *"the transfer
really is in flight"*.

## 4. The build clause `xnu_entry_837`

`build_entry.sh` (`-ge 44`) refuses the build unless `st_data_end_probe`'s **linked** body holds:

1. the `orr rN, rN, #2` that adds `SDHCI_INT_DATA_END` (bit 1), and
2. at least **two stores** and **one load** of `INT_ENABLE 0x34` (base `0xf9824000`, `[rN, #2356]`) —
   the write **and the read-back**, and
3. the six `xnu_live_storage_de_*` keys as strings in the linked image.

So an enable written **without being read back**, or an enable of the **wrong bit**, is refused at build
time. (The clause `xnu_entry_838` for the rung-46 reset exists at `-ge 45` but is **inert at this
value** — rung 46 is not built.)

## 5. Safety, and where the ladder stands

`fastboot boot` only; nothing flashed. The only store is an `INT_ENABLE` bit, restored at the exit; no
device, no megabyte, no command — **no byte of the medium can move**, and the card stays in TRAN with
its block length and its CSD/EXT_CSD exactly as rungs 42–44 left them.

The chain is CMD0 → CMD1 → CMD2 → CMD3 → CMD9 → CMD7 → CMD13 → CMD8 → CMD16 → **CMD17**. The command
path works end to end; the data path is **stuck at the block**, and this arm is the single measurement
that decides whether the stuck state is *a masked completion* or *a held line*. **`frozen/` is a record,
not a queue — no park is pressed.** **THE GOAL IS NOT MET:** no sector read, no partition table walked,
no filesystem, no mount; TWRP-to-storage stays withheld.