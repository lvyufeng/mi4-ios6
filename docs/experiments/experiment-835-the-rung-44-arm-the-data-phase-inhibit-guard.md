# 835 — the rung-44 arm: wait on `DATA_INHIBIT` too when the command carries a data phase

**A BUILD AND A PARK, AND NOTHING ELSE.** Arm `armed-storage-65824de4`, `STAGE90_XNU_STORAGE_PROBE=43`,
entry bin `65824de4…`, elf `bca21c2f…` 6,749,376 B, payload `stage90-qcdt.img` `d9747a8c…` 8,589,312 B.
Readiness **5 of 5** exit 0, `make check` exit 0, park verified **11 of 11** against
`records/revert-set.txt`. **PARKED, NOT PRESSED, NOT ARMED-FOR-PRESS.**

It is **the rung-43 press's own repair**, and it adds **no command, no register, no megabyte, no device
act and no new immediate**: one conditional inside `st_send_command`'s existing inhibit wait.

---

## 1. What the rung-43 press handed over

834's press measured CMD17's command **landing** on every command-side cell the arm was built to move
(`_rd_complete = 1`, `_rd_err = 0`, `_rd_state = 4`, `_rd_flags = 0xB5`, `_rd_word_bits = 0x111A`,
`_rd_word = _rd_word_read = 0x113A`) with **no data** (`_rd_words_gated = 0`, `_rd_data_wait_timeout = 1`,
`_rd_data_ticks = 0x016e38cf`, `_rd_int_status_end = 0x1` — RESPONSE only, `_rd_gpt = 0`).

The decisive cell is a **same-boot comparison**: `_rd_blkcnt_held = 0` against CMD8's
`_ext_blkcnt_held = 1`. The duplicate `BLOCK_COUNT = 1` write was **silently dropped**, because SDHCI
write-protects `BLOCK_COUNT 0x06` while `PRESENT_STATE 0x24`'s **Command Inhibit (DAT)** bit (bit 1)
is set, and **CMD8 left that bit set and never cleared it** (`_rd_ps_end = _ext_ps_end = 0x01f80206`,
bit 1; `_ext_int_data_end = 0`). The block then ran a one-block read with a count of **zero**, no
`DATA_AVAILABLE` fired, and the 1.25 s bound expired.

## 2. The defect, and why the fix is in the guard

This image's guard tested **bit 0 alone** — `ST_SDHCI_CMD_INHIBIT` (`entry_storage.c:2114`) is `0x1`,
Command Inhibit (CMD) — where the vendor waits on **both** for a data command:

```
sdhci.c:1087    mask = SDHCI_CMD_INHIBIT;
sdhci.c:1089    if (cmd->data || (cmd->flags & MMC_RSP_BUSY))
sdhci.c:1090            mask |= SDHCI_DATA_INHIBIT;
sdhci.c:1094            mask &= ~SDHCI_DATA_INHIBIT;   /* the non-data path */
```

**CMD17 is `adtc` — it has a data phase — so the vendor would have waited on `DATA_INHIBIT`, and this
image did not.** Either half alone is harmless: a stale `DATA_INHIBIT` with a guard that waits on it
never reaches the write, and a guard that ignores it reaches a write that only a data-command
predecessor can refuse. **They are two halves of one bug**, and the fix belongs in the guard — because
the vendor's own rule is the guard's, and because a guard is the only place the image can *report* the
condition rather than silently act on a stale bit. This is `[[mi4-one-value-two-definitions]]`' shape
one register up: `PRESENT_STATE` bit 1 is one quantity with two readings ("the transfer is still
active" vs "a read can start"), and this ladder's guard read only bit 0 while the hardware enforced
bit 1.

## 3. What this rung is

`st_send_command`'s wait now computes its mask as

```c
r->inhibit_mask = ST_SDHCI_CMD_INHIBIT;                        /* 0x1 */
if ((mmc_flags & ST_MMC_CMD_ADTC) != 0u)
    r->inhibit_mask |= ST_SDHCI_DATA_INHIBIT;                  /* 0x2 */
```

and waits on `r->inhibit_mask` where the rung-43 image waited on bit 0 alone. `ST_MMC_CMD_ADTC`
(`0x20`) is the ladder's own stand-in for the vendor's `cmd->data` — the same bit `entry_storage.c`
ORs into the block's COMMAND word as `SDHCI_CMD_DATA`. The vendor's `else mask &= ~SDHCI_DATA_INHIBIT`
arm is why **every non-data rung below (CMD13, CMD16) is untouched**: no pressed arm's evidence moves.

## 4. What the press would read

Four new cells beside the command:

| cell | reading | means |
| --- | --- | --- |
| `_rd_inhibit_mask` | **`0x3` on CMD17** | the data command waited on both bits — the repair |
| | `0x1` on CMD17 | **the rung-43 image wearing this arm's name** (build-refused) |
| | `0x1` on every non-data rung below | `ST_MMC_CMD_ADTC` did not leak into a data-less command |
| `_rd_inhibit_dat` | `0x2` | `DATA_INHIBIT` still set at CMD17's entry — the wait had to happen, and it did |
| | `0` | the bit was already cleared (CMD8's transfer ended at the block after all — the defect is elsewhere) |
| `_rd_inhibit_before` / `_rd_inhibit_polls` | | whether the wait had to run at all |

**Three outcomes, all safe and all readable**: the inhibit clears within the bound → CMD17 proceeds and
the sector that should have arrived in rung 43 arrives here; it does not clear → `inhibit_timeout = 1`,
CMD17 is **not sent**, and the reading is *CMD8's transfer never formally ended at the block* — the next
defect, one command down; and CMD16 and every non-data rung below are untouched.

## 5. The build clause, `xnu_entry_835`

It refuses the build unless `st_send_command`'s linked body holds:

1. the ADTC test `ands rN, rN, #32` (0x20 = `MMC_CMD_ADTC`);
2. **both** compiled mask constants `moveq #1` (CMD_INHIBIT alone) and `movne #3` (CMD_INHIBIT | DATA_INHIBIT);
3. the `DATA_INHIBIT` extraction `and rN, rN, #2`;
4. at least three stores into the result struct's low slots (the mask, the extracted bit and the
   CMD_INHIBIT bit are all published);
5. all four `xnu_live_storage_rd_inhibit_*` keys as strings in the linked image.

So a bit-0-only mask wearing this arm's name is **refused at build time**, and each missing half names
its own defect. The clause passed on the first linked build: `st_send_command` (at `0x8000cff8`) shows
`8000d0ac: ands r3, r9, #32` / `moveq r2, #1` / `movne r2, #3` / the store, plus `and r2, r2, #2`.

## 6. Safety

`fastboot boot` only, nothing flashed, **no byte of the medium can change**: CMD17 is
`adtc [31:0] data addr R1` and `MMC_DATA_READ` (`core.h:41`) moves bytes **card-to-host**; the sector
read is one the card's own `EXT_CSD_SEC_CNT = 0x01d5a000` says exists; the rung touches no register
this ladder has not already reached since rung 38; and the data-phase bound is rung 41's
measured-sufficient 1.25 s. `STAGE90_XNU_PWR_WAIT_TICKS=384000` is inherited from rung 9 unchanged.

**NO PRESS HAS BEEN MADE AND NONE IS OWED: this arm is PARKED and NOT ARMED-FOR-PRESS, and the park is
a RECORD and not a queue.**

## 7. Where the ladder stands

The chain is CMD0 → CMD1 → CMD2 → CMD3 → CMD9 → CMD7 → CMD13 → CMD8 → CMD16 → **CMD17**. The card is
in TRAN, has been told a block is 512 bytes, and has been asked for sector 1 — the command landed and
the block refused the arming write. This rung widens the guard so the wait covers the bit the hardware
enforced. After it: the sector read, the GPT walk, the filesystem → mount → TWRP-to-storage.

**THE GOAL IS NOT MET.** No sector read, no partition table walked, no filesystem, no mount;
TWRP-to-storage stays withheld.