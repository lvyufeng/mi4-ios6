# 701: the rung-4 pre-registration — the driver's own reset, and why the clock is **not** on this path

699 left one next step named: *the driver's own `sdhci_reset(SDHCI_RESET_ALL)` and the clock, now with
before-values*. This record is the design of that step, and it is written **before** the code, because
reading the vendor's own path turned up two facts that decide what the rung may contain — one of them
narrows it, and one of them takes the clock off it entirely.

**Nothing here is sent and nothing is built.** This is the pre-registration the ladder's next rung will be
read against, in the same shape as 696 §4 and 698 §4.

## 1. What the driver actually does, read out of the vendor's own source

`sdhci_reset(host, SDHCI_RESET_ALL)` is `sdhci.c:229-279` and, for this SoC, it is **one byte write and a
bounded poll** — everything else in the function is guarded by state a freshly initialized host does not
have:

| line | the act | reached on this SoC? |
| --- | --- | --- |
| `:241` | `ier = sdhci_readl(SDHCI_INT_ENABLE)` if `SDHCI_QUIRK_RESTORE_IRQS_AFTER_RESET` | only with the quirk; `sdhci.c` sets it for one platform family, not `sdhci-msm` |
| `:243` | `host->ops->platform_reset_enter` | **no** — `sdhci_msm_ops` (`sdhci-msm.c:2653-2666`) has no such member |
| **`:246`** | **`sdhci_writeb(host, mask, SDHCI_SOFTWARE_RESET)`** — `0x01` to `hc_mem + 0x2F` | **yes, and it is the rung's whole store** |
| `:249` | `if (mask & SDHCI_RESET_ALL) host->clock = 0;` | software state only — no register touched |
| `:254-256` | `check_power_status(host, REQ_BUS_OFF)` if `host->pwr && (mask & SDHCI_RESET_ALL)` | **no** — `host->pwr` is 0 (below) |
| `:259-268` | poll `sdhci_readb(SDHCI_SOFTWARE_RESET) & mask`, max 100 × `mdelay(1)` (`:267`) | **yes** — the same byte, read |
| `:271` | `platform_reset_exit` | **no** — same reason as `:243` |
| `:272-278` | `QUIRK_RESTORE_IRQS_AFTER_RESET`, then `enable_dma` | no quirk, and no DMA use in the fixture |

**`SDHCI_RESET_ALL` is `0x01`** (`sdhci.h:114`), so the mask and the write are the same value and the poll
tests the same bit it set — the standard's "the bit is self-clearing" contract, which **697 measured
directly**: the vendor's own mode sequence polled `CORE_SW_RST` and it was already clear on the first read
(`_mode_rst_polls=1`, `_mode_rst_ticks=0x6`). That is a *different* reset bit in a *different* block, and it
is cited only as evidence that this controller's reset bits behave self-clearingly.

**And the two guards that keep this rung small are worth naming, because each one is a cell of the driver's
state and not a property of the hardware:**

* `host->pwr` is 0 on a fresh host and `sdhci_set_power` (`sdhci.c:1336-1339`) **returns `-1` early when the
  requested power equals the current one** — so the standard bus-off write
  (`sdhci_writeb(host, 0, SDHCI_POWER_CONTROL)`, `:1342`, the write 531 §8 named) is **not reached** on a
  host that has never powered up, and neither is the MSM's `check_power_status(REQ_BUS_OFF)` after it.
* **`sdhci_msm_check_power_status` writes no register at all** (`sdhci-msm.c:2179-2209`): it compares the
  request against *software* state (`msm_host->curr_pwr_state`, `curr_io_level`) and, when that state does
  not already satisfy it, **`wait_for_completion(&msm_host->pwr_irq_completion)`** — it blocks on a power IRQ
  the PMIC/GCC raises. **That is a bus wait with no bound, and it is the one act in this path that could
  hang this device.** It is unreachable here *because of the `host->pwr` guard*, and that is exactly why
  rung 4 must **not** emulate it: a payload that "finished the driver's init" by calling it would be waiting
  for an interrupt nothing in this image raises.

## 2. The clock is not a register write on this SoC — it is the GCC, and that is a separate step

That last sentence of 699 §6 is what this reading corrects. The generic `sdhci_set_clock` writes
`SDHCI_CLOCK_CONTROL`; **`sdhci_msm_ops` replaces it** (`.set_clock = sdhci_msm_set_clock`,
`sdhci-msm.c:2659`), and `sdhci_msm_set_clock` (`:2402-2535`) never writes `CLOCK_CONTROL`:

* it reads and writes **`CORE_VENDOR_SPEC`** — `sdhci-msm.c:92` `#define CORE_VENDOR_SPEC 0x10C`, in
  **`core_mem`** — for `CORE_CLK_PWRSAVE` (`:93`, bit 1) and `CORE_HC_MCLK_SEL_*` (`:96`), i.e. **a fifth
  offset on a base the census currently bounds to four**;
* and the actual rate/enable is the **clock framework**: `sdhci_msm_prepare_clocks` (`:2315`) is
  `clk_prepare_enable` on the controller/bus/ff/sleep clocks and `clk_set_rate(msm_host->clk, sup_clock)`
  (`:2520`), which on msm8974 is `clock-8974.c`'s SDCC branch — registers in the **GCC** at `0xfc400000`.

**`0xfc400000` is the megabyte 692 pressed and measured as NOT MAPPED**: its first dereference faulted
(`far=0xfc4004c0`, `fsr=0x5`, the section translation fault), and that address is the very clock gate the
probe read as an interlock. So "set the clock" on this device means *mapping a third block, then reaching a
clock gate and an SDCC clock branch in it* — a step of the same shape 692 and 693 were about, with its own
hazards, and it is **deliberately not bundled into rung 4**. Rung 4 is the reset alone.

## 3. The before-values — every one of them measured by 699's press

The reset's effect is a measurement **only because** these exist, which is why 698 was a rung of reads
before a rung of writes:

| register | before-value (699) | what the reset would be read against |
| --- | --- | --- |
| `SOFTWARE_RESET 0x2F` | **`0x00`** | the poll's premise: nothing is in progress, so a non-clearing bit after the write is the reset *failing*, not a leftover |
| `POWER_CONTROL 0x29` | **`0x00`** | **the owed cell.** `BUS_POWER` is clear, so the standard block does not believe the bus is powered — and this rung reads it **back after** the reset, which is the question 698 §2 could only pose |
| `CLOCK_CONTROL 0x2C` | `0x0003` | internal clock enabled **and stable**, SD clock enable (bit 2) clear — so the reset's own `host->clock = 0` has no register to move, and the clock step is genuinely later |
| `PRESENT_STATE 0x24` | `0x01f80000` | both inhibit bits clear: nothing is in flight, so a reset is not being asked to abort a transfer |
| `CORE_PWRCTL_MASK 0xE0` | `0x0000000f` | **four bits of power IRQ are routed** — the hazard below is armed, and 699 §1 is what makes it readable as *armed* rather than unknown |

## 4. Pre-registered: the cells rung 4 will be read against

The arm is rungs 1–3 unchanged (probe, mode sequence, census) plus **the reset at the tail of
`entry_storage_probe`**, after the census. The ending is 690's clock, so a run that ends at the ending still
carries everything below.

| key | what a reading means, and what the alternative would be |
| --- | --- |
| `_rst_wrote` | `1` if the `SOFTWARE_RESET <- 0x01` store was reached. `0` with `_rst_stage=0` is the refusal cell: the rung's guard did not open (which, given 699's readings, would itself be the finding) |
| `_rst_polls` | how many reads of `0x2F` it took for the mask to read clear. **`1` is the expected value** and is what 697's `CORE_SW_RST` poll also measured; a large number is a slow reset; hitting the bound is the next cell |
| `_rst_ticks` | the CNTVCT ticks the poll took, on the same 19,200,000 Hz 699's ending read out of `cntfrq`. The bound is **1,920,000 ticks = 100 ms**, the vendor's own timeout (`sdhci.c:251-252`), so the arm's bound and the driver's bound are the same number and a difference would be a record error |
| `_rst_timeout` | `1` if the bound expired with the bit still set — the driver's own `Reset 0x%x never completed` path (`:260-264`). **A `1` here is not a fault of the run**: it is the reading that this controller would not clear the bit, and it is why the poll is bounded rather than open |
| **`_reg_power_control_after`** | **the rung's most important new cell and the one 698 §2 explicitly owed.** `0x00` means the standard reset did not turn the bus on; a value with bit 0 set would mean the reset *changed the block's belief about the bus*, which the driver would then have to reconcile with `CORE_PWRCTL` |
| `_reg_software_reset_after` | the last value read in the poll — the same byte the poll tested, published so the poll's own terminal state is a reading and not an inference |
| `_reg_pwrctl_status_after` | **the hazard's cell.** 696 §3 pre-registered that the reset may latch a power-IRQ status when the previous state was `BUS_ON`, and 699 §3 measured `_core_power=0x00441` (the bus-on state the vendor's logic was written for). `0` means nothing latched again; non-zero means the reset *did* latch one, and the four masked bits of `_reg_pwrctl_mask` are what say it also went to the GIC |
| `_reg_host_control_after`, `_reg_present_state_after` | the two registers the *generic* core's reset path can touch on other platforms (`HOST_CONTROL` is restored under a quirk; the inhibit bits say whether a transfer was aborted). Reading them makes "this reset moved exactly one register" a measurement |
| `_rst_stores = 1` | **counted bottom-up**, like `_reg_loads`: the number of stores this rung makes, so the record's "one store" and the log's number are two derivations of one fact |
| the negative cell: **a non-return (exit 2)** | the write to a block whose clock is off is a bus wait nothing ends — the same class as 692's and 697's, now on the *write* side. It is the reason the write is one byte to one register and not a sequence |

**And what is deliberately NOT published**: no `CLOCK_CONTROL` write and no clock cell, because §2 takes the
clock off this rung. Rung 4 writes **exactly one register**.

## 5. The build clause this rung needs: the census learns a second device base

This is the first rung whose store is **not** on `core_mem`. The census 698 repaired classifies each store's
base register by what the body materializes it with (`DEV` / `IMG` / `AMB` / `UNK`) and bounds the `DEV`
class to **one base carrying four offsets in order** (`120 0 120 120`). Rung 4 adds a store on
**`hc_mem` (`0xf9824900`)**, so the clause has to become *two named bases with their own expected offset
lists*:

| base | window | offsets, in order | why |
| --- | --- | --- | --- |
| `core_mem` `0xf9824000` | `0x800` | `120 0 120 120` | the vendor's mode sequence, unchanged since 696 |
| **`hc_mem` `0xf9824900`** | **`0x11c`** | **`0x2F`, exactly once** | **the standard reset byte, and nothing else in that window** |

**`POWER_CONTROL 0x29` stays unreachable by construction and that is the clause's whole value**: it is a byte
register in the *same* window, four offsets from the one rung 4 writes, and writing 0 to it **is** a bus-off
request on this SoC (531 §8). A census that names `0x2F` and refuses everything else in `hc_mem` makes "the
rung that writes the reset bit cannot write the bus-off bit" a property of the linked image rather than a
sentence in a comment — which is exactly the shape [[mi4-a-claim-in-a-comment-is-not-a-check]] asks for. The
alignment census (§698 5.1) already covers the widths: `0x2F` is a byte store and `0x29` a byte store, both
satisfying their own width, so the *width* clause does not distinguish them — the *offset* clause is the one
that does.

## 6. What this does not do

* **It does not set the clock** (§2: that is the GCC, a third block, and its own step).
* **It does not touch the bus power state** — no `POWER_CONTROL` write, no `CORE_PWRCTL` write, no
  `check_power_status` emulation; §1 is the enumeration of why each of those is out of this path *in the
  driver itself*, not merely out of this rung.
* **It issues no command, reads no sector, mounts nothing.** One byte written to one register of a controller
  whose register file has now been read ten times.
* **It does not build or press** — this record is the pre-registration; the rung's code, its census clauses
  and its press are the next step's.
* **TWRP-to-storage stays withheld**: the user's condition is that the OS can already be entered and stays,
  and the OS is not observed doing that.

## 7. Owed

* **Two line numbers in 698 §7 are off by one and by two, corrected here by measurement** (`grep -n` on the
  vendor's file): the clock line is `sdhci.c:249` (698 says `:250`) and the reset poll is `:259-268` (698 says
  `:246-266`). Nothing built on those citations is wrong — the code they point at is the code named — but a
  citation a reader follows to the wrong line is the smallest member of the family this project keeps records
  for, and the numbers above are the measured ones.
* **The rung-4 code itself**: the `#if STAGE90_XNU_STORAGE_PROBE >= 4` block, the `#error` bound moved from
  3 to 4, `build_entry.sh`'s `case` and its two-base census, then the build and (separately) the press.
* **The clock step, now correctly named**: `sdhci_msm_set_clock` at `CORE_VENDOR_SPEC 0x10C` plus
  `clk_set_rate` on the GCC's SDCC clocks — i.e. **map `0xfc400000` first**, which is where 692's fault was,
  and then the clock gate the probe read as an interlock becomes the subject.
* **Carried, unchanged**: what actually returns a run (8/17/24/27/24 s); the ending's first store still
  faulting into a panic (`RESTART_REASON 0x0fa0065c`); the 691 §5 `entry_note_wfi` readback;
  `entry_reset.h`'s false IMEM claim; 676 §6 / 677 §6; the 684-owed runner clause for the 678 arm;
  `tools/xnu_dt_requirements.py` and the `"master"` value; the two peer-lane tripwire repairs; the gate's
  narration for `STAGE90_XNU_STORAGE_PROBE` being two rungs short (reported by message, 700 §8); and the
  seam address pinned in two files.
