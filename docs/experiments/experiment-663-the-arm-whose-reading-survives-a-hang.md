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

**And the literal's status is not what this step first said — corrected by 664, in place, because the claim
was published here.** The entry image carries **no** occurrence of `f9017`, in any of the four encodings an
instruction can put an address in (flat text, `movw`/`movt` halves, an offset immediate from the GIC's base,
and literal-pool words) — so the bite's literal *is* new. But **`fc4ab000` is not absent**: it is built as
`movw r3,#0xbfff` + `movt r3,#0xfc4a` + `str r2,[r3,#-4095]` at `8000578c-80005798`, inside **`entry_epilogue`**
— the entry image's own `noreturn` run-ending reset, which writes `RESTART_REASON ← 0x78665501` and then
`PSHOLD ← 0` and halts. **So the PS_HOLD half of §3's step 4 already exists and is exercised on every
returning run**, and the rehearsal of §3.1 is a rehearsal of the *bite* alone. 664 has the measurements; the
flat grep that produced the false absence could not see a `movw`/`movt` pair, and the tree's own
`tools/check_storage_refs.py:24` names that exact `movt`.

**Two mechanisms, then — and the honest design is to force BOTH, in `platform_reboot`'s own order.** Its
comment is not a preference: PS_HOLD *and* the bite are kept together precisely because either one alone
has an unknown failure mode, and this arm has no way to tell which one would have worked. So: PS_HOLD
first (the payload's order at `stage90_main.c:1067-1071`, whose comment says it preserves the RAM
console), then the bite (`:314-315`'s idiom).

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

### 3.1 But the premise is a reset this device has failed to deliver five times — so rehearse it first

The whole design rests on one sentence: *force the reset, and the run comes back with the pair*. **That
sentence is in doubt, and the doubt is measured rather than speculative**, because the reset this design
forges is the same class of reset that has already failed here:

* The payload's watchdog was **armed** for this arm — the 652 capture carries
  `hw_watchdog_bark_ticks_written=0x000c7fb5` and `hw_watchdog_bite_ticks_written=0x000dffac`, which over
  `WDT_HZ = 32765` are the documented **25 s bark / 28 s bite** — and **nothing pets it once XNU runs**
  (`hw_watchdog.c` writes `WDT_REG_RST` in exactly two places: the arming path at `:231` and the forced bite
  at `:315`). So on any hang longer than 28 s the bite is due and fires on its own, with no software
  involvement.
* **And the phone did not come back.** The 03:53:50 run hung, the bite was due 28 s after the arming, and the
  phone was still off the bus **282 s** later.
* The gate has counted the same outcome four times before (*"the last four hangs (517's first run, 521, 522,
  526) did NOT come back and each needed a power press"*, with 526's port silent for hours against a bite due
  at 28 s).

**So a forced reset may not return the run either**, and the failure would be indistinguishable from the
hang it was meant to escape: another power press, another lost log, and a press spent on a design question
rather than on the frontier. That is a bad way to learn this.

**The fix is a rehearsal arm, and it costs one press:**

> **An arm that does *nothing* at the seam except force both reset paths and not return.** No operation, no
> pair, no new instrument. Its verdict is one bit and it is unambiguous: **the phone comes back (the reset
> path works, and 663 §2's design is sound) or it does not (the reset path is broken here, and §2 would have
> been a wasted press).**

Three properties make this the right first spend rather than an indulgence:

* Its **only** changed variable is the forced-reset code, so it attributes nothing else — 653's `SLOT_NULL`
  rule again.
* Its **failure direction is informative**, which is rare in this project: both outcomes buy a fact.
* It is the same discipline 661 applied to R1–R3 and m668 to the mutant copy — **rehearse the scaffolding
  before trusting the thing built on it.** Here the scaffolding is the reset.

**And 664 narrows it to one of the two branches: rehearse the bite alone.** The PS_HOLD half of step 4 is not
scaffolding to be trusted — it is `entry_epilogue`'s own run-ending store, exercised on every run that has ever
returned a log, whereas the bite has never been observed to fire against five non-returns. "Force both" is
still right for the *design* in §2 (either net may be the one that fails); the *rehearsal* should force the
unproven one.

And it is cheap to make: 663 §2's step 4 verbatim, with steps 1–3 replaced by nothing. It must be recorded in
`revert-set.txt` before its press like any other arm (R2 enforces it), and its own config must say which
reset paths it forced.

### 3.2 Why step 4 must be a hardware write: the OS's own reboot path is a NULL hook behind an infinite spin

§2's step 4 says *"through the payload's own reboot path"*, and that phrase needs the paragraph below, because
this tree holds **two** things a reader could call a reboot path and only one of them can reset this device.
The other does not merely fail — it **hangs**, which is the one outcome this whole design exists to avoid. The
measurement was taken on the frozen images and it closes the question rather than narrowing it.

**(a) The platform hook is an unfilled `.bss` slot.** `PE_halt_restart` is
`int (*)(unsigned int) = 0` at `iokit/Kernel/IOPlatformExpert.cpp:288` and is **never assigned a non-null
value anywhere in the tree**: the only four assignments in the whole repo are that same `= 0` definition, once
per XNU copy (`external/*/iokit/Kernel/IOPlatformExpert.cpp`, `:288` in the built 4570 tree), and every other
mention is a read, a prototype, an `#include` comment or an export-list line — in the built tree,
`IOPlatformExpert.h:65`, `config/IOKit.exports:98`, `osfmk/kdp/ml/x86_64/kdp_machdep.c:39` and the two reads
in **`osfmk/i386/acpi.c:128,135`**, which are i386 sources this ARM build does not compile
(`find out -name acpi.o` → nothing). In the image it is `0x805858d8` (`B`), and **both** overrides read it and
take the `-1` branch when it is zero:

| function | the read | the branch taken with the slot zero |
| --- | --- | --- |
| `IOPlatformExpert::haltRestart` (`0x801762c0`) | `0x801762d4  ldr r2,[r0]` ← `0x805858d8` | `0x801762dc  beq 0x801762ec` → **`mvn r0,#0`; `bx lr`** |
| `IODTPlatformExpert::haltRestart` (`0x801781f0`) | `0x80178224  ldr r1,[r0]` ← `0x805858d8` | `0x8017822c  beq 0x80178240` → **`mvn r0,#0`; `pop {r4,pc}`** |

So the virtual call `PEHaltRestart` makes — `0x8017712c`, vtable offset 892 through `gIOPlatform`
(`0x805858d4`) — returns **-1 without performing any reset on either platform class**.

**(b) `halt_all_cpus` does not return; it spins.** (`0x80011e84`, and it is the OS reboot path's last
instruction)

```
80011ea0:  bl  8003a9b4 <printf>
80011eb0:  bl  80177010 <PEHaltRestart>
80011eb4:  b   80011eb4 <halt_all_cpus+0x30>      <- forever
```

`host_reboot` (`0x800fc3e8`) takes `halt_all_cpus` on its default branch and `PEHaltRestart` on the
`r1 & 0x100` branch, and **neither touches PS_HOLD nor the watchdog**: there is no hardware write anywhere in
the OS's own reboot path. An arm that entered it would hang with whatever `SCTLR.C` state the seam left and
with nothing left to bring the phone back — strictly worse than doing nothing at all.

**(c) It is not reachable from the seam in any case.** `reboot_kernel` (`0x8029f230`) has **exactly one** `bl`
in the whole image — `bsd/kern/kern_xxx.c:140`, inside the `reboot(2)` syscall, whose only caller is userland —
and **none** of `reboot`, `halt`, `shutdown` or `pei` is among the **77** names the entry image wraps
(`nm … | grep __wrap_ | sed 's/^__wrap_//'`). So a `--wrap=reboot_kernel` hook, or a `--wrap=host_reboot` one,
would be entered by nothing the arm can reach: the wrap would move `.text` for a hook no path calls.

**So the rule §2 depends on, measured in three independent ways:** the only reset available to the entry side
is an **explicit hardware write**, which is §3's two primitives. *"Use the OS's own reboot path"* is not a
cheaper third option that was overlooked in this design — it is a NULL function pointer behind an infinite
loop, and §3.1's rehearsal arm must implement step 4 as the writes, not as a call.

**And (a) is a claim about a *scan*, so it needs the scan's own caveat — the first version of that paragraph
was wrong from it.** `grep -rIn PATTERN .` in this environment **does not descend into `external/` at all**:
the string `PE_halt_restart` has **55** occurrences under `external/` and a `.`-rooted recursion reports **4**,
all of them in `docs/`, with **nothing on stderr** to say so; a control (`IOService::getPMRootDomain`, 127 hits
under `external/`) reads **1**. The enumeration above was re-taken with the path named — `grep -rIn PATTERN
external/` — which is why the four `= 0` definitions in four XNU copies are visible here at all. **A
whole-tree grep is not evidence about `external/`**; the two claims that survive are the one verified by an
explicit path and the one verified in the image by `nm`/`objdump`.

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

**Note the ordering first: the rehearsal arm of §3.1 comes before this one.** It is cheap, its answer
is a fact either way, and without it this arm's premise is unmeasured.

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
image's symbol table, and the two logs from the 03:53:50 run. **§3.2's readings are read-only too:**
`arm-none-eabi-objdump -d` over `out/stage90/xnu_arm_entry.elf`, `nm` over the same file, and greps over the
source tree — no write to `out/`, and no build. No edit to any file in 660 §5's closure, and the arm still
verifies against the `armed-seam-poc-a43304f2` park. `fastboot boot` only, never `flash`; the design in §2
ends a run through the payload's **own** named reboot path and not through storage.
