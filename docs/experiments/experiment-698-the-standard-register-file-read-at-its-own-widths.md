# 698: the standard register file, read at its own widths — the before-values the driver's reset needs

697's press put the controller in SDHCI mode and left one cell of 696's table unanswered: the
pre-registration's post-mode pair came back as **neither** branch, and the reason was a *name*, not a
reading — the key called `hci_version` reads `hc_mem + 0x00`, which the vendor's own header says is
`SDHCI_DMA_ADDRESS`, while the version register is at `0xFE`. That is corrected here, and the correction is
the smaller half of this step.

The larger half is that the block's **standard register file has never been read at all**. Every key this
project has published about `0xf9824900` so far is one of two *words* — whatever sits at `0x00` and `0x40`.
The registers a driver's bring-up actually reads and writes — `PRESENT_STATE`, `HOST_CONTROL`,
`POWER_CONTROL`, `CLOCK_CONTROL`, `SOFTWARE_RESET`, `SLOT_INT_STATUS` — are unread, and the next act on this
path is the driver's own `sdhci_reset(SDHCI_RESET_ALL)`, which touches four of them. **This step takes the
before-values, read-only, so that the reset's effect on each is a measurement rather than a guess.**

**Nothing is written in this step, and nothing is sent by it.** The press is the next step's act.

## 1. The widths are part of the contract, and an unaligned Device read faults

The vendor's own register dump is the authority for how each register is accessed, and it is a single block
of `sdhci.c` (`:98-128`):

```c
	       sdhci_readw(host, SDHCI_HOST_VERSION));      /* 0xFE */
	       sdhci_readl(host, SDHCI_PRESENT_STATE),       /* 0x24 */
	       sdhci_readb(host, SDHCI_HOST_CONTROL));       /* 0x28 */
	       sdhci_readb(host, SDHCI_POWER_CONTROL),       /* 0x29 */
	       sdhci_readw(host, SDHCI_CLOCK_CONTROL));      /* 0x2C */
	       sdhci_readw(host, SDHCI_SLOT_INT_STATUS));    /* 0xFC */
	       sdhci_readl(host, SDHCI_CAPABILITIES),        /* 0x40 */
	       sdhci_readl(host, SDHCI_MAX_CURRENT));        /* 0x48 */
```

with `SDHCI_SOFTWARE_RESET 0x2F` accessed by `sdhci_readb`/`sdhci_writeb` (`:246`, `:259`), and
`CORE_PWRCTL_CTL 0xE8` / `CORE_PWRCTL_MASK 0xE0` in `core_mem` by `readl_relaxed` (`sdhci-msm.c:2879`,
`:2946`). **This is not a style preference.** `0x29`, `0x28` and `0x2F` are byte registers at offsets no
4-byte access can reach, and `0xFE` is 16-bit at an offset that is not 4-aligned either: a 32-bit load at
any of those four addresses is an **unaligned access to Device memory, which faults on ARMv7** — the same
class of fault 692 measured on this very block (`fsr=0x5`, a section translation fault), reached here by
*alignment* rather than by translation. So this arm adds three widths, not one:

| width | where | offsets |
| --- | --- | --- |
| 8-bit | `st_read8` | `0x28` `HOST_CONTROL`, `0x29` `POWER_CONTROL`, `0x2F` `SOFTWARE_RESET` |
| 16-bit | `st_read16` | `0x2C` `CLOCK_CONTROL`, `0xFC` `SLOT_INT_STATUS`, `0xFE` `HOST_VERSION` |
| 32-bit | `st_read32` | `0x24` `PRESENT_STATE`, `0x40`/`0x44`/`0x48`, and `core_mem`'s `0xE0`/`0xE8` |

**And "the widths are right" is made a property of the linked image rather than a sentence in a comment**:
`build_entry.sh` gains an **alignment census** (§5) — for every memory instruction in `entry_storage_probe`
whose base register is not `sp`, its offset must be a plain immediate satisfying its own access width's
alignment, and a register-offset form refuses the build. That is the clause equivalent of the store census,
and it covers the stores as well as the loads for free.

## 2. The window the reads must stay inside, and the one register that is only read

`msm8974.dtsi:503` is `reg = <0xf9824900 0x11c>, <0xf9824000 0x800>;` with `:504`
`reg-names = "hc_mem", "core_mem"` — so **`hc_mem` is 0x11C bytes long**, and every one of the nine `hc_mem`
offsets above is inside it (`0xFE` is the largest, 254 < 284). The image maps a whole 1 MB section, so a read
outside that window would still *work*; it would be a read of a register this block does not declare, which
is why the window is written down here and every offset is checked against it by hand. (The two `core_mem`
offsets are inside `0x800` by inspection.)

**`POWER_CONTROL 0x29` is READ and never written, and 696 §2's reason for not reading it no longer holds.**
That paragraph argued a read of an address the arm will not act on "is a load that can only fault" — a
fault-risk argument, made when the block's reachability was the open question. The window is now proven
twice (694 and 697 pressed, this block read 28 and 39 keys through it), and the value has become
decision-relevant: **it is the before-value for the one hazard the standard reset carries.** `sdhci.c:1342`
and `:1353` are `sdhci_writeb(host, 0, SDHCI_POWER_CONTROL)` — the *generic* SDHCI core turns the bus off by
writing 0 to this byte, and 531 §8's reading is that on this SoC that write **is** a bus-off request, which
is why `sdhci-msm.c` overrides power handling through `CORE_PWRCTL` and the PMIC instead. Whether
`SDHCI_RESET_ALL` clears this register is the question the next step must not discover on the bench, and
the answer starts with knowing what it reads *now*.

## 3. The rename, its readers, and why this one is safe

`ST_SDHCI_HCI_VERSION 0x00` and the two keys built on it are renamed to what they read:
`ST_SDHCI_DMA_ADDRESS`, `xnu_live_storage_dma_address`, `xnu_live_storage_mode_dma_address`. The evidence is
in 697 §3: `sdhci.h:27` is `#define SDHCI_DMA_ADDRESS 0x00`, and a soft register that the core reset clears
is what the pre/post pair (`0x10` → `0x00`) measured.

[[mi4-one-value-two-definitions]]'s newest instance is **m699 — one rename, two readers**, whose lesson is
that a rename fails *silent* when a reader is missed. So the readers were enumerated before the edit and
there are **exactly three**: the two sites in `entry_storage.c`, and one line in 694's document. The parked
captures are immutable history and are mapped in §7. Nothing in `stages/stage90/*.sh` or `tools/` names
either key (grepped), so no clause goes quiet — and **the readers that a rename cannot update are the
reason the record maps the old name to the new one** rather than assuming a reader will infer it.

## 4. Pre-registered: the cells this arm will be read against

Every key below is a new publication, the ending is unchanged (690's clock, 115,200,000 ticks), and the
whole census runs **after** the mode sequence, so a run that ends at the ending still carries all of them.

| key | offset | what a reading means, and what the alternative would be |
| --- | --- | --- |
| `_mode_host_version` | `hc_mem+0xFE`, 16-bit | **the cell 697 left owed.** `SDHCI_VENDOR_VER_MASK 0xFF00` / `SDHCI_SPEC_VER_MASK 0x00FF` (`sdhci.h:242-245`), and `sdhci-msm.c:2911` branches on the vendor nibble. A plausible spec version (3 or 4 in the low byte) is the first statement this project can make about the *standard* register file being the one the DT's `sdhci@f9824900` names. **A reading of 0 or 0xffff is the finding that this register does not answer even in SDHCI mode** — which would move the whole question from the mode bit to the block |
| `_power_control` | `hc_mem+0x29`, 8-bit | **the step's most important before-value.** Bit 0 is `BUS_POWER`; the vendor's own `CORE_PWRCTL_BUS_ON` name is the MSM side of the same fact. A value with bit 0 set says the standard block believes the bus is powered; **a reading of 0x00 says it does not**, and that reading would make the next step's `SDHCI_RESET_ALL` harmless by construction rather than by argument |
| `_clock_control` | `hc_mem+0x2C`, 16-bit | bit 0 `internal clock enable`, bit 2 `SD clock enable`, bits 8-15 the divider. 694's `_bcr`/`_cbcr` proved the **branch's** clock is enabled (`clock-8974.c`'s CBCR), which is not the same thing as this register's SD clock: a reading with bit 2 clear says `sdhci_set_clock` is ahead of the driver, and bit 0 clear says even the internal clock is stopped |
| `_present_state` | `hc_mem+0x24`, 32-bit | the flags and `SDHCI_CMD_INHIBIT` (bit 0) / `SDHCI_DATA_INHIBIT` (bit 1), which should both be clear on a block with no transfer in flight. **`SDHCI_CARD_PRESENT` (bit 16) must NOT be read as "no card"**: `sdhci-msm.c:2896` sets `SDHCI_QUIRK_BROKEN_CARD_DETECTION` and the DT's `qcom,bus-width = <8>` describes a soldered eMMC whose detection is a GPIO — so 0 is the expected value and is not evidence about the medium |
| `_software_reset` | `hc_mem+0x2F`, 8-bit | **a check, and it must read 0.** The bit is self-clearing; a non-zero read is a reset stuck in progress, which would falsify 696's and 697's "no reset ran" rather than this arm's premise |
| `_slot_int_status` | `hc_mem+0xFC`, 16-bit | which slot has a pending interrupt. Non-zero is not a fault — the vendor enables interrupts later — but it is the register `sdhci_reset`'s `SDHCI_QUIRK_RESTORE_IRQS_AFTER_RESET` path exists for |
| `_capabilities_1` | `hc_mem+0x44`, 32-bit | the upper capability word, unread until now; `_capabilities` (`0x40`) has been read three times and this is its sibling |
| `_max_current` | `hc_mem+0x48`, 32-bit | the identification set's last word |
| `_pwrctl_mask` | `core_mem+0xE0`, 32-bit | **the other half of 696 §3's hazard.** 697 measured the *status* (0, nothing latched); the mask says whether a power IRQ would have been *routed* to the GIC at all, which is the half a status register cannot answer. `sdhci-msm.c:2946` writes `INT_MASK` here |
| `_pwrctl_ctl` | `core_mem+0xE8`, 32-bit | the before-value for the vendor's own acknowledge (`:2879` reads it, `:2884` writes it back with the success bits, `:2069` writes it a byte at a time). **The vendor reads it 32-bit and writes it 8-bit at one site**, which is recorded because it is exactly the kind of thing a reader would otherwise re-derive |
| `_reg_loads = 10` | — | the census's own count, **counted bottom-up** rather than written down: a number the arm publishes about what it did, so the record's "ten reads" and the log's number are two derivations of one fact (m688) |
| a non-return (exit 2) | — | unchanged in shape and now much less likely: the gate is proven open twice, so this means **the register file did not tolerate a read at a width the vendor's own accessors use** — an alignment fault, which §1 and §5 exist to make unreachable by construction |

## 5. The build's part: the alignment census

`build_entry.sh`'s store census (696 §7.2) refuses a rung ≥ 2 image whose probe stores anywhere but the
four offsets on the controller's base. It was expected to pass unchanged — "this step's rung adds no store" —
and **it did not**: the rung-3 build was refused, and *this record's own premise was the thing that was
wrong about it*. The refusal was

```
FAIL: entry_storage_probe stores to [r6:4 ], which are neither one of the four the record names
(base r5: 120 0 120 120) nor an offset-0 write of a pointer (the body's first-call guard).
... The stores found on the four's base are [120 0 120 120 ] and the rest are [r6:0 r6:4 ]
```

**`[r6:4]` is not a device store.** `r6` is `.LANCHOR0` at `0x805541a4`, and the two words the body stores
there are `g_storage_probed` and — the flag this very step added — `g_storage_mode_complete` at
`0x805541a8`. GCC put the two adjacent `.bss` flags on one base register, so the second one arrives as
offset 4, and 696's clause read *any* non-`sp` base as a device base and allowed only offset 0 outside the
four. The clause's sentence said "every store in that body", and the quantity it meant to bound was "every
store **to the block**" — the [[mi4-measurement-defects]] shape, where the extractor's scope and the claim's
scope are two different things and only the narrower one is true of the machine.

So the clause is repaired here, and **the repair is what makes 698's "no new store" a reading rather than an
assumption**: each store's base register is classified by what the body materializes it with, over the whole
body, and the classes are

| class | evidence | effect |
| --- | --- | --- |
| `DEV` | a `movt` with high half ≥ `0xf000` (this SoC's blocks are `0xf90..`, `0xf98..`, `0xfc4..`) and no image one | bounded: the four offsets, one base, in order |
| `IMG` | a `movt` with high half < `0xf000` (this image's `0x8000_0000`–`0x8059_0000`) and no device one | listed in the build log, out of scope |
| `AMB` | both — GCC reuses registers (`r3` carries `0x8054` and `0xf982` in this very body) | **refuses the build** |
| `UNK` | no `movt` at all — a literal-pool or computed address | **refuses the build** |

`AMB` and `UNK` refuse because the clause cannot say which address the store uses, and a build check has to
fail closed. **The first draft of the repair classified in line order and was wrong in the unsafe
direction**: it judged a store at `0x8000d478` against the `movt r3, #0xf982` that only appears at
`0x8000d52c`, so a device base materialized *after* its store would have been read as `IMG` — waived. The
classification is therefore collected over the whole body and applied at the end, and the clause now
**prints its classification on success** (`xnu_entry_698: the probe's stores, classified by base register -
device [120 0 120 120 ] on r5, image [r6:0 r6:4 ], ambiguous [], unknown []`) rather than passing silently
([[mi4-silence-is-a-reading-only-if-success-is-silent]]).

**The branch is proven in both directions on the real artifact and on controls**: the live rung-3 image and
the pressed rung-2 arm both accept with exactly the four device stores (their images differ only in which
`.bss` guard the body has), and seven mutations of the rung-3 disassembly each refuse through the branch the
mutation was built to reach — offset `120 → 124` (the offsets string), a fifth device store (the count), a
store moved onto the `IMG` base `r6` (the count falling to three), a device store moved to `r3` (`AMB`), to
`r9` (`UNK`), and a fifth store added on a second device base `r4` (the second-base branch).

## 5.1 The alignment census, beside the store census

Beside it goes a second
clause of the same shape, run for rung ≥ 3: for every memory instruction in `entry_storage_probe` whose base
register is not `sp`, the offset must be a plain immediate that satisfies its own access width's alignment
(`ldr`/`str`+`ldrd`/`strd` → 8, `ldrh`/`strh` → 2, `ldrb`/`strb` → 1), and a register-offset or
writeback-immediate form refuses the build with the instruction quoted. **This is the clause that makes §1's
widths a property of the artifact**: `0x29` read 32-bit, or `0xFE` read 32-bit, or any offset computed into
a register, stops the build instead of faulting on the bench.

## 6. What this does not do

* **It does not touch the medium, the card, or the bus's power state.** Ten reads and a rename; no store
  anywhere, and the store census proves it.
* **It does not perform the driver's reset or clock set-up.** It measures the registers those two would
  change, so that the next step's cells have before-values.
* **It does not mount anything or make storage usable.**
* **TWRP-to-storage stays withheld**: the user's condition is that the OS can already be entered and stays,
  and the OS is not observed doing that.

## 7. Owed

* **The driver's own reset and clock**, named so it is not re-derived: `sdhci_reset(SDHCI_RESET_ALL)` writes
  `0x01` to `SOFTWARE_RESET 0x2F` (byte) and polls it clear (`sdhci.c:246-266`), and **`sdhci.c:250` is the
  line that matters**: `if (mask & SDHCI_RESET_ALL) host->clock = 0;` — the reset stops the clock, so
  `sdhci_set_clock` must follow, and the survey's `CLOCK_CONTROL` cell is its starting point. **And the
  reset's own publisher must read `POWER_CONTROL` back afterwards**, because whether the standard reset
  clears the bus-power bit is the one thing this step cannot answer and the next one can.
* **The `_dma_address` rename's old name**, for any reader of a 694/697 capture: those logs' `*_hci_version`
  keys are this arm's `*_dma_address` keys, same offset, same read.
* **Carried, unchanged**: what actually returns a run (8/17/24/27 s); the ending's first store still faulting
  into a panic (`RESTART_REASON 0x0fa0065c` unmapped); the 691 §5 `entry_note_wfi` readback; `entry_reset.h`'s
  false IMEM claim; 676 §6 / 677 §6; the 684-owed runner clause for the 678 arm;
  `tools/xnu_dt_requirements.py` and the `"master"` value; the two peer-lane tripwire repairs; the gate's
  narration for `STAGE90_XNU_STORAGE_PROBE`; and **the seam address pinned in two files** (`entry_trace.c`
  and `run_and_capture.sh`), which the last step had to move in the second one after readiness refused the
  tree with nothing spent.
* **The step after the reset**: the clock (`sdhci_set_clock` on `CLOCK_CONTROL`) and only then the bus
  power state, because the first act the *medium* would see is a command, and 531's sequence puts
  `CORE_PWRCTL_CTL`'s bus-power/IO-voltage state ahead of `CMD0`/`CMD8`.
