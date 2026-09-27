# 753: the key read after the power byte reads `BUS_ON`, and 748 §4 named a different key — one register, two keys, two moments

**Read from the archived captures and the payload source. No device action of any kind** — no `fastboot`,
no `adb` to the device, no press, no gate, no runner, no build, and `out/` untouched.

**The one-line finding.** 748 §4 dismissed `CORE_PWRCTL_STATUS 0xDC` with the sentence *"the archive has
read it after the power byte on fifteen boots, all zero"* — and the **count is right** and the **key is
right** (`_reg_pwrctl_status_after`, fifteen captures) but the **moment is not**: that key is emitted at
the end of the **reset** stage, `entry_storage.c:784` inside `st_driver_reset` (`:719`), and the probe
runs `st_driver_reset()` **first**, before `st_pwr_irq_arm()`, `st_power_set()` and `st_pwr_wait()`. The
key that *is* read either side of the power byte is a **different** one, `_pwr_status_after`
(`:1388`) — and it reads **`0x00000002`**, `CORE_PWRCTL_BUS_ON`, on **all twelve** captures that write
the byte, against `0x00000000` before it. **So the register 748 called *informative and the information
says nothing pending* is, at the moment 748 meant, reporting the ladder's own power request as latched**
— and the archive then records the block's handler acknowledging exactly that.

## 0. Why this was read out of a capture rather than spent on a press

It is one register, one store and two keys, all three of which the archive already holds on a dozen
boots. **A press would re-measure a number that is already in hand twelve times**, and the press the
ladder is actually waiting on (rung 21, `armed-storage-46fe6737`) is where the next *new* reading lives.
This is the same economy 749 and 750 were written under.

## 1. The two keys, and the two moments — from the source, not from a remembered paragraph

Both keys read the **same address**, `ST_CORE_MEM_BASE + ST_CORE_PWRCTL_STATUS`, and they are emitted
from **different functions** with **different rung guards**:

    entry_storage.c:719   static void st_driver_reset(void)          /* #if ... PROBE >= 4 */
    entry_storage.c:784       ST_LIVE("xnu_live_storage_reg_pwrctl_status_after",
                                      st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_STATUS));

    entry_storage.c:1374  ST_LIVE("xnu_live_storage_pwr_status_before", ... /* rung 7 */ ...);
    entry_storage.c:1384      st_write8(ST_HC_MEM_BASE + ST_SDHCI_POWER_CONTROL, (uint8_t)(pwr | ST_SDHCI_POWER_ON));
    entry_storage.c:1388  ST_LIVE("xnu_live_storage_pwr_status_after",  ... /* the same address */ ...);

**And the probe calls the reset stage first.** `entry_storage_probe`, in file order:

    :4063  st_driver_reset();      /* >= 4  */
    :4076  st_clock_census();      /* >= 5  */
    :4089  st_clock_set();         /* >= 6  */
    :4100  st_pwr_irq_arm();       /* >= 8  */
    :4113  st_power_set();         /* >= 7  */
    :4127  st_pwr_wait();          /* >= 9  */
    :4176  st_cmd_path();          /* >= 11 */
    :4186  st_pwr_irq_after();     /* >= 8  */

so `_reg_pwrctl_status_after` is read **before the power byte is written, before the client is
registered, and before the wait exists** — on every rung, by construction, not by accident.

**The source is not ambiguous about this.** `st_driver_reset`'s own comment at `:711` sits under the
reset's before-values and says so:

> `_reg_pwrctl_mask` reads `0x0000000f` - four bits of power IRQ routed, which is what makes
> **`_reg_pwrctl_status_after` below** a reading and not a formality

"The reset may latch a power-IRQ status when the previous state was `BUS_ON`" — i.e. the key is a
question about the **reset**. 748 §4 moved it forward to the power byte. **That is the whole defect: a
true cell, read for a moment it is not taken at** ([[mi4-one-value-two-definitions]], and the same shape
as `m732`, where a cell that stands for a register had to be asked *when* it was read).

## 2. What the two keys measure — twelve captures, one value each

    $ for k in pwr_status_before pwr_status_after reg_pwrctl_status_after; do ...; done

| key | moment | captures | distinct values |
| --- | --- | --- | --- |
| `_pwr_status_before` | **before** the `POWER_CONTROL 0x29` store | **12** | **`0x00000000`** |
| `_pwr_status_after`  | **after** that store | **12** | **`0x00000002`** |
| `_reg_pwrctl_status_after` | end of the **reset** stage, i.e. before the store | **15** | **`0x00000000`** |

`0x02` is `ST_CORE_PWRCTL_BUS_ON` (`sdhci-msm.c:62-75`). **So the readable content of this register is
the ladder's own power request, and the archive has it on twelve boots: `0x00 → store → 0x02`.**

The twelve are `rung7`, `rung8`, `rung9` and `rung12` … `rung20` — every storage rung that writes the
power byte, which is rung 7 onward. The three captures without the pair (`rung6`, `701`, `705`) are
rungs that do not write it, and at `rung6` the `_reg_pwrctl_status_after` zero is trivially pre-byte.

**Note what the fifteen-versus-twelve difference is.** 748 §4's count of fifteen is `_reg_pwrctl_status_after`
from `rung6` through `rung20`, and it is exact. The document read the right key; what it got wrong is the
moment. **A count can be right and the reading still be of the wrong instant**, which is why the moment is
the thing §1 had to settle from the source.

## 3. The block then answers, and the handler's own cells are the acknowledgement

The rung-8 client (`st_pwr_irq`, `:1508`) is entered on the power line and publishes its whole decode.
Across the **eleven** captures carrying it — `rung8`, `rung9`, `rung12` … `rung20`:

| key | captures | distinct values | what it is |
| --- | --- | --- | --- |
| `_pwr_irq_status` | **11** | **`0x00000002`** | the block's `BUS_ON`, read as a byte |
| `_pwr_irq_status32` | **11** | **`0x00000002`** | the same moment, read as a word |
| `_pwr_irq_ctl_before` | **11** | **`0x00000000`** | `CORE_PWRCTL_CTL`, before the ack |
| `_pwr_irq_ctl_after` | **11** | **`0x00000001`** | the same, after the ack of `BUS_SUCCESS` |
| `_pwr_irq_calls_probe_end` | **11** | `0x00000000` (×2), `0x00000001` (×9) | whether the client ran inside the probe |

**Two readings at two widths agreeing on eleven boots is the rung's own self-check and it passes** —
`readb_relaxed` and `readl_relaxed` are the same quantity here, which is what §1's comment claimed and
what the archive now confirms. And `_ctl_before = 0` → `_ctl_after = 1` is the vendor's own sequence
read either side of the ack store at `:1573-1576`: the request is unacknowledged, then acknowledged.

**So the power path of this block is exercised, latched, delivered and acknowledged on nine consecutive
rungs.** Whatever else is true of CMD1 and CMD2, it is not that the ladder left a power request
outstanding.

## 4. The wait's own view, and the one rung that disagreed

`st_pwr_wait` (`:1757`, rung 9+) is the ladder's own reader of the same handshake. Across the **ten**
captures carrying it:

| key | captures | distinct values |
| --- | --- | --- |
| `_pwr_wait_timeout` | 10 | `0x00000000` (**9**), `0x00000001` (**1**) |
| `_pwr_wait_ctl_after` | 10 | `0x00000001` (**9**), `0x00000000` (**1**) |

**The single disagreeing rung is rung 9, and it is the only one** — the same capture whose sibling cells
were `timeout = 1`, `ctl_after = 0`. Every rung from 12 to 20 the wait sees `BUS_SUCCESS` in
`CORE_PWRCTL_CTL` inside its own bound, and does not time out.

And the *timing* is the part worth having, because it is a number the ladder can use. `_pwr_irq_at` minus
`_pwr_wait_t0` — the handler's entry measured against the wait's own clock base — across every capture
carrying both:

| capture | ticks | | capture | ticks |
| --- | --- | --- | --- | --- |
| rung12 | **1033** | | rung17 | **1044** |
| rung13 | **1059** | | rung18 | **1048** |
| rung14 | **1055** | | rung19 | **1053** |
| rung15 | **1045** | | rung20 | **1053** |
| rung16 | **1049** | | **rung9** | **1922347** |

**Nine consecutive boots agree to within 26 ticks (2.5 %) on ≈1,050 ticks, and rung 9 is 1,831× that.**
Rung 9 is not a slow delivery by the block; it is a different *kind* of number, and §5 says which kind.

## 5. Rung 9's timeout is a fact about the waiter's `CPSR`, not about the block

The cells are in the same capture:

| | `_pwr_wait_cpsr` | `_pwr_wait_ticks` | `_pwr_wait_bound` | `_pwr_wait_timeout` |
| --- | --- | --- | --- | --- |
| **rung 9** | **`0x80000093`** — bit 7 (`I`) **SET** | 1921194 | 1920000 | **1** |
| **rung 12** | **`0x80000013`** — bit 7 **clear** | 1415 | 384000 | **0** |
| **rung 20** | **`0x80000013`** — bit 7 **clear** | 1435 | 384000 | **0** |

Rung 9 waits with interrupts **masked**; rung 10 is the rung whose whole content is *the mask comes off*
(`_wait_cpsr_after = 0x20000093`, `I` clear). And the consequence is in rung 9's own numbers: the handler
entered **1,153 ticks after the loop gave up** (1,922,347 against a bound of 1,920,000 — **it lost by
0.06 % of its own bound**).

**Measured:** the two rungs differ in the waiting context's `I` bit and in nothing else on this path;
rung 9's handler entered ~1,150 ticks after the wait ended; nine rungs with `I` clear see the handshake
at ≈1,050 ticks.

**Inference, labelled:** on rung 9 the block asserted `BUS_ON` on the same ≈1,050-tick schedule and the
**pending** interrupt simply was not delivered until the waiting context restored `CPSR`, so the wait's
poll could not see `CORE_PWRCTL_CTL` set. *The direct measurement — the block's assertion instant on
rung 9 — is not in the archive; that rung's `_pwr_irq_at` is stamped at the handler's entry, after the
unmask.* What the archive does establish is the direction of the causality: **`_pwr_wait_timeout = 1` on
rung 9 is a property of the reader, and the record has been quoting it beside the block.**

## 6. What this does to 748 §4, and what it does to rung 21's candidate set

**To 748 §4 — its conclusion survives and its reason does not.** 748 §4 wrote:

> **So `0xDC` is a dead end for the ladder's next question for the opposite reason `0xFC` is**: it is
> informative and the information says *nothing pending*. ... A block that was never asked to power a bus
> has nothing to report about powering one.

Both halves of the reason are now measured false for rungs 8–20: the block **was** asked
(`_pwr_status_after = 0x02` on twelve captures), and it **did** report (`_pwr_irq_status = 0x02` on
eleven). The zero 748 quoted is real and is a reading, taken at the end of the reset stage before any of
that — so **`0xDC` remains a dead end for the ladder's next question, for the simpler and stronger reason
that the key 748 quoted is read before the block is asked anything, and the key read after the byte is the
one that answers.** `0xDC` is not worth a rung; that verdict is unchanged, and it is now attached to the
right pair of moments.

**To rung 21's candidate set — it removes one more explanation, and this is the part that matters.**
724 §2's hypothesis 4 (*the bus is unpowered*) was already refuted by rung 13 from the command's own
moment (`_cmd2_power_control = 0x0b` with `_cmd2_power_bus = 1`, a **readback**), and 748 §5 removed the
card-present gate. What the cells above add is the **other side of the handshake**: not that the byte was
written, but that **the block raised `BUS_ON` in response to it and the handler acknowledged it, on nine
consecutive rungs, at a latency reproducible to 2.5 %.** A block that is holding an unanswered power
request would look like `_pwr_status_after = 0x02` with no `_pwr_irq_ctl_after = 0x01`; the archive has
never shown that pair. **So among the readings this image can take, no power request is left outstanding
when CMD1 and CMD2 are issued.**

**The caveat, stated rather than buried.** The handler's decode **folds the vendor's two regulator
branches to the success direction** (*"there is no regulator call here to fail"*, `:1551`), so
`_pwr_irq_ctl_after = 0x01` is the **ladder's own acknowledgement**, not a PMIC's. **What is proved is
that the block's handshake ran to completion and was answered by this image; what is not proved is that
the eMMC's supplies are physically up**, because nothing in this image talks to the PMIC — the same
boundary 692 met from the other side ([[mi4-a-device-address-can-be-right-and-undereferenceable]]). The
elimination above is therefore an elimination **among the explanations this ladder can distinguish**, and
it is stated that way on purpose.

## 7. What this document does not say

- **It does not say the ladder's power path is correct.** It says the block latched `BUS_ON` and this
  image acknowledged it. §6's caveat is the boundary of that claim.
- **It does not touch CMD1 and CMD2's silence.** It removes one candidate for it and adds no reading
  about either command. **749 §4b's rule — a word that asks for a response and carries no `INDEX` is
  declined — remains the only surviving explanation**, and rung 21 is still the rung that tests it.
- **It does not say 748 §2 or §3 are wrong.** §2 (`SLOT_INT_STATUS`, dump-only, sixteen zeros) and §3
  (`CARD_PRESENT`, gated out by the vendor's quirk) are untouched and were re-read here; this corrects
  §4 only, and it corrects its *reason*, not its verdict.
- **It does not move rung 9's capture or its rung.** Rung 9's press is spent and its reading stands;
  what changes is which side of the handshake the `timeout = 1` is attributed to.
- **It does not arm anything.** No press is owed, no firer is armed, `out/` is untouched, and **no press
  may be spent without the operator's authorization.** Rung 21 remains **ARMED AND NOT PRESSED**.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. It removes one candidate
  reading from the rung-21 decision and corrects one landed document's reasoning, and that is the whole
  of what it claims.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist; 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
