# 663: the arm whose reading survives a hang — designed from the non-return, before the repeat fires

662 measured the non-return: the acting arm was sent, the phone left the bus, and **nothing came back** — no
log, no pair, no frontier reading. The gate had pre-registered exactly this (*"Expect a power press, and
expect the log to go with it"*), and it is the fifth such hang in this project (517's first run, 521, 522,
526, and now this).

This step draws the consequence that the non-return forces, and designs the arm it implies. **Nothing is
built** — a press watcher is armed again (pid 3454466), so `out/` is frozen (§6). The design is written here
so that it is not improvised later, which is the same rule 651/653/658 §2 followed.

## 1. The consequence: a hang destroys the reading, so more instrumentation cannot help

Every instrument in this chain writes into a **RAM console in the top of DRAM**, and the top of DRAM is
reachable only by a run that comes back. So:

> **For any arm whose run ends in a hang, no amount of instrumentation produces a reading.** The log exists,
> and it is lost. 662's exit-2 message says the same thing in the runner's own words: *"a failure to return
> means the log is likely unrecoverable anyway"*.

That closes a whole class of next steps. Adding a marker before the `pop`, publishing one more key, sampling
one more register — none of it can be read out of a non-return. **The only way to read a hang is to make the
run end in a return instead.**

And there is a second, harder consequence for the frontier question. 662 §4 left three explanations standing
(repair worked and XNU hung later / the operation hung the machine / the reset happened but Android did not
come up), and §4 proposed spending the next press on a **repeat of the same arm**. This step revises that,
on measurement rather than on taste: **a plain repeat reproduces an unreadable non-return.** At n = 1 the
repeat's only possible verdicts are "back" (which the 574 park already did without the operation) or "no
reading again". The repeat is a coin flip on information; the arm below is not.

## 2. The design: end the run at the seam, on purpose, through the path the payload already owns

There are two primitives in this tree and **the arm's second half is a step that ends the run on purpose**;
§3 measures which of the two to use and finds that the stronger one needs no new mapping at all. Both already
exist:

* **the SoC watchdog's forced bite** — `stage90_hw_watchdog_bite_now` (`hw_watchdog.c:311-318`), whose
  registers sit at `0xf9017000` (bite at `+0x14`) — **inside the `0xf9000000` megabyte the entry side already
  maps every run**;
* **PS_HOLD = 0** — `platform_reboot`'s write at `stage90_main.c:1068` (`MSM8974_PSHOLD = 0xfc4ab000`,
  `stage90.h:16`), inside the 1 MB strongly-ordered section the payload maps at `0xfc400000`
  (`xnu_arm_vm_init_full_pmap.c:416`) — the path whose own comment says *"The warm reboot preserves the
  RAM-console"*.

And the entry side can install that section into XNU's **live** page tables with the routine this tree already
uses for exactly this purpose:

```
uint32_t entry_mmio_section(uint32_t va, uint32_t pa, uint32_t *slot_before_out, uint32_t *desc_out)
```

which is the call that put the GIC at `0xf9000000` into XNU's own L1 and that 484 measured end to end (it
re-reads `TTBR0`/`TTBR1` at every install and publishes `xnu_live_gic_l1_moved`, precisely because the console's
cached table pointer once went stale). So the arm is:

| step | what | why |
| --- | --- | --- |
| 1 | enter the seam and read `b0`/`b1`, as the acting arm does | the two words are published |
| 2 | run the operation: `FlushPoC_DcacheRegion(slot, 8u)` | the single variable 662 identified |
| 3 | read `a0`/`a1` and restore, as the acting arm does | the pair is published — **this is the reading** |
| 4 | **force the reset** — the watchdog's bite (two stores into the section the GIC probe already installed, §3) or, failing that, `entry_mmio_section(0xfc400000, …)` then store 0 to `0xfc4ab000` | end the run on purpose, through the payload's own reboot path |
| 5 | do **not** return to the exit | the run ends here instead of at whatever killed it |

**The point of step 5 is that it makes the run return whatever would have happened next.** If the operation
is what hangs the machine, step 5 never executes and the arm behaves like the acting arm — and *that* is a
reading. If the operation is benign, step 5 executes, the phone warm-reboots, the log comes back, and the
pair is in it.

**And the pair is then a decision, not a curiosity**, because 657 §4's four cells were pre-registered:

| the pair this arm reads | `STALE LINE, WRITTEN OUT` (`a1 == rtcpre_pop`) | `CLEAN LINE` (`a1 == b1`) | `CHANGED` |
| --- | --- | --- | --- |
| **the run reaches step 4 and returns** | the operation acted exactly as 657 §4.2 argued — one `mcr`, clean **and** invalidate — and it did **not** hang the machine | the line was already clean (the invalidate happened, invisibly) or the operation was inert | the word at that offset is not this boot's deadline; compare by hand |
| **the run never reaches step 4** | the operation hangs the machine **and** still writes the line out | the operation hangs the machine without writing anything | hangs after a partial effect |

So a **2 × 3 table with five informative cells**, against a plain repeat's one informative outcome. That is
the whole argument for building this rather than re-sending the acting arm.

## 3. Which reset path to force — and the measurement removes the harder question

`platform_reboot` does **two** things, and its own comment (`hw_watchdog.c:33-34`) says why:

> *"BITE NOW: `platform_reboot()` calls this after its PS_HOLD write. That makes the reboot path independent
> of the PMIC: if PS_HOLD does not take effect — one reading of …"*

So the proven path is **PS_HOLD *plus* the SoC watchdog's bite as an independent second net**, and the gate
notes every run that the entry image carries no address in the watchdog's page (*"pool 0 / movt 0 / mov 0"*,
with the payload at 10). That looked like the arm's hard part. **It is not, and the measurement is a single
comparison of two addresses** — which is why it belongs here rather than in the arm's design later:

```
hw_watchdog.c:94   #define MSM8974_WDT_BASE     0xf9017000u
hw_watchdog.c:100  #define MSM8974_WDT_REG_BITE 0x14u        -> the bite register is 0xf9017014
hw_watchdog.c:53   * 0xf9017000 is inside the 1 MB section 0xf9000000-0xf90fffff, which both the …
entry_gic.c:377    entry_mmio_section(STAGE90_GIC_DIST_BASE, STAGE90_GIC_DIST_BASE, …)   /* 0xf9000000 */
```

**The bite register is inside the megabyte the entry side already maps, every run, for the GIC probe.** So
the seam needs **no new mapping and no new tool**: the entry image can force the bite by writing the two
registers the payload's own `stage90_hw_watchdog_bite_now` writes (`hw_watchdog.c:314-315`):

```
HW_WRITE(0xf9017014, 1u);    /* BITE = 1 tick   - the device's own idiom, msm_watchdog_v2.c's bark handler */
HW_WRITE(0xf9017008, 1u);    /* RST  = 1        - pet, so the counter starts and reaches it */
then spin, as the payload does
```

and it is the **stronger** of the two paths, because the payload's own comment says the bite is the one that
does not depend on the PMIC. (The entry side still has to *carry the address*, which is a new literal in the
entry image — a build, which this arm is anyway, and the reason this is a new pre-registration and not an edit
to the acting arm.)

**Two mechanisms, then, and the primary is now the second one:**

1. **Watchdog bite (primary).** Two stores into a section already installed; the independent net; the
   device's own idiom; **no new mapping**. Its one requirement is that the section's attributes are the ones
   the GIC probe installed (`entry_mmio_section(0xf9000000, 0xf9000000, …)`, `0xc` — strongly-ordered, which
   is what a device register needs). That is the same install that works today.
2. **PS_HOLD store (fallback).** `entry_mmio_section(0xfc400000, 0xfc400000, …)` then store 0 to
   `0xfc4ab000` — the address the payload writes at `stage90_main.c:1068` (`MSM8974_PSHOLD`, `stage90.h:16`),
   and the path whose comment says *"The warm reboot preserves the RAM-console"*. It costs one more section
   install and relies on one net instead of two. It is the right choice **if** the bite's section turns out
   not to be writable from the entry side for a reason not visible in the addresses — which this step has
   measured and found no reason to expect.

Whichever is used, the arm must record **which** it used in its config, for the reason 574's record gives: a
switch that decided the build and is not in the record is a switch the next reader cannot see.

## 4. What the arm must also do, and the one thing it must not

* **It must keep the acting arm's switches otherwise identical** (`SEAM_POC=1`, `SEAM_MEASURE=0`,
  `IDLE_NO_SLEEP=0`, `SLOT_NULL=1`), because the whole value of the comparison is that the *only* other
  difference from the arm that did not return is the forced ending. Two changed variables would attribute
  nothing — the same rule 653 applied to `SLOT_NULL`.
* **It must be recorded in `revert-set.txt` before its press**, by hand, in the step that measured it. That is
  no longer advice: the gate clause landed in 662 §5 (**R2**) refuses a fire whose bytes are not one of the
  recorded sets, and it prints `11/11` for the acting arm against `2/11` for the others. A new arm that is not
  recorded **cannot be fired at all** — which is the hole 660 §3 measured, now closed, and this is the first
  arm it will apply to.
* **It must not be built while a watcher is armed** (658 §2 / 660 §5). With **R1** landed, a build would make
  readiness row 1 red and the watcher would refuse the fire — so the failure mode is now a refused press
  rather than an unrehearsed one, which is the improvement working. The rule stands anyway.

## 5. What this arm does not settle, and the arm after it

The forced ending **deliberately discards the frontier question**: a run that reboots at step 4 says nothing
about whether the boot would have got past the `pop`. That question needs an arm that *does* return through
the exit — which is the arm after this one, and its design depends on this one's answer:

* If this arm returns with `a1 == rtcpre_pop`, the operation is proven benign and effective, and the
  frontier arm becomes: the same operation, **without** the restore stores, or with the maintenance taken at
  a level the operation reaches — 657 §4.2's "one level below" candidate, and 658 §2's `CSSELR` walk.
* If this arm never reaches step 4, the operation is fatal *by itself*, and the next step is to bisect the
  operation — the flush, the invalidation, or the two restore stores — one per arm, with each new arm forced
  to end at step 4 so that every one of them is readable. **That is the property this design buys: the
  bisection is readable, where today it would be a sequence of power presses and no logs.**

## 6. State, and what is frozen

* **The watcher is armed** (pid **3454466**, budget 604800 s from 2026-09-25 04:02:58), single-shot, two
  conditions, with **R1** in it: it now *tests* readiness and exits rather than `say`ing it. A heartbeat
  monitors it (pid **3467622**, 60 s, passive — no sudo, no fastboot, no adb, no runner).
* **The phone is dark and needs a power press.** The watcher will fire the moment `4a2fe00b` reappears, which
  means the phone will reboot into the payload within seconds of Android coming up.
* **Frozen until that fire:** `out/`, `xnu_arm_boot/`, the gate, the runner, the readiness tool — 660 §5's
  closure. This step changed **one document and one index row**, and ran no build.
* **`out/` still holds `armed-seam-poc-a43304f2`** (the gate's new clause prints `11/11`), so the repeat the
  watcher fires **is** the arm that did not come back — which is the intended, pre-registered behaviour, not an
  accident.

## 7. Safety

No device action: no `fastboot`, no `adb`, nothing sent anywhere, **nothing written to storage**. Every
reading is host-side — `hw_watchdog.c`'s and `stage90_main.c`'s and the payload map's sources, the entry
image's symbol table, and the two logs from the 03:53:50 run. No build, no edit to any file in 660 §5's
closure, and the arm still verifies against the `armed-seam-poc-a43304f2` park. `fastboot boot` only, never
`flash`; the design in §2 ends a run through the payload's **own** named reboot path and not through storage.
