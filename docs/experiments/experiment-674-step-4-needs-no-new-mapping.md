# 674: step 4 needs no new mapping at all — and what the owed press therefore buys

The press is owed and armed (672/673). This step went one level down into 663 §2's step 4 — *force the
reset* — and found that both of its registers are already in the page table XNU runs under, which
**corrects a published claim** in 663 §3 and changes what the owed press's reading is evidence for.

Read-only: no build, no byte under `out/`, **no edit to `stages/stage90/xnu_arm_boot/**`** (the closure
673 measured), no gate or runner call, no device addressed.

## 1. The correction: both registers are already mapped, by the payload, in XNU's own L1

663 §3's bullet 2 says the PS_HOLD half *"costs one more section install"* — `entry_mmio_section(0xfc400000,
0xfc400000, …)` before the store. **It does not.** The payload builds the L1 table the handed-off kernel
runs under, and two of its Phase-4 device sections are the registers step 4 needs
(`stages/stage90/xnu_arm_vm_init_full_pmap.c:410-416`, into `stage90_candidate_l1`, whose own comment calls
it *"the one a handed-off kernel would run under"*):

```
map_l1_section_mmio(stage90_candidate_l1, 0xf9000000u, 0xf9000000u);    /* GIC        */
map_l1_section_mmio(stage90_candidate_l1, 0xfc400000u, 0xfc400000u);    /* PS_HOLD    */
```

`0xf9017014` (the bite) is in the first megabyte; `0xfc4ab000` (PS_HOLD) is in the second. So:

* **The seam runs in that context.** `entry_seam_flush` is entered from XNU's `cpu_idle`, i.e. on the
  payload-built tables, so both sections are live before the first XNU instruction and stay live through
  the idle loop.
* **And PS_HOLD's writability there is not an inference** — it is 664's measurement seen from the other
  side. `entry_epilogue` (`entry_stubs.c:4229`) stores 0 to `MSM8974_PSHOLD` from the entry image's own
  code and *every run that has ever returned a log proves that store lands*; it can only land through this
  same section. The seam's store is the same store, at an earlier point of the same boot, on the same
  tables.
* **So `entry_mmio_section` is not needed for either half.** 663 §3's argument for the bite — *"no new
  mapping"* — is right, and it is right for PS_HOLD too; the sentence that says otherwise is the one this
  step corrects. The only new bytes in the entry image remain the bite's address literal (664 measured that
  `f9017` occurs **0** times in the entry image in all four encodings) and the two stores.

## 2. What that does to the owed press's meaning

663 §3.1's doubt is real and stays: a *hang* is not a fault, so `fleh_dabort` never runs,
`entry_epilogue` never executes, and the only net that reaches a stuck machine is the SoC's own countdown —
which is the net the 652 arm did **not** come back on (bite due 28 s, phone still dark 282 s later). The
owed self-test press measures exactly that net, and it is still the right first spend.

**What changes is what its result licenses.** Step 4's order is 663 §3's own: **PS_HOLD first**, then the
bite — the payload's order, whose comment keeps the two together *"precisely because either one alone has
an unknown failure mode"*. PS_HOLD-first is on the ground §1 just measured (live tables + a store proven on
every returning run), so:

| the press says | what it licenses | what it does **not** |
| --- | --- | --- |
| the bite fired (~28 s) | a second, PMIC-independent net works — recorded, and worth having in every later arm | nothing new about step 4's mapping |
| the bite did not fire (~90 s) | the bite is **not** available here as a net | **not** "the frontier arm will hang": its first net is PS_HOLD, which is measured |
| no return (exit 2) | the bounded spin's own PS_HOLD path did not land either — a finding about *that* path, and the first evidence against PS_HOLD-first | — |

So the press is a **redundancy** measurement, not a gate on the frontier arm. The pre-registered order is
unchanged (it is still the cheapest way to learn the bite's state while a press is available, and 663
§3.1's reason for rehearsing scaffolding before trusting it is sound), but the sentence *"the reset path is
broken here, and §2 would have been a wasted press"* is now too strong: §2's return rides the net that
every returning run has already exercised. What would make §2 a wasted press is a failure of the **third**
row, which is a new and specific question rather than the general one.

## 3. And step 4 gains one requirement it did not have

663 §2's step 5 is *"do not return to the exit"* — and as written it is a sentence about intent, not a
property of code. If the stores land, the machine is gone; if the **first** store does not land, execution
would fall back into `cpu_idle`, reach the `pop`, and hang — i.e. the arm would become the very run it was
built to avoid. `platform_reboot` already has the shape that makes it a property
(`stage90_main.c:1068-1080`): store, `dsb sy`, then `for (;;) wfe`. **Step 4 must end in that same loop**,
so that "do not return" is a control-flow fact and the arm cannot fall through into the exit. And the arm
must record *which* net it used in its config, per 574's rule (a switch that decided the build and is not
in the record is a switch the next reader cannot see).

## 4. What this step did not do, and the honest limits

* **It did not measure a device.** Everything above is read from the tree and from two earlier measurements
  (664's PS_HOLD proof, 484's `entry_mmio_section` proof); the writability of `0xfc4ab000` at the seam is
  an inference from "the store lands for `entry_epilogue`", not a new observation. **Nothing here is
  evidence that the bite fires** — that stays unmeasured, which is why the press is still owed.
* **It did not touch the arm.** 673 measured that an edit to `xnu_arm_boot/**` makes the gate refuse the
  arm in `out/`, so §3's requirement and 663 §3's correction are *recorded for the next arm's build* and
  deliberately not applied: the window (pid 426955, deadline 13:21:30 UTC) is open on the owed press.
* **It does not re-open the press order.** The self-test arm is armed, gated, parked and pre-registered;
  this step argues about how its *result* should be read, and about the arm that follows it.

## 5. Safety, and what this does not do

Read-only throughout: `grep`, `sed` and `Read` over sources, plus the two device-list reads the readiness
tool has always made. **No build, no byte written under `out/`, no edit to `xnu_arm_boot/**`, no gate or
runner invocation, no `fastboot`, no boot, nothing written to storage, the neighbour `33e80afe`
untouched.** The armed launcher was left alone (a `ps` of its pid, a `tail` of its log).

**It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The frontier is where 652 left it — XNU
reaches pid 1, runs the userland phase, dies at the idle exit's `pop {fp, pc}` — and this step produces no
boot and no reading. It narrows what the next two arms must get right, which shortens the road to the
frontier's answer without being the answer. **TWRP-to-storage stays withheld**, because
「如果os已经能进去了的话」 is unmet.
