# 712: the rung-9 pre-registration — the driver's own completion, and the one statement `sdhci_set_power` makes after rung 7's byte

711 pressed rung 8 and answered two questions, one of them the rung's own. The rung's: **the line has
an owner** — `_irq_other_count` is absent from the log, the third client is intid 170 with
`st_pwr_irq`'s address in `_irq_cli_handler`, and the acknowledge cleared the latch
(`_pwr_irq_status_after = 0x00`, the client called **once**). The one it did not ask: **the client's
single call came *after* the probe's own tail** (`_pwr_irq_calls = 0x1` beside
`_pwr_irq_calls_probe_end = 0x0`), so the handler ran somewhere between the probe and the 690 ending
and the log does not say when.

Rung 9 is the **next statement of the driver's own path**, and it is the statement 708 deliberately
skipped: `sdhci_set_power`'s own

```c
sdhci_writeb(host, pwr, SDHCI_POWER_CONTROL);          /* sdhci.c:1370 - rung 7's store          */
if (host->ops->check_power_status)
        host->ops->check_power_status(host, REQ_BUS_ON);  /* sdhci.c:1371-1372 - THE NEXT LINE  */
```

708's record says why the wait was not taken: `sdhci_msm_check_power_status` (`sdhci-msm.c:2179`)
takes its `done` from a completion that only the threaded power IRQ completes, and at rung 7 this image
delivered no IRQ at all. **Rung 8 delivered it. Rung 9 takes the wait.**

## 1. The vendor's function, read exactly

```c
static void sdhci_msm_check_power_status(struct sdhci_host *host, u32 req_type)
{
        ...
        if ((req_type & msm_host->curr_pwr_state) ||
                        (req_type & msm_host->curr_io_level))
                done = true;
        /* ... init_completion so that a completion raised BEFORE this call does not
         *     let the next wait return immediately ... */
        if (done)
                init_completion(&msm_host->pwr_irq_completion);
        else
                wait_for_completion(&msm_host->pwr_irq_completion);
}
```

Three things are load-bearing and none of them is obvious from the call site:

1. **The request is not written to the controller here.** `req_type` is compared against **two
   driver-side fields** (`msm_host->curr_pwr_state`, `msm_host->curr_io_level`, `:327`'s same struct),
   and those are written by the *handler's tail* (`:2092-2096`):
   ```c
   if (pwr_state)  msm_host->curr_pwr_state  = pwr_state;
   if (io_level)   msm_host->curr_io_level   = io_level;
   complete(&msm_host->pwr_irq_completion);
   ```
   So `check_power_status` is a **cache read plus a block**, and the cache is filled by the interrupt.
   The controller's own request was made by the byte (rung 7) and answered by the handler (rung 8); this
   function only *waits* for that to have happened.
2. **`curr_pwr_state`/`curr_io_level` are set from the decode's locals, and for a `BUS_ON` status the
   decode sets *both*** (`:2023-2029`): `pwr_state = REQ_BUS_ON`, `io_level = REQ_IO_HIGH`. The
   predicate `req_type & curr_io_level` with `req_type = REQ_BUS_ON` is therefore *also* satisfied by
   the io level on this path — two independent bits of the same predicate, which is why the cell table
   below publishes them separately.
3. **On the first call the predicate is false and the vendor blocks.** A fresh host has both fields
   0, the handler has not run, `done = false`, and `wait_for_completion` blocks until the handler's
   `complete()`. That is the vendor's own behaviour on this exact path, and it is *why* rung 7 could
   not take it: 709's press is the measurement that the line it would block on had no owner.

`REQ_*` (`sdhci.h:290-294`): `REQ_BUS_OFF (1<<0)`, **`REQ_BUS_ON (1<<1)`**, `REQ_IO_LOW (1<<2)`,
`REQ_IO_HIGH (1<<3)`.

**And it is called exactly once in `mmc_power_up`.** PASS A reaches it at `:1372`; PASS B
(`MMC_POWER_ON`) calls `sdhci_set_power` a second time and returns at `:1352`
(`if (host->pwr == pwr) return -1;`) before any check, so no second wait. 708 measured the same fact
from the store side: rung 7's log reads `_pwr_before = 0x00 → _pwr_after = 0x0B`, **one** store, which
is also the reading that `SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER` is not set on this host (it is set
nowhere in `sdhci-msm.c`; the only power quirk there is `SDHCI_QUIRK_SINGLE_POWER_WRITE`, `:2897`).

## 2. The rung, and the twenty cells it pre-registers

Rung 9 = rung 8 **plus** the vendor's own completion, taken as far as this image can take it:

* **the handler gains the vendor's tail** (`:2092-2096`), written into **this image's own words** —
  `g_pwr_curr_state`, `g_pwr_curr_io_level` (the values the vendor's two struct fields would hold, from
  the vendor's own decode) and `g_pwr_irq_done`, the completion's stand-in. Rung 8's record refused to
  touch those fields "as a cache (a field of a `struct sdhci_msm_host` this image does not have - a
  cell computed here would be the driver's name on a second definition)"; **rung 9 changes that
  position for a reason that is not a change of mind**: what rung 8 refused was publishing a *cell*
  under the driver's name. Rung 9 ports the *transition* the vendor's code performs, under this image's
  own names, and the build clause that reads the handler's body is what keeps the port from being a
  claim.
* **`st_pwr_wait` is the vendor's own check**, with the vendor's `done` predicate evaluated first —
  the `init_completion` branch is *taken*, not skipped, because on that branch the vendor does not wait
  and the arm must not either — and the block replaced by a **bounded tick poll**
  (`stage90_cntvct_read()`, the same sampler rung 4's reset poll uses). **The poll's end condition is
  the controller's own `CORE_PWRCTL_CTL` bit `BUS_SUCCESS`** — the device-side signature the handler
  writes at `:2069` — and not the image-side flag, and the reason is a defect this project names rather
  than meets: **the handler may run with the caches on and the probe runs with `SCTLR.C` clear**
  (`xnu_live_seam_sctlr = 0x30c57879`), so a `g_pwr_irq_done` written by the handler can sit in L1/L2
  while the probe's direct read of the same address sees DRAM — one flag with two definitions
  ([[mi4-one-value-two-definitions]]), and a poll armed with it would time out on a machine where the
  handler had already run. `CTL` is the same event *through the device*: it is Strongly-ordered, the
  probe already read it at `0` on rung 7's press (`_pwr_ctl`), the handler read `ctl_before = 0` and
  wrote `0x01` on rung 8's, and no other arm of this ladder writes it. **The image-side flag is still
  written and still published** (`_wait_done`), because the *disagreement* between the two is the
  reading: a device ack with `_wait_done = 0` is a driver-side completion that did not cross the cache
  boundary, which is exactly what a real driver's `complete()` would have to survive.
* **`__attribute__((noinline))`, and it is the mirror of rung 6's `always_inline`.** Rung 8's lesson
  was that a helper the build cannot see into hides its stores, so the helpers that write device
  registers are inlined into the probe's body. This function's device access is a **read** (which no
  store census covers) and what has to be checked about it is *its own bounded shape* — the budget
  constant, the flag it reads and the fact that the probe calls it once. A body whose shape the clause
  must read has to be a body.

| # | key | pre-registered | what it decides |
| --- | --- | --- | --- |
| 1 | `_wait_req` | `0x02` (`REQ_BUS_ON`) | the request the driver makes |
| 2 | `_wait_bound` | `0x001d4c00` (1,920,000 ticks = 100 ms at 19.2 MHz) | the arm names its own budget in the log |
| 3 | `_wait_state_before` | **`0`** — a fresh host's `curr_pwr_state` | the cache is empty, so this is the blocking branch |
| 4 | `_wait_io_before` | **`0`** | the same, on the predicate's other half |
| 5 | **`_wait_done_before`** | **`0`** — the vendor's own `done` | `0` = the vendor would have **blocked** (`:2205`); `1` = the interrupt beat the check and the vendor resets the completion instead |
| 6 | `_wait_reset` | `0` (1 iff cell 5 is 1) | the `init_completion` analogue, taken only on the vendor's own branch |
| 7 | `_wait_ctl_before` | **`0`** — the controller has not been acknowledged yet | the poll's own baseline, and the cell that makes the edge real |
| 8 | **`_wait_ctl_after`** | **`0x01`** (`BUS_SUCCESS`) on the success path | **the poll's end condition**: the handler's device-side signature, read through the device so no cache can hide it |
| 9 | `_wait_polls` | > 0 | the poll ran |
| 10 | `_wait_ticks` | ≤ the bound | **the number the vendor's unbounded wait would have blocked for**, if cell 11 is 0 |
| 11 | `_wait_timeout` | `0` on the success path | `1` = the budget expired with the ack unset |
| 12 | **`_wait_done`** | `1` | the **image-side** flag after the poll. **The pair (8, 12) is the new cell**: `8=1` with `12=0` is a completion whose driver-side half did not cross the cache boundary — one event, two memories |
| 13 | `_wait_cpsr` | I bit **clear** | **the cell that tells the two timeout causes apart**: `I` set means this site cannot see the handler at all, whatever the budget |
| 14 | `_wait_state_after` | `REQ_BUS_ON` (`0x02`) on the success path | the vendor's field, as this arm's word |
| 15 | `_wait_io_after` | `REQ_IO_HIGH` (`0x08`) | the second half of the predicate, filled by the same decode |
| 16 | `_wait_cc_before` / 17 `_wait_cc_after` | — | `SDHCI_CLOCK_CONTROL 0x2C` read either side of the wait: **settles 711 section 3**, where the same register read `0xE045` on one boot and `0xE047` on the next across a byte the two arms run identically |
| 18 | `_pwr_irq_state` | `REQ_BUS_ON` (`0x02`) | the handler's `pwr_state` local, published where the vendor assigns it |
| 19 | `_pwr_irq_io` | `REQ_IO_HIGH` (`0x08`) | the handler's `io_level` local — rung 8 published this as `_pwr_irq_io_level`; rung 9 publishes the pair, and the older key is **kept** (`_pwr_irq_io_level` = `_pwr_irq_io`) so 709's and 711's cells stay comparable |
| 20 | `_pwr_irq_at` | — | CNTVCT on the handler's **entry**: `_pwr_irq_at - _wait_t0` is **the interrupt's latency from the byte**, measured rather than assumed, and it is the number that tells a budget-expired press what budget to use next |
| 21 | `_wait_t0` | — | CNTVCT immediately before the wait, so cell 20 has a base even when the wait times out |
| 22 | `_wait_calls` | `_pwr_irq_calls` at the wait's end | the client's **entry** count beside its device-side ack (cell 8) — the vendor's `complete()` is at the handler's tail, so the two are different readings of one event and the pair says whether an entry ever failed to complete |

**Four outcomes, each a different line in the log, and the first is the one the rung is for.**

1. **the completion arrives inside the wait** → `_wait_ctl_after = 0x01`, `_wait_timeout = 0`,
   `_wait_ticks` small, `_wait_state_after = 0x02`. **The driver's power handshake completes, on
   hardware, inside this image's own probe** — with `_wait_ticks` as the number the vendor's
   `wait_for_completion` would have blocked for, and `_pwr_irq_at - _wait_t0` as the latency that made
   it that number. This is the first rung at which the path `sdhci_set_power` → controller → IRQ →
   handler → ack runs end to end inside one image.
2. **the ack arrives and the flag does not** (`_wait_ctl_after = 1`, `_wait_done = 0`) — the outcome the
   two-memory design exists to be able to read. The handler ran inside the wait (its ack is in the
   device, `_wait_calls` says so) and its `.bss` write is not visible to the probe's direct read. That
   is a reading about **this image's contexts**, not about the device: at the moment the probe runs,
   `SCTLR.C` is clear, and the vendor's `complete()` — a normal-memory write — would have to be cleaned
   to the PoC to be seen from there. It is the same shape as 686's correction of 652 (a pair of reads
   that cannot see the copy that answers the reader).
3. **the budget expires** → `_wait_timeout = 1`, and **cell 13 decides what it means**: `I` clear is a
   statement about the device and the line (the next press raises `_wait_bound` and the answer is cell
   20); **`I` set is a statement about the site** — the probe runs inside Apple's cache-off idle-exit
   window, and if that window also masks IRQs then no budget can ever see the handler from here. The
   two are **not** the same reading, which is why the budget is a switch rather than a constant.
4. **`_wait_done_before = 1`** (the interrupt beat the check) → cells 5 and 6 are 1, the vendor takes
   `init_completion`, and the wait does not block at all. A reading in its own right: it would say the
   line was taken between the byte and the next statement, i.e. interrupts are on at this site and the
   latency is under two instructions.

## 3. The placement, and why rung 9's block is not at the end

Every rung of this ladder appends to the probe. Rung 9 appends to the **statement order inside the
power act**: the wait runs **immediately after `st_power_set()`** and **before** rung 8's own tail
count. The reason is `sdhci.c:1370-1372` — the byte and the check are adjacent statements of one
function, so an arm that ran the wait anywhere else (at the probe's tail, say) would be measuring its
own order rather than the driver's. The consequence for rung 8's cells is deliberate and small:
`_pwr_irq_calls_probe_end` still means "at the probe's own tail", and now the probe's tail is *after*
the wait, so the pair (`_pwr_irq_calls` in the handler, `_calls_probe_end` at the tail) reads the
same way it did and gains a second reading next to it (`_wait_calls`, at the wait's end). The order in
the probe's body becomes: rung 6's clock set, rung 8's registration and arming, rung 7's byte,
**rung 9's wait**, rung 8's tail count.

## 4. What rung 9 does not do

* **It does not run the three arms that may sleep** (`sdhci_msm_setup_vreg`, `setup_pins`,
  `set_vdd_io_vol`). The vendor declares them threaded (`devm_request_threaded_irq(..., NULL, ...,
  IRQF_ONESHOT)`, `:2937-2939`) precisely because they may sleep, and this image's client runs in
  `fleh_irq_kernel`'s frame. 711's log is the measurement that the decode's `ack` is correct without
  them (`_pwr_irq_ack = 0x01` with no vreg call). The wait is what the *driver* does around them; it is
  not a substitute for them, and a bus that is on without a rail is still a bus without a rail — the
  card's rail stays a separate owed item.
* **It does not mask the power events** (`CORE_PWRCTL_MASK`, `INT_MASK = 0xF` at `:2946`). 709 left
  that as the smaller alternative; rung 8 took the other one and rung 9 depends on the line staying
  unmasked, because the completion is delivered *by* the handler.
* **It does not touch `hc_irq`** (SPI 123 = intid 155). It stays unowned, so a delivery on it is still
  a stop rather than a storm.
* **It does not write `CORE_PWRCTL_CTL`.** The request is the byte's own consequence (709) and the ack
  is the handler's (710) — this function, as the vendor wrote it, has **no device store at all**, and
  the one it would take on the `done` branch is an image-side `init_completion`. That is the whole of
  its device-facing shape: two reads of `CLOCK_CONTROL 0x2C` and nothing else.
* **It does not re-derive the byte, the clock or the mode.** Rung 9's only new device accesses are the
  two reads above.

## 5. The build clauses rung 9 needs

1. **the ladder opens to 9** (`case` and the `#error` text), and the text says what rung 9 is and what
   it is not, so a `-D` of 9 in a tree whose source has no rung 9 still reaches the preprocessor as a
   refusal.
2. **`STAGE90_XNU_PWR_WAIT_TICKS` is a range-checked switch with its value asserted in the image**:
   default `1920000` (100 ms at the 19,200,000 Hz 699's ending read out of `cntfrq`, the same budget
   rung 4's reset poll carries, and the same number `sdhci.c:251`'s "Wait max 100 ms" bounds the
   driver's own reset poll with), `#error` outside `[1, 19200000]` — **0 is refused rather than read as
   "no wait"**, because the no-wait arm is rung 8 and a budget of 0 would make this rung's own cells
   indistinguishable from the rung below it; above 1 s the press is spending the run's own 6,000 ms
   ending clock. And **the clause refuses the build when the `movw`/`movt` pair for that value is
   absent from `st_pwr_wait`'s body** — the m720 shape, a switch no build reads, refused at the one
   place it can be.
3. **`st_pwr_wait`'s own body is classified**, the way rung 8's handler is and for the same reason
   (the probe's clause disassembles `entry_storage_probe` and this is its own symbol): its window ends
   at `nm -S`'s size rather than `next_global`; exactly **one** `bl <st_pwr_wait>` occurs in
   `entry_storage_probe`'s body; its device accesses, computed as **absolute addresses** (the rung-8
   clause's rule, because GCC folds offsets against whichever base is in a register), are
   `0xf982492c` read as a **halfword twice** (`CLOCK_CONTROL 0x2C`) and **`0xf98240e8` read as a byte**
   (the poll's `CORE_PWRCTL_CTL`, at the width the vendor's `readb_relaxed` uses) and **nothing else**;
   and its non-device accesses are the shared words, the counter and the CPSR read.
4. **the rung-8 clause's inline-memory assertion is widened, and with a reason rather than a number.**
   It currently says the handler's non-device accesses are *exactly* `IMG:ldr IMG:str` — the call
   counter, read and incremented. Rung 9's handler writes three more image-side words on the vendor's
   own lines. The clause must name **which** four words and where, because the property it protects is
   not "there are two" but "there is no device access hiding in the class the clause waives" (m704's
   false-negative direction): the check is that every one of the widened set is a symbol **in this
   image's own `.bss`**, resolved by address, and that no `UNK`/`DEVLO` entry appears.
5. **the handler's device accesses do not move**: `0xf98240dc`, `0xf98240e4`, `0xf98240e8`,
   `0xf9824a0c` at the widths 711 measured, with their counts at the same minimums. A rung that adds a
   tail to a handler must not add a device access to it, and that is a property of the linked image.

**The clause that must not be forgotten, and it is the one thing this pre-registration changes about an
existing check**: clause 4 above. `build_entry.sh`'s rung-8 clause asserts the handler's non-device
accesses *exactly*, so rung 9's source will **refuse the rung-8 clause** — the build being right, and
the refusal is the clause doing its job. The clause is widened in the same step as the source, and the
widening is what the record's cell 16-18 are read against.

## 6. Owed by rung 9, and what it does not answer

* **the rail.** Nothing in this image powers a supply; the bus going on is the controller's own
  sequence, and a card that needs 3.3 V from a PMIC this image does not talk to will still not answer.
* **the rate.** 705 measured the register file at `gpll4`/div 4 (192 MHz) while the driver believes
  `clk_rate = get_min_clock()` and skips the RCG write (706 section 2) — so the card clock is 480x
  what the driver thinks. A command issued at that belief is a command at the wrong bit rate.
* **`CORE_VENDOR_SPEC`'s MCLK field at the vendor's own address.** 710 section 6 named this the next
  rung after 8, on the assumption it would be the cheapest thing to do; 711 section 2.4 measured that
  the field's readback is a *clock* question (the vendor's address holds `0xa1c`, and act 6's RMW wrote
  what it read) and **this pre-registration moves it to the rung that re-enters
  `sdhci_msm_set_clock`**, where the same function writes it, rather than spending a press on it
  alone: the power path is the path in progress and the completion is its next statement.
* **The command path** (`sdhci_send_command`), which is where "the card answers" begins.
* Carried unchanged from 711 section 6: the width clause of the store census never fired on a WIDENED
  DEVICE store; the ending's first store faulting (`0x0fa0065c`, still unread); the 691 §5
  `entry_note_wfi` readback; `entry_reset.h`'s false IMEM claim; 676 §6 / 677 §6; the 684-owed runner
  clause; `tools/xnu_dt_requirements.py` and the `"master"` value; the two peer-lane tripwire repairs;
  BIT(29) of `_clk_ahb_cbcr` published and unnamed; the seam address pinned in two files (rung 9's
  growth may move it, in which case the build refuses and both files are re-pinned together); and the
  gate's narration of `STAGE90_XNU_STORAGE_PROBE`, now **seven rungs short (2 through 8)** — peer lane,
  by message and never by edit.
