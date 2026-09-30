# 830 — the rung-41 press: the bound was the whole defect, and the data path works

**A PRESS**, made autonomously under the standing instruction. One gate exit 0 / 0 FAIL, one runner
exit 0; `fastboot boot` only, nothing flashed, nothing written to storage; `33e80afe` absent from BOTH
device lists hand-checked immediately before the gate. **The phone returned** (Android up, uptime 1 min).

| | |
| --- | --- |
| arm | `armed-storage-af522aca`, `STAGE90_XNU_STORAGE_PROBE=40` (rung 41) — now SPENT |
| capture | `out/stage90/captures/rung41-extwait-20260930-043427-last_kmsg.txt` 648,135 B `a0c811d6…` |
| gate / run | 53,684 B `b5135977…` / 90,882 B `26933353…` |
| result | **THE CARD'S 512 EXT_CSD BYTES ARRIVED INSIDE THE GATED SECOND PASS** |

---

## 1. The press answered every cell the arm was built to move

The rung-40 press measured the wait fire at 20.03 ms with the block silent
(`_ext_data_wait_timeout = 1`, `_ext_data_gated = 0`). **Raising the one bound to 1.25 s moved
everything:**

| cell | rung-40 press | **rung-41 press** |
| --- | --- | --- |
| `_ext_data_wait_timeout` | 1 | **0** |
| `_ext_data_gated` | 0 | **0x80** (all 128 words) |
| `_ext_data_ticks` | `0x5ddf7` = 20.03 ms | **`0x67ba5` = 424,869 ticks = 22.13 ms** |
| `_ext_sec_count` | 0 | **`0x01d5a000`** (~14.68 GiB) |
| `_ext_rev` | 0 | **7** |
| `_ext_structure` | 0 | **2** |
| `_ext_card_type` | 0 | **0x57** |
| `_ext_complete` | 1 | 1 |
| `_ext_resp` | `0x00000900` | `0x00000900` (CMD8's own R1, state 4 = TRAN) |

**The card's data arrives at ~22.1 ms — just past the 20 ms bound the previous arm chose.** So the
rung-40 press's timeout was not a broken gate, a dead card or a wrong register: it was a bound 2 ms
short. **The class is [[mi4-one-value-two-definitions]] in the form that costs a whole rung** — 20 ms
and 1.25 s are two readings of "how long the transfer takes", and the code was using the smaller while
the hardware needed the larger.

## 2. What the two passes now say together

- **The first pass (rung 38's, unconditional)** reads `_ext_words_gated = 0` and
  `_ext_data_avail_seen = 0`: it ran too early and the FIFO was empty.
- **The second pass (gated on `DATA_AVAILABLE`, bounded by 1.25 s)** reads `_ext_data_gated = 0x80`
  and `_ext_data_wait_timeout = 0`: every word arrived.

`_ext_doing_read_seen = 0x80`, `_ext_int_data_avail = 1`, `_ext_irq_data = 0x0270003a` — the block
entered DOING_READ and raised the data IRQ. `_ext_err = 0`, `_ext_timeout = 0`, `_ext_inhibit_timeout
= 0`. **No hang, no error, no illegal state.**

**AND ONE CELL IS SMALLER THAN ITS NAME.** `_ext_data_waited = 0` is the **last word's** inner-sample
count — the outer loop resets `waited` at each of the 128 words and `ST_LIVE`s it once after the loop,
so it reports the final iteration, not a total. The loop's accumulated wait is `_ext_data_ticks`.
This is a m-class artifact the arm carries rather than a reading it lacks; recorded here so a future
reader does not read `waited = 0` as "no waiting happened".

## 3. Where the ladder stands

The eMMC chain is CMD0 → CMD1 → CMD2 → CMD3 → CMD9 → CMD7 → CMD13 → CMD8 → **CMD16**. Rungs 37–41
walked the card into TRAN (CMD7), read CMD8's 512 EXT_CSD bytes, restored the completion witness,
added the `DATA_AVAILABLE` gate, and fixed its bound. **The data path now works end to end.** The
recommended next rung is **CMD16 `SET_BLOCKLEN`** — the driver's own next command — then sector reads →
filesystem → mount → TWRP-to-storage.

**THE GOAL IS NOT MET.** No sector read, no filesystem, no mount; TWRP-to-storage stays withheld.