# Experiment 106 — XNU's `_start` executes on MSM8974

Date: 2026-09-17
Build switch: `STAGE90_XNU_ENTRY = 1` (default off), gate flag `--allow-xnu-entry`
Other switches: `HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/kmsg-xnuentry1.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: _start ran to completion and branched to arm_init
```

The second line was written by code inside the entry image, with the MMU disabled, after XNU's own
`osfmk/arm/start.s` had done everything a kernel entry point does. The device returned to Android
on its own.

**Every claim this project has made since Stage76 about public XNU not executing is now obsolete.**
XNU's real `_start` ran on this device, unmodified, end to end.

The decisive argument that the line came from the entry image rather than the payload is not the
string — it is the ordering: the payload's last instruction is the jump. Nothing it owns executes
after `stage90 xnu_entry: jumping to XNU's _start`, so anything in the log below that line was
written by the image that was jumped to.

## What XNU actually did

From `osfmk/arm/start.s`, in order, all of it on this hardware:

- `cpsid if`; invalidated the I-cache; read SCTLR, OR'd in `SCTLR_ICACHE | SCTLR_PREDIC`, wrote it
  back — XNU's first act, exactly as the source says.
- Loaded `physBase`, `virtBase` and `memSize` from the `boot_args` at offsets 8, 4 and 12 — the
  layout `tools/check_xnu_struct_abi.py` has been checking against XNU's own header since Phase 2.
- Patched all eight entries of `ExceptionVectorsTable` with `LOAD_PHYS_ADDR`-converted handler
  addresses.
- Wrote `TTBR0` = `TTBR1` = `topOfKernelData` (0x00220000) and `TTBCR` = `TTBCR_N_1GB_TTB0`.
- Invalidated the TTEs, built a V=P section for its own PC, then mapped the kernel with 1 MB
  sections from `physBase` for `memSize`, spilling to an L2 table where it needed to.
- Mapped a page for the high exception vectors at `0xffff0000`, from our `ExceptionVectorsBase`.
- Cleaned and invalidated the L1 and L2 caches by set and way, from `CCSIDR` geometry.
- Loaded SP from `intstack_top`, then in `join_start`: set DACR, PRRR and NMRR, programmed SCTLR
  with TEX remap, high vectors, access-flag and swap-enable, turned on both caches and the MMU,
  flushed the TLB, enabled VFP, and branched to `arm_init`.

That is a real kernel entry sequence — page tables, caches, MMU, exception vectors, coprocessors —
executing on a phone whose bootloader has no idea any of it exists.

## Why it worked first time

`experiment-103` is the reason this was a small change rather than a large one, and it is worth
naming because it was counter-intuitive: **`start.s` does not want a page table from us, it builds
its own.** The roadmap had planned a page-table builder for Phase 2 on the assumption that XNU
would adopt tables we produced. Reading `start.s` showed the opposite, and that removed work
instead of adding it. What XNU needs is:

- a `boot_args` describing where it was linked (`physBase`, `virtBase`, `memSize`,
  `topOfKernelData`),
- a writable, 16 KB-aligned `topOfKernelData` with room, and
- the twenty symbols `_start` references, none of which is kernel logic — eight exception vector
  targets, a vector table, a vector code page, four stack/global pointers, and `arm_init`.

So the payload's whole job is mechanical: map the window, copy the image, zero its BSS, build a
`boot_args`, publish both, jump.

## The two design choices that made it work

**1. `physBase == virtBase`.** `_start` converts every symbol to a physical address with
`addr - virtBase + physBase`. Setting both to the image base makes that the identity — and that,
in turn, is what lets the entry image's epilogue turn the MMU *off* and keep executing, because
its own address is the same number either way. This is what makes the experiment's evidence
possible at all: `ram_console` is at `0xde500000`, far outside the 2 MB window XNU maps, so the
only way to report the result is to stop translating first.

**2. The entry image reports its own failures.** `_start` installs exception vectors before it
switches tables, so the twenty symbols include eight real handlers. Each disables the MMU and
writes a line naming which vector fired. Without that, a fault in XNU's entry path would be a hang
with no log at all — the exact failure mode this project has spent two sessions learning to avoid.
As it happened none fired, and `grep -c "real XNU entry: exception"` returning 0 is part of the
result rather than the absence of one.

## The Phase 1 cache work paid for itself here

Two calls from `experiment-97`/`-98` are load-bearing in this jump and nowhere else so far:

- `cache_clean_dcache_range` over the whole window before jumping. With `ICACHE_DCACHE` the copied
  image and the `boot_args` may still be dirty, and XNU reads them through page tables it is about
  to write itself — which the payload's D-cache knows nothing about.
- `cache_invalidate_icache_all`. The copy just put *instructions* at addresses nothing has ever
  executed from; a stale line there would be fetched instead.

Neither was needed for the payload's own correctness. Both are needed the moment something outside
the payload's cache view starts reading its memory, which is what entering a kernel is.

## What this does and does not establish

**Does:** XNU's own entry code executes on MSM8974, and its page-table construction, cache
programming, MMU enable and vector installation all work on this SoC. This is the barrier the
project's notes described as never having been crossed.

**Does not:** make XNU a kernel, and this is the part to be exact about. `_start` branches to
`arm_init`, and the `arm_init` it branches to is a Stage-owned stub, because XNU's real one is not
in the image. Everything after that point in a real kernel — `arm_vm_init`, `machine_startup`,
the scheduler — does not exist here. So the honest statement is: **XNU's entry point runs; XNU
does not run.** "Entering the operating system" is still ahead, and the next thing it needs is the
symbol *after* `arm_init`, which is a much larger body of code than the entry path.

Also not established: any of this is useful yet. The stub reboots immediately. Nothing XNU
initialised is observed to work, because nothing after `arm_init` exists to observe it.

## Safety, for the record

The jump is one-way by construction, and the software dead-man is deliberately *not* armed across
it — it needs the payload's GIC and vector state, which `_start` replaces. The hardware watchdog
is the only net, and it is the right one: it needs nothing from the CPU's configuration. Nothing
was written to storage, `fastboot boot` is non-persistent, and the MMIO touched is IMEM,
PS_HOLD and the watchdog. No brick risk; the worst case was a ~28 s watchdog reset.
