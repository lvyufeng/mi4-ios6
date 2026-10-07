# Experiment 912 — the residence wall may be memory: XNU is given a ~8 MB free region

Date: 2026-10-07. **Host-side: a finding read out of the port's own logs, and one rung designed. A
press is owed; the press decides.** This did not come from the 909 idle investigation — it came from
re-reading what the entry handoff tells XNU about its memory, and it is a confounder that every 909 arm
so far has held constant.

## 1. The finding — the live `memSize` gives XNU ~8 MB of free RAM, and no arm has ever changed it

`xnu_entry_build_args` sets `boot_args.memSize = STAGE90_XNU_ENTRY_SIZE` (`src/xnu_entry_jump.c:150`) —
**the entry window's size, and the kernel's view of physical memory in one number** (the build script's
own comment at `src/entry/build_entry.sh:28622-28625` says so: *"it is also `boot_args->memSize` …
therefore the kernel's view of physical memory"*). The window is `0x01000000` (16 MB,
`build_entry.sh:28626`).

**The 909 capture confirms it on the device** — `909-resident-2nd-…-last_kmsg.txt:3904`:

    xnu_entry_args_physBase=0x80000000
    xnu_entry_args_memSize=0x01000000
    xnu_entry_args_topOfKernelData=0x80800000

So `physBase = 0x80000000`, `memSize = 0x01000000`, `avail_end = 0x81000000`, and
`topOfKernelData = 0x80800000`. XNU's free region is what `arm_vm_init` leaves after the kernel, its
boot tables and the device tree: `avail_start = topOfKernelData + 10 pages = 0x8080A000`, so

    FREE = avail_end - avail_start = 0x81000000 - 0x8080A000 = 0x7F6000 = 7.96 MB

**And this is not a new observation — it is an old one that was never repaired.** Experiment 444, which
moved the device tree out of that region, wrote the same thing down and left it:

> `avail_end = 0x81000000` and XNU's whole free region is `0x81000000 − 0x8070A000` = `0x1F6000` ≈
> **2.05 MB**. The allocator had consumed 2.07 MB by the walk: free memory was, for practical purposes,
> **gone**. That is a second symptom of the same shape — one value with two meanings — and it is not
> fixed by moving the tree.

444's *tree fix* landed; its second half — *"it is not fixed"* — did not. The window was later raised to
16 MB and `topOfKernelData` moved to +8 MB, so the free region is now 7.96 MB rather than 2.05 MB. **No
experiment in the corpus has ever varied it, and no capture carries a memory reading** — the keys that
publish `mem_size`/`sane_size`/`avail_end` (`src/entry/entry_stubs.c:8290-8302`) live in the entry's
*replacement* `pmap_bootstrap`, which compiles only `#ifndef STAGE90_ENTRY_REAL_PMAP_BOOTSTRAP` —
and the live build runs the **real** XNU `pmap_bootstrap`, so those keys never publish at all (the
908/909 captures confirm: no `xnu_entry_pmb_*` line). The port therefore has **no memory reading in any
capture**, by construction.

## 2. Why this is a real confounder for the residence wall, and why it is not yet proof

**For.** The residence rung's object is a boot that **stops at ~13.4 s**, inside the 5th idle pass
(909 R10/R12/R13). R13's own census is the sharpest fact: every run that ever idled past 8 passes had
its ending armed to fire at exactly its pass count — *"the ending has been the thing that stops the idle
path in every measured run but 909's"*. So 909's 5 passes are **the longest un-ending idle this project
has ever run**, and it is also the one that stopped. A kernel running an OS on 7.96 MB of RAM, dying
after ~13 s of real uptime, is precisely a memory-exhaustion shape — and the port has never ruled it out,
because it has never been able to see its own free-memory number at the failure.

**Against — and this is why the press, not the argument, decides.** R10 narrowed the stop to a *specific
place*: the `wfi` returned four times (four complete `before`/`after` pairs) and the death is between the
**5th** `platform_cache_idle_enter` and its exit. A pure OOM does not obviously explain a death localized
to one pass of one code path; it predicts a fault wherever the next allocation lands, not necessarily the
idle tail. So the two hypotheses are not exclusive and this rung cannot separate them by argument alone.

**The discriminator.** Raise the free region by a large factor and hold everything else. If memory is the
constraint, the behaviour changes sharply — the death moves, vanishes, or the idle path's 5th pass
completes; if the idle path faults independently of memory, the death is unmoved at pass 5 / ~13.4 s. One
press, one number changed, and the existing log keys read the answer without any new hook.

## 3. The rung — 912a: widen the entry window, so the free region is 8× larger

The one-line change: `ENTRY_SIZE` `0x01000000` → **`0x04000000` (64 MB)** (`build_entry.sh:28626`). The
window is also `memSize`, so this raises `avail_end` to `0x84000000` and the free region to
`0x84000000 − 0x8080A000` = **`0x3F6000` ≈ 55.9 MB** — and, if the tree limit and `topOfKernelData` are
kept proportional, more. **Whichever way, the free region grows by ~8× in one rebuild.**

- **Safe.** `[0x81000000, 0x84000000)` is real RAM — the device's high bank `[0x80000000, 0xde700000)`
  has no holes below `0x80000000` (§911 §2), so telling XNU it owns 64 MB points it at memory, never at
  MMIO. This is the *opposite* of 911a's hazard (911a's risk is raising `memSize` past a hole); here the
  span is provably clean. It rides the normal `fastboot boot` gate and touches no boot-chain byte.
- **`memSize` is the only value that must move with it.** `start.s` maps `memSize / 1 MiB` sections and
  the payload's identity map covers `[ENTRY_BASE, ENTRY_BASE + STAGE90_XNU_ENTRY_SIZE)`; both are
  `ENTRY_SIZE`-driven, so they move together. The tree limit and `topOfKernelData` are derived from the
  image and the `+0x100000`/`+0x200000` gaps, so they stay put — the window simply has more room *above*
  them, which is exactly the free region.

**Verdict cells.** The idle census (`xnu_live_idlestack_calls`, `slot_post_calls`) against 909's 5/4,
and the time to death against 13.4 s — both existing keys. **Plus one genuine source change**: the port
has *no* memory reading at all (the `pmb` keys sit behind `STAGE90_ENTRY_REAL_PMAP_BOOTSTRAP`, which the
live build does not define — §1), and XNU's `max_mem` sysctl is not reachable from this log. So 912a
should add a **live memory publish on the idle path** — the entry wrapper can read the kernel's own
`extern uint32_t mem_size, sane_size;` (already declared at `entry_stubs.c:8279/8283`) and emit them
like `xnu_live_idlestack_calls`, so the free-memory number is in the log *at* the wall. That is the one
edit that turns "8 MB free" from a host-side computation into a device-measured fact.

## 4. What this does and does not claim

- **It does not claim OOM is the cause.** It claims OOM has never been excluded, the free region is
  provably tiny (7.96 MB, measured from the image and confirmed in the 909 log), and one safe rebuild
  excludes or confirms it.
- **It is not 911.** 911 asks XNU to *recognize* 1.5/3 GB; 912 asks whether the *current* 8 MB free
  region is what ends the boot. They share the `memSize` field, so **911a and 912a cannot be pressed
  as one arm** without confounding the residence verdict — but 912a is the smaller, safer change and
  should go first: if the boot survives on a bigger free region, 911a gets a live kernel to extend.
- **Nothing here touches the device.** No press. The arm is built and parked; the press is the
  operator's.