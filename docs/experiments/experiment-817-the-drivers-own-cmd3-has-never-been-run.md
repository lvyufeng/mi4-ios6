# 817 — the driver's own CMD3 has never been run: nine presses on a chain where CMD1 never completed

**A HOST-SIDE READING OF THE ARCHIVE. NOTHING WAS BUILT, NOTHING WAS SENT, NO BYTE UNDER `out/`
MOVED, AND THE ARM IS UNTOUCHED.** Every number below is a cell read out of captures already
committed to this repository. Rung 35 (`armed-storage-47c657af`, switch value 34, CMD9) is **armed
and not pressed** and this step does not go near it.

---

## 1. The live deviation this step is about

The ladder's CMD3 does **not** put the driver's command word on the bus. Measured against the
vendor's own two files:

| | flags (`core.h`) | command word | where |
| --- | --- | --- | --- |
| the driver, `mmc_set_relative_addr` → `mmc_ops.c:204` | `MMC_RSP_R1` = `PRESENT\|CRC\|OPCODE` | **`0x031A`** | `sdhci.c:1140-1143` maps `MMC_RSP_OPCODE` → `SDHCI_CMD_INDEX` |
| the ladder, live from ordinal rung 21 up | `ST_MMC_RSP_R1_NOIDX` = `PRESENT\|CRC` | **`0x030A`** | `entry_storage.c:2252`, guarded `#if STAGE90_XNU_STORAGE_PROBE >= 20` |

`_rca_flags = 0x00000015` / `_rca_word = 0x0000031a` on the one press of the driver's word;
`_nidx_flags = 0x00000005` / `_nidx_word = 0x0000030a` on every press since. The build asserts all
three words at compile time (`entry_storage.c:2391`, `:2402`, `:2415`), so this is not a reordering
nobody looked at: **the ladder permanently removed one bit from the driver's CMD3, and the guard is
`>=`.**

## 2. What that bit costs the reading

`SDHCI_CMD_INDEX` asks the controller to compare the Index field of the response against the command
index and raise `SDHCI_INT_INDEX` (`0x00080000`) when it differs. With the bit set, a completion with
no error means the frame that arrived **was CMD3's response**. With it clear — the live arm — a
completion with no error means only that *a CRC-valid 48-bit frame arrived*.

That is not a footnote. It is precisely the open branch of 813 §5:

> **Whether the card is what rang.** The block reports that a frame arrived; it does not report who
> sent it. The reading does not distinguish a card's answer from any other CRC-valid frame on those
> lines.

**The ladder is running with the one check that would answer that question switched off**, and has
been for fourteen rungs.

## 3. Why it was switched off — and why that reason is an artifact

Three pressed arms, one command:

| arm | capture | word | `_complete` | `_timeout` | `_any_polls` | poll budget | `_inhibit_seen` | `_inhibit_last` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `_rca_*` | rung19-rca-20260926-180922 | `0x031A` (INDEX **on**) | **0** | **1** | **0** | **exhausted** (5,088,000) | **1024/1024** | `0x01f80001` (bit 0 **set**) |
| `_nrsp_*` | rung20-nrsp-20260927-012235 | `0x0300` (no response) | 1 | 0 | `0x21b` = 539 | 4,903 ticks | 538 | `0x01f80000` |
| `_nidx_*` | rung34-cidgate-20260928-144115 | `0x030A` (INDEX off) | 1 | 0 | `0x584` = 1,412 | 11,043 ticks | 0 | `0x01f80000` |

Read alone, that table says: the INDEX bit hangs this controller, and removing it fixed the command.
`_nrsp_*` even rules out the narrow window as the cause of the hang, because that arm ran with the
same narrow window (`_nrsp_ena_wrote = 0x00000001`, `_rca_ena_wrote = 0x00000001`) and completed.

**It is not read alone.** On the `_rca_*` boot, `CMD_INHIBIT` was asserted at the **first** sample and
at all 1,024 of them: the block was already busy when CMD3's window opened. That is what made this
step go and look at what came before CMD3 in the same log — and the answer is §4.

## 4. The measurement: the chain was dead

Every capture in `out/stage90/captures/` that publishes a CMD1 or CMD2 outcome, oldest first:

| capture | date | `_cmd1_complete` | `_cmd1_timeout` | `_cid_complete` | `_cid_timeout` |
| --- | --- | --- | --- | --- | --- |
| rung15-enable | 2026-09-26 10:13 | **0** | **1** | — | — |
| rung17-cid | 2026-09-26 14:43 | **0** | **1** | **0** | **1** |
| rung18-rb | 2026-09-26 15:59 | **0** | **1** | **0** | **1** |
| **rung19-rca** | **2026-09-26 18:09** | **0** | **1** | **0** | **1** |
| **rung20-nrsp** | **2026-09-27 01:22** | **0** | **1** | **0** | **1** |
| rung21-noidx | 2026-09-27 | **0** | **1** | **0** | **1** |
| rung22-dllcensus | 2026-09-27 08:04 | **0** | **1** | **0** | **1** |
| rung24-cmdline | 2026-09-27 09:06 | **0** | **1** | **0** | **1** |
| rung29-2win | 2026-09-27 15:20 | **0** | **1** | 0 | 0 |
| **rung30-c1win** | **2026-09-27 23:45** | **1** | 0 | 0 | 0 |
| rung31-nocrc | 2026-09-28 03:43 | 1 | 0 | 0 | 0 |
| rung32-tout | 2026-09-28 08:13 | 1 | 0 | 0 | 0 |
| **rung33-opcond** | **2026-09-28 12:31** | 1 | 0 | **1** | 0 |
| rung34-cidgate | 2026-09-28 14:41 | 1 | 0 | **1** | 0 |

**Not one press before rung 30 had CMD1 complete.** Nine arms ran with `_cmd1_timeout = 1` and
`_cmd1_any_polls = 0` — the poll burned its **entire ~1.2 s budget** (5,088,000 iterations) with
`INT_STATUS` never latching a single bit of any kind. **CMD2 completed for the first time at rung 33,
two presses ago.**

And the boundary is not unexplained: `_c1_ena_wrote` (the window written to `INT_ENABLE 0x34`, which
the vendor's own header names at `sdhci.h:119`) does not exist before rung 30 and reads
**`0x000f0001`** from rung 30 on — the four command-error bits added to the window. Rung 30's own
capture name is `c1win`.

So the honest reading of §3's table is:

* **On the `_rca_*` boot, CMD1 and CMD2 had both timed out on the full budget before CMD3 ran.** The
  `CMD_INHIBIT` asserted at CMD3's first sample is not a symptom of the INDEX bit; it is the block
  still inhibited by a CMD2 that never finished.
* **`_rca_any_polls = 0` over 5,088,000 polls is an absence with at least two producers** — the
  INDEX bit, and a chain in which the two commands below had already failed. The press does not
  separate them. `[[mi4-silence-is-a-reading-only-if-success-is-silent]]`, at the level of a whole
  command chain.

**The rung-19 press therefore does not establish that the INDEX bit hangs this controller.** And the
arm that adopted `ST_MMC_RSP_R1_NOIDX` — `rung21-noidx`, 2026-09-27 — was pressed on the same dead
chain (`_cmd1_complete = 0`, `_cid_complete = 0`). **The deviation was chosen from a measurement taken
on a chain that was not working, and no press has ever run the driver's CMD3 word on a chain that
does work.**

## 5. What this does not claim

* **It does not claim the INDEX bit is safe.** It claims the existing evidence cannot decide it. The
  hypothesis that a 48-bit `R1` carries no Index field for `SDHCI_CMD_INDEX` to compare — and that a
  controller may therefore never see the comparison succeed — is consistent with a hang and is
  **not** what this measurement shows. `_rca_err = 0` is not evidence against it either: on that arm
  `INT_ENABLE 0x34` held **`0x00000001`**, so the four command-error status bits were not enabled and
  an index error could not have been latched at all.
* **It does not claim the archive before rung 30 is worthless.** It claims that every reading in it
  taken *above CMD1* was taken on a chain in which CMD1 had never completed, and that a decision
  resting on those readings must be re-decided rather than inherited.
* **It does not claim to explain rung 30.** The window widening and the completion appear together in
  the archive; which caused which, and by what mechanism, is not measured here.

## 6. The arm this suggests, pre-registered — and NOT built

**Restore the driver's own word and run it on the chain that works**: `ST_MMC_RSP_R1` (`0x031A`) in
place of `ST_MMC_RSP_R1_NOIDX` (`0x030A`), one bit, with the rung-35+ window (`0x000f0001`) already in
force, so the four error bits are latched. Three outcomes, all of them readings:

| outcome | reading |
| --- | --- |
| completes, `_err = 0` | **the INDEX bit is safe on this controller** and the ladder gets an *attributable* CMD3 response — the frame is CMD3's, which is what 813 §5 could not say |
| completes with `INT_INDEX` (`0x00080000`) set | **the response is not carrying index 3** — the first direct statement the ladder can make about what is on those lines |
| hangs with `CMD_INHIBIT` asserted and no interrupt | **the removal was right and is now measured on a live chain** rather than inherited from a dead one |

**IT IS NOT BUILT HERE, AND THE REASON IS THE PARK.** A one-bit edit to `entry_storage.c` rebuilds the
entry image, which moves `xnu_arm_entry-sources.txt`'s hash of that file — one of the eleven members of
the armed park. Building it would force a withdrawal-and-repark of an arm that is **armed, unspent, and
the operator's to press**, which is the price 814 paid when the arm had *not* been authorised. So this
step is written and left; the arm is the operator's.

## 7. The goal

**THE GOAL IS NOT MET.** No transfer completes, no filesystem is reached and no mount is made, so
**TWRP-to-storage stays withheld**. Rung 35 (`armed-storage-47c657af`) is **armed and not pressed**,
readiness is 5 of 5 exit 0, `make check` is exit 0, and **the press is the operator's.**
