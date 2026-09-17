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

## What was tried next, and did not work

With `_start` executing, the obvious next increment is to put something *inside* `arm_init` that
runs XNU's own code from the kernel's context rather than the payload's — a device-tree walk, a
boot-argument parse, on the tree and the `boot_args` XNU was actually handed.

It was built and run, and it does not work yet. Recorded because the shape of the failure is more
useful than the fact of it:

- **XNU objects link into the entry image and `_start` still completes.** 4570's own
  `pexpert/gen/device_tree.c`, `bootargs.c` and `arm_pe_bootargs.c` all linked alongside
  `start.s`, and the entry ran to completion with them present. That much is established.
- **Two ABI findings came out of it.** First, XNU 4570's `device_tree.c` is *not* the 2050 one the
  payload links: the iterator API changed from `DTCreateEntryIterator(startEntry, DTEntryIterator
  *iterator)` with an opaque pointer to `DTInitEntryIterator(startEntry, DTEntryIterator iter)`
  with a caller-owned struct. `osfmk/arm/machine_routines.c:465` and
  `pexpert/arm/pe_identify_machine.c:112` both call the 4570 form, so any 4570 caller linked
  against 2050's implementation will not link — a trap Phase 4 would have walked into. 4570's own
  `device_tree.c` compiles with the project's shims; it is in `out/stage90` and reproducible.
  Second, it `panic()`s on a malformed tree, which is worth knowing before a tree is handed to it.
- **The results were not obtained, because of a defect in the reporting.** Values arrived from the
  walk — `tree_ptr`, `tree_len`, `cmdline_len`, `cmdline_head` all reported correctly — but the
  key strings for several entries came back empty or truncated, so which lookups succeeded and
  which failed cannot be read off the log. The values that did arrive suggested `/cpus`, `/arm-io`
  and the timer lookup all failing *inside* the kernel context, which would itself be a finding,
  but that reading is not trustworthy while the reporting is broken and is not claimed here.
- **What was fixed while diagnosing it, and kept**: the epilogue must **clean** the D-cache before
  clearing `SCTLR.C`. The first version did not, and everything `arm_init` wrote with XNU's caches
  on was discarded — the Phase 1 documents say exactly this and this image had not read them. That
  is fixed and in the shipped entry.
- **What was reverted**: the probe itself. `STAGE90_XNU_ENTRY=1` ships the version that works and
  whose output is unambiguous. Half-working code is not left in the tree; the probe is described
  here and its source is not kept.

**A second attempt, and where it stopped.** The reporting was rebuilt rather than patched: a flat
character buffer, then writing each entry straight into the `ram_console` window after adding that
window to XNU's own page table (so nothing had to survive any later state change). The mapping is
correct — TTBR0 read live rather than guessing where `_start` left the table, an existing kernel
section's attribute bits reused rather than hand-written, TLB flushed, and each write cleaned to the
point of coherency by MVA.

It produced **no output at all** from `arm_init`, while the epilogue's own write worked as before.
Carrying three numbers out through the epilogue — a writer that demonstrably functions — gave:

```
map_applied=0x00000000   size_before=0x00000000   size_after=0x00000000
```

`map_applied` is the L1 entry read back after the store, and it is **zero**, which means the store
did not take effect. `size_before` is set by the first statement inside `entry_write` when it is
called from `arm_init`, and it is zero too — so that call did not reach its own body either. Two
stores that must have executed, both reading back as nothing, in a function that ran to completion
and reached the epilogue.

**That inference was wrong, and a third run corrected it.** Carrying the numbers in *registers*
around a single store/read-back — a path no failed store can corrupt, unlike the globals the
previous attempt used — gave:

```
ttbr0        = 0x0022004a      base 0x00220000 (topOfKernelData), flags 0x4a
ttbcr        = 0x00000002      N=2, TTBR0 covers the whole address space
dacr         = 0x00000001      domain 0 client
sctlr        = 0x30c5787d      M=1 C=1 I=1 TRE=1 AFE=1 HIGHVEC=1
probe_wrote   = 0xa5a5a5a5
probe_readback= 0xa5a5a5a5
```

So **stores from `arm_init` do take effect and read back correctly, and the MMU state `_start`
leaves behind is exactly what the source says it should be.** The previous run's zeroes were
evidence about the *reporting path*, not about stores: the globals it wrote and the pointer table
it read them back through came out wrong, which is a worse failure than no output, because it
looks like data.

**What is actually unresolved is the reporting, and it is nondeterministic.** Re-running the same
probe shape produced a legible line in one run and *nothing at all* in the next — the second run
did not even reach the epilogue's own write, which is the one path that had worked in every earlier
run. That is where the next attempt starts: not at the MMU, which is now measured and correct, but
at why output from inside `arm_init` appears only sometimes.

Two things were fixed on the way and are worth keeping:

- **The epilogue must clean the D-cache before clearing `SCTLR.C`.** The first version did not, and
  everything `arm_init` wrote with XNU's caches on was discarded. The Phase 1 documents say exactly
  this and this image had not read them. That fix is in the shipped entry.
- **A TLB invalidate is not enough to publish a page-table change on its own here.** The write
  needs a clean to the point of coherency first, for the same reason.

What shipped is the configuration from the first attempt, because it is the one whose output is
unambiguous. The probe is reverted rather than left half-working, and the second attempt exists
only as this record.

## Safety, for the record

The jump is one-way by construction, and the software dead-man is deliberately *not* armed across
it — it needs the payload's GIC and vector state, which `_start` replaces. The hardware watchdog
is the only net, and it is the right one: it needs nothing from the CPU's configuration. Nothing
was written to storage, `fastboot boot` is non-persistent, and the MMIO touched is IMEM,
PS_HOLD and the watchdog. No brick risk; the worst case was a ~28 s watchdog reset.
