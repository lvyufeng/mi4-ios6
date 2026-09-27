# 765: the interrupt-enable census — the vendor enables eleven status bits and the ladder enables one, so "the block said nothing" is a masked reading

**Host-side only. No device action, no press, no gate against a device, no runner, no build, `out/`
untouched, no arm moved.** The measurements are the vendored kernel tree read at line numbers, the ladder's
own store census in `src/entry/entry_storage.c`, and the cells of the rung-22 capture
(`out/stage90/captures/rung22-dllcensus-20260927-080441-last_kmsg.txt`). **No rung is built from this
document** — §4 is a pre-registration for the operator's decision.

**Why this exists.** 726 refuted five hypotheses about the stall and, for the second of them, gave a reason
that this step read back and does not hold:

> Hypotheses 1 (never issued), **2 (wrong bit — refuted by construction, `status_any` covers the whole
> register)**, 3 (in flight and stuck), 4 (bus unpowered) and 5 (clock root off) are ALL REFUTED

**The ladder's own measurement, three rungs later, is that this block latches a status bit only while its
enable stands** — 730's headline: *the completion was there all along and `INT_ENABLE` was the mask*. If
that is the rule, then **coverage of a read is not enablement of a latch**, and the ladder has enabled
exactly one status bit in its whole history. §1 is the census; §2 is what it does to that refutation; §3
removes the candidate 764 §4 named.

## 1. What the vendor's driver enables, and what the ladder does not

**The vendor's own set.** `sdhci_init` (`sdhci.c:284-296`) ends in

```c
sdhci_clear_set_irqs(host, SDHCI_INT_ALL_MASK,
        SDHCI_INT_BUS_POWER | SDHCI_INT_DATA_END_BIT |
        SDHCI_INT_DATA_CRC | SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_INDEX |
        SDHCI_INT_END_BIT | SDHCI_INT_CRC | SDHCI_INT_TIMEOUT |
        SDHCI_INT_DATA_END | SDHCI_INT_RESPONSE |
                     SDHCI_INT_AUTO_CMD_ERR);
```

and `sdhci_clear_set_irqs` (`sdhci.c:181-189`) writes that one value to **both** registers:

```c
ier = sdhci_readl(host, SDHCI_INT_ENABLE);
ier &= ~clear;  ier |= set;
sdhci_writel(host, ier, SDHCI_INT_ENABLE);    /* 0x34 */
sdhci_writel(host, ier, SDHCI_SIGNAL_ENABLE); /* 0x38 */
```

**`SDHCI_INT_ALL_MASK` is `(unsigned int)-1`** (`sdhci.h:152`), so the clear is total; the eleven named bits
are `0x01000000 | 0x00800000 | 0x00400000 | 0x00200000 | 0x00100000 | 0x00080000 | 0x00040000 | 0x00020000 |
0x00010000 | 0x00000002 | 0x00000001` = **`0x01FF0003`**. Both registers are read and written as **32-bit**
words, so a write at `0x34` reaches `INT_ENABLE` *and* the error-status-enable half at `0x36`, and a write at
`0x38` reaches `SIGNAL_ENABLE` *and* its error half at `0x3A`.

**And the error bits are in the set**: `SDHCI_INT_TIMEOUT 0x00010000` (`sdhci.h:130`), `SDHCI_INT_CRC
0x00020000`, `SDHCI_INT_END_BIT 0x00040000`, `SDHCI_INT_INDEX 0x00080000` — the four that
`SDHCI_INT_CMD_MASK` (`sdhci.h:144-147`) groups as *this command's own* error set.

**Where it runs on the ladder's path.** `sdhci_init(host, 0)` is called from `sdhci_add_host` at
`sdhci.c:3589`, in the probe, between `regulator_get(mmc_dev(mmc), "vmmc")` at `:3583` and the debug
register dump at `:3592`. **It is the first and only interrupt-enable write on this driver's power-up
path**, and `sdhci_msm_ops` (`sdhci-msm.c:2653-2664`) overrides neither `.irq` nor `.set_timeout` and never
writes `0x34` or `0x38`. So the vendor's enable state, before its first command, is **`INT_ENABLE =
SIGNAL_ENABLE = 0x01FF0003`**.

**The ladder's set, enumerated the way 761 §5 enumerated the vendor's writes.** Every write to
`INT_ENABLE 0x34` in `src/entry/entry_storage.c`:

| line | value written | error bits? |
| --- | --- | --- |
| `:2619`, `:2820`, `:3166`, `:3289`, `:3434`, `:3728` | `int_enable \| SDHCI_INT_RESPONSE` (or `1`) | no — bit 0 only |
| `:2629` | `0u` | no |
| `:2667`, `:2753`, `:2867`, `:3215`, `:3335`, `:3497` | the value read back before (restore) | no |

and **the ladder's `SIGNAL_ENABLE 0x38` write count is `0`** — `grep` returns no write to
`ST_SDHCI_SIGNAL_ENABLE` anywhere in the file, and every rung since 17 publishes it as *read and never
written*.

**And `TIMEOUT_CONTROL 0x2E` is a register neither side writes on this path.** The ladder has zero
occurrences of it. The vendor writes it only in `sdhci_prepare_data` (`sdhci.c:827-828`), inside

```c
if (data || (cmd->flags & MMC_RSP_BUSY)) {
        count = sdhci_calc_timeout(host, cmd);
        sdhci_writeb(host, count, SDHCI_TIMEOUT_CONTROL);
}
if (!data)
        return;
```

— and the ladder's commands (`0x0000`, `0x0102`, `0x0209`, `0x0300`, `0x031A`, `0x030A`) are all data-less
and none carries `MMC_RSP_BUSY`. **So on the vendor's own path this register is also left at its reset value
for every command the ladder has ever sent** — it is not a vendor act the ladder skipped. It is a register
the ladder has never *read*, which §4 adds as one cell.

**The measured values agree with all of the above.** In the rung-22 capture:
`_int_enable_before = 0x00008000`, `_int_enable_held = 0x00008001` (after a write of `1`),
`_int_enable_readback = 0x00008000` (after a write of `0`), `_nidx_sig_enable = 0x00000000`. Compare the
vendor's `0x01FF0003`.

## 2. So `_status_any = 0` is a masked reading, and 726's second refutation does not hold as stated

The ladder's headline negative is `_nidx_status_any = 0` over `_nidx_any_polls = 0`, with
`_nidx_polls = 0x004db800` = 5,093,376 samples — and the same shape at rungs 13, 17, 18, 19: the block is
taken, started, held for a full 1.2 s, and **reports nothing in `INT_STATUS`**.

726 read that as refuting *wrong bit*, because the poll reads `INT_STATUS 0x30` as a **32-bit word**
(`st_read32`, confirmed at `entry_storage.c:2296`, `:2299`, `:2361`, `:2603`, `:2621`) and a 32-bit read at
`0x30` covers `0x30`–`0x33`, i.e. the normal status **and** the error status half — where
`SDHCI_INT_TIMEOUT` (`0x00010000`, bit 16 = error status bit 0) lives.

**The read covers the bit. The latch may not.** 730's press measured, on this very block, that a status bit
appears **only while its enable stands**: the completion was latched all along and `INT_ENABLE` was the mask,
and clearing the enable made the same completion vanish. 733's rule and 738's refinement
(*necessary and not sufficient*) are the same finding from two more presses. **If the block gates latching
on the enable, then a poll over a register whose error-enable half is zero cannot see an error bit no matter
how many samples it takes** — and the ladder has never once set an error enable.

**The correct statement, therefore, is narrower than 726's and stronger than "nothing was latched":**

> Across every response-demanding command this ladder has sent — five of them, each polled about 5.09 million
> times — **no bit was ever seen in `INT_STATUS`** — and **`INT_STATUS`'s error half has had its enable at zero for every one of those
> samples**, so the absence is a reading about the mask as much as about the block. **It cannot distinguish
> "the controller had nothing to report" from "the controller's error reporting was switched off."**

**This is the project's own defect class, on the ladder's most-quoted cell.** `_status_any = 0` is cited in
five experiment documents as *the block said nothing*; what it actually measures is *no enabled bit was
latched*. **It is m720's shape** (an absence with more than one producer — the branch did not run, the
channel refused, the cap dropped it) with a fourth producer nobody had listed: **the enable was off.**

**And one observation that needs a name rather than a verdict.** `INT_ENABLE 0x34` reads `0x00008000` — bit
15, `SDHCI_INT_ERROR`, the normal-half error *summary* — both before the ladder's first write and after a
write of `0` (the value is sticky against a zero-store). Two readings are open and this document does not
choose between them: **(a)** bit 15 of `0x34` is read-only on this IP and tied to something the ladder has
not identified; **(b)** the block is asserting an error summary while every individual error enable is zero.
**§4 reads `0x32` and `0x30`'s error half with the enables up, which separates them in one cell.**

## 3. And 764 §4's first candidate is refuted by the vendor's own DT

764 §4 named three remaining candidates and made the first the most likely:

> **the card is not powered** — the eMMC's VCC/VCCQ come from the PMIC and **no rung in this ladder has ever
> touched the PMIC**, which is the only candidate explaining both halves

**Checked against the tree that defines the vendor's behaviour, and it does not hold.** The MTP board file
gives the eMMC slot a supply *and marks it always-on* (`msm8974-mtp.dtsi:384-410`):

    &sdhc_1 {
        vdd-supply      = <&pm8941_l20>;   /* the card's VCC  */
        vdd-io-supply   = <&pm8941_s3>;    /* the card's VCCQ */
        qcom,vdd-always-on;
        qcom,vdd-io-always-on;
        qcom,vdd-voltage-level    = <2950000 2950000>;
        qcom,vdd-io-voltage-level = <1800000 1800000>;
        ...
        status = "ok";
    };

and the vendor's MSM driver **reads that property and honours it**: `sdhci_msm_dt_get_vreg_info` parses
`"qcom,%s-always-on"` into `vreg->is_always_on` (`sdhci-msm.c:1072-1074`), and `sdhci_msm_vreg_disable`'s
own comment is **"Never disable regulator marked as always_on"** (`:1795-1796`), with the always-on branch
only demoting the regulator to low-power mode (`:1813-1817`).

**And the property the SDHCI core looks for is absent**: `sdhci.c:3583` asks for
`regulator_get(mmc_dev(mmc), "vmmc")` — the con_id `vmmc`, where the DT supplies `vdd-supply` — so it takes
the `IS_ERR` branch, prints *"no vmmc regulator found"*, and sets `host->vmmc = NULL`. Every
`mmc_regulator_set_ocr` call in the power path (`sdhci.c:1625-1626`, `:1667-1669`, `:1688-1689`) is guarded
by `host->vmmc &&`, so **it is a no-op on this board**.

**So the vendor does not turn the card's power on either.** The eMMC's VCC and VCCQ are always-on PMIC
outputs on this platform, and the driver's whole power act for a data-less command is the `POWER_CONTROL
0x29` byte — which is exactly what rung 7 (708) transcribed, and what `_pwr_after = 0x0b` reports.
**"The ladder never enabled the card's VCC" is not a missing act**, and 764 §4's candidate 1 is withdrawn.

**Stated at the strength of the evidence, because the boundary matters**: the Mi 4's own board dts is not in
this tree (a downstream addition, the same gap 761 §6 worked around), so what is measured is that **no board
file present here supplies `vmmc` to `sdhc_1` and the platform's own board file marks both rails
always-on**. That refutes *"the ladder skipped a power act the vendor performs"*; it does not by itself
prove the rails are up on the physical device at the moment of the press — the DT is the vendor's
declaration, not a measurement of the PMIC.

## 4. The pre-registration for the next arm — the enables come up, and the line stays down

**One act, two stores and three reads, all in `INT_ENABLE 0x34` and `INT_STATUS 0x30`'s neighbourhood.**
Written as 756 §6 and 762 §2 were, with every branch naming the next act, and **no rung is built from it
without the operator's authorization.**

**The arm:** in the rung-21/22 window's position — before the response-demanding CMD3, with the rung-21
command still sent so the log carries the comparison — write

- `INT_ENABLE 0x34 <- (1 | SDHCI_INT_TIMEOUT | SDHCI_INT_CRC | SDHCI_INT_END_BIT | SDHCI_INT_INDEX |
  SDHCI_INT_RESPONSE)` — **five bits, the three R1-visible error bits plus the completion**, so a single
  press distinguishes *the card answered wrongly* from *the card did not answer* from *nothing happened*;
- with **`SIGNAL_ENABLE 0x38` held at ZERO** — never written, as at every rung since 17 — so **no interrupt
  line can rise and no delivery can end the run at the dispatcher**;
- then read `INT_STATUS 0x30` **whole, 32-bit**, and `TIMEOUT_CONTROL 0x2E` as a **byte**;
- then the two stores are undone in the ladder's usual order with both readbacks published.

**Why this is safe, and it is 730's own measurement rather than an argument.** That press wrote `INT_ENABLE`,
took no command on the bus, and took no interrupt — `_post_end_calls = 7` and no `_irq_other_*` key. The
enable register is not the line; `SIGNAL_ENABLE` is. **And no new register class, no new window, no new
mapping**: `0x34`, `0x38` and `0x2E` are all inside `hc_mem`'s declared `0x1a0`, and `0x2E` is a byte read
of the same window the ladder has been writing since rung 4.

**The pre-registered answer space:**

| `INT_STATUS`'s error half afterwards | reads as | the next act |
| --- | --- | --- |
| **`0x00010000` (`SDHCI_INT_TIMEOUT`) latched** | **the card did not answer and the controller knew it** — the stall is the CARD's silence, not the block's refusal, and the whole "the controller runs a command and never sets its status" reading of rungs 13–22 is refuted in one bit | the subject becomes the card: the bus width, the CMD/DAT pads, the 400 kHz/200 MHz clock at the card's pins, and the `qcom,pad-*` settings the board file carries — **not** the controller's register file |
| `0x00020000` / `0x00080000` / `0x00040000` (CRC / INDEX / END_BIT) | **the card answered and the word was malformed** — a live card on a mis-sampled or mis-configured bus | the subject is sampling and bus width: `HOST_CONTROL 0x28`'s width bits and the DLL question 761 §5 closed |
| **nothing, with the enables up** | the strong form of the negative: a command that was taken, started and held for 1.2 s never reached the point of arming a response timeout — which is a statement about the block's internal command path, and the one reading the ladder has never been able to make | the subject is the block: `TIMEOUT_CONTROL 0x2E`'s value, `CLOCK_CONTROL`'s divider at the moment of the command, and whether `PRESENT_STATE`'s bit 24 (the CMD line level) ever moves for a response-demanding word |
| `_newint_status` bit 15 only, or the same `0x00008000`-shaped value as before | the sticky bit is a property of the IP and not a report | §2's open observation closes with no news, and the third row's subject is the one to take |

**And the two cells that cost nothing and should ride along:** `TIMEOUT_CONTROL 0x2E` read as a byte (never
read by this ladder, and zero on the vendor's own path for every data-less command — so a non-zero value
would be a finding), and `PRESENT_STATE 0x24`'s **bit 24**, the CMD line's own signal level, sampled during
the poll — 749 §3 measured that exact bit separating two producers of `inhibit_seen = 0`, and no rung has
sampled it *during* a response-demanding command.

## 5. What this document does not say

- **It does not claim the enables are the cause of the stall.** §2 says the *absence of an error report* is
  not evidence the block was silent; it names no mechanism, and no rung in it has been built.
- **It does not say 726 was wrong to refute four hypotheses.** Hypotheses 1, 3, 4 and 5 are untouched, and 4
  is *strengthened* by §3. What does not hold is the **reason** given for refuting hypothesis 2 — coverage
  of a read is not enablement of a latch — and hypothesis 2 is thereby re-opened rather than restored.
- **It does not contradict 730, 733 or 738.** It applies their measured rule to the half of the register
  they were not looking at.
- **It does not re-open 764 §1.** The DLL answer stands and its path stays closed.
- **It does not build, arm, park or press anything.** `out/` still holds the pressed rung-22 arm; the park
  is intact; no firer is armed; **§4 becomes an arm only with the operator's authorization**, and a press
  after that is a second authorization.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. What it buys is that **the next
  press is spent on a question the ladder has never asked, instead of on a fourth sample of a masked one.**

## 6. New instance — **m773: coverage of a read read as enablement of a latch**

726 refuted *wrong bit* with *"`status_any` covers the whole register"*. The poll does cover the whole
register — **of the read**. A bit whose *enable* is zero need not appear in that read at all, and the
ladder's own 730 press had already measured, on this block, that the latch is gated by the enable. **A
statement about what a tool looks at is not a statement about what can arrive.**

**Shape to suspect first: a read's WIDTH offered as proof that an absence is real.** The width is a property
of the accessor (`st_read32`) and is checkable in the source; what the register *latches* is a property of
the block and has to be measured. **The test: for every bit a negative reading is being drawn from, name the
enable, the mask and the mode that must be set for that bit to appear — and if any of them is not set, the
reading is about the setting.** Related: [[mi4-silence-is-a-reading-only-if-success-is-silent]] (m720 —
the absent key's three producers; this adds the fourth), [[mi4-one-value-two-definitions]],
[[mi4-a-claim-in-a-comment-is-not-a-check]] (m772), and 730/733/738, which are the measurements that make
this instance decidable rather than speculative.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block starts a
response-demanding command and never completes one — and 「让os可以正常启动并且挂载存储」 is not reached, so
**TWRP-to-storage stays withheld.**
