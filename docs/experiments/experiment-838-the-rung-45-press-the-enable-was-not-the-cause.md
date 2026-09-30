# 838 — the rung-45 press: the enable was not the cause, and the data state machine is stuck

**A PRESS**, made under the standing instruction. One gate exit 0, one runner exit 0; `fastboot boot`
only, nothing flashed, nothing written to storage; `33e80afe` absent from BOTH device lists hand-checked
immediately before the gate. **The phone returned** (adb up afterwards, uptime 1 min).

| | |
| --- | --- |
| arm | `armed-storage-f8d91170`, `STAGE90_XNU_STORAGE_PROBE=44` (rung 45) — now SPENT |
| capture | `out/stage90/captures/rung45-dataend-20260930-090017-last_kmsg.txt` 650,634 B `94d107a1…` |
| result | **THE HYPOTHESIS IS REFUTED. The missing `DATA_END` enable was NOT the cause; the block's data state machine is genuinely stuck.** |

---

## 1. The enable was ACCEPTED, and the latch still did not appear

The arm's own precondition is met — the store took and read back equal:

| cell | reading | means |
| --- | --- | --- |
| `_de_ena_before` | `0x00008000` | the window's `INT_ENABLE` after the chain (bit 15 is the hardware's own) |
| `_de_ena_wrote` | `0x00008002` | `before \| SDHCI_INT_DATA_END` — bit 1 added |
| **`_de_ena_held`** | **`0x00008002`** | **THE ENABLE WAS ACCEPTED** — equal to what was written, so the store was not dropped |

**And then, with bit 1 set, the completion did not latch:**

| cell | reading | means |
| --- | --- | --- |
| `_de_int_status` | **`0x00000000`** | no `INT_STATUS` bit at all after the enable |
| **`_de_data_end`** | **`0`** | **THE HYPOTHESIS REFUTED** — `DATA_END` did not latch |
| `_de_data_avail` | `0` | no data pending either |

If the missing enable had been the cause, bit 1 would have read 1 here — the latch is a level the block
asserts at a transfer's own end, and this reading is taken on the state rung 44 left behind with **no new
transfer**. It read 0. So **the enable was NOT the explanation**, and that refutation is the arm's
deliberate worth: the class rung 38's `INT_RESPONSE` repair closed on the **command** side is now
measured **NOT** to recur on the data side — the two are different defects.

## 2. The line and the machine are still held

The reading that no enable can gate points the same way:

| cell | reading | means |
| --- | --- | --- |
| `_de_ps` / `_de_ps_end` | `0x01f80206` / `0x01f80206` | unchanged before and after the store |
| **`_de_dat_line`** | **`1`** | `DAT_LINE_ACTIVE` — the line's own **level**, still active |
| **`_de_data_inhibit`** | **`1`** | `DATA_INHIBIT` still set |
| `_de_doing_read` | `1` | the block believes a read is in progress |

So the block's **data state machine is genuinely stuck** — not merely masked. `PRESENT_STATE` is the same
`0x01f80206` the rung-44 press measured at three earlier points, now a **fourth** sampling in a new boot.

## 3. The guard still refuses, because the line is still held

The chain above this rung behaved exactly as 836 left it — the widened mask waits, the line never clears,
and CMD17 is withheld:

| cell | reading | means |
| --- | --- | --- |
| `_rd_inhibit_mask` | `0x3` | the widened guard, still in the bin |
| `_rd_inhibit_dat` | `0x2` | `DATA_INHIBIT` still set at CMD17's entry |
| `_rd_inhibit_timeout` | `1` | the mask never cleared |
| `_rd_sent` | `0` | **CMD17 still not issued** — the guard is still doing its job |
| `_rd_int_status_end` | `0x21` | CMD8's RESPONSE \| DATA_AVAIL, the stuck transfer's own trace |

**So the three presses read as one story:** 834 named the guard (the widened mask untried); 836 tried it,
it refused, and moved the defect one command down; **838 tested the press's own candidate (a) and REFUTED
it** — the completion was not merely masked. **The frontier is now rung 46: the block's data state machine
must be RESET** (`SOFTWARE_RESET 0x2F`, `SDHCI_RESET_DATA` 0x04) — the rung-44 press's **candidate (b)**,
now the only one left by elimination.

## 4. Where the ladder stands

The chain is CMD0 → … → CMD16 → **CMD17**. The command path works end to end; the data path is stuck at
the block, and **the stuck state is not a switched-off witness — it is a held data state machine.** No
firer is armed and no press is owed: the next arm (rung 46) is not yet built, and the press is the
operator's. **THE GOAL IS NOT MET:** no sector read, no partition table walked, no filesystem, no mount;
TWRP-to-storage stays withheld.