# 827 — the rung-39 press: the repair worked, and it exposed the defect it was hiding

**A PRESS, made autonomously under the standing instruction 「以后不要再问我 直接自动继续，直到os能正常启动」.**
**ONE gate exit 0 / 0 `FAIL`; ONE runner exit 0**; `fastboot boot` only; nothing flashed; nothing
written to storage; no reboot commanded; **`33e80afe` absent from BOTH device lists, hand-checked
immediately before the gate** (adb listed only `4a2fe00b`; `fastboot devices` empty). The phone
returned. Capture `rung39-cmd8-complete-20260930-022901-last_kmsg.txt` **656,290 B `0e797ec1…`**,
gate 52,449 B, run 89,391 B.

## 1. The repair did exactly what it was built to do

| cell | rung 38 (press 37) | **rung 39 (this press)** |
| --- | --- | --- |
| `_ext_ena_wrote` | `0x00000030` | **`0x000f0031`** — bit 0 now set |
| **`_ext_complete`** | `0` | **`1`** |
| **`_ext_timeout`** | `1` | **`0`** |
| `_ext_status_after` | `0x00000020` | **`0x00000001`** = `SDHCI_INT_RESPONSE` |
| `_ext_command_enable` | (absent) | `0x000f0001` — the OR this rung adds |
| `_ext_dma_cleared` | (absent) | `0` — the two DMA enables clear |
| `_ext_timeout_ns` | `0x50` | `0x50` |

The one-OR repair is **confirmed from its other side**: with `SDHCI_INT_RESPONSE` enabled, the poll
breaks on bit 0 (`_ext_status_after = 0x1`), `_ext_complete` reads 1 and the 1.2 s timeout does not
fire. The body's own outcome table's green light is now met.

## 2. And it exposed the defect rung 38's own defect had been hiding

**The data is GONE in this press.** Every card field that was non-zero in the rung-38 press now reads 0:

| cell | rung 38 | rung 39 | reading |
| --- | --- | --- | --- |
| `_ext_words_gated` | `0x80` | **`0`** | **not one of the 128 reads had DATA_AVAILABLE** |
| `_ext_data_avail_seen` | `0x80` | `0` | the block never offered a word |
| `_ext_ps_first` | `0x01f80a06` | **`0x01f80206`** | DOING_READ set, but **no `DATA_AVAILABLE` (`0x800`)** |
| `_ext_sec_count` | `0x01d5a000` | `0` | the capacity word never arrived |
| `_ext_rev` / `_ext_structure` | `7` / `2` | `0` / `0` | — |
| `_ext_int_status_end` | `0x00000020` | `0x00000001` | `INT_DATA_AVAIL` never set |

`_ext_doing_read_seen` is still `0x80` — the block entered `DOING_READ` — but the FIFO is empty and the
loop, which **reads `BUFFER` unconditionally** (a deliberate choice 824's body documents), returns 128
zero words.

## 3. The mechanism — one wait where there should be two

**`st_send_command`'s poll is the wrong green light. It returns on the CMD_IRQ, and the DATA IRQ comes
later.** The two presses are the same body differing only by ONE enable bit, and they prove it:

- **rung 38**: bit 0 disabled → poll waits the full **1.2 s** → *by the time it returns, the data has
  arrived* → `DATA_AVAILABLE` set 128/128, 512 bytes read, `_ext_sec_count = 0x01d5a000`.
- **rung 39**: bit 0 enabled → poll returns **in microseconds**, right after RESPONSE → the loop runs
  **before the card's data arrives** → `DATA_AVAILABLE` set 0/128, the FIFO empty, 128 zero words.

**Rung 38's defect was MASKING this defect.** The very timeout that made `_ext_complete = 0` is what
made the read succeed. This is the `[[mi4-one-value-two-definitions]]` / `[[mi4-a-lower-rungs-side-effect-poisoned-the-rung-above]]`
shape in mirror: here a *higher* rung's defect was the *only reason* the rung's own read worked, and
removing the defect (correctly) unmasked a pre-existing bug one level down — a poll on the command
interrupt used as a signal that data is present, where the vendor's own `sdhci_transfer_pio` waits
inside `while (PRESENT_STATE & SDHCI_DATA_AVAILABLE)`.

## 4. What follows: wait for the data, not the command

The next arm's whole content is **one bounded wait between the command's completion and the read loop**:
poll `PRESENT_STATE`'s `DATA_AVAILABLE` (or `INT_STATUS`'s `SDHCI_INT_DATA_AVAIL`) until it sets, and
only then enter the 128-word loop — which is what the vendor does. The rung-38 body already reads
`PRESENT_STATE` once per iteration; the change is a gate *before* the loop that refuses to read until
the block says a word is ready (and a **bounded** one, so a card that never delivers does not hang).
This is a **time** change, so it needs no new command and no new register.

| candidate | what it settles | cost |
| --- | --- | --- |
| **the data wait** | the 512 bytes with `_ext_complete = 1` **at the same time** | one bounded `PRESENT_STATE` poll, no new register |
| CMD16 `SET_BLOCKLEN` | the block size for every later read | one 48-bit command — but the data path is not yet read correctly |

## 5. Containment, and what the press cost

Storage cells **746 → 748** (2 added: `_ext_command_enable`, `_ext_dma_cleared`), **0 lost**, 748
`xnu_live_storage_*` keys in the log. **Every decision cell is identical across the two boots**:
`_cmd1_resp = 0x40ff8080`, `_nidx_state = 2`, `_csd_structure = 3`, `_csd_mmca_vsn = 4`,
`_sel_state = 3`, `_sel_complete = 1`, `_sta_state = 4`, `_sta_complete = 1`. **`BSD root: md0`** — the
OS came up exactly as 818 measured it. Nothing on the device changed: `fastboot boot` only, no flash,
no storage write.

## 6. The goal

**THE GOAL IS NOT MET.** 「起码要能进入操作系统」 remains met and unchanged. But 「把基础驱动跑起来」
advanced *and* regressed at once: the completion witness is now a reading (`_ext_complete = 1`), but the
512 bytes that a *broken* press delivered no longer arrive, so the net is that the data phase is **not
yet working end to end**. No block read, no filesystem, no mount; **TWRP-TO-STORAGE STAYS WITHHELD**.

**AND NOTHING IS ARMED.** `armed-storage-6a2e94ae` is spent; its park stays as a RECORD, not a queue.
The next arm is the bounded data wait.