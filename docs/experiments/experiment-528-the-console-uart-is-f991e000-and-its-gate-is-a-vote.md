# 528: the console's UART is `0xF991E000`, and its clock gate is a vote rather than the CBCR

**A host-side reading: no device, no build, no switch.** It closes two items other documents left open -
524 section 5's *"the address itself has to come off the device tree"* and 527 section 6's *"the write
must not be enabled until the UART's clock and pinmux are actually up, which is a separate question"* -
as far as the tree can close them, and it names the one part of the question that only the device can
answer.

What the tree says, in one paragraph: **the console's UART is the low-speed UART at physical
`0xF991E000`**, its interrupts are SPI 108, its **interface clock is enabled by a vote bit in the APCS
block rather than by a write to the clock's own CBCR**, and its **core clock is a branch whose CBCR is
`0xFC400704`** - not `0xFC400684`, which is what the node's own DTS **label** would lead a reader to
write. The DTS label on that node is wrong by one instance, the node's own bus property, the clock
lookup and four other SoC clock files in this tree all say so, and for a payload that writes registers
the difference is the whole job.

## 1. The base, from four witnesses in the tree

| witness | file | what it says |
| --- | --- | --- |
| the SoC's iomap header | `msm_iomap-8974.h:41-42` | `MSM_DEBUG_UART_PHYS 0xF991E000` (under `CONFIG_DEBUG_MSM8974_UART`); the sibling `msm_iomap-8084.h:39` says the same |
| the serial aliases | `msm8974-{cdp,fluid,liquid}.dtsi:18`, `msm8974-mtp.dtsi:19` | `serial0 = &blsp1_uart1` - `serial0` is the console alias |
| the board dtsi that turns it on | `msm8974-mtp.dtsi:24-26` | `serial@f991e000 { status = "ok"; }`, in the one board dtsi in this tree carrying `Copyright (C) 2015 XiaoMi, Inc.` |
| the node | `msm8974.dtsi:253-265` | `blsp1_uart1: serial@f991e000`, `compatible = "qcom,msm-lsuart-v14"`, `reg = <0xf991e000 0x1000>`, `interrupts = <0 108 0>` |

The three Xiaomi board files (`msm8974pro-ac-pm8941-mtp.dts`, `-v4.dts`, `-v5.dts`) carry no uart line
at all, so they inherit the MTP dtsi's `status = "ok"` - the same node 524 section 5 listed without
picking a winner among the three `qcom,msm-lsuart-v14` instances. **The winner is `0xF991E000`**, and no
documentation is needed for it: `MSM_DEBUG_UART_PHYS` is the SoC header's own answer to "which UART is
the debug UART", and the alias agrees.

## 2. The label that must not be used to find the gate

The node is labelled `blsp1_uart1` and it is not BLSP1's UART1. Three independent statements inside this
same tree say it is **UART2**:

1. **the node's own bus property** - `qcom,msm-bus,name = "serial_uart2"` (`msm8974.dtsi:259`), inside
   the very node labelled `blsp1_uart1`;
2. **the clock lookup, keyed by address** -
   `CLK_LOOKUP("iface_clk", gcc_blsp1_ahb_clk.c, "f991e000.serial")` (`clock-8974.c:4962`) and
   `CLK_LOOKUP("core_clk", gcc_blsp1_uart2_apps_clk.c, "f991e000.serial")` (`:4982`). The key is
   `"f991e000.serial"` - the driver's device name is built from the `reg` address, so **the label plays
   no part in which clock the driver gets**;
3. **four other SoC clock files in the same tree** pair the same way -
   `f991d000`<->`gcc_blsp1_uart1_apps_clk`, `f991e000`<->`uart2`, `f991f000`<->`uart3`:
   `clock-9625.c:1791`, `clock-krypton.c:1848`, `clock-samarium.c:3459-3460`, `clock-8226.c:3293`.

The two number series agree, which is the tie-breaker: the UART instances step **0x1000** in the address
map (`0xF991D000`, `0xF991E000`, `0xF991F000`) and **0x80** in the GCC (`0x0684`, `0x0704`, `0x0784`) -
the same index in both. `blsp1_uart1` is the label that is out of step, and `clock-8974.c:4981` is the
matching loose end on the other side: `CLK_LOOKUP("core_clk", gcc_blsp1_uart1_apps_clk.c, "")` names no
device, because this dtsi instantiates no node at `0xF991D000`.

**So for the base `0xF991E000` the core gate is `BLSP1_UART2_APPS_CBCR = 0x0704`**
(`clock-8974.c:356`), and the reader who takes the label writes bit 0 of `0x0684` - the CLI one's
branch - and the console stays dark while every surface says the right thing was done. This is
`mi4-one-value-two-definitions` again, in its cheapest form: **a name is not an address, and only one of
the two is what hardware is wired to.**

## 3. The two gates, and the trap in one of them

The clock lookup above says the console UART needs exactly two clocks: `iface_clk` and `core_clk`.

**`iface_clk` = `gcc_blsp1_ahb_clk`, and it is a *vote* clock.** `clock-8974.c:1766-1775`:

```c
static struct local_vote_clk gcc_blsp1_ahb_clk = {
        .cbcr_reg = BLSP1_AHB_CBCR,            /* 0x05C4 */
        .vote_reg = APCS_CLOCK_BRANCH_ENA_VOTE, /* 0x1484 */
        .en_mask  = BIT(17),
        .base = &virt_bases[GCC_BASE],
        .c = { .ops = &clk_ops_vote, ... },
};
```

and `clk_ops_vote.enable` is `local_vote_clk_enable` (`clock-local2.c:1036`, `:600`):

```c
ena = readl_relaxed(VOTE_REG(vclk));   /* *base + vote_reg */
ena |= vclk->en_mask;
writel_relaxed(ena, VOTE_REG(vclk));
branch_clk_halt_check(vclk->halt_check, c->dbg_name, CBCR_REG(vclk), BRANCH_ON);
```

`VOTE_REG(x)` is `*(x)->base + (x)->vote_reg` (`:56`). So enabling the console's interface clock is

```
   0xF9011000 + 0x1484 = 0xF9012484   |= BIT(17)
```

(`APCS_GCC_CC_PHYS 0xF9011000`, `clock-8974.c:5776`; `APCS_CLOCK_BRANCH_ENA_VOTE 0x1484`, `:528`) - and
**the CBCR at `0xFC4005C4` is not the enable for this clock**: it is the halt status the vote drives, and
the enable path only ever *reads* it. A payload that enables the "obvious" bit at `0xFC4005C4` does
nothing at all on this SoC. That is the trap this section exists for, and it is the same shape as
section 2: two registers named by one source, and the wrong one is the one that looks right.

**`core_clk` = `gcc_blsp1_uart2_apps_clk`, an ordinary branch.** `clock-8974.c:1921-1930`:
`.cbcr_reg = BLSP1_UART2_APPS_CBCR`, `.ops = &clk_ops_branch`, parent `blsp1_uart2_apps_clk_src`;
`branch_clk_enable` (`clock-local2.c:374`) is
`readl; |= CBCR_BRANCH_ENABLE_BIT (BIT(0), :62); writel`. So

```
   0xFC400704  |= BIT(0)
```

Its parent is the root clock generator `blsp1_uart2_apps_clk_src` at `BLSP1_UART2_APPS_CMD_RCGR = 0x070C`
(`:149`, `:1100`), whose sub-registers are `+0x4` CFG (divisor `[4:0]`, source select `[10:8]`, MND mode
`[13:12]`), `+0x8` M, `+0xC` N, `+0x10` D (`clock-local2.c:50-53`, `:68-70`), set by `set_rate_mnd`
(`:127`) which waits for `CMD_RCGR_CONFIG_UPDATE_BIT` (bit 0) to clear. The frequency table
(`ftbl_gcc_blsp1_2_uart1_6_apps_clk`, `:1066`) carries the 115200-baud source as
`F(7372800, gpll0, 1, 192, 15625)`.

**The rate is the one thing the payload should not touch.** A UART's baud rate is the pair (source clock
rate, the divisor in the UART's own registers), and the divisor was programmed by whoever configured the
UART - the bootloader. Enabling a branch does not change a rate; *reprogramming* the RCG without knowing
what divisor is in the UART is how a console comes up as garbage. The two writes above are the whole of
the safe set.

**A third CBCR exists per instance and is not a clock:** every UART has an APPS branch and a SIM branch
(`BLSP1_UART2_APPS_CBCR 0x0704` / `BLSP1_UART2_SIM_CBCR 0x0708`). The header defines all twelve; no
`sim` clock struct is built from any of them in `clock-8974.c`, so the SIM branches are names without
owners. The console needs the APPS one.

## 4. Order, and the bound

Two rules, both from 527 section 6 and now with named addresses under them:

* **Enable before the first access to the UART's registers.** The premise is that an access to a
  peripheral whose AHB clock is gated is not a benign read on this interconnect; **this tree does not
  evidence that premise**, and it is kept because the cost of being wrong is the project's most
  expensive failure - a non-return with no log, which has now happened three times (521, 522, and the
  owed run). Both enabling writes are to blocks that are always on from the apps processor's view (GCC
  at `0xFC400000`, APCS at `0xF9011000`), so they are safe *before* any UART register is touched.
* **Bound the poll.** `while (!(read(SR) & TX_READY));` (524 section 3) is correct against a running UART
  and an unbounded spin against a gated one, and the console path is the one path every later reading
  depends on.

## 5. What the tree cannot answer: the pins

**There is no UART pin configuration in this tree.** `find arch/arm/boot/dts -name '*pinctrl*'` returns
nothing, and no dts defines a uart pin group - so the nodes here never carry a `pinctrl-0`, and the
pinmux for the console is entirely the bootloader's, inherited and unstated.

What the tree does give is where a pin's configuration *would* be read: `MSM8974_TLMM_PHYS 0xFD510000`
(`msm_iomap-8974.h:34`) and `GPIO_CONFIG(gpio) = MSM_TLMM_BASE + 0x1000 + 0x10 * gpio`
(`gpio-msm-v2.c:89` - over the *virtual* `MSM_TLMM_BASE`, so the register **offset**
`0x1000 + 0x10 * gpio` is what transfers to the physical block, not the base constant).

So the pinmux half of 527 section 6's limit is **not answerable from the tree** - which GPIOs, and which
alt-function value, is a property of this board's wiring and its bootloader, and it is answered by
**reading** the TLMM's config words on the device, not by writing them. That is the same shape as the
clock question below: a reading decides it, and reading is the safe instrument.

## 6. Does this phone's own Android kernel use that UART? A reading of the three defconfigs

| config | `MSM_SERIAL_DEBUGGER` | `SERIAL_MSM_HSL` | `SERIAL_MSM_HSL_CONSOLE` | `ANDROID_RAM_CONSOLE` | `PSTORE` |
| --- | --- | --- | --- | --- | --- |
| `cancro_user_defconfig` | **not set** (`:402`) | y (`:1773`) | **not set** (`:1774`) | y (`:3109`) | not set (`:3296`) |
| `lineageos_cancro_defconfig` | - | y (`:372`) | - | y (`:594`) | - |
| `msm8974_defconfig` (reference) | - | y (`:310`) | **y** (`:311`) | y (`:461`) | - |

Two consequences, both narrowings of earlier documents:

1. **On cancro the low-speed UART is not the kernel's console.** The reference 8974 MTP configuration
   compiles the HSL console in (`CONFIG_SERIAL_MSM_HSL_CONSOLE=y`); cancro's own configurations do not.
   This phone's kernel logs to the Android ramconsole, and that is consistent with 527 section 2 - the
   OS's text survives because it is written to a memory block, not to a wire. So a console that drives
   the UART is a channel *added*, for the reason 527 section 8 gave (it is the one channel that outlives
   a hang), not a channel *restored*.
2. **524's vendored `debug_putc` is not a driver this phone runs.** `CONFIG_MSM_SERIAL_DEBUGGER is not
   set` on cancro, so `msm_serial_debugger.c` is not compiled into this phone's kernel at all. It is
   still the right *reference* - its register sequence is the hardware's, and 524 section 2's map came
   from it - but "already in the tree" is not "already in this phone's boot path", and 524 section 1
   read the two as one. The distinction matters exactly here: as a *driver* it would have brought the
   UART up; as a *source* it only tells us the sequence.

## 7. The instrument this specifies - and where it goes in the queue

Everything in section 3 is a *read* if the payload only reads it, and reads of all five blocks (the six
BLSP1 UART CBCRs, the AHB CBCR, the APCS vote word, the TLMM pin config words) cannot gate anything,
because all of them are always-on from the apps processor's side. So one small arm settles the question
527 section 6 could only name:

| what to read | where | why |
| --- | --- | --- |
| six BLSP1 UART CBCRs | `0xFC400684 + 0x80*n`, n = 0..5 (`0x0684`..`0x0904`) | **which instance is running at handoff** - the `ENABLE` bit (bit 0) is set by whoever turned it on, and the label question of section 2 becomes visible as data |
| the AHB / interface gate | `0xFC4005C4` (status) and `0xF9012484` (the vote, bit 17) | whether the bootloader voted the BLSP1 AHB clock in |
| the TLMM config words | `0xFD510000 + 0x1000 + 0x10*g` for a small `g` | which pins are muxed away from GPIO (section 5) |

Its two readings decide the console edit's shape:

* **the console branch is already enabled at handoff** -> the bootloader left it on, the payload needs no
  clock writes at all, and 527 section 6's bounded polled putc can be tried first - the cheapest possible
  test, and the one that costs nothing if it fails;
* **it is gated** -> the payload enables it with the two writes of section 3, **in that order**, before
  its first UART access, and the pinmux reading says whether the pins are even usable.

**This arm is specified here, not scheduled ahead of 526.** 526 is built, gated, frozen and **unrun**;
its run is owed a power press, its image differs from 522's by exactly one thing, and running anything
else first spends the press on a different question and leaves the null's ledger unread. When the device
is back, 526 goes first, and this arm follows it - the console question is 523's phase, and 526 is still
522's.

## 8. What this does not decide

* **The pinmux** (section 5) - tree-silent, device-answerable, and a reading.
* **Whether the Mi 4 exposes this UART on anything reachable.** Unchanged from 524 section 7: the SoC's
  debug port being `0xF991E000` does not say a connector is wired to it, and no reading in this tree can.
* **Nothing about 522 or 526.** This is a reading of files already in the tree, done without the device.

## 9. Safety

Unchanged and untouched: **no device, no build, no switch, no edit to any running script**, and nothing
is ever flashed in this project. `fastboot boot` only, one non-persistent boot per run, through
`preflight_boot_check.sh --allow-xnu-entry` then `run_and_capture.sh --allow-xnu-entry`. The phone is
off the bus (last `usb 3-10` event: 522's `18d1:d00d` device 88 disconnecting at 14:14:46 on 2026-09-22)
and owes a power press before 526 can run.
