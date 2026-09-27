# 784: the eMMC's two rails are RPM resources, and which PMIC carries them is the tree's business

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER, NOTHING
BUILT.** `out/` was not touched and no entry source was edited, so the armed rung-28 arm
(`armed-storage-d5d98738`, `STAGE90_XNU_STORAGE_PROBE=27`), every park and every capture are
byte-identical before and after: `stage90-qcdt.img` `93026ca1…` and `xnu_arm_entry.bin` `d5d98738…`,
both equal to the set lines `records/revert-set.txt` carries. One press spent by this step: **none**.
Every input below is `.gitignore`d (`.gitignore:2` `xiaomi4-cancro-backup-*/`), read on this machine
and landed as a **tracked** reading, as in 779, 781 and 782.

## 1. The candidate this step attacks, and the shape it was in

`records/revert-set.txt` and the safety gate's own notes carry the frontier's first untested
candidate in this form: **the eMMC's VCC/VCCQ come from the PMIC, and no rung in this ladder has ever
touched the PMIC** — *named, not established*. 780 weakened the record's reason for closing it (the
vendor's `sdhci-msm` does enable both rails through `regulator_enable`, and its `always_on` flag only
affects the *release* path) and did not close it, because nothing in this archive holds a PMIC,
regulator or rail reading.

**This step reads the same file 779, 781 and 782 read — the device's own `dt.img` — and asks the
question none of them asked: what are the two rails, and how would the AP reach them at all?**

## 2. The rails, resolved through the same node's own phandles

`sdhci@f9824900` declares exactly two supplies (`vdd-supply`, `vdd-io-supply`) and no third. Resolving
each phandle to the node that carries it, in every tree that has the node:

| supply | rail the device declares | name | init | `qcom,set` | min/max | always-on |
| --- | --- | --- | --- | --- | --- | --- |
| `vdd-supply` | `/soc/qcom,rpm-smd/rpm-regulator-ldoa20/regulator-l20` | `8084_l20` (tree #0) / `8941_l20` (trees #1, #2, #3, #5) | `0x2d0370` | 3 | equal | **none** |
| `vdd-io-supply` | `/soc/qcom,rpm-smd/rpm-regulator-smpa4/regulator-s4` (tree #0) · `/soc/qcom,rpm-smd/rpm-regulator-smpa3/regulator-s3` (trees #1, #2, #3, #5) | `8084_s4` / `8941_s3` | `0x1b7740` | 3 | equal | **none** |

`0x2d0370` is 2,950,000 µV (2.95 V, the eMMC's VCC) and `0x1b7740` is 1,800,000 µV (1.8 V, VCCQ/IO) —
the same two voltages 781's census read off the controller node, now read off the rails themselves.
`qcom,set = 3` is the RPM state set the request applies to.

## 3. The path is invariant and the name is not — one path, two definitions

**`rpm-regulator-ldoa20/regulator-l20` is the same path in all five trees, and it carries two
different names**: `8084_l20` on tree #0 and `8941_l20` on the other four. The same holds for the
`vdd-io` rail, which additionally moves between `smpa4/regulator-s4` and `smpa3/regulator-s3`.

So on this device **the rail's identity is a property of the tree the bootloader selects**, which 781
established for the pad word and which is the same hardware-subtype question one level down:

| the device's own arbiter declares | the eMMC's rails are named for |
| --- | --- |
| tree #0: `qcom,pm8941@2`, `qcom,pm8941@3`, `qcom,pma8084@0`, `qcom,pma8084@1` | `8084` — **the PMA8084** |
| trees #1, #2, #3, #5: `qcom,pm8941@0`, `qcom,pm8941@1`, `qcom,pm8841@4`, `qcom,pm8841@5` | `8941` — **the PM8941** |

**And the rail's name and its chip's node name do not contain each other in the direction a substring
test would need.** The rail is `8941_l20`; the SPMI child is `qcom,pm8941@0` — `8941` is a substring.
The PMA8084's rail is `8084_l20` and its children are `qcom,pma8084@0` / `@1` — `8084` is a **suffix**
of `pma8084` and not a substring of its front. That is why the rule the check states is *the rail's
token is the last four characters of a declared SPMI child's name*, and why it is stated as a rule:
a rail named with no `_` has no token and is not attributed to a chip at all.

## 4. The rails are not always-on, and the file marks other rails always-on — so the absence is computed

This is the sharpest reading in the step, and it **corrects an inference 781's record drew**.

The collector node carries `qcom,vdd-always-on` and `qcom,vdd-io-always-on` in every tree. 781 read
that as *the rails are always-on, at their provenance*. **The device's own file says something
different, and says it by contrast:** across the six trees there are **42** separate always-on
declarations (`regulator-always-on` / `regulator-boot-on`) — on `vph_pwr_vreg`, `l2`, `l3`, `l12`,
`l18`, `l22`, `lvs1`, the `disp_*` pair, `spi_eth_phy_vreg` — and **0 of the 42 lands on a rail the
ladder's controller declares.** The tool computes that count and prints it; it does not assert it.

So the device's own tree is a file that *uses* the always-on vocabulary, and it deliberately does
**not** use it for LDO20 or S3/S4. Which means `qcom,vdd-always-on` is the **consumer-side** flag the
vendor driver reads on its release path (780), and it is **not** the RPM's statement that the rail is
held up. **The rails' runtime state at the moment the ladder runs is a request, not a declaration** —
and the only requester 780 could name is `sdhci-msm`'s `vreg_enable`, which the ladder's payload does
not execute, because the ladder is not the Linux driver.

## 5. The AP's declared routes, and why no store in this ladder can power the card

The same file declares both families of regulator this SoC has, and puts the eMMC's two in the
**RPM** one:

| family | declared as | how the AP reaches it |
| --- | --- | --- |
| **RPM resource** | `/soc/qcom,rpm-smd/...` — `compatible = "qcom,rpm-regulator-smd"`, **no `reg` at all** | `/soc/qcom,smem@fa00000` (`0xfa00000`) with `/soc/qcom,smem@fa00000/qcom,smd-rpm` (`compatible = "qcom,smd"`, edge `0xf`), and a node whose `rpm-channel-name` is **`rpm_requests`** with `rpm-channel-type` `0xf` |
| SPMI-direct register | `/soc/qcom,spmi@fc4c0000/qcom,<pmic>@n/regulator@XXXX` — **78 to 126 of them per tree** | the SPMI arbiter `/soc/qcom,spmi@fc4c0000` (`compatible = "qcom,spmi-pmic-arb"`, `reg = <0xfc4cf000 0x1000 0xfc4cb000 0x1000 0xfc4ca000 0x1000>`, `reg-names = core intr cnfg`) |

**The eMMC's rails are in the first row, and it has no address.** A rail under `qcom,rpm-smd` carries
no `reg`; its configuration is an entry in the RPM's own resource database and the AP asks for it **by
naming a channel**. So:

- **No rung can power this card with a store.** There is no address to write that is the rail.
- **The candidate is not unreachable, it is a different order of work**: an SMD/RPM client that finds
  the `rpm_requests` channel in shared memory and sends a request — not a register poke.
- **And the ladder has never touched any of it, measured rather than remembered**: `grep` over
  `src/entry/entry_storage.c` finds the regulator/PMIC/SPMI vocabulary **twice**, both times in the
  two comments carrying 779's wrong `msm8974pro-ac-pm8941-mtp-v5.dts` citation (`:300`, `:3550`). No
  store to the arbiter, no RPM request, no LDO/SMPS register — the safety gate's *no rung has ever
  touched the PMIC* is now **established**.

**One qualification, because it is a real touch and it is in the same megabyte**: the payload's own
reset path writes **PS_HOLD at `0xfc4ab000`** (`src/entry/entry_reset.h:37`) in `entry_epilogue` on
every returning run. That is an AP-mapped PMIC line — and `0xfc4ab000` is in the `0xFC4` megabyte
where `0xfc4004c0` already faulted on 692's press. So the precise statement is: **the ladder has never
touched a regulator; it does write one PMIC line, and the megabyte it lives in is not uniformly
mapped.** The entry source says so itself (`:50-52`).

## 6. And the RPM firmware in this device's own backup carries both PMICs, so the tree is the decider

`rpm.img` (1,048,576 bytes, sha256 `18402f25…`) is the RPM's own firmware. Its strings name **both**
PMIC targets — `/rpm/pmic/target/pm8x41` and `/rpm/pmic/target/pma8084` — plus
`/rpm/pmic/client/rpm_init`, `/pmic/client/lpddr`, `PMIC_ARB`, the rail-name fragments (`ldoa`, `smpa`,
`vsa`, `ldoa4K`) and the channel name `rpm_requests` the device tree also names.

So the firmware is not the differentiator either: **one RPM image serves both boards, and which rail
the eMMC sits on is decided by the tree the bootloader selects off the hardware.** That makes the
hardware-subtype question load-bearing for the power candidate in exactly the way 781 made it
load-bearing for the pad candidate, and it means a future rung that named a PMIC would have the
one-path-two-definitions defect 779/781/782 found three times.

## 7. Landed as artifacts, because a sentence is not a constraint

| artifact | what it is |
| --- | --- |
| `tools/derive_sdc1_pads.py --rails` | the reading of §2–§6, from any QCDT blob: the two supplies resolved through their phandles, the path and the name on each tree, the always-on contrast **computed** (0 of 42), the arbiter's windows and children, and the RPM channel. It also prints **the verdict the tracked record's check will reach about each rail**, computed on the same rule object, so a producer that could write a line its own checker refuses says so at read time |
| `records/sdc1-pad-candidates.txt` | the **tracked** reading: `# rail` lines (path, name, pmic, init-uv, set, always-on, trees), `# spmi` lines (windows, `reg-names`, children, trees) and `# rpm` lines (channel, type, edge, smem, trees) — plus the corrected closing note, which now says the rails are **not** always-on and that `qcom,vdd-always-on` is the consumer flag |
| `tools/check_sdc1_pad_expectation.py` | **a fourth refusal**: the record's rails must attribute to PMICs the record's **own** `# spmi` lines declare, on the rule *the token is the last four characters of a declared child's name*. A rail named for a chip the same record does not declare is **779's defect one level down** — a citation to another board standing where a reading of this one should be |

**The refusal is falsified, not asserted.** Six self-test cells pin `rail_census_verdict` as a pure
function (the device's own reading passes; `8084_l20` against a record declaring only the PM8941
refuses, and the converse; a rail with no token refuses; no rail lines refuses; rails with no declared
chip refuses), and eight more pins `rail_verdict`'s token rule directly — including the case a
substring test would get **wrong**, `8084_l20` against `qcom,pma8084@0`, which passes, and
`8084_s4` against `qcom,pm8941@0`, which refuses. `tools/derive_sdc1_pads.py`'s own `--selftest` grew
from 11 cells to **26** (8 rail attributions, 1 token rule, and 10 against the device's own file,
including the computed 0-of-42).

**And `tools/verify_press_ready.sh` was run once, host-side, with no device action — 5 of 5, exit 0**
— because this step appends a block to `records/revert-set.txt`, the file the arm-resolution and
park-verification tools parse. It reports the same arm: `armed-storage-d5d98738`, 11 of 11 members
hashed in place against the record, the park equal to the live `out/`, the gate exit 0 under
`--allow-xnu-entry`, and the press catcher usable. So the record's new block did not disturb the arm
resolution, and that is a reading rather than an assumption.

**And one coupling was caught by writing it down**: `--write-record` will write a rail line whatever
it reads, so a rail with no PMIC token would be written as `pmic -` — and the check refuses exactly
that. **A producer that can write what its own checker refuses is the m749 class**, so the verdict is
printed by the producer at read time (§7's first row) rather than left to `make check` to discover.

## 8. What this does not do, and what it does not say

- **It does not say the rails are up, and it does not say they are down.** No capture in this archive
  holds a PMIC, SPMI, RPM or rail reading of any kind. What changed is that the candidate now has a
  **shape**: two named rails on a named family, reached by a named channel, with the PMIC's identity
  left to the tree.
- **It does not close the power candidate, and it does not reopen it as the frontier.** It does
  something narrower and more useful: it makes the *cheap* rungs cheaper to judge. The rung-28 arm's
  pad question needs no PMIC at all, and a "power the rails" rung is now known to be an RPM client
  rather than a store.
- **It does not change the arm.** Not a cell, not a store, not a byte; the guard, the pad fork and the
  CMD-line counts are what 782 left them.
- **It does not name which tree the bootloader selects** (781 §5, unchanged), so it does not name
  which PMIC is on this board — only that the device's own file declares both candidates and that the
  RPM firmware serves both.
- **No press is spent and none is authorized.**

## 9. Two cheap readings this step makes nameable, and neither is proposed

Both are `reg` windows the device's own tree declares, both are *readings of the RPM's own state*, and
neither is a rung:

| window | what the device declares | what it could answer |
| --- | --- | --- |
| `/soc/qcom,rpm-log@fc19dc00` | `compatible = "qcom,rpm-log"`, `reg = <0xfc19dc00 0x4000>`, and the RPM firmware's own format string is `%s (Enabled: 0/1)` | whether the RPM ever logged a rail as enabled — a *reading* of the power state with no SPMI sequence |
| `/soc/qcom,rpm-master-stats@fc428150` | `compatible = "qcom,rpm-master-stats"`, `reg = <0xfc428150 0x3200>` | the RPM master's own counters |

**Each is in a megabyte this image's tables do not cover** — `0xFC19` and `0xFC42` — and `0xFC428150`
sits inside the `0xFC4` megabyte whose GCC page faulted on 692's press. So each carries the same
interlock cost the ladder already pays for a new window, and 692's rule applies unchanged: `addr >> 20`
is the check before any arm dereferences a new device register. **Named here so the next rung is a
choice; not designed, not proposed, and not authorized.**

## 10. Owed, and named rather than left to be inferred

- **The rail's physical address is not in this file and this step did not invent one.** The RPM-family
  declaration names the rail by `regulator-name` and does not name an SPMI slave; which of
  `qcom,pm8941@0`/`@1` carries LDO20 is a question for the RPM's resource database or the PMIC's own
  numbering. That is what `rpm.img` is for, and parsing it was not attempted here.
- Unchanged from 775–783: the four `5,088,000`s and the mis-citation at `entry_storage.c:302-303`
  (COST-owed, to be carried by a build); the set-comparison pad repair (779 §7, pre-registered, not
  built); the `rung_para` correction for values 12..23; the seam-address class;
  `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; the 737 window paragraph (paid by the rung-28 build);
  `fdt_nodes`'s lack of a synthetic FDT cell (782 §6); and 783's window-scope check.
- **`rail_name` has no cell.** The two spellings a DT uses for `regulator-name` are exercised by the
  device's own file (both are present) and by nothing synthetic — a hand-built cell for the
  cells-instead-of-a-string spelling is the obvious tightening and is not done.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block issues a
response-demanding command, drives the CMD line, and **times out because the card does not answer** —
and 「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage stays withheld.**
