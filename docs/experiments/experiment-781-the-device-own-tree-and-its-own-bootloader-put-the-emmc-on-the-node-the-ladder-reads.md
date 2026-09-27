# 781: the device's own tree and its own bootloader put the eMMC on the node the ladder reads — and the tree that wins is a hardware subtype no file here carries

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER,
NOTHING BUILT.** `out/` was not touched and no entry source was edited, so the armed rung-28 arm
(`armed-storage-d5d98738`, `STAGE90_XNU_STORAGE_PROBE=27`), every park and every capture are
byte-identical before and after. One press spent by this step: **none**. The two files read below
are the device's own, both `.gitignore`d — so, as in 779, these are readings of *this machine's*
backup and not of the repository, and the reading is committed as a **tracked record** so a clone
holds it.

## 1. What was read, and why the record and not a citation

| file | bytes | gitignored by | what was read from it |
| --- | --- | --- | --- |
| `xiaomi4-cancro-backup-20260604-112053/boot-unpacked/dt.img` | 2,521,088 | `.gitignore:2` | the controller census, §2 — sha256 `c8faf487…`, the same input `records/sdc1-pad-candidates.txt` names |
| `xiaomi4-cancro-backup-20260604-112053/aboot.img` | 4,194,304 | `.gitignore:2` | every string in the bootloader, §5 |
| `xiaomi4-cancro-backup-20260604-112053/boot.img` | 33,554,432 | `.gitignore:2` | the stock kernel command line, §4 |

779 established that the ladder's only board-derived constant is **MTP V5's word and not this
device's**, because the comment beside it cites a vendor board file that is not in the repository
and does not describe this device. This step asks the same question one level down: not *which
pad word* the device declares, but **whether the node the ladder reads is a controller this
device enables at all, and whether a second one is enabled beside it**. Both are in the same
`dt.img` and neither is visible in the pad table 779 read.

## 2. The census: 6 trees, 20 controller nodes, 4 addresses

`tools/derive_sdc1_pads.py --mmc-census` (added here) walks every tree and prints every node
whose name this SoC gives a controller. The whole table, from the device's own file:

| address | node | status | `reg` | on trees | always-on | `bus-speed-mode` |
| --- | --- | --- | --- | --- | --- | --- |
| **`0xf9824900`** | `sdhci@f9824900` | **ok** | `<0xf9824900 0x1a0 0xf9824000 0x800>` | 5 of 6 (#0,#1,#2,#3,#5) | **yes / yes** | `HS400_1p8v, HS200_1p8v, DDR_1p8v` |
| `0xf9864900` | `sdhci@f9864900` | disable | `<0xf9864900 0x11c 0xf9864000 0x800>` | 5 of 6 | — | — |
| `0xf98a4900` | `sdhci@f98a4900` | **ok** | `<0xf98a4900 0x11c 0xf98a4000 0x800>` | 5 of 6 | **NO / NO** | — |
| `0xf98e4900` | `sdhci@f98e4900` | disable | `<0xf98e4900 0x11c 0xf98e4000 0x800>` | 5 of 6 | — | — |

**Two nodes are enabled and two are not, and the vendor's own `msm8974.dtsi` names what they
are** — `sdhc1 = &sdhc_1; /* SDC1 eMMC slot */`, `sdhc2 = &sdhc_2; /* SDC2 SD card slot */`,
`sdhc3`/`sdhc4` *SDIO* (`:24-27`), with the four addresses at `:500`, `:530`, `:557`, `:599`.
**So the ladder's address is SDC1, the eMMC slot**, and the other enabled one is SDC2, the SD
card slot. The device's own file agrees with the vendor's on all four addresses and on which two
are enabled.

**The address alone does not say which is which, and the properties do.** SDC2 is enabled, has
its own `vdd-supply`/`vdd-io-supply`, and would look like a candidate for an eMMC to a run that
only had the address. What separates them, field by field, in the device's own declarations:

| | `0xf9824900` (SDC1) | `0xf98a4900` (SDC2) |
| --- | --- | --- |
| `qcom,vdd-always-on`, `qcom,vdd-io-always-on` | **present** | **absent** |
| `qcom,bus-speed-mode` | `HS400_1p8v, HS200_1p8v, DDR_1p8v` | absent |
| `qcom,vdd-io-voltage-level` | 1,800,000 / 1,800,000 (`0x1b7740 0x1b7740`) | 1,800,000 / 2,950,000 (`0x1b7740 0x2d0370`) |
| `qcom,pad-pull-on` / `qcom,pad-drv-on` | four entries (CLK, CMD, DATA, **RCLK**) | **three** entries (no RCLK) |

An always-on supply pair, a declared 1.8 V-only I/O, a four-entry pad array with the RCLK field,
and an HS400/HS200 list is an **eMMC on a non-removable slot**; a two-ended voltage range, no
always-on flags and no RCLK field is a **card in a socket**. That is a reading of the device's
own declarations and not an inference from the labels.

## 3. The ladder's node is enabled in every tree that carries it, with the window the ladder uses

`ST_HC_MEM_BASE 0xf9824900u` and `ST_CORE_MEM_BASE 0xf9824000u` (`src/entry/entry_storage.c`)
are the two windows of **the first `reg` cell pair of a node the device's own tree marks `ok`
in all five trees that carry it** — and the first cell's length is `0x1a0`, the *widened* window
531 read out of the vendor's `msm8974pro.dtsi:1763-1769` (which widens `&sdhc_1` from `0x11c` so
that `CORE_DDR_200_CFG 0x184` is inside it). **The device's own file says the same thing the
vendor's override said**, and it is the same number: `0x1a0` on `0xf9824900` in five trees,
`0x11c` on each of the other three controllers.

**And the one property that varies across those five trees is the pad declaration 779 found.**
The census prints it as varying, per tree, on purpose:

```
qcom,pad-pull-on           0x0 0x3 0x3 0x1
qcom,pad-drv-on            0x7 0x4 0x4 (trees #0) | 0x4 0x4 0x4 (trees #1, #2, #3, #5)
```

That is 779's table from the other direction: the trees declare **two** words, `0x00009FE4` on
the PMA8084 MTP tree and `0x00009F24` on the other four, and the ladder's `ST_TLMM_SDC1_EXPECT`
is the second. It is also the reason the census prints a *variation* and not the first tree's
value: an aggregate row carrying tree #0's array would be 779's defect one level up.

## 4. And the device's own bootloader says which controller the device boots from

`boot.img`'s kernel command line, the one the bootloader wrote, is:

```
console=none vmalloc=340M androidboot.hardware=qcom msm_rtb.filter=0x3b7 ehci-hcd.park=3
androidboot.bootdevice=msm_sdcc.1 androidboot.selinux=permissive buildvariant=userdebug
```

**`androidboot.bootdevice=msm_sdcc.1`**: the boot device is SDCC instance **1**. The string is
on record already — experiment 01 and 608 carry the same line, the latter for the TWRP image —
**but it has never been used as what it is here: the device's own statement that the thing it
boots Android from is the controller at `0xf9824900`.** That is the eMMC, and it is the node the
ladder reads. `androidboot.emmc=true` and `androidboot.hwversion=47` sit in the longer form of
the same line in experiment 01.

## 5. What the bootloader does *not* say, and why 779's open question is not answerable here

779 left *which of the device's trees the bootloader selects* unestablished, and this step
narrows that from "not established" to **"not establishable from anything in this archive"** —
which is worth having, because it is the difference between a step that is owed and a step that
cannot be done:

* The **only** board-identification string anywhere in the 4,194,304 bytes of `aboot.img` is
  `qcom,board-id`. There is no `cancro`, no `xiaomi`, no reference-board name and no table of
  board-id values in the bootloader — the matching is generic, against the `qcom,msm-id` /
  `qcom,board-id` cells each tree carries.
* Those cells are the file's own differentiator, and they are: `(8, 1)` for the PMA8084 MTP tree,
  `(8, 256)` MTP, `(8, 1024)` MTP V4, `(8, 1280)` MTP V5, `(8, 0)` 8974Pro-AA/AB MTP, and the
  SAMARIUM RUMI tree carries no `board-id` at all and no controller either.
* **A board-id is a value the bootloader reads off the hardware**, not a value in a file. So the
  question "which tree wins" is a question about a fuse/strap state on the physical device. It
  is answered by the device — and the rung-28 arm's `_pad_raw` is the cheapest place it has ever
  been asked.

## 6. What this closes, and what it is the repair of

* **`the ladder may be reading a controller the eMMC is not on, or a node the device's own tree
  disables` is closed, by the device's own files.** It was never named as a candidate in the
  record, and every rung since 531 has assumed it: the base, the window and the identity of the
  block were read out of the *vendor's* device trees and out of the driver. The address is now
  confirmed against the file the device itself carries, in all five trees that declare it, with
  `status = "ok"`, with the supplies, and — independently — against the bootloader's own
  `androidboot.bootdevice=msm_sdcc.1`.
* **It is also the repair, one level up, of 779's defect.** 779's finding was that a citation to
  a board file that is not this device's had been standing in for a reading of this device. The
  same class of assumption ("SDC1 is the eMMC") had been standing in for a reading of the
  device's own tree; it now has one, and a check behind it (§7).
* **It does not change the frontier.** The candidate space after the rungs is still *the card's
  power and the clock at its pins* — 780 weakened the rail reason for closing the first, and the
  armed rung-28 arm is what asks the pad question. Nothing here bears on why a correctly
  configured block that drives the CMD line gets no answer; it removes the *address* as a
  possible explanation, which is a whole class off the table at no cost.

## 7. The repair is a record line and a second refusal, so it is a check and not a sentence

A sentence in a document is not a constraint, and this one has to hold on a machine where the
`dt.img` does not exist. So:

| artifact | what it is |
| --- | --- |
| `records/sdc1-pad-candidates.txt` | now carries, per distinct controller address, one machine-readable `# node 0x… <name> core 0x… window 0x… status … trees n/m` line — rewritten by `tools/derive_sdc1_pads.py --write-record` from the device's own file, beside the input's sha256 it already carried |
| `tools/derive_sdc1_pads.py --mmc-census` | the census of §2, printable from any QCDT blob, with a property that differs between trees printed as differing and the trees named |
| `tools/check_sdc1_pad_expectation.py` | **a second refusal, about addresses**: the record must name `ST_HC_MEM_BASE`'s address, with status `ok`, paired with `ST_CORE_MEM_BASE` — otherwise `make check` refuses |

**The refusal is falsified, not asserted.** Four scratch records: the ladder's node line removed
→ refused; its status flipped to `disable` → refused; its `core` base swapped to the other
enabled node's → refused; the address renamed → refused. Each prints the reading it refused on.
Six cells in the tool's own `--selftest` pin `node_verdict` as a pure function (including the
case where *both* controllers are carried and only one is the ladder's), and `make check` runs
it. **What the refusal does not check, said plainly**: it compares the record against the
source's two `#define`s, so it catches an address the device's own tree does not enable *and*
cannot see whether the record itself was written from this device — that half is the input's
sha256, which is a value a rewrite cannot invent and a reader has to compare.

## 8. What this does not say

* **It does not say the card is powered, clocked, or able to answer.** The node being enabled,
  the supplies being declared always-on and the boot device being SDCC1 are declarations and an
  identification; **not one capture in this archive contains a PMIC, SPMI, regulator or card
  reading of any kind.**
* **It does not pick a device tree** (§5), and it does not say the pads are configured — that is
  what the armed arm reads.
* **It does not touch `out/`**, does not build, and spends no press: the rung-28 arm is still
  armed and unspent and the press is the operator's.

## 9. What is owed, and named rather than left to be inferred

- **`tools/derive_sdc1_pads.py` still has no self-test** — 779's debt, now one mode wider
  (`fdt_nodes`, `census`, `prop_summary` are new code in the same file, and `prop_summary`'s
  "print a difference as a difference" rule is exactly the kind of behaviour a cell should pin).
- Unchanged from 775–780: the four `5,088,000`s and the mis-citation at `entry_storage.c:302-303`
  (COST-owed, to be carried by a build); the substantive pad repair (compare against the **set**
  of candidate words, pre-registered by 779, not built); the `rung_para` correction for values
  12..23; the seam-address class; `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; the 737 window
  paragraph (paid by the rung-28 build).

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block issues a
response-demanding command, drives the CMD line, and **times out because the card does not
answer** — and 「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage stays withheld.**
