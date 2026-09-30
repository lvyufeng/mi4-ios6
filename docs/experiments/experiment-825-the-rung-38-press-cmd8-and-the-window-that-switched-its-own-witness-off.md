# 825 — the rung-38 press: CMD8 landed and the 512 bytes arrived, and the window switched off the witness that would have said so

**A PRESS, under the operator's authorization asked for and given in this session** — the answer was
*"Press rung 38 now"*. **ONE gate exit 0 / 0 `FAIL`; ONE runner exit 0**; `fastboot boot` only;
nothing flashed; nothing written to storage; no reboot commanded; **`33e80afe` absent from BOTH device
lists, hand-checked immediately before the gate** (neither log carries that check — it is this
session's own reading, named here rather than implied). The phone returned to Android **29 s after the
send** (the runner's own line, `seen via: adb`).

**AND THE ANSWER IS TWO-SIDED.** The DATA PATH WORKS: **CMD8's own R1 is in the register**
(`_ext_resp = 0x00000900`, state 4 = TRAN), and **the card's EXT_CSD fields are non-zero and mutually
consistent** in a buffer that was `.bss`-zeroed and is written by nothing else — `_ext_rev = 7`,
`_ext_structure = 2`, `_ext_card_type = 0x57`, `_ext_sec_count = 0x01d5a000` = 30,777,344 sectors
(15,757,840,128 B ≈ 14.68 GiB, a 16 GB part). **The 512 bytes arrived.** But the body's own
completion witness reads **`_ext_complete = 0` / `_ext_timeout = 1`**, and the reason is a defect in
THIS RUNG'S OWN WINDOW, not in the card: **`_ext_ena_base = 0` and `_ext_ena_wrote = 0x30`** — the
window enabled only the PIO pair (`DATA_AVAIL | SPACE_AVAIL`), and **never `SDHCI_INT_RESPONSE`
(bit 0)**, the one enable bit every command rung ORs through `ST_SDHCI_INT_ENABLE_CMD`. `INT_STATUS`'s
latch honours `INT_ENABLE`, so the command-completion bit was never allowed to set — and the poll ran
its full 1.2 s budget. **The frontier is now a one-OR repair, and then CMD16.**

---

## 1. The press

| | |
| --- | --- |
| arm | `armed-storage-92b6c552`, switch **VALUE 37 = ordinal rung 38**, CMD8 `SEND_EXT_CSD` |
| what was sent | `stage90-qcdt.img` `0756bab7…`, 8,589,312 B — the runner's own line confirms the declared arm is the recorded set of the bytes to be sent |
| the runner's arm check | `declared arm 'armed-storage-92b6c552' == the recorded set of the bytes to be sent` |
| capture | `out/stage90/captures/rung38-cmd8-20260930-005139-last_kmsg.txt` **653,497 B sha `a87a0e93…`** |
| beside it | `…-gate.log` 52,225 B `be5f739f…`, `…-run.log` 89,164 B `0340c3b0…` |
| readiness | `tools/verify_press_ready.sh` resolved the arm **as the recorded set** before the gate: 5 of 5, exit 0 |
| the run's ending | `_post_end_calls = 7`, `_seam_post_end_ticks = 0x06ddd000 = 115,200,000`, `_post_cntfrq = 0x0124f800 = 19,200,000` — **6.0000 s, the ladder's own deadline**, the known forced ending (`[[mi4-the-run-ends-at-entry-epilogue]]`), unchanged since 520 |

**THE ARM IS NOW SPENT.** Its park stays in `out/stage90/frozen/armed-storage-92b6c552/` because a
park is the record of what was built — **`frozen/` is a record, not a queue**.

## 2. The command, as it went out

| cell | value | reading |
| --- | --- | --- |
| `_ext_op` / `_ext_arg` / `_ext_arg_wrote` | `8` / `0` / `0` | CMD8 `SEND_EXT_CSD`, argument 0 |
| `_ext_flags` / `_ext_flags_mapped` | `0x000000b5` = 181 / `0x0000001a` = 26 | `mmc_ops.c:263`'s `MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC`, beside the five-bit mapping |
| **`_ext_word` / `_ext_word_read`** | **`0x0000083a` = 2106** | **the DATA bit (`SDHCI_CMD_DATA 0x20`) ORed onto the mapped `0x081A`** — the word 824 predicted and the build asserts; `0x081A` (2074) appears in no cell |
| `_ext_sent` / `_ext_calls` / `_ext_done` | `1` / `1` / `1` | one command, one body, one exit |

The three halfword setup registers were written and are held:

| cell | value | reading |
| --- | --- | --- |
| `_ext_blksz` / `_ext_blksz_held` | `0x00007200` | `BLOCK_SIZE` = 512-byte block |
| `_ext_blkcnt` / `_ext_blkcnt_held` | `1` | `BLOCK_COUNT` = 1 |
| `_ext_trns` / `_ext_trns_held` | `0x00000012` | `TRANSFER_MODE` = READ, 1 block |

## 3. The response: CMD8 DID complete

| cell | value | reading |
| --- | --- | --- |
| `_ext_gate_resp` / `_ext_gate_state` | `0x00000900` / `4` | the gate held: the card was in TRAN, so the phase ran (`_ext_gated = 0`) |
| **`_ext_resp`** | **`0x00000900`** | **CMD8's OWN R1** — the card answered with state 4 (TRAN), illegal 0, ready 1 |
| `_ext_rsp_present` / `_ext_state` / `_ext_illegal` / `_ext_ready` | `1` / `4` / `0` / `1` | the response was requested, read, and decoded |

**This is the cell that says the command completed**, and it is independent of the enable defect in §4:
the block latched the card's R1 into `RESPONSE` and the ladder read it. State TRAN is both the
precondition the gate required and the answer CMD8's own R1 gives back — as it must be, because
`SEND_EXT_CSD` does not move the card.

## 4. Why `_ext_complete = 0`: the window switched off its own witness

| cell | value | reading |
| --- | --- | --- |
| **`_ext_ena_base`** | **`0x00000000`** | `INT_ENABLE` as re-read at rung 38 — **every rung below 37 writes `int_enable` back at its own exit**, so rung 38 starts from 0 |
| **`_ext_ena_wrote`** | **`0x00000030`** | the window wrote **only** `ST_SDHCI_INT_PIO_IRQS` = `DATA_AVAIL (0x20) | SPACE_AVAIL (0x10)` |
| `_ext_ena_add` | `0x00000030` | the OR the body applies |
| `_ext_sig_enable` | `0x00000000` | `SIGNAL_ENABLE` stayed zero — the deliberate departure of 824 §2, unchanged |
| `_ext_complete` / `_ext_timeout` / `_ext_err` | **`0`** / **`1`** / `0` | the command-completion poll saw nothing and ran its full budget |
| `_ext_polls` / `_ext_status_after` | (full) / **`0x00000020`** | the ONLY latched bit at the end is `DATA_AVAIL` — the one bit the window enabled |

**The mechanism is exact and structural.** Every command rung from 23 ORs `ST_SDHCI_INT_ENABLE_CMD` =
`0x000F0001`, which carries **bit 0 = `SDHCI_INT_RESPONSE`** — the bit `st_send_command`'s poll waits
on `(status & SDHCI_INT_CMD_MASK)`, with `complete = (status & SDHCI_INT_RESPONSE)`. rung 37's window
wrote `0x000F0001` (`_sta_ena_wrote`), latched bit 0, and read `_sta_complete = 1`. **rung 38's window
wrote `0x30` and dropped it.** `INT_STATUS`'s latch honours `INT_ENABLE`: the block latched the
enabled DATA bit (`0x20`) but never the disabled RESPONSE bit, so the poll — watching for
`INT_CMD_MASK`, of which bit 0 is the only one this window could produce — spun to its 1.2 s bound.

**The falsification is sharp**: the body's own outcome table (824, `entry_storage.c:6030-6046`)
pre-registered `_ext_complete = 1` as the green light. The press measured `0` **beside a card that
plainly answered and a buffer that plainly holds a transfer.** `_ext_complete = 0` is therefore NOT
"the command did not complete" — it is "the witness that would have said so was switched off", the
`[[mi4-silence-is-a-reading-only-if-success-is-silent]]` shape turned on the arm's own instrumentation.
**The repair is ONE OR**: the window must OR `ST_SDHCI_INT_ENABLE_CMD` with the PIO pair — which is
also what the vendor does, because `sdhci_init` (`sdhci.c:291-296`) leaves `SDHCI_INT_RESPONSE`
enabled for the whole life of the host idle window, and rung 37's restore to 0 is what left rung 38 a
zero base to add to.

## 5. The PIO read: 128 words, and the buffer is fresh

| cell | value | reading |
| --- | --- | --- |
| `_ext_words_read` | `0x00000080` = 128 | **the loop's bound, a CONSTANT and not a count** |
| **`_ext_words_gated`** | **`0x80`** | **all 128 iterations saw `DATA_AVAILABLE`** — the FIFO offered a word every time |
| `_ext_do_read_seen` / `_ext_data_avail_seen` | `0x80` / `0x80` | `DOING_READ` and `DATA_AVAILABLE` set on every iteration |
| `_ext_ps_first` / `_ext_ps_last` | `0x01f80a06` | `PRESENT_STATE` with `DATA_AVAILABLE 0x800` set |
| `_ext_ps_end` | `0x01f80206` | the same register after the transfer, `0x800` now clear |

**And the buffer is a fresh transfer, not a zeroed `.bss`.** `st_ext_csd[128]` is zeroed at image
start and written **only** by this rung's loop; `_ext_w0 = _ext_w127 = 0`, but the four fields at
bytes 192–215 are non-zero and mutually consistent:

| cell | value | reading |
| --- | --- | --- |
| `_ext_rev` | **`7`** | `EXT_CSD_REV` = eMMC v4 (`mmc.h:303`); `mmc.c:322` rejects above 7 |
| `_ext_structure` | **`2`** | `EXT_CSD_STRUCTURE` (`mmc.h:304`), a valid CSD structure |
| `_ext_card_type` | **`0x57`** | `26 | 52 | DDR_1_8V | SDR_1_8V | HS400_1_8V` — a real eMMC 5.x type byte |
| **`_ext_sec_count`** | **`0x01d5a000`** = 30,777,344 | the density: × 512 = **15,757,840,128 B ≈ 14.68 GiB — a 16 GB part** |

**Four independent non-zero fields at four different offsets is not something a stuck or absent read
produces.** The words that stayed 0 (`w0`, `w127`) are the card's own zero bytes, not a silence —
which is exactly why 824 §6 published them raw beside the decoded fields rather than instead of them.

## 6. The timeout arithmetic, off the carried CSD

| cell | value | reading |
| --- | --- | --- |
| `_ext_csd_carried` | `1` | rung 35's assembled CSD was valid, so the timeout was computed from the CARD, not from a raw register word (m814 rule 4) |
| `_ext_csd_tacc_ns` / `_ext_csd_tacc_clks` | `8` / `0` | `TAAC` 8 ns, `NSAC` 0 — this card's own timing |
| `_ext_timeout_ns` / `_ext_timeout_us` | `0x50` = 80 ns / `1` | `mmc_set_data_timeout`'s target, ceiled |
| `_ext_timeout_step0` | `0x00014000` = 81,920 µs | the base-clock step-0 bound |
| `_ext_tout_count` / `_ext_tout_max` / `_ext_tout_held` | `0` / `0xF` / `0` | step 0 already covers the 1 µs target — **no doubling, count 0**; `0xF` is the reserved max this host reaches (the quirk) |

The vendor's arithmetic ran and produced a count the block accepted. `TIMEOUT_CONTROL` was read
(`_ext_tout_was = 0`), written `0`, and restored to `0` — **the first press in this ladder to write
it**, and it lands read-only-rung territory.

## 7. Containment, and what the press cost the machine

Storage cells **673 → 746**; **0 keys lost**; **73 added — the entire `_ext_*` family and nothing
else**. Of the whole-log cells, **36 moved and every one is a poll count, a tick count, or a
free-running timestamp** (`_cid_polls`, `_clk_set_cc_ticks`, `_pwr_irq_at`, `_sta_polls`, …). **Not
one decision cell moved**: `_cmd1_resp = 0x40ff8080`, `_nidx_state = 2`, `_csd_structure = 3`,
`_csd_mmca_vsn = 4`, `_sel_state = 3`, `_sel_complete = 1`, `_sta_state = 4`, `_sta_complete = 1` are
**identical across the two boots**. The OS came up exactly as 818 measured it — **`BSD root: md0,
major 2, minor 0`**, the init sequence ran.

**Nothing on the device changed.** `fastboot boot` only, no flash, no storage write. The new surface
is the controller's own registers, mapped since rung 14; **no new megabyte and no new device**.

## 8. What follows: a one-OR repair, then CMD16

**Rung 38 is one `|=` from working.** The next arm adds `ST_SDHCI_INT_ENABLE_CMD` to the rung-38
window (`word = int_enable | ST_SDHCI_INT_ENABLE_CMD | ST_SDHCI_INT_PIO_IRQS`), which restores the
completion witness rung 37 relied on and lets `_ext_complete` read 1. **The build can assert the
fix structurally**: the same clause that already checks `_ext_word_hit` on `0x083A` can require the
window's stored value to carry bit 0. This is the cheapest kind of rung — one immediate moved into an
OR — and it is the arm that turns §5's "the data arrived" from a four-field inference into a latched
completion beside it.

| candidate | what it would settle | cost |
| --- | --- | --- |
| **the window fix** | `_ext_complete = 1` beside the data — the completion made a reading | **one OR**, the arm rung 38 should have been |
| **CMD16 `SET_BLOCKLEN`** | the block size for every later read (`mmc_set_blocklen`) | one 48-bit command, no data phase — but rung 38 has now already proved the data path works |
| **CMD17/18** | an actual sector read | the next real step after the block size |

**THE CARD IS A WORKING eMMC ON A WORKING DATA PATH.** The one thing between this press and a real
read is an enable bit the arm forgot to set — and the press proved that by delivering the data anyway.

## 9. The goal

**THE GOAL IS NOT MET.** 「把基础驱动跑起来」 took the step it was aimed at: **the first data phase in
this ladder completed** — 512 bytes came off the card into a fresh buffer, CMD8's R1 is in the
register, and the controller's PIO path ran 128 word reads. But **no block read has been made, no
filesystem is reached and no mount is made**, so 「让os可以正常启动并且挂载存储」 is not reached and
**TWRP-TO-STORAGE STAYS WITHHELD**, for 818 §5's reason: the storage it would be written to is the one
thing this ladder has not got working end-to-end. The first clause, 「起码要能进入操作系统」, remains
met and is unchanged by this press (§7).

**AND NOTHING IS ARMED.** No build has been made since this press; `out/` holds the spent rung-38
park; no arm is armed and no press is owed or authorized.