# 834 — the rung-43 press: CMD17's command landed, its data did not, and the guard is the defect

**A PRESS**, made under the standing instruction. One gate exit 0 / 620 lines, one runner exit 0;
`fastboot boot` only, nothing flashed, nothing written to storage; `33e80afe` absent from BOTH device
lists hand-checked immediately before the gate. **The phone returned** (adb up afterwards).

| | |
| --- | --- |
| arm | `armed-storage-7228e3f9`, `STAGE90_XNU_STORAGE_PROBE=42` (rung 43) — now SPENT |
| capture | `out/stage90/captures/rung43-cmd17-20260930-0639-last_kmsg.txt` 657,994 B `709a80be…` |
| gate / run | `…-gate.txt` 54,132 B `1b853cec…` (620 lines) / `…-run.txt` 90,802 B `19b1c285…` (1125 lines) |
| result | **CMD17 WAS PUT ON THE BUS AND ANSWERED — AND NO DATA ARRIVED. THE DATA-PHASE INHIBIT GUARD IS THE DEFECT.** |

---

## 1. CMD17's command landed, exactly as the arm was built to send it

Every command-side cell the arm asserted moved the right way:

| cell | reading | means |
| --- | --- | --- |
| `_rd_sent` / `_rd_calls` | 1 / 1 | sent once, called once |
| `_rd_op` | `0x11` = 17 | `MMC_READ_SINGLE_BLOCK` |
| `_rd_arg` / `_rd_arg_wrote` | `0x200` = 512 each | LBA 1 << 9, the byte address |
| `_rd_flags` | **`0xB5` = 181** | `MMC_RSP_SPI_R1 \| MMC_RSP_R1 \| MMC_CMD_ADTC` — **the number the clause enforces, and the fix from `0x19D` is confirmed on hardware** |
| `_rd_flags_mapped` | `0x1A` | the five-bit mapping |
| `_rd_word_bits` / `_rd_word` / `_rd_word_read` | `0x111A` / `0x113A` / `0x113A` | the block's `COMMAND` register held the data-bit word — **`SDHCI_CMD_DATA` ORed in, so the block was told this is a data command** |
| `_rd_complete` / `_rd_err` / `_rd_state` | 1 / 0 / 4 | CMD17 answered with a valid R1, card in TRAN |
| `_rd_gate_state` | 4 | the precondition read the card's own R1 and let the command through |

**So the command path is perfect.** The defect is downstream of it.

## 2. The data did not arrive, and the arm's own bound caught it

| cell | reading | means |
| --- | --- | --- |
| `_rd_words_gated` | **0** | not one of the 128 words was gated in |
| `_rd_data_wait_timeout` | **1** | the 1.25 s bound expired |
| `_rd_data_ticks` | `0x016e38cf` = 24,000,719 | the full bound, consumed |
| `_rd_int_status_end` | **`0x1`** | RESPONSE **only** — `DATA_AVAILABLE` is **not** set |
| `_rd_int_data_avail` | 0 | the transfer never signalled data |
| `_rd_w0`/`_rd_w1`/`_rd_w127` | 0 / 0 / 0 | the buffer is `.bss`, untouched |
| `_rd_gpt` | **0** | **no GPT header — because no sector came back at all** |

**`_rd_gpt = 0` here is not "the medium is not GPT".** It is the arm's derived cell reading a buffer
nothing filled. The distinction is the whole reason `_rd_words_gated` sits beside it.

## 3. The decisive comparison is in the same log

**CMD8 runs earlier in the SAME boot and its read delivered all 128 words** — but through **rung 40's
gated second pass**, not rung 38's unconditional first pass:

| cell | CMD8 (same boot) | CMD17 (same boot) | means |
| --- | --- | --- | --- |
| `data_gated` (2nd pass) | **`0x80`** = 128 | `_rd_words_gated` = **0** | CMD8 delivered the whole block, CMD17 delivered nothing |
| `data_ticks` | `0x67bab` = 424,875 | `0x16e38cf` = 24,000,719 | 22.13 ms vs the full 1.25 s bound |
| `words_gated` (1st pass) | 0 | (n/a) | rung 38's pass ran too early for CMD8 too |
| `int_status_end` | **`0x21`** (RESP + **DATA_AVAIL**) | **`0x1`** (RESP only) | CMD8's data-available fired; CMD17's never did |
| `int_data_end` | **0** | 0 | **neither transfer formally ended at the block** |

So the two data commands are armed **identically** (`_rd_ena_wrote = _ext_ena_wrote = 0x000f0031`,
`blksz = 0x7200`, `blkcnt = 1`, `trns = 0x12`, `readback = 0x8000`) and differ in exactly one arming
cell, which is the defect:

## 4. The defect: `BLOCK_COUNT` could not be written, and the reason is the inhibit guard

| cell | CMD8 | CMD17 |
| --- | --- | --- |
| `blkcnt` written | 1 | 1 |
| **`blkcnt_held`** (readback) | **`0x1`** | **`0x0`** |

**The duplicate write of `1` was rejected.** SDHCI's spec is explicit: `BLOCK_COUNT` is
**write-protected while the Command Inhibit (DAT) bit of `PRESENT_STATE` is set**. And that bit is set:

| cell | reading |
| --- | --- |
| `_ext_ps_end` | `0x01f80206` — bit 1 set |
| `_rd_ps_end` | `0x01f80206` — bit 1 set |
| `_rd_ps_end` (in `st_send_command`) | `0x01f80206` — bit 1 set **at CMD17's entry** |

Bit 1 of bit-1-manifested `PRESENT_STATE` is **`SDHCI_DATA_INHIBIT` (Command Inhibit DAT)**.
**CMD8 set it and never cleared it** (`_ext_int_data_end = 0` — the transfer never completed at the
block; rung 41's press saw the same `_ext_int_status_end = 0x21` and the same class was left unread).

**And this ladder's inhibit guard checks only bit 0.** `ST_SDHCI_CMD_INHIBIT` (`entry_storage.c:2114`)
is `0x1` — **Command Inhibit (CMD)** — and `st_send_command`'s wait (`entry_storage.c:3273`) tests that
one bit. **The vendor waits on BOTH bits for a data command**:

```
sdhci.c:1087    mask = SDHCI_CMD_INHIBIT;
sdhci.c:1089    if (cmd->data || (cmd->flags & MMC_RSP_BUSY))
sdhci.c:1090            mask |= SDHCI_DATA_INHIBIT;
sdhci.c:1094            mask &= ~SDHCI_DATA_INHIBIT;   /* the non-data path */
```

**CMD17 is `adtc` — it has a data phase — so the vendor would have waited on `DATA_INHIBIT`, and this
image did not.** The chain is then closed end to end:

> CMD8's transfer leaves `DATA_INHIBIT` set → CMD17's guard sees bit 0 clear and issues anyway →
> `BLOCK_COUNT` is write-protected so the write is silently dropped → the block still holds
> `BLOCK_COUNT = 0` → **a one-block read with a count of zero transfers zero bytes** → no
> `DATA_AVAILABLE`, no `DATA_END`, the bound expires, `_rd_gpt = 0`.

**Every cell of the failure is a cell the arm published.** `_rd_blkcnt_held = 0` is the one that names
it, and it was read out **before** CMD17 was sent.

## 5. Why "the CMD8 leftover" and "the guard" are one defect and not two

Either alone is harmless: a stale `DATA_INHIBIT` with a guard that waits on it never reaches the
write, and a guard that ignores it reaches a write that only a data-command predecessor can refuse.
**They are two halves of one bug, and the fix belongs in the guard** — because the vendor's own rule
is the guard's, and because a guard is the only place the image can *report* the condition rather
than silently act on a stale bit. This is [[mi4-one-value-two-definitions]]' shape one register up:
`PRESENT_STATE` bit 1 is one quantity with two readings ("the transfer is still active" vs "a read
can start"), and this ladder's guard read only bit 0 while the hardware enforced bit 1.

## 6. The next rung, and why it is one edit

**Wait on `DATA_INHIBIT` as well when the command carries a data phase** — `sdhci.c:1087-1094`
transcribed: `mask = CMD_INHIBIT; if (mmc_flags & ST_MMC_CMD_ADTC) mask |= DATA_INHIBIT;`. Three
outcomes, all safe and all readable:

- **the inhibit clears within the bound** → CMD17 proceeds, and the sector that should have arrived in
  rung 43 arrives in rung 44;
- **it does not clear** → `inhibit_timeout = 1`, CMD17 is **not sent**, and the reading is *CMD8's
  transfer never ended at the block* — which is the next defect, one command down, and is the negative
  a press must be able to report rather than hang on;
- **CMD16 and every non-data rung below are untouched** — the vendor's `mask &= ~SDHCI_DATA_INHIBIT`
  arm, so no pressed arm's evidence moves.

**No firer is armed and no press is owed: the next rung is BUILT, PARKED and NOT PRESSED, and the
press is the operator's.** `fastboot boot` only; nothing flashed; `MMC_DATA_READ` moves bytes
card-to-host, so no byte of the medium can change.

## 7. Where the ladder stands

The chain is CMD0 → CMD1 → CMD2 → CMD3 → CMD9 → CMD7 → CMD13 → CMD8 → CMD16 → **CMD17**. The card is
in TRAN, has been told a block is 512 bytes, and **has now been asked for sector 1 — the command
landed and the block refused the arming write because a data-inhibit bit was left set.** The next rung
is the guard. After it: the sector read, the GPT walk, filesystem → mount → TWRP-to-storage.

**THE GOAL IS NOT MET.** No sector read, no partition table walked, no filesystem, no mount;
TWRP-to-storage stays withheld.