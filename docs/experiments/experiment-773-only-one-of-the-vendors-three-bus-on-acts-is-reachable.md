# 773: of the vendor's three BUS_ON acts, exactly ONE is reachable — and it is the register rung 26 reads

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER.** `out/` still
holds **`armed-storage-a1378a48`** (`STAGE90_XNU_STORAGE_PROBE=25`, ordinal rung 26), **ARMED AND NOT
PRESSED**. No arm is built here and no press is spent. Every claim below carries its `file:line` so it can
be checked the same way, and the one claim that was previously asserted is now **counted**.

## 1. What this document is for

769 §3 established that the vendor writes the SDC1 pads from inside `sdhci_msm_pwr_irq`'s
`CORE_PWRCTL_BUS_ON` branch, and that the ladder's own handler acks the latch rather than performing that
work — so the pads "have been attempted by no rung". That is the mechanism 770/772's arm reads.

**What was not established is the size of the reachable set.** The BUS_ON branch does **three** things, and
until now nothing had asked which of them a rung could do. The answer is one, it is the pad register, and
this document is the count.

## 2. The census: every caller, in the vendor tree

```
sdhci_msm_setup_pins / sdhci_msm_setup_pad
  sdhci-msm.c:964   the pad writer itself
  sdhci-msm.c:991   the pins dispatcher itself
  sdhci-msm.c:1000    -> setup_pad, the only call
  sdhci-msm.c:2019  sdhci_msm_pwr_irq, CORE_PWRCTL_BUS_ON,  enable = TRUE   <-- the only enable
  sdhci-msm.c:2034  sdhci_msm_pwr_irq, the other branch,       enable = false
  sdhci-msm.c:3144  the teardown path,                         enable = false
```

**Exactly one call in this tree turns the pads ON, and it is the power IRQ's BUS_ON branch.** Both other
calls pass `false` and write the *off* arrays. So 769 §3's claim is not an inference from reading one
function's neighbourhood — it is a census over the whole tree, and it is **confirmed**.

`sdhci_msm_setup_pins` also carries a latch — `if (!pdata->pin_data || (pdata->pin_data->cfg_sts ==
enable)) return 0;` (`:994-995`) — so within one Linux boot the pads are written **once**, on the first
BUS_ON, and never again unless the slot is powered off. That matters for the read: a board that went
straight to `fastboot` without a Linux boot has the pads at **bootloader** state, whatever that is, and
**nothing in this repository can say what the bootloader wrote.**

## 3. The three acts, and which the ladder can reach

```
sdhci-msm.c:2015-2032   /* Handle BUS ON */
    if (irq_status & CORE_PWRCTL_BUS_ON) {
        ret  = sdhci_msm_setup_vreg(msm_host->pdata, true, false);      /* (1) */
        if (!ret) {
            ret  = sdhci_msm_setup_pins(msm_host->pdata, true);          /* (2) */
            ret |= sdhci_msm_set_vdd_io_vol(msm_host->pdata, VDD_IO_HIGH, 0);  /* (3) */
        }
```

| # | act | what it writes | reachable from `hc_mem`? |
| --- | --- | --- | --- |
| 1 | `sdhci_msm_setup_vreg` (`:1826`) | a **regulator** — `pdata->vreg_data`, over the PMIC/SPMI | **no** |
| 2 | `sdhci_msm_setup_pins` (`:991`) → `setup_pad` (`:964`) | `msm_tlmm_set_hdrive` / `msm_tlmm_set_pull`, both landing in **`MSM_TLMM_BASE + 0x2044`** = physical **`0xFD512044`** | **YES** — one register, in the `0xFD5` megabyte rung 25 installs |
| 3 | `sdhci_msm_set_vdd_io_vol` (`:1922`) | `sdhci_msm_vreg_set_voltage` on `vreg_data->vdd_io_data` — **a regulator again**, and it returns immediately if `!pdata->vreg_data` | **no** |

**So the vendor's PIN work is the only one of the three the ladder can perform, and it is exactly the
register rung 26 reads.** That is not a coincidence of this document's framing: it is why 769 §4 could
pre-register an expected value at all. Acts 1 and 3 both go to `regulator_*`, which the ladder has no path
to; the pad register is the single piece of the vendor's BUS_ON work that lives in ordinary MMIO.

**And the same census retires a standing worry about act 1.** 769 §8 noted `qcom,vdd-always-on` /
`qcom,vdd-io-always-on` for `&sdhc_1` (`msm8974-mtp.dtsi:385-395`). `sdhci_msm_setup_vreg`'s own head says
what that means when the data is absent — *"vreg info unavailable, assuming the slot is powered by always
on domain"* (`:1834-1837`, `goto out`, `ret = 0`). **So on a board whose supplies are always-on, acts 1
and 3 are already satisfied by construction and their unreachability costs nothing.**

## 4. The pre-registered rung-27 design: read, compare, write ONLY if unconfigured, read back

**One register, one read, at most ONE store, and the store is conditional on the read.** This strictly
contains rung 26 — every rung-26 key survives — so one press answers the pad question **and** the fix
question together, and rungs 25 and 26 need never be pressed.

```
    word_before = *0xFD512044                       (rung 26's own read, kept)
    if (word_before == ST_TLMM_SDC1_EXPECT)          /* 0x00009F24, the DT's own value */
        published: _pad_write_skipped = 1, _pad_writes = 0
    else
        *0xFD512044 = (word_before & ~ALL_SEVEN_MASKS)
                    | (ST_TLMM_SDC1_EXPECT & ALL_SEVEN_MASKS);   /* one RMW */
        published: _pad_write_skipped = 0, _pad_writes = 1
    word_after = *0xFD512044
```

**The store is faithful to the vendor's own, field for field.** `msm_tlmm_set_field`
(`gpio-msm-common.c:481-496`) is the whole mechanism:

```c
    mask    = (1 << width) - 1;
    reg_val = __raw_readl(reg);
    reg_val &= ~(mask << config->off);
    reg_val |= (val & mask) << config->off;
    __raw_writel(reg_val, reg);
    mb();
```

— a plain read-modify-write with a mask, under a spinlock the ladder does not need (nothing else in this
image touches the register). **No unlock sequence, no write-one-to-clear, no FIFO, no shadow register.**
The seven field positions are `tlmm_hdrv_cfgs`/`tlmm_pull_cfgs` (`gpio-msm-common.c:59-88`): hdrive
CLK/CMD/DATA at 6/3/0 (width 3) and pull CLK/CMD/DATA/RCLK at 13/11/9/15 (width 2) — the same seven
constants `ST_TLMM_SDC1_EXPECT` already decodes, fixed by the six `_Static_assert`s rung 25 added.

**The single RMW covers all seven fields at once**, which is what the vendor's six separate calls
(`3` hdrive + `4` pull, each its own read-modify-write) add up to. **The order is the vendor's for the
first five** — `drv->on[]` is CLK, CMD, DATA and `pull->on[]` is CLK, CMD, DATA, RCLK — but a single
masked RMW makes the order **irrelevant**, which is the property that makes one store safe where six
interleaved ones would need an argument.

## 5. Why the conditional is the right shape, and not a way to avoid stating the risk

**It makes the arm's answer a fork rather than a guess, and it makes the store's necessity a reading.**

| `_pad_raw` (rung 26's own cell) | `_pad_write_skipped` | what it means | the next act |
| --- | --- | --- | --- |
| **`0x00009F24`** | `1` | the pads were **already configured** — by the bootloader, since no rung and no Linux on this path wrote them | **the pad candidate is RETIRED**, and the frontier is the PMIC rails alone (acts 1/3, unreachable) — a bus this ladder cannot enter, and the goal's storage clause needs a different approach |
| anything else, incl. `0x00000000` | `0` | the vendor act **was never applied on this path** | **the write has been made and read back**: `_pad_after == 0x00009F24` means the fix landed |

**And the store is not hidden behind the branch.** The build clause for rung 27 must assert the body's
device set is **exactly one store to `fd512044`, guarded, plus the read** — an arm whose store could fire
unconditionally is a different arm and is refused. The safety reading the log carries is
`_pad_writes ∈ {0, 1}`, published as the actual count and never as a constant.

## 6. What a single masked RMW to this register cannot do

Stated because a first store to a second device is exactly where an unstated risk lives:

- **It cannot reach any other register.** The address is one word; there is no indexed or block access.
- **It cannot reach another peripheral's pads.** `SDC1_HDRV_PULL_CTL = 0x2044` is distinct from SDC2's
  `0x2048` (`gpio-msm-common.c:37-42`), and the seven masks are disjoint — the sum equals the OR, which
  is the `_Static_assert` rung 25 already carries.
- **It cannot change a FUNCTION.** Every field of that register is drive strength or pull; 769 §2
  established by register layout that there is no function-select field to write.
- **It does not put the bus at risk.** The register is at rest in every state that matters here: the
  ladder issues its command before or after this store, not during, and no transfer is in flight.
- **It does not touch the PMIC, the rails, or any clock** — acts 1 and 3, the ones that could brown out a
  rail, are the two the census says are unreachable, and this document does not propose reaching them.
- **It is not the DLL path and not the clock register**, so 764 §1 and 768 §4 stay closed.

## 7. What this document does not say

- **It does not say the pads are the cause.** §4 pre-registers the reading that settles it; a pad register
  already holding `0x9F24` retires the candidate, and that is the **more likely** row — a board that boots
  from this eMMC to `fastboot` has had *something* drive that bus successfully.
- **It does not say a configured pad register means the card will answer.** `PULL SDC1_CMD` is necessary
  and not sufficient: the clock at the pins, the bus width and the card's own power-on state are all
  unmeasured, and 768 §6 already noted the idle line reads HIGH on every arm on record — which is what a
  pull-up produces **and** what a floating line that happens to sit high looks like.
- **It does not build the arm.** This is the pre-registration, in the order 769 §6 pre-registered rung 25
  and 770 built it.
- **It does not spend a press and it does not authorize one.**
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount, no driver.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist and 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
