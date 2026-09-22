# 531: the eMMC controller is two windows and a mode bit

**A host-side reading: no device, no build, no switch.** It opens the storage line at the level below the
one 529 and 530 left it at. 530 showed *what a block device must register* — `bdevsw_add(-1, &mdevbdevsw)`,
`cdevsw_add_with_bdev`, `makedev`, `devfs_make_node(..., DEVFS_BLOCK, ...)` — and 529 showed *that this tree
has no HFS to mount with*. This one reads the controller underneath both, out of the vendor's own Linux 3.4
driver at `external/android_kernel_xiaomi_cancro/`, the same source 524 used to find the console's polled
putc. Nothing here is a new claim about XNU; the claim is about the hardware's programming surface, and the
vendor driver is the only place in this repository where it is written down.

In one paragraph: **the eMMC controller's register file is at `0xf9824900`, and it is not in SDHCI mode
until a bit in a second, lower window says so.** That bit is `CORE_HC_MODE 0x78` bit 0, in the block at
`0xf9824000` that the device tree calls `core_mem`; the driver's probe soft-resets the core through
`CORE_POWER 0x0` bit 7 and sets that bit *before it touches a single standard register*
(`drivers/mmc/host/sdhci-msm.c:2832-2868`). The two DT entries are **named** — `reg-names = "hc_mem",
"core_mem"` (`arch/arm/boot/dts/msm8974.dtsi:503-504`) — and the split is not the one the addresses suggest:
`hc_mem` holds the standard SDHCI block `0x00`–`0x3F` **and** the vendor block `0x100`–`0x188`, while
`core_mem` holds the legacy SDCC words the probe needs first. The name is not even unique in that file: the
neighbouring, driverless `sdcc1` node calls its own *whole* window `core_mem`, at the same address. Three
further readings decide whether a hand-written reader is bounded. **Neither SDCC1 gate is a vote** — both are
`clk_ops_branch`, so `BIT(0)` into their own CBCR with a readback of bits 31:28, and 528's BLSP1 rule does
not transfer; `SDCC1_BCR 0x04C0` is defined in two clock files and used in neither. **The card-clock divider
is not in `CLOCK_CONTROL`** — the driver's `QUIRK2_ALWAYS_USE_BASE_CLOCK` path writes `div = 0` and takes the
frequency from the clock tree, whose divider is `SDCC1_APPS_CMD_RCGR 0x04D0`. And **bus power is a PMIC
request, not `POWER_CONTROL 0x29`**: the driver says so in a comment, and then avoids ever writing 0 to that
register, because on this SoC writing 0 *is* a bus-off request. The good news is the data path: PIO needs
nothing but `TRANSFER_MODE 0x0C` bit 0 left clear, which is the same polled idiom as 524's `debug_putc`.

## 1. Two windows, and which one holds what

`sdhc_1` is declared once, in `msm8974.dtsi:500-528`, and it is one node with two memory entries:

```
reg = <0xf9824900 0x11c>, <0xf9824000 0x800>;
reg-names = "hc_mem", "core_mem";
interrupts = <0 123 0>, <0 138 0>;
interrupt-names = "hc_irq", "pwr_irq";
qcom,clk-rates = <400000 20000000 25000000 50000000 100000000 200000000>;
qcom,bus-speed-mode = "HS200_1p8v", "DDR_1p8v";
...
status = "disable";
```

`sdhci_msm_probe` maps them separately and resolves the second **by name** — `platform_get_resource_byname(pdev,
IORESOURCE_MEM, "core_mem")` then `devm_ioremap` (`sdhci-msm.c:2833-2836`) — so the assignment below is the
driver's own, not an inference from the ordering:

| window | address | what lives in it |
| --- | --- | --- |
| `hc_mem` | `0xf9824900` | SDHCI standard `0x00`–`0x3F` **and** vendor `0x100`–`0x188`: `CORE_DLL_CONFIG 0x100`, `CORE_DLL_STATUS 0x108`, `CORE_VENDOR_SPEC 0x10C`, `CORE_VENDOR_SPEC_ADMA_ERR_ADDR0/1 0x114`/`0x118`, `CORE_CSR_CDC_* 0x130`–`0x164`, `CORE_CSR_CDC_GEN_CFG 0x178`, `CORE_DDR_200_CFG 0x184` |
| `core_mem` | `0xf9824000` | the legacy SDCC words: `CORE_POWER 0x0`, `CORE_MCI_VERSION 0x050`, `CORE_MCI_DATA_CTRL 0x2C`, `CORE_HC_MODE 0x78`, `CORE_TESTBUS_CONFIG 0x0CC`, `CORE_PWRCTL_STATUS 0xDC`, `CORE_PWRCTL_MASK 0xE0`, `CORE_PWRCTL_CLEAR 0xE4`, `CORE_PWRCTL_CTL 0xE8`, `CORE_SDCC_DEBUG_REG 0x124` |

The vendor offsets are `#define`s at the top of `sdhci-msm.c` (`:54-157`), and the split is checkable rather
than conventional: **no offset is ever used against both bases.** Enumerating them by name out of the driver
gives nine that only ever appear as `core_mem +` (`POWER`, `HC_MODE`, `MCI_VERSION`, `MCI_DATA_CTRL`,
`PWRCTL_STATUS`, `PWRCTL_MASK`, `PWRCTL_CLEAR`, `PWRCTL_CTL`, `TESTBUS_CONFIG`) and fifteen that only ever
appear as `host->ioaddr +` (`DLL_CONFIG`, `DLL_STATUS`, `VENDOR_SPEC`, the `CSR_CDC_*` block and
`DDR_200_CFG`), with an empty intersection — which is what makes the table above the driver's own assignment
rather than a reading of the addresses. **The reading a grep invites and the hardware refuses is "the vendor
registers are in the second window"**: they are in the first, at `0xf9824900 + 0x10C`, and the second window
is the *lower* address — `0xf9824000` — which is the reason the guess goes the wrong way.

**And `core_mem` is a name two nodes in this file both use, for two different windows.** The next node up,
line 313, is the legacy controller, and its *first* region carries the same `reg-names` string with the map
at offset zero:

```
313: sdcc1: qcom,sdcc@f9824000 {
314:         cell-index = <1>; /* SDC1 eMMC slot */
315:         compatible = "qcom,msm-sdcc";
316:         reg = <0xf9824000 0x800>,
317:                 <0xf9824800 0x100>,
318:                 <0xf9804000 0x7000>;
319:         reg-names = "core_mem", "dml_mem", "bam_mem";
```

So `0xf9824000` is `core_mem` in both nodes, and the string means *the window whose base is the register
file* in the legacy node and *a sibling window the register file is a part of* in the sdhci one. An
alphabetic grep for the name in `drivers/mmc/host/` reaches `msm_sdcc.c` first, whose addressing is
`host->core_memres->start + MMCIFIFO` (`msm_sdcc.c:547`, region resolved by the same
`platform_get_resource_byname(..., "core_mem")` at `:5901-5902`) — a *different* scheme for the *same*
silicon, and the two drivers agree on the register they both name: `MCI_VERSION 0x050`
(`msm_sdcc.h:175`) and `CORE_MCI_VERSION 0x050` (`sdhci-msm.c:139`) are the same word at the same address,
reached as `core_mem + 0x50` by both. That is the confirmation, and it is also the trap: the legacy driver
has **no Kconfig entry in this tree** — `drivers/mmc/host/Kconfig:461` declares `MMC_SDHCI_MSM` and nothing
for `msm_sdcc` — and neither cancro defconfig selects one, so its offsets are a reading of the register file
and not a path this kernel can take. **A node with no driver is still a document about the hardware.**

## 2. The mode bit: the register file does not answer until `HC_MODE_EN`

The probe's comment is the whole finding, and it is four words long: `/* Reset the core and Enable SDHC mode
*/` (`:2832`). What follows is the order, and it is not optional:

```c
writel_relaxed(0, core_mem + CORE_HC_MODE);                    /* :2845  unset HC_MODE_EN   */
writel_relaxed(readl_relaxed(core_mem + CORE_POWER) |
               CORE_SW_RST, core_mem + CORE_POWER);            /* :2848  set SW_RST (bit 7) */
readl_poll_timeout(core_mem + CORE_POWER, pwr,
                   !(pwr & CORE_SW_RST), 10, 1000);            /* :2856  poll it clear      */
writel_relaxed(HC_MODE_EN, core_mem + CORE_HC_MODE);           /* :2864  HC_MODE_EN = 1     */
writel_relaxed(readl_relaxed(core_mem + CORE_HC_MODE) |
               FF_CLK_SW_RST_DIS, core_mem + CORE_HC_MODE);    /* :2867  bit 13             */
```

`CORE_SW_RST` is `(1 << 7)` in `CORE_POWER 0x0` and `HC_MODE_EN` is `0x1` with `FF_CLK_SW_RST_DIS` `(1 << 13)`
in `CORE_HC_MODE 0x78` (`:55-60`). The poll's budget is 10 µs × 1000, and the driver's own comment derives
the real figure — the reset takes up to 10 HCLK + 15 MCLK, which at the minimum rates the driver names (hclk
27 MHz, mclk 400 kHz) is **~40 µs** (`:2850-2855`).

Then the pending power interrupt is cleared before any interrupt is enabled, because `CORE_SW_RST` can
raise one if the previous state was `BUS_ON` or `IO_HIGH_V`: read `PWRCTL_STATUS`, write it straight back to
`PWRCTL_CLEAR`, and OR the acknowledgement into `PWRCTL_CTL` — `CORE_PWRCTL_BUS_SUCCESS 0x1` if
`BUS_ON|BUS_OFF` was pending, `CORE_PWRCTL_IO_SUCCESS (1<<2)` if `IO_HIGH|IO_LOW` was (`:2877-2884`,
`:2867-2875`'s comment). The mask `INT_MASK 0xF` goes into `PWRCTL_MASK` only after the handler is
registered (`:2946`).

**Why this is the project's oldest mistake in a new address range:** if a first attempt reads `CAPABILITIES
0x40` or `HOST_VERSION 0xFE` before this sequence, it is reading a block that is not in SDHCI mode, and
"the controller does not answer" is the wrong conclusion — the block is *there*, it is in legacy SDCC mode.
The checkable reading that says the mode took is `CORE_MCI_VERSION` at `core_mem + 0x050`: it is the one
place in this driver where the hardware is compared against an exact constant, `CORE_VERSION_310
0x10000011` (`:139-140`, compared at `:2623-2626`), and the comparison is *directional* — the driver skips a
workaround when the core **is** 3.1.0 and takes it otherwise. So the honest form of the check is: read
`0x050`, log it, and let `0x10000011` mean "3.1.0, workaround not needed" rather than "the controller is
alive".

## 3. The window size is the top vendor register

The Pro device tree overrides the node rather than redefining it — `&sdhc_1 { ... }` at
`msm8974pro.dtsi:1763-1769` — and changes exactly three properties:

```
reg = <0xf9824900 0x1a0>, <0xf9824000 0x800>;
qcom,clk-rates = <400000 20000000 25000000 50000000 100000000 192000000 384000000>;
qcom,bus-speed-mode = "HS400_1p8v", "HS200_1p8v", "DDR_1p8v";
```

The `0x11c` → `0x1a0` widening is not a round number either way. The highest `CORE_*` offset used against
`hc_mem` is `CORE_DDR_200_CFG 0x184` (`:128`), i.e. `0x188` exclusive, and the CDC block that only HS400
tuning needs begins at `CORE_CSR_CDC_CTLR_CFG0 0x130` (`:105-119`). So the non-Pro window `0x11c` covers
`0x00`–`0x11B` — the standard block, `CORE_DLL_CONFIG 0x100`, `CORE_DLL_STATUS 0x108`, `CORE_VENDOR_SPEC
0x10C` and the two ADMA error-address words, but **nothing from `0x130` up** — while the Pro's `0x1a0`
covers `0x00`–`0x19F` and reaches `0x188`. That is exactly "HS200 is the ceiling" versus "HS400 tuning is
reachable", which is what the two `reg` values sit beside: `qcom,bus-speed-mode` ends at `DDR_1p8v` in the
base node and begins at `HS400_1p8v` in the override (`msm8974.dtsi:508`, `msm8974pro.dtsi:1769`), and the
clock table's top moves 200 MHz → 384 MHz with it. The window is not unique to 8974 either: `msm8226.dtsi:795`
and `msm8610.dtsi:350` both carry `0x11c`. **Consequence for a reader: any access at `0x130` or above is
depending on the Pro override; on the base node those addresses are outside the declared window.**

## 4. Neither SDCC1 gate is a vote — and one address is a decoy

528's rule was that a Qualcomm clock gate can be a **vote** in a separate SoC register rather than a CBCR bit
(the BLSP1 AHB vote at `0xF9012484` bit 17). It does not transfer here, and the distinction is structural
rather than an address convention: it is the clock's own struct **type** and its `ops` field, and the type is
the register set — a `local_vote_clk` has a `vote_reg` and an `en_mask` to spend, a `branch_clk` has neither
and cannot express the vote at all.

```c
/* clock-8974.c:2327-2337 — the vote shape, for contrast */
static struct local_vote_clk gcc_prng_ahb_clk = {
        .cbcr_reg = PRNG_AHB_CBCR,
        .vote_reg = APCS_CLOCK_BRANCH_ENA_VOTE,
        .en_mask  = BIT(13),
        .base = &virt_bases[GCC_BASE],
        .c = { .dbg_name = "gcc_prng_ahb_clk", .ops = &clk_ops_vote, ... },
};
/* :2339-2348 */
static struct branch_clk gcc_sdcc1_ahb_clk = {
        .cbcr_reg = SDCC1_AHB_CBCR,
        .has_sibling = 1,
        .base = &virt_bases[GCC_BASE],
        .c = { .dbg_name = "gcc_sdcc1_ahb_clk", .ops = &clk_ops_branch, ... },
};
/* :2350-2359 */
static struct branch_clk gcc_sdcc1_apps_clk = {
        .cbcr_reg = SDCC1_APPS_CBCR,
        .base = &virt_bases[GCC_BASE],
        .c = { .parent = &sdcc1_apps_clk_src.c, .dbg_name = "gcc_sdcc1_apps_clk",
               .ops = &clk_ops_branch, ... },
};
```

The discriminator is the **type**, and the type is the register set: `struct local_vote_clk` has `vote_reg`
and `en_mask` (`clock-local2.h:126-134`) while `struct branch_clk` has `has_sibling`, `cur_div`, `max_div`
and `halt_check` (`:99-109`). The vote path's enable is `ena |= vclk->en_mask` written to
`VOTE_REG(x)` = `*(x)->base + (x)->vote_reg` (`clock-local2.c:56`, `:608`), and the SDCC1 clocks cannot
express that at all — neither struct even has the field.

Both SDCC1 clocks are `clk_ops_branch` with a `cbcr_reg`, and `branch_clk_enable` (`clock-local2.c:373-390`)
is read, `|= CBCR_BRANCH_ENABLE_BIT`, write, then halt-check. The enable bit is `BIT(0)` (`:62`) and the halt
check (`:333-371`, the branch-clock halt check) polls **bits 31:28 of the same register** — `BRANCH_CHECK_MASK BM(31,28)`
with `BRANCH_ON_VAL 0x0`, `BRANCH_NOC_FSM_ON_VAL 0x2` and `BRANCH_OFF_VAL 0x8` (`:325-328`) — up to
`HALT_CHECK_MAX_LOOPS 500` × `udelay(1)`, so at most 500 µs (`:36`, `:352-368`). So the whole enable is two
reads and one write on `SDCC1_APPS_CBCR 0x04C4` and `SDCC1_AHB_CBCR 0x04C8` (`clock-8974.c:339-340`), with
the readback in place. The card clock's root is `sdcc1_apps_clk_src` and its divider is `SDCC1_APPS_CMD_RCGR
0x04D0` (`:140-143`).

Two details that will mislead a reader of the header:

- **`SDCC1_BCR 0x04C0` is defined twice and referenced nowhere.** `clock-8974.c:244` and
  `clock-8610.c:75` both define it; `grep -rn SDCC1_BCR` over the tree's `.c` and `.h` returns exactly
  those two lines and no use. A grep for `SDCC1_` in `clock-8974.c` produces three near-neighbours —
  `0x04C0`, `0x04C4`, `0x04C8` — and only two are gates. The define is not a stray: `bcr_reg` is a real
  `branch_clk` field, documented as "block reset register" (`clock-local2.h:92`, `:103`) and consumed by
  `__branch_clk_reset` (`clock-local2.c:511-530`), so `0x04C0` is the value that field *would* hold —
  and neither SDCC1 entry sets it, which is the checkable form of "there is no block reset in this
  driver's SDCC1 path".
- **`has_sibling = 1` on the AHB clock is load-bearing for an *unrelated* reason**: `branch_clk_handoff`
  only falls through to `clk_get_rate(c->parent)` when `!branch->has_sibling` (`:504-506`), so with the flag
  set the AHB clock's rate is not derivable from its parent — which is why `list_rate` answers `-ENXIO` for
  it. It says nothing about whether the gate needs a vote; the type and the `ops` do.

## 5. The card clock is not in `CLOCK_CONTROL`, and `CAPABILITIES` is present and wrong

Two of the driver's quirks are statements about where values live, and both move a value out of the register
a hand-written reader would look in.

**`host->max_clk` does not come from `CAPABILITIES 0x40`.** `sdhci.c:3270-3283` reads the base-clock field
out of caps and then throws it away — `if (host->max_clk == 0 || host->quirks &
SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN) { ... host->max_clk = host->ops->get_max_clock(host); }` — and the msm
ops supply `msm_host->pdata->sup_clk_table[sup_clk_cnt - 1]` (`sdhci-msm.c:2236-2243`), which is the device
tree's `qcom,clk-rates` list. `get_min_clock` returns entry `[0]` (`:2228-2234`). So **the identification
frequency is a number the board declares, `400000`, and the top is `200000000` on the base node and
`384000000` on Pro** (`msm8974.dtsi:507`, `msm8974pro.dtsi:1768`). `0x40` is not absent — it answers, and
what it answers with is discarded. A register that lies is worse to program against than one that is zero.

**The divider is not in `CLOCK_CONTROL 0x2C` either.** `QUIRK2_ALWAYS_USE_BASE_CLOCK` makes `sdhci.c` write
`div = 0` into the divider fields (`:1282-1283`) — the fields are still programmed, they are programmed
*zero* — because the rate is set by `clk_set_rate` on `sdcc1_apps_clk_src` (`sdhci-msm.c:2519-2522`), whose
divider is the RCGR at `0x04D0`. The SDHCI spec puts the card-clock divider in `0x2C`; this SoC puts it in
the GCC. That is the same shape as the `rootdev` finding in 530: **one value, two candidate definitions, and
the register that looks right is the one that is not it.**

The same quirk family also repurposes the timeout, and the consequence is a *legal reserved value*:
`QUIRK2_ALWAYS_USE_BASE_CLOCK` computes `curr_clk = host->clock / 1000`, `QUIRK2_DIVIDE_TOUT_BY_4` divides it
by four, and the timeout is derived from that rather than from `host->timeout_clk` (`sdhci.c:780-786`);
`QUIRK2_USE_RESERVED_MAX_TIMEOUT` then skips the clamp that would force the count down to `0xE`
(`:795-800`). So `TIMEOUT_CONTROL 0x2E` can legitimately hold `0xF` on this part. Two more of the list are
"the register is not usable": `QUIRK2_BROKEN_PRESET_VALUE` makes `sdhci.c` return before it touches the
preset-value mechanism at all (`:2292-2298`), so `HOST_CONTROL2 0x3E`'s preset-enable bit must not be
relied on; and `QUIRK2_IGN_DATA_END_BIT_ERROR`, `QUIRK2_IGNORE_CMDCRC_FOR_TUNING`,
`QUIRK2_IGNORE_DATATOUT_FOR_R1BCMD`, `QUIRK2_SLOW_INT_CLR` and `QUIRK2_RDWR_TX_ACTIVE_EOT` are the driver's
statement that particular error bits are unreliable on this part (`:2900-2927`). The last two are
version-gated (§7).

## 6. Bus power is a PMIC request — and the driver's quirk is a safety rule

The deviations are named by the driver itself, in the comment that introduces the quirk list
(`sdhci-msm.c:2891-2895`):

```
 * Following are the deviations from SDHC spec v3.0 -
 * 1. Card detection is handled using separate GPIO.
 * 2. Bus power control is handled by interacting with PMIC.
```

The mechanism is a second interrupt and a request/acknowledge register pair. `interrupts = <0 123 0>, <0 138
0>` with `interrupt-names = "hc_irq", "pwr_irq"` (`msm8974.dtsi:505-506`); the driver requests `pwr_irq`
separately (`:2929-2943`) and its handler is `sdhci_msm_pwr_irq`; the requests are `CORE_PWRCTL_BUS_OFF 0x01`,
`CORE_PWRCTL_BUS_ON (1<<1)`, `CORE_PWRCTL_IO_LOW (1<<2)`, `CORE_PWRCTL_IO_HIGH (1<<3)` in `CORE_PWRCTL_CTL
0xE8`, acknowledged with `CORE_PWRCTL_BUS_SUCCESS 0x1` / `CORE_PWRCTL_IO_SUCCESS (1<<2)` (`:67-75`).

The safety half is `SDHCI_QUIRK_SINGLE_POWER_WRITE` (`:2897`). The spec's sequence is "clear the power
register, then set the new value", and `sdhci.c:1345-1357` skips the clear only for hosts carrying this quirk
— and what the skipped arm does is not merely a write: it writes 0 to `POWER_CONTROL 0x29` **and then calls
`host->ops->check_power_status(host, REQ_BUS_OFF)`**, which on msm is the function that issues the PMIC
bus-off request. **So on this SoC, "clear power then set power" is a request to the PMIC to power the eMMC
down and back up in the middle of a boot.** A hand-written reader must write `POWER_CONTROL` exactly once and
never zero it. This is not a brick risk — nothing in this project is ever written to storage, so the worst
outcome remains a phone that needs a power press — but it is a device-side action with a real consequence,
and it is the kind of thing the driver's quirk list exists to prevent.

**This is also the one open question that bounds the whole storage line.** If the eMMC's rail can only be
switched through the PMIC path, then a reader that writes `POWER_CONTROL` and waits for the card to answer
is waiting on the wrong register — but if the bootloader has already powered the eMMC and left it
enumerated (it read the payload off *something* before handing over), the card may already answer at
handoff, and the reader is bounded after all. Which of the two holds is a question only hardware answers,
and it is named as 531's open one rather than assumed either way.

## 7. One register, two version fields, two programming consequences

`HOST_VERSION 0xFE` is a 16-bit register with two independent fields, and both change what a reader must
program: the **spec** version is bits 7:0 and the **vendor** version is bits 15:8.

```c
/* sdhci.h */
#define SDHCI_HOST_VERSION     0xFE
#define  SDHCI_VENDOR_VER_MASK 0xFF00
#define  SDHCI_VENDOR_VER_SHIFT 8
#define  SDHCI_SPEC_VER_MASK   0x00FF
#define  SDHCI_SPEC_VER_SHIFT  0
#define  SDHCI_SPEC_300        2
```

`sdhci.c:3177-3178` takes the spec version as `(readw(0xFE) & SDHCI_SPEC_VER_MASK) >> 0`, and every
preset-value and 3.00-only path tests `host->version >= SDHCI_SPEC_300` (`:1225`, `:1973`, `:2075`,
`:2294`). The msm driver takes the *other* field — `(host_version & SDHCI_VENDOR_VER_MASK) >>
SDHCI_VENDOR_VER_SHIFT`, compared against `SDHCI_VER_100 0x2B` (`:54`, `:2909-2914`) — and a match adds two
quirks: `QUIRK2_SLOW_INT_CLR` (40 µs delay when clearing an interrupt at the 400 kHz identification
frequency) and `QUIRK2_RDWR_TX_ACTIVE_EOT` (set DAT-line software reset, bit 2) (`:2915-2925`). So **the
first honest read of the controller is one 16-bit read at `0xFE`, taken after §2's sequence, and it decides
both the preset-value question and the interrupt-clearing question** — which is a cheap first register
access with two consequences, and the only read in this document that must happen before the clock is
programmed.

## 8. The data path: PIO is reached by not asking for DMA

`TRANSFER_MODE 0x0C` bit 0 is `SDHCI_TRNS_DMA 0x01` (`sdhci.h:38`). With it **clear**, the SDHCI state
machine transfers through `SDHCI_BUFFER 0x20`, and the readiness signal is either the interrupt status or
`PRESENT_STATE` — `SDHCI_PRESENT_STATE 0x24` bit 11 `SDHCI_DATA_AVAILABLE 0x00000800` for a read,
`SDHCI_SPACE_AVAILABLE 0x00000400` for a write (`:64-74`); from `INT_STATUS 0x30` the same states appear as
`SDHCI_INT_DATA_AVAIL 0x20`, `SDHCI_INT_SPACE_AVAIL 0x10` and `SDHCI_INT_DATA_END 0x02` (`:118-125`), which
can be read and cleared without enabling a single interrupt in `INT_ENABLE 0x34` or `SIGNAL_ENABLE 0x38`.
The driver never takes this path, and the reason is not a requirement: `sdhci_prepare_data` sets
`SDHCI_REQ_USE_DMA` whenever `host->flags & (SDHCI_USE_SDMA | SDHCI_USE_ADMA)` is set (`sdhci.c:843-844`),
and those flags are set from what `CAPABILITIES 0x40` advertises — `SDHCI_CAN_DO_ADMA2 0x00080000`
(`sdhci.h:191`). **Advertising is not an obligation.** Nothing in the register map forces the DMA bit, and
the register set for a polled single-block read is `ARGUMENT 0x08`, `BLOCK_SIZE 0x04`, `BLOCK_COUNT 0x06`,
`TRANSFER_MODE 0x0C`, `COMMAND 0x0E`, `RESPONSE 0x10`, `BUFFER 0x20`, `PRESENT_STATE 0x24` and `INT_STATUS
0x30` — with no ADMA descriptor list, no `ADMA_ADDRESS 0x58`, no IRQ, and no GIC path at all.

That is what makes the first storage arm bounded **in this project's own idiom**: the same
`while (!(read(status) & READY)); read/write(data);` shape as `debug_putc` in 524 and the putc 523 priced,
with a timeout on every poll and no interrupt to service.

## 9. The first reading is self-verifying, and the signature is knowable in advance

The cancro's kernel is built with GPT support declared for it, in both configs that exist for the device:

```
arch/arm/configs/cancro_user_defconfig:208:CONFIG_MSDOS_PARTITION=y
arch/arm/configs/cancro_user_defconfig:218:CONFIG_EFI_PARTITION=y
arch/arm/configs/lineageos_cancro_defconfig:37:CONFIG_EFI_PARTITION=y
```

so the expected disk layout is GPT and the expected signature at **LBA 1** is the 8 bytes `EFI PART`
(`45 46 49 20 50 41 52 54`), with the MBR fallback at LBA 0 ending `55 AA`. That gives the first arm a
reading whose *value* proves the path and not merely that a command completed: read one 512-byte block and
compare eight bytes. The same command returns the partition-entry LBA, their count and the header's CRC
fields, which is the input the next step needs — 530's `bdevsw`-and-`mdevlookup` shape decides *how* the
payload presents a device, and this decides *what it is presenting*.

Two honest limits on that. `CONFIG_EFI_PARTITION=y` is the kernel's declared support, i.e. evidence about
the platform, not a reading of this device's disk; the check should accept either signature and let the
run's log say which one was found. And §6's question — whether the card answers at all without the PMIC
path — comes first: if it does not, the block read never happens and the first reading is the `0xFE` version
register of §7 plus the `0x050` core version of §2, both of which need §2's sequence and nothing else.

## 10. What this does not decide

- **The PMIC/power question of §6** — whether the eMMC is already powered and enumerated at handoff, which
  is the difference between a bounded polled read and a PMIC interrupt path this payload does not have.
- **Whether §2's sequence is a prerequisite or a re-do.** The bootloader just read the payload off storage;
  if it leaves the core in HC_MODE with the clock running, the sequence is idempotent but the *initial*
  state (`CORE_POWER` bit 7, `CORE_HC_MODE` bits 0 and 13, and the CBCRs) should be **read and logged**
  rather than assumed either way. That log line is itself a reading and costs nothing.
- **Whether XNU needs a real SDHCI driver or the payload's polled reader plus 530's `bdevsw` registration is
  enough** for a root device. 530 established that the payload can supply the `dev_t` with no edit to
  `bsd_init`; whether anything *above* that needs the driver to exist as a driver (retries, block-size
  negotiation, a card-removal path) is not answered here.
- **HFS+ versus a purpose-built read-only filesystem** — 529/530 priced both, and nothing in this document
  changes that trade.
- **Where the volume comes from.** The goal's own answer is TWRP, and it stays withheld: the precondition
  「如果os已经能进去了的话」 is still unmet, since XNU reaches user mode and then dies at the idle exit's
  `pop {fp, pc}`.

## 11. Safety

Nothing in this document is a device action, and nothing in it proposes one that writes storage. The one
hazard it *does* name is §6's: `POWER_CONTROL 0x29` written as 0 is, on this SoC, a request to the PMIC to
power the eMMC off, which the vendor driver's `SDHCI_QUIRK_SINGLE_POWER_WRITE` exists to avoid — a reader
must write that register once, with the bus-power and voltage fields set together, and never clear it. The
standing constraint is unaffected: the worst case remains a phone that needs a power press, never a brick,
because nothing in this project is ever written to storage.

526 remains **built, gated, frozen and unrun**, and the phone is off the bus and owes a power press before
it can run. No file outside `docs/` was written by this experiment.
