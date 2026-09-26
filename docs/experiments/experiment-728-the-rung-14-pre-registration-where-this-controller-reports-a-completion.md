# 728: the rung-14 pre-registration — where this controller reports a completion, and the one place in the source the probe must stand or it never runs

Host-side, before any build or press. This is the pre-registration for the arm that follows the pressed
rung-13 arm `armed-storage-instant-e8dc64f7`, and it is written from 726 section 3's three-part plan with
**one design constraint that 726 could not have had and that rung 13's own log supplies**.

## 1. The constraint: placed after CMD1, this probe would never execute

726 section 3 sketches the rung as "read the registers the completion could be hiding in, write one bit of
`INT_ENABLE` with `SIGNAL_ENABLE` at zero and read `INT_STATUS` immediately, restore `INT_ENABLE`". What it
does not say is **where** in `st_cmd_path` that block stands, and rung 13's own log settles it:

    _cmd_gated = 1        (726 section 2's census)

`st_cmd_path` sets `gated = 1` and **returns before CMD1** when `c0.sent == 0 || c0.complete == 0 ||
c0.err != 0` — and 726 measured `_cmd0_status_any = 0x00000000`, so `c0.complete == 0` and the gate fired.
**Every key rung 13's new cells publish for CMD1 is absent from its log for exactly this reason**
(`zero _cmd1_* keys`). So a rung-14 block placed after the commands — the obvious place, and where 726's
prose reads as if it goes — would sit on a path **this machine has never taken and has no reason to take**,
and the press would buy a log with no rung-14 key in it at all. That is m720's shape (an absent key has
three producers: the branch did not run, the channel refused, the cap dropped it) with the first producer
guaranteed by construction.

**The block therefore stands immediately after CMD0's publishes and before the gate**, where it runs
whenever CMD0 was *sent* — which is the path both of the last two presses took.

## 2. What the rung is, and why it is a measurement and not another attempt

726 refuted all five of 724 section 2's hypotheses and left one: **this controller completes a command and
does not set its interrupt-status register.** The SDHCI specification's normal-interrupt path is
`INT_STATUS → INT_ENABLE → SIGNAL_ENABLE → the line`, and a block implemented as *the status bit latches
only if its enable is set* would produce exactly rung 13's log. The rung has three parts and **none of them
is a new command**:

1. **The elsewhere-reads.** `SLOT_INT_STATUS 0xFC` — the standard file's *other* status register, which the
   vendor reads (`sdhci.h`, `sdhci-msm.c`) and which this ladder has **never read at all**; `CORE_PWRCTL_STATUS
   0xDC` — the vendor-specific path through `core_mem` that has already answered once
   (`_pwr_irq_status32 = 0x02`); `COMMAND 0x0E`; and `PRESENT_STATE 0x24`. A completion recorded in any of
   them is a completion the arm was reading past.
2. **The one-bit enable probe.** Write **one** bit of `INT_ENABLE 0x34` (the `RESPONSE` bit, `0x00000001`)
   with **`SIGNAL_ENABLE 0x38` left at zero**, then read `INT_STATUS 0x30` **immediately**. If the block
   ANDs the two enables — which is what the vendor's own `sdhci_enable_irq`/`sdhci_disable_irq` pair and
   `sdhci.c:295`'s mask compose — the line never rises, the status bit latches, and the run gets the
   answer **and** the log.
3. **The restore, published.** `INT_ENABLE` is written back to 0 and **read back**, so "the gate is back
   where it was" is a cell and not a claim.

## 3. The safety contract

**One store offset, twice, and the whole of this rung's device-write surface.** Rung 12's clause records
that `st_cmd_path` reads the two enable registers and stores to neither — "a store to either of them is the
act that can let the block raise intid 155 and end the run". This rung makes that store, deliberately, and
the argument for it is 726 section 3's, unchanged: the block's `hc_irq` is SPI 123 → intid 155, a line this
image hands to nobody, and **a delivery ends the run at the dispatcher as `_irq_other_count = 1` with
`_irq_other_iar = 155` — an ending this image already reads and survives** (709's press ended exactly that
way on intid 170 and the device came back). So the failure mode is a diagnosis and not a lost device, and
the run is bounded by the payload's own armed watchdog.

**The window in which the line can rise is two device reads wide**, and it is closed by the restore whether
or not the read latched anything. `SIGNAL_ENABLE` stays at zero throughout — that is the half of the pair
this rung does **not** touch, and the reason the hypothesis is testable without putting the line in play.

## 4. The cell table, and the outcomes it must be able to report

| cell | reads | the two answers it separates |
|---|---|---|
| `_int_calls` | — | 1 ⇒ the body ran. Its absence on a log whose `_cmd_sent = 1` is a path this rung did not take (section 1), not a device that said nothing |
| `_int_slot_status` | `hc_mem+0xFC` (halfword) | non-zero ⇒ the completion was recorded in the standard file's **other** status register all along, and rung 11's poll was reading the wrong one |
| `_int_pwrctl_status` | `core_mem+0xDC` | non-zero ⇒ the vendor path answered for this command too; zero ⇒ that path is the power-IRQ's and not the command's |
| `_int_cmd_word` | `hc_mem+0x0E` (halfword) | `0x0000` again ⇒ the block still holds the driver's word at this moment |
| `_int_present` | `hc_mem+0x24` | `CMD_INHIBIT` clear ⇒ the block is idle and free, so nothing is in flight that would explain a missing completion |
| `_int_status_before` | `hc_mem+0x30` | the baseline: 0 again ⇒ nothing latched under the old enable state, which is rung 13's finding reproduced one rung later |
| `_int_enable_before`, `_int_sig_enable` | `hc_mem+0x34`, `0x38` | both 0 ⇒ **the gate this rung writes through was closed, and its closure is a reading and not an assumption** |
| `_int_enable_wrote` | — | `0x00000001` (the `RESPONSE` bit) — the value written, beside the value found |
| **`_int_status_after`** | `hc_mem+0x30` | **the rung's answer.** Non-zero ⇒ the block latches a status bit only when its enable is set, the completion was there to be seen, and the frontier moves from *where does this controller report* to *why the enable was clear* — plus, if the bit set is `RESPONSE`, the card answered CMD0 and rung 11's `_cmd0_resp` of 0 is the cell that was wrong. Zero ⇒ the enable is not the mask, this block's status register is written by something else, and the vendor's second path (above) or the line itself is the remaining candidate |
| `_int_enable_restored`, `_int_enable_readback` | `hc_mem+0x34` | `0` and `0` ⇒ the arm put the gate back. A readback that is not 0 is a run whose *own* record says it left a line enabled, and the next press must not be taken from that arm |
| the ending's own cells | unchanged | `_irq_other_count`, `_irq_other_iar` — if the line rose anyway, this is where it says so, and 155 is the reading that says the two enables are not ANDed |

**Two outcomes are pre-registered as diagnoses rather than failures**: a run that ends at the dispatcher
with `_irq_other_iar = 155` (the enables are not ANDed), and a run whose `_int_status_after` is zero (the
enable is not the mask). **Neither is a lost device**, both are read off this table, and both are strictly
smaller questions than "where does this controller report a completion".

## 5. What the build will refuse, named before it refuses

**The seam constant will move a fourth time.** Every rung since 696 has pushed the entry group past the
next page boundary, and rung 13's addition is another few kilobytes of `.text`; the exit's
`bl FlushPoU_Dcache` sits at `0x800492d8` on the pressed arm. The build's own clause compares the
disassembly against `entry_trace.c`'s `STAGE90_XNU_SEAM_LR` **and** `run_and_capture.sh`'s
`EXIT_POP_LR_LITERAL`, and 725 records that both copies of that constant had to move together. **The
disassembly decides the direction**, and the four other addresses the seam prose pins are re-read in the
same pass.

**And the store census will refuse the first build of this rung**: `st_cmd_path`'s clause asserts *no store
at all*, and rung 14's body must be a function of its own — `noinline, noclone`, like `st_send_command`,
`st_cmd_census` and `st_cmd_path` — precisely so that the new store lands in a window this rung declares
rather than inside one an earlier rung asserted. A body justified as instrumentation is the easiest place
for a store to hide (rung 12's clause says so), and this rung is the first since rung 7 whose declared
store set contains offset `0x34`.

## 6. What the rung is not

* **It is not a new command.** No `ARGUMENT`, no `COMMAND`, no `RESPONSE`, no data-path register, no
  `POWER_CONTROL`, no GCC word, and no byte of the medium. The two commands and the gate between them are
  rung 11's, unchanged.
* **It is not a claim that the completion exists.** `_int_status_after = 0` is a result.
* **It does not make the enable persistent.** The restore is the last act, and the readback is its cell.
* **It does not advance 「把基础驱动跑起来」 by itself** — it moves the frontier one question down the same
  line, which is what this ladder has done at every rung.
