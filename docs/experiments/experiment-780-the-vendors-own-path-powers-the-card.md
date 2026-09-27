# 780: the vendor's own path powers the card — the record's reason for closing the power candidate is a driver reading that does not apply to this board

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE, NO RUNNER, NO FIRER, NOTHING BUILT.** `out/` was not
touched, so the armed rung-28 arm (`armed-storage-d5d98738`, `STAGE90_XNU_STORAGE_PROBE=27`) and every park
are byte-identical before and after. One press spent by this step: **none**. Every vendor line below was
read in the checkout at `/mnt/data/mi4-ios6/external/android_kernel_xiaomi_cancro/` — which `.gitignore:12`
excludes, so these citations are a reading of this machine's checkout and not of the repository.

## 1. The sentence this step is about

`records/revert-set.txt:5636-5646` refutes 764 §3's first candidate — *"the card is not powered … no rung in
this ladder has ever touched the PMIC"* — and it does so in two halves. The second is the load-bearing one,
because it is the one that claims to have exhausted the vendor's own power work:

> And `sdhci.c:3583` asks for `regulator_get(..., "vmmc")` — the con_id `vmmc`, where the DT supplies
> `vdd-supply` — so it takes IS_ERR, prints "no vmmc regulator found" and sets `vmmc = NULL`, making every
> `mmc_regulator_set_ocr` call a NO-OP. **The vendor does not turn the card's power on either.**

The first half is true and this step re-verified it (`sdhci.c:3583-3587`, and every use guarded by
`host->vmmc &&` at `:1625`, `:1667`, `:1688`, `:1833`). **The second sentence is false**, and the first half
does not support it.

## 2. `host->vmmc` is not the rail the vendor uses

`sdhci.c`'s `host->vmmc` is the **SDHCI core's own** rail handle. `sdhci-msm.c` — the driver that binds
`sdhc_1` on this board (`compatible = "qcom,sdhci-msm"`; the legacy `sdcc1` node is `status = "disabled"`,
`msm8974-mtp.dtsi:368-370`) — has its **own** vreg table, populated from the same node's `vdd-supply` and
`vdd-io-supply` and driven by its own path. A no-op on the core's handle says nothing about that path.

Read end to end, in the checkout:

```
sdhci_msm_probe:2826        ret = sdhci_msm_vreg_init(&pdev->dev, msm_host->pdata, true);
sdhci_msm_vreg_init:1873    if (!is_init) goto vdd_io_reg_deinit;   /* is_init is true here */
                            sdhci_msm_vreg_init_reg(dev, curr_vdd_reg);      /* regulator_get */
                            sdhci_msm_vreg_init_reg(dev, curr_vdd_io_reg);
                            ret = sdhci_msm_vreg_reset(pdata);
sdhci_msm_vreg_reset:1861   ret = sdhci_msm_setup_vreg(pdata, 1, true);      /* ENABLE */
                            ret = sdhci_msm_setup_vreg(pdata, 0, true);
sdhci_msm_setup_vreg:1826   vreg_table[0] = curr_slot->vdd_data;
                            vreg_table[1] = curr_slot->vdd_io_data;
                            for (i = 0; i < ARRAY_SIZE(vreg_table); i++)
                                if (vreg_table[i]) { if (enable) ... }
sdhci_msm_vreg_enable:1765  ret = regulator_enable(vreg->reg);   /* NO is_always_on TEST */
```

**So on any Linux boot of this board, `regulator_enable()` is called on both `pm8941_l20` and
`pm8941_s3`** — and the sentence the record rests on is not a weaker version of that, it is the opposite of
it.

## 3. And `qcom,vdd-always-on` does not cause the early return the record cites

The record's first half also leans on `sdhci_msm_setup_vreg`'s early return, whose own comment reads
*"vreg info unavailable, assuming the slot is powered by always on domain"* (`:1834-1836`) — the phrase
`always on` appears in it, which is what made it look like the always-on property's effect. It is not. The
guard is :

```
:1832   curr_slot = pdata->vreg_data;
:1833   if (!curr_slot) {                       /* vreg_data == NULL */
:1834       pr_debug("... assuming the slot is powered by always on domain\n", ...);
:1836       goto out;
```

and `vreg_data == NULL` **cannot happen on a DT-bound device**:

- `sdhci_msm_populate_pdata:1397` allocates it **unconditionally**, before parsing anything, and `goto out`s
  only on allocation failure;
- the per-rail parse (`:1060-1064`) returns early — leaving that rail's struct NULL — **only when the
  `-supply` phandle is missing**, and this board declares both.

What `qcom,vdd-always-on` actually does is one thing, in one place, and it is in the **release** path:

```
:1072   snprintf(prop_name, MAX_PROP_SIZE, "qcom,%s-always-on", vreg_name);
:1073   if (of_get_property(np, prop_name, NULL))
:1074       vreg->is_always_on = true;
:1796   if (vreg->is_enabled && !vreg->is_always_on) {          /* regulator_disable */
:1813   } else if (vreg->is_enabled && vreg->is_always_on) {   /* demote to LPM instead */
```

**`always_on` means "enable it and never release it".** It is the opposite of "skip the regulator call".

## 4. So the conclusion survives, on a different and stronger reading

The record's *conclusion* was that acts 1 and 3 of the vendor's `CORE_PWRCTL_BUS_ON` branch cost the ladder
nothing because the board's supplies are always-on. **That survives — and the reading it should have rested
on is better than the one it used.** `sdhci_msm_vreg_reset` enables both rails and then calls the *disable*
path immediately; on an always-on rail that path does not disable (`:1796` is false) but only demotes to LPM
(`:1813-1817`, and `qcom,vdd-lpm-sup` is declared at `msm8974-mtp.dtsi:389`). **The vendor's own bring-up is
what leaves the rails enabled, and the flag is what keeps them so.**

Two consequences, and they are the point of the step:

1. **764 §3's candidate 1 is not strengthened — it is weakened.** Nothing in this tree disables the card's
   supplies, and the vendor's path explicitly enables them. A board that also boots *from* this eMMC to
   `fastboot` has had that bus powered and driven. The candidate stays **unmeasured**, which is the honest
   state, but it is no longer a loose end a reader would expect to be productive.
2. **What the record is owed is a reason, not a conclusion.** A future reader taking *"the vendor does not
   turn the card's power on either"* at face value could argue from it that the card's power is nobody's act
   — which is the opposite of what the tree says.

## 5. And "the PMIC is unreachable" is false as stated, in a way worth naming

773 §3's table counts acts 1 and 3 as `regulator_*`, hence "**no** — reachable from `hc_mem`?". That verdict
is right about the *act* and wrong as a statement about the *block*: the PMIC's interface has an MMIO
window on this SoC, and it is **inside a megabyte this ladder already installs and has been reading through
since 694**.

| what | where | in a mapped megabyte? |
| --- | --- | --- |
| the SPMI arbiter | `msm8974.dtsi:848-854`, `qcom,spmi@fc4c0000`, `reg = <0xfc4cf000 0x1000>, <0xfc4cb000 0x1000>, <0xfc4ca000 0x1000>` | **yes** — `0xFC4` is `ST_GCC_BASE`'s section (`entry_storage.c:280`), and `_gcc_map = 1` in every capture since 694 |
| the AP's route to a rail | `msm8974.dtsi:2102-2105`, `qcom,rpm-smd`, `rpm-channel-name = "rpm_requests"`, `rpm-channel-type = <15> /* SMD_APPS_RPM */` | **not MMIO at all** — a vote into shared memory, answered by the RPM firmware |

**So the accurate statement is narrower and more interesting than "unreachable":** the arbiter's three pages
*are* readable from a megabyte the ladder already holds, and the PMIC's own registers are **not memory
mapped at all** — they are SPMI-addressed by `usid:offset` — so reading a rail's state would mean driving
the arbiter through its command registers, a protocol and not a load. And the path the AP would *use* to
change a rail is `rpm-smd`, which is not the arbiter.

**This step does not propose such a rung, and the ladder has never read `0xFC4Cxxxx`.** It names a window
that exists, states what it would take, and stops: driving an SPMI bus is an act with a failure mode the
ladder's ladder has not measured, and the previous candidate in this direction is 748's two dead-end
registers.

## 6. What this changes, and what it does not

- **What it changes**: one sentence in the record, which now says what the tree says; and a statement in
  the gate's own narration for the armed arm, which called the PMIC rails the frontier row 1 leaves.
- **What it does not change**: no cell, no arm, no park, no capture, no constant. `out/` is untouched. The
  armed rung-28 arm's own readings and the fork discount 779 landed are unmoved.
- **What it does not establish**: that the rails are up at press time. That remains **unmeasured** — no
  capture in this archive contains a PMIC, SPMI or regulator reading of any kind, and every tree on this
  device declaring the supplies always-on is a *declaration*.
- **No press is spent and none is authorized.**

## 7. Owed, and named rather than left to be inferred

- **The record's own sentence is corrected in place** (it is the arm record, and a superseded reading there
  would be re-quoted); the correction carries the line cites above.
- **764 §3's candidate 1 keeps its standing as unmeasured**, and its status is now stated with the weaker
  evidence base rather than the stronger one the record claimed.
- Unchanged from 775–779: the `rung_para` correction for values 12..23; the seam-address class;
  `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; the 737 window paragraph; the four `5,088,000`s and the
  mis-citation at `entry_storage.c:302-303` and `:3054`/`:3861` (all COST-owed, to be carried by a build);
  the substantive pad repair (compare against the **set** of candidate words) pre-registered by 779 and not
  built; and `derive_sdc1_pads.py`'s missing self-test.

## 8. What this document does not say

- **It does not say the card is powered.** It says the vendor's path powers it and nothing here disables it.
- **It does not say the power candidate is dead.** It says the reason the record gave for closing it does
  not apply to this board, and that the candidate's evidence base is weaker than the record implied.
- **It does not propose a PMIC rung** and does not authorize one, and it does not spend a press.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block issues a
response-demanding command, drives the CMD line, and **times out because the card does not answer** — and
「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage stays withheld.**
