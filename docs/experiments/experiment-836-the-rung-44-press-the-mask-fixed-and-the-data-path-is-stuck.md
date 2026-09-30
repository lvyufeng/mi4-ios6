# 836 — the rung-44 press: the mask is fixed, and the data path is stuck

**A PRESS**, made under the standing instruction. One gate exit 0 / 613 lines, one runner exit 0;
`fastboot boot` only, nothing flashed, nothing written to storage; `33e80afe` absent from BOTH device
lists hand-checked immediately before the gate. **The phone returned** (adb up afterwards, uptime 2 min).

| | |
| --- | --- |
| arm | `armed-storage-65824de4`, `STAGE90_XNU_STORAGE_PROBE=43` (rung 44) — now SPENT |
| capture | `out/stage90/captures/rung44-inhibit-20260930-074538-last_kmsg.txt` 649,975 B `7bab8faa…` |
| gate / run | `…-gate.txt` 53,586 B `835a875a…` (613 lines) / `…-run.txt` 90,529 B `b123f6f0…` (1121 lines) |
| result | **THE REPAIR IS IN THE PRESS (`_rd_inhibit_mask = 0x3`) AND IT *WORKED* — AND IT FOUND THE DEFECT ONE COMMAND DOWN: CMD8 LEFT THE DATA PATH STUCK.** |

---

## 1. The repair is in the press, and it is the arm's pre-registered row 2

| cell | reading | means |
| --- | --- | --- |
| **`_rd_inhibit_mask`** | **`0x3`** | the wait tested `CMD_INHIBIT \| DATA_INHIBIT` — **the fix landed; a `0x1` here would have been the rung-43 image wearing this arm's name** |
| **`_rd_inhibit_dat`** | **`0x2`** | `DATA_INHIBIT` **was still set at CMD17's entry** — the wait had to happen, and it did |
| `_rd_inhibit_timeout` | **`1`** | the mask never cleared within the bound |
| `_rd_inhibit_polls` | `0xa700` = 42,752 | the full budget, run out — the poll did not exit early |
| `_rd_inhibit_before` | `0` | `CMD_INHIBIT` (bit 0) was clear; **only the data-phase bit was set**, which is exactly why a bit-0-only guard issued where the vendor would have waited |
| `_rd_sent` | **`0`** | **CMD17 was NOT issued** — `st_send_command` returns with `sent` still 0 on an inhibit timeout |

**This is the arm's own pre-registered row 2, exactly**: *"it does not clear → `inhibit_timeout = 1`,
CMD17 is **not sent**, and the reading is CMD8's transfer never formally ended at the block — the next
defect one command down, and the negative a press must be able to report rather than hang on."* **The
fix did what it was built to do: it turned a silent data-phase failure into a named precondition
refusal.** The mask cell is the proof the repair was in the bin; the timeout cell is the proof the wait
was doing work.

**And the non-data control is in the same log**: CMD0, CMD1 and CMD16 all read
`_cmd0_inhibit_timeout = _cid_inhibit_timeout = _blk_inhibit_timeout = 0` — every non-data command
cleared its bit-0-only mask. So `ST_MMC_CMD_ADTC` did **not** leak into the mask for a command with no
data phase, which was the arm's third pre-registered row. **Only `adtc` commands wait on the data bit,
and only the `adtc` command here is refused.**

## 2. The command cells confirm CMD17 was armed and then withheld

Everything up to the guard's refusal moved right; everything after it did not run:

| cell | reading | means |
| --- | --- | --- |
| `_rd_op` | `0x11` = 17 | opcode correct |
| `_rd_arg` | `0x200` = 512 | LBA 1 << 9 |
| `_rd_flags` | `0xB5` = 181 | `MMC_RSP_R1 \| MMC_CMD_ADTC` |
| `_rd_word_bits` / `_rd_word` | `0x111A` / `0x113A` | the five-bit word and the block's own `SDHCI_CMD_DATA`-ORed word |
| `_rd_calls` | 1 | called once |
| `_rd_lba` / `_rd_blksz` | `1` / `0x7200` | the request's address and the 512-byte block |
| `_rd_trns` / `_rd_readback` | `0x12` / `0x8000` | `TRANSFER_MODE` and `BLOCK_SIZE` were written (before the command) |
| `_rd_word_read` / `_rd_complete` / `_rd_state` / `_rd_resp` | `0` / `0` / `0` / `0` | **the block's `COMMAND` register was never written and no response ever came — CMD17 was not sent** |
| `_rd_blkcnt_held` | `0` | `BLOCK_COUNT` still 0 (this rung writes it, but the command that would consume it never went out) |
| `_rd_gated` / `_rd_done` | `0` / `1` | the precondition did NOT refuse — **the guard did**, which is a different cell |
| `_rd_gate_state` / `_rd_gate_resp` | `4` / `0x900` | the card's own R1 said TRAN before anything was written |

**So the precondition passed and the guard refused.** `_rd_gated = 0` with `_rd_done = 1` is the
opposite of row 5 (`_rd_gated = 1` with `_rd_done = 0`): the card was ready, and the block was not.

## 3. The defect one command down: CMD8 left the data path **stuck**

The inhibit wait timed out because `DATA_INHIBIT` never cleared. **`PRESENT_STATE` is now measured at
THREE points in the same boot and it does not move:**

| cell | reading | bit 1 (`DATA_INHIBIT`) | bit 2 (`DAT_LINE_ACTIVE`) |
| --- | --- | --- | --- |
| `_ext_ps_end` (after CMD8's transfer) | `0x01f80206` | **set** | **set** |
| `_rd_ps_before` (at CMD17's entry) | `0x01f80206` | **set** | **set** |
| `_rd_ps_end` (after the refusal) | `0x01f80206` | **set** | **set** |

**Bit 2 is `SDHCI_DAT_LINE_ACTIVE`** — the DAT line is being held active — and it is set beside bit 1.
So this is not merely a stale inhibit flag: **the block's data state machine believes a transfer is in
progress, and the DAT line is active.** And CMD8's own end cells say the same thing from the other side:

| cell | CMD8 (this boot) | means |
| --- | --- | --- |
| `_ext_int_status_end` | `0x21` | RESPONSE **\| DATA_AVAILABLE** — the data arrived |
| `_ext_int_data_end` | **`0`** | **`DATA_END` never latched** — the transfer never formally completed |
| `_ext_data_gated` | `0x80` = 128 | all 128 words were read out |
| `_ext_words_gated` | `0` | rung 38's unconditional first pass read nothing (it ran too early) |

**So CMD8's 512 bytes were delivered and read, but the host controller's data state machine was never
told the transfer ended.** `DATA_END` did not latch, `DATA_INHIBIT` stayed set, the DAT line stayed
active — and the next data command (CMD17) cannot arm because `BLOCK_COUNT` is write-protected while
`DATA_INHIBIT` is set, and the widened guard now refuses to send it rather than silently dropping the
arming write.

**The two presses now read as one story, and neither is wrong:**
- **834 (rung 43)** measured CMD17's command landing with no data, and named the wrong half — the guard —
  because the widened mask had not been tried. It corrected it to the guard.
- **836 (rung 44)** tried the widened guard and it **refused the command**, which proves the guard was
  correct **and** moves the defect one command down: **the refusal is not the bug, the stuck data path is.**
  The guard is the *instrument* that finally read it.

This is `[[mi4-silence-is-a-reading-only-if-success-is-silent]]` in its cleanest form: the widened guard
is a check that now succeeds by *printing a refusal*, and the refusal is the reading.

## 4. What is measured about the stuck path, and what is only a hypothesis

**What is measured** (all cells above, one boot): `DATA_INHIBIT` and `DAT_LINE_ACTIVE` are both set and
**do not move across three samplings** (`_ext_ps_end = _rd_ps_before = _rd_ps_end = 0x01f80206`);
CMD8's `_ext_int_data_end = 0` (no `DATA_END` ever latched); and the window the transfer ran under had
**`DATA_END` (bit 1) NOT enabled** — `_ext_ena_wrote = 0x000f0031` = `INT_CMD_MASK 0x000f0001` |
`PIO_IRQS 0x30` (DATA_AVAIL | SPACE_AVAIL), with bits 1 (`DATA_END`) and 3 (`DMA_END`) clear.

**What is only a hypothesis, and is named as one**: that the missing `DATA_END` enable *is the cause* of
the stuck state. The measurement is consistent with it — a completion bit that is masked off cannot
latch, and the driver's own path (`sdhci.c:806-815`, `sdhci_set_transfer_irqs`) enables `DATA_END` and
waits on it — but the press did not test it, and this project does not promote a mechanism to a
conclusion on consistency. **The class is the one rung 38's repair already fixed once**: a witness
switched off reads exactly like a condition that never happened (`INT_RESPONSE` was off there, `DATA_END`
is off here — same shape, one command's worth further along).

**So the next rung has two candidate repairs and a measurement that tells them apart**, and it is
deliberately not reduced to one edit:

- **(a) ENABLE `DATA_END` in the window** and re-read `INT_STATUS` after CMD8's transfer, so the host's
  data state machine latches completion and clears `DATA_INHIBIT`/`DAT_LINE_ACTIVE` on its own. If they
  clear, the hypothesis is confirmed **and** CMD17 can proceed in the same boot.
- **(b) RESET THE DATA LINE** (`SOFTWARE_RESET` byte, `SDHCI_RESET_DAT` bit 0x04 — distinct from the
  `RESET_ALL` 0x01 rung 4 writes) before CMD17, the driver's own error-recovery move
  (`sdhci_do_reset(host, SDHCI_RESET_DATA)`). If `DATA_INHIBIT` clears after the reset, **the host was
  the holder**; if it clears but `DAT_LINE_ACTIVE` stays, **the card is holding the line** — a card-side
  reading and a different defect.

**(a) is preferred because it is the diagnosis and not the workaround**: it answers whether the enable
was the cause rather than clearing the symptom and moving on. Both are safe and both are readable —
`fastboot boot` only, nothing flashed; a host-controller reset moves no byte of the medium; and the card
is in TRAN and neither repair re-initializes it.

**No firer is armed and no press is owed: the next rung is not yet built, and the press is the
operator's.**

## 5. Where the ladder stands

The chain is CMD0 → CMD1 → CMD2 → CMD3 → CMD9 → CMD7 → CMD13 → CMD8 → CMD16 → **CMD17**. The card is in
TRAN and has been told a block is 512 bytes. **The command path works end to end; the data path is
stuck at the block, and the rung-44 guard is the instrument that named it.** The next rung is the data
path's completion, then the sector read, the GPT walk, the filesystem → mount → TWRP-to-storage.

**THE GOAL IS NOT MET.** No sector read, no partition table walked, no filesystem, no mount;
TWRP-to-storage stays withheld.