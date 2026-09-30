# 832 — the rung-42 press: CMD16 `SET_BLOCKLEN` landed, and the data path is open

**A PRESS**, made autonomously under the standing instruction. One gate exit 0 / 0 FAIL, one runner
exit 0; `fastboot boot` only, nothing flashed, nothing written to storage; `33e80afe` absent from BOTH
device lists hand-checked immediately before the gate. **The phone returned** (Android up, uptime
1 min).

| | |
| --- | --- |
| arm | `armed-storage-3c6803f6`, `STAGE90_XNU_STORAGE_PROBE=41` (rung 42) — now SPENT |
| capture | `out/stage90/captures/rung42-cmd16-20260930-0530-last_kmsg.txt` 649,913 B `b7d80ca9…` |
| result | **CMD16 LANDED — EVERY CELL THE ARM WAS BUILT TO MOVE MOVED, AND NO CELL DID NOT** |

---

## 1. The press answered every cell the arm was built to read

| cell | reading | means |
| --- | --- | --- |
| `_blk_complete` | **1** | the command completed |
| `_blk_err` | **0** | no controller error |
| `_blk_timeout` | **0** | the bound did not fire |
| `_blk_inhibit_timeout` | **0** | the controller came idle |
| `_blk_illegal` | **0** | **the card did NOT refuse `SET_BLOCKLEN`** |
| `_blk_switch_err` | **0** | no switch error |
| `_blk_state` | **4** | `R1_STATE_TRAN` — the card is still in TRAN |
| `_blk_resp` | **`0x00000900`** | R1: `R1_READY_FOR_DATA` (bit 8) set, state TRAN |
| `_blk_sent` / `_blk_calls` | 1 / 1 | sent once, called once |
| `_blk_word`, `_blk_word_read`, `_blk_word_wrote` | **`0x101A`** each | the driver's own word went on the bus, the block held it, and it was read back |
| `_blk_arg`, `_blk_arg_wrote` | **`0x200`** = 512 each | the length the driver assigned went out, and the block held it |
| `_blk_arg_is_blk` | **1** | the argument written equals `ST_MMC_BLK_LEN` |

**So `_blk_complete = 1` with `_blk_err = 0` and `_blk_state = 4` is CMD16 LANDING** — the row the arm's
own pre-registration named as "the frontier is the FIRST SECTOR READ".

## 2. The two cells that made it a reading rather than a comparison, and how they came back

The arm's own pre/post pair was built to be informative on the rung-37 pattern (m823), where
`_sta_state_held` read 0 against a pre-registered 1 because **the field is sampled at response time**:

| cell | reading |
| --- | --- |
| `_blk_pre_state` | **4** (TRAN, from `RESPONSE` before the command) |
| `_blk_pre_ready` | 1 |
| `_blk_state_held` | **1** |

Here the two agree — CMD16 read a field (state TRAN) that **it did not change**, which is exactly
right for a command whose whole content is a length and which is forbidden from moving the card's
state. `_blk_resp_moved = 0` (`_blk_resp_pre = _blk_resp_post = 0x00000900`) says the `RESPONSE`
register was not rewritten by this command — **a 48-bit response moves `RESPONSE + 0` alone**
(mo823's negative, measured twice before), and this reading is consistent with it.

## 3. The whole chain in one boot, and the run's own ending

The press also re-read every rung below it, unchanged, in the same log: `_sel_resp = 0x00000700`
(CMD7's R1), `_sta_state = 4` (CMD13), `_ext_resp = 0x00000900` (CMD8's R1), and the card's own fields
back where the rung-41 press left them — `_ext_data_gated = 0x80` (all 128 words),
`_ext_data_wait_timeout = 0`, `_ext_sec_count = 0x01d5a000` (~14.68 GiB), `_ext_rev = 7`,
`_ext_structure = 2`. **The chain CMD0 → … → CMD16 ran end to end in one boot with no fault** (0
`unable to handle` / `Kernel panic` / `exception:` lines) and the run ended on its own clock
(`_post_end_calls = 8`, last `_post_elapsed = 0x09200844`).

## 4. Where the ladder stands

**The data path is open.** The card is in TRAN, has been told a block is 512 bytes, and the controller
has read 512 bytes from it. The frontier is **the first sector read — CMD17 `READ_SINGLE_BLOCK` at LBA
1**, where the reading is self-verifying: the eight bytes `EFI PART` (a GPT) or an MBR ending `55 aa`.
Then filesystem → mount → TWRP-to-storage.

**THE GOAL IS NOT MET.** No sector read, no filesystem, no mount; TWRP-to-storage stays withheld.