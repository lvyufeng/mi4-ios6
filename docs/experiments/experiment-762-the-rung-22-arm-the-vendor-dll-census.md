# 762: the rung-22 arm — the vendor's DLL and `HOST_CONTROL2` census, three reads and no store, and the readiness row that refused my own narration for not naming the arm's bound

**Host-side. One arm was BUILT and PARKED; no press was spent, no gate was fired against a device, no
runner ran, no firer is armed, and nothing was flashed or written to storage.** The measurements are two
builds' own exit status and clause output, `cmp`/`sha256sum` over the live `out/stage90` and the new park,
`tools/verify_press_ready.sh`, `make check`, and the vendored kernel tree read at line numbers. `out/`'s
previous arm (`armed-storage-46fe6737`, the arm 758's press spent) is untouched.

**Why this exists.** 756 §1 named two registers the vendor's own bring-up programs and this ladder has
never touched; 756 §6 pre-registered the discriminating reads; 761 §5 closed the alternative space by
enumerating every register the vendor's power-up path writes, leaving exactly **two** candidates and both
of them *disables the ladder never makes*. This step is the one that turns those three documents into an
arm: **three reads, no store, at the one moment they mean anything** — after rung 9's wait and before the
first command.

## 1. The arm

`armed-storage-c3007c37`, identified by the sha256 prefix of its own `xnu_arm_entry.bin`
(`c3007c37891a4d38…`). Switch **value 21**, which this record counts as its **ordinal rung 22** — the two
spellings are one apart (755 §7).

| member | sha256 |
| --- | --- |
| `stage90-qcdt.img` | `07f2c11a…` |
| `xnu_arm_entry.bin` | `c3007c37891a4d38…` |
| `stage90.img` | `e7b0d572…` |
| `stage90.bin` | `5c33ca84…` |
| `stage90.elf` | `b76577eb…` |
| `xnu_arm_entry.elf` | `c22769b1…` |

The switch set, read out of the parked `xnu_arm_entry-config.txt` rather than remembered — and this is the
list the build's arm-change gate prints, which is how it caught two switches I had wrong while building
(`STAGE90_XNU_ISTACK_SEPARATE=0`, whose default is 1, and `STAGE90_XNU_POST_END_TICKS=115200000`, whose
default is 0):

    STAGE90_XNU_STORAGE_PROBE=21
    STAGE90_XNU_PWR_WAIT_TICKS=384000
    STAGE90_XNU_ISTACK_SEPARATE=0
    STAGE90_XNU_POST_END_RUN=0
    STAGE90_XNU_POST_END_TICKS=115200000

**The arm is purely additive and that is deliberate.** It changes **no guard on a rung that has already
been built, armed and pressed**: rung 21's call site is guarded `>= 20`, so a value-21 image carries *both*
the census and the `0x030A` CMD3, and the log carries the DLL readings taken before the first command
beside a second sample of the stall. That costs nothing and cannot make the reading less attributable.

## 2. The three reads, and the window they are taken in

| register | address | width | why |
| --- | --- | --- | --- |
| `HOST_CONTROL2 0x3E` | `f982493e:ldrh` | halfword | `sdhci.h:79`; the register `sdhci_msm_set_uhs_signaling` writes **unconditionally** at `sdhci-msm.c:2597`, and the only thing in this image that could have cleared its UHS field is rung 4's `SDHCI_RESET_ALL` |
| `CORE_DLL_CONFIG 0x100` | `f9824a00:ldr` | word | `sdhci-msm.c:80`; `sdhci_msm_set_uhs_signaling` sets `CORE_DLL_RST` (`1<<30`, `:2578`) and `CORE_DLL_PDN` (`1<<29`, `:2583`) here, at `host->clock <= CORE_FREQ_100MHZ` (`:2567`; 100 MHz, `:164`), with the vendor's own comment *"the feedback clock must be provided and DLL must not be used so that tuning can be skipped"* |
| `CORE_DLL_STATUS 0x108` | `f9824a08:ldr` | word | `sdhci-msm.c:89`; bit 7 is `CORE_DLL_LOCK` |

**The window is `hc_mem` and not `core_mem`, and that is the one error that would make these readings mean
something else.** The vendor reaches both registers as `host->ioaddr + CORE_DLL_CONFIG`, and
`host->ioaddr` is `sdhci_pltfm_init`'s **first** memory resource — `hc_mem` (`msm8974.dtsi:500`,
`reg-names = "hc_mem", "core_mem"`). The DT settles it: `msm8974pro.dtsi:1765` widens `&sdhc_1` from the
base node's `0x11c` to **`0x1a0`**, and `0x100`/`0x108` lie beyond `0x11c` — the window was widened to cover
exactly this pair. `core_mem + 0x100` is a different register file one window away, and the two windows
share their high half `0xf982`, so **a reading taken there would publish under these names with every cell
in the log agreeing with the record**. All three addresses are in the megabyte `0xf98` this image already
installs and reads, so 692's `addr >> 20` interlock is satisfied by construction.

## 3. The build clause, which is what makes "three reads, no store" a fact rather than a sentence

`build_entry.sh` gained a 69-line clause (`if [[ $STORAGE_PROBE -ge 21 ]]`) that asserts, on the linked
image:

- the symbol `st_dll_census` exists;
- **its device store set is EMPTY** — the whole safety argument for reading above the command gate, which
  is 739's argument for rung 18 and what 736's press measured the cost of violating (bit 15 rides with any
  write to `INT_ENABLE 0x34`, the gate read `0x00008000`, and no command went on the bus);
- its access set is **exactly** `f982493e:ldrh f9824a00:ldr f9824a08:ldr`, each count `=1`, in that
  program order;
- its image-side set is empty;
- it is called **exactly once**, from `entry_storage_probe`, and **the call line precedes `st_cmd_path`'s**.

The build printed, exit 0:

    xnu_entry_756: st_dll_census's device accesses are [f982493e:ldrh f9824a00:ldr f9824a08:ldr ]
    with counts [f982493e:ldrh=1 f9824a00:ldr=1 f9824a08:ldr=1 ] … entry_storage_probe calls it
    once, on disassembly line 1273, BEFORE st_cmd_path on line 1277 …

**One defect was found by the clause and not by review, and it is m731's.** The third build refused with
*"st_dll_census is not in the linked image while STAGE90_XNU_STORAGE_PROBE=21"*, because the body was
declared plain `static` and GCC inlined it away — `noinline` alone does not stop GCC cloning; the body
needed `__attribute__((noinline, noclone))`. A body a clause has to read must be a body.

## 4. The readiness row refused this step's own narration, and it was right

The first `verify_press_ready.sh` after parking the arm returned **4 of 5, exit 1**:

> `FAIL  the arm is named by a reading` — the arm named above does not quote the WAIT'S BOUND the entry
> record carries: `out/stage90/xnu_arm_entry-config.txt` says `STAGE90_XNU_PWR_WAIT_TICKS=384000` and the
> sentence above writes no `STAGE90_XNU_PWR_WAIT_TICKS=384000`.

**This is a real omission and not a formality.** `STAGE90_XNU_STORAGE_PROBE` names the **rung**;
`STAGE90_XNU_PWR_WAIT_TICKS` names **which arm at that rung** — so a narration quoting the rung alone names
two different arms with one sentence, which is exactly what 715 §5 recorded being measured (714's arm and
716's arm both carry `STORAGE_PROBE=9`, and the row printed `ok` on 716's bytes while the paragraph printed
the literal `1920000` and `100 ms` for both). My rung-21 paragraph named the rung, the switch value, the
ordinal, the set and every cell — and not the bound.

**And the repair has a non-obvious shape worth recording.** I first appended the bound sentence to
`rung_para 21 "…"`, which is where the rung's *consequence* paragraph lives — and the row **still** refused,
because `rung_para` appends to `entry_conseq` while the check reads `arm`, which for a storage arm is
`entry_arm`. The sentence had to go where rungs 19 and 20 put theirs: at the end of the arm's own
description. **So the file has two narration channels and the check reads one of them** — a distinction
nothing in the prose states, and one I got wrong on the first attempt. The second attempt reached
**5 of 5, exit 0**, and the two positions are one `$wpt_txt$wpt_which` apart.

## 5. What the press would answer, and what it costs

The three pre-registered outcomes (756 §6), each naming the next act:

- **`_dll_rst = 1` with `_dll_pdn = 1`** — the DLL is already reset and powered down by something that ran
  before this image. **756 §3's hypothesis is dead, nothing further is owed on this path, and it was bought
  with three reads.** This is the cheapest possible answer.
- **`_dll_rst = 0` or `_dll_pdn = 0`** — the block is running with an undischarged DLL at 400 kHz against
  the vendor's own comment. **Doing what the vendor does becomes the cheapest untried thing on this path**,
  and that write is the next rung, needing its own pre-registration because it is a store to a register no
  rung has written.
- **`_dll_ctrl2_uhs`** (= `_dll_ctrl2 & 7`) answers 756 §4's second half in one cell: `0` means rung 4's
  `SDHCI_RESET_ALL` did reach `HOST_CONTROL2` and the spec argument holds; nonzero means the reset does not
  clear that field on this IP and the vendor's `ctrl_2` write at `:2597` is live too.

`_dll_status` is published as a register beside its named bit, because the off-meaning of a bit is not the
same reading as the bit — the four named bits are four of thirty-two, and a reader with a different
question should not have to spend a press on the other twenty-eight.

**The cost, stated at 760 §5's width:** one press, if the rung is to be measured, and the arm in `out/` its
*rebuildability* from the tree once `src/` moves. Not the park, not the capture, and not reproducibility.

## 6. What this document does not say

- **It does not say the arm is correct hardware-wise.** It says the build refused nothing, the clause
  asserts the shape, and the window argument is settled from the DT. The three values are the press's to
  read.
- **It does not spend a press or arm a firer.** `armed-storage-c3007c37` **is armed and NOT pressed**, and
  spending it is the operator's decision. No firer exists; the press path is the two commands readiness
  prints, run by hand once each.
- **It does not move the previous arm.** `armed-storage-46fe6737` is still parked, untouched.
- **It does not re-open 756 or 761.** It is the arm those two documents pre-registered, built.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount, and no device was touched. What
  it buys is one thing: **the next press is one that could return nothing to do, and that is a complete
  answer rather than a wasted run.**

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block starts a
response-demanding command and never completes one — and 「让os可以正常启动并且挂载存储」 is not reached, so
**TWRP-to-storage stays withheld.**
