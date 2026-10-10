# Experiment 971 — the console dies at `arm_vm_init`: D13 builds a fresh system table where 4570 copies the boot table

**Status:** ✅ **BUILT and PARKED (`armed-d13-89bcc6e3`, entry bin `89bcc6e3`) — NOT PRESSED.**
Continues the 970h press (2026-10-10), which BOOTED the D13 line for the first time and stopped dead
on `arm_vm_init: switching translation-tables now...` — exactly this rung's frontier. The kernel
object `out/xnu_kernel_obj_d13/osfmk_arm_arm_vm_init.o` was rebuilt from the staged source and the
linked image now calls `bcopy` where it called `bzero` (verified by value, §6). `verify_press_ready`
5/5; `make check` 0; the live tree matches `armed-d13-89bcc6e3` exactly. **THE PRESS IS THE
OPERATOR'S.**

Supersedes nothing; continues `experiment-970-the-window-is-ram-not-just-address.md` §6g (970g) and
`experiment-970h-the-pre-switch-tlb-flush-destroys-the-boot-tables-live-entry.md` (whose press this
follows).

---

## 0. The arm — how it was built, and one discrepancy found on the way

The entry image was linked with the arm's own switch set and the kernel object recompiled from the
971-staged source. **The pressed line (970h, `armed-d13-299ee994`) had `STAGE90_XNU_MEM_TOTAL=1` in
its record but NO `MEM_TOTAL` edit in the LINKED image** — its `arm_vm_init` disassembles to `bzero`
only and never branches to `PE_get_default`, and the live pool object `U bzero` (only) agrees. So
`MEM_TOTAL` was inert on the pressed arm: the 958 3 GB-report change is **not in any image that has
been shipped** and is a separate rung, not a live defect. To reproduce the pressed line faithfully
(970h + 971, no MEM_TOTAL) the 971 object was compiled WITHOUT `-DSTAGE90_XNU_MEM_TOTAL=1` and
installed over the live pool object. Verified faithful: its defined-symbol set is byte-identical to
the old object's and its undefined set differs by exactly `+bcopy` (the 971 change). The platform
block (`out/xnu_platform_obj_d13/`) was rebuilt with the arm's mount define set
(`MOUNT/HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD/CARD_TOTAL/FULL_EXTENT/CARD_COW/STORAGE_PROBE`,
`HDD_WRITE=0`) and **without** the HFS port (`STAGE90_HFS=0`), because D13 defines `is_suser`,
`is_suser1`, `vnode_name`, `proc_tbe` natively — the parked ELF's `is_suser1` is the native one the
shim never supplies.

**Two defects in the *journal* environment, not the artifact, are recorded here because they cost
this session time:** (a) an early scratch compile that did not pass `XNU_PLATFORM_OBJ_OUT` wrote the
LIVE platform pool with the default (mount-off) defines — `stage90_root_media.o` silently went from
`ROOT_FROM_CARD=1` to the default; this is the "one value, two definitions" class with a build in the
middle, caught only because `build_entry.sh`'s `xnu_entry_882/888` comparisons refuse a two-script
disagreement. The platform pool was rebuilt with the correct set and the full build then ran clean.
(b) `tools/build_xnu_arm_kernel.sh --dir osfmk` also runs the out-of-manifest platform block, so a
`--dir` run is not a single-object build; the pool wipe at `:444` applies to it too.

---

## 1. The two halves of one silencing

`entry_stubs.c:2210-2214` states the live channel's own contract in prose:

> the console's own sections go in before XNU's `arm_vm_init` copies the boot table: it computes
> `cpu_ttep = topOfKernelData + ARM_PGBYTES * 4` and `bcopy(boot_tte, cpu_tte, ARM_PGBYTES * 4)`
> (`osfmk/arm/arm_vm_init.c:370-380`), and **the copy is what carries the console's descriptors into
> the table the MMU walks afterwards**. So the console's latch stays valid *because* its install
> happened first.

The D13 entry line has **two** ways to lose that console, and they are the same loss one rung apart:

| rung | where | D13 | 4570 |
|------|-------|-----|------|
| **970g** | `_start` | `__start` takes the MMU fast path (`locore.s:61 beq mmu_initialized`, taken because the payload jumps in with `SCTLR.M=1`); the boot path never runs | `_start` writes `TTBR0` unconditionally and **zeroes the boot table** (`start.s:159-167`) |
| **971** | `arm_vm_init` | `arm_vm_init.c:352-353` `cpu_ttb = gTopOfKernel + L1_SIZE; bzero(phys_to_virt(cpu_ttb), L1_SIZE);` — a **fresh** table, nothing copied | `arm_vm_init.c:385-391` `boot_ttep = args->topOfKernelData; cpu_ttep = boot_ttep + ARM_PGBYTES*4; bcopy(boot_tte, cpu_tte, ARM_PGBYTES*4);` — the **same address** D13 bzeroes, filled by a copy |

970g makes the console reachable through the `_start` → `arm_init` window (the first ~15 keys). 971
makes it survive `set_mmu_ttb(cpu_ttb)` (`arm_vm_init.c:428`), which is what the keys *after*
`arm_vm_init` need. Without 971 the press of 970g proves the first window and then goes silent again.

## 2. Why D13's copy is the whole fix (no second edit)

4570 follows the copy with a loop writing `ARM_TTE_TYPE_FAULT` over the copied V==P L1 slots
`[ttenum(gPhysBase), ttenum(gVirtBase))` (`arm_vm_init.c:396-411`). D13 needs no such loop here:

- **D13 maps the identity region fresh, after this point.** `l2_cache_to_range(identityCachePA,
  identityBaseVA, …)` (`:376`) and `l2_map_linear_range(identityCachePA, gPhysBase - sectionOffset,
  gPhysBase + gMemSize)` (`:388`) write `L1_TYPE_PTE` over the L1 slots for `[gVirtBase, gVirtBase +
  gMemSize)` — the very slots any copied identity **section** would occupy. The copied V==P sections are
  **overwritten**, not inherited.
- **On the shipped arm `gPhysBase == gVirtBase == 0x80000000`,** so 4570's clear range is **empty**.

What the copy carries into the system table is therefore exactly what must survive: the identity
sections (overwritten next), the entry console at `0xde500000`, its megabyte twin `0xde600000`, the
alias `0xee500000`, and any device sections the early probes installed.

## 3. The edit

```
-    bzero((void*)phys_to_virt(cpu_ttb), L1_SIZE);
+    bcopy((void*)phys_to_virt(gTopOfKernel), (void*)phys_to_virt(cpu_ttb), L1_SIZE);
```

`cpu_ttb == gTopOfKernel + L1_SIZE` is the system table; `gTopOfKernel` (offset 10485760 =
`0x00A00000`, i.e. PA `0x80A00000`, the same value 970g's `invalidate_tte` zeroes) is the boot table.
`bcopy` and `bzero` are both declared in the same reachable header (`osfmk/libsa/string.h:90-91`; the
object already references `strncmp` from it), so the change adds no declaration and compiles under the
tree's own recipe.

- `tools/patch_d13_vm_init.py` — the idempotent edit (anchor matched whole; the replacement's token is
  the "already applied" marker).
- `tools/stage_d13_vm_init.sh` — the thin, named applier (`external/` is re-provisionable).
- `tools/check_d13_vm_init_staged.sh` — re-derives the property (the copy is present, the bzero is gone,
  the copy is **adjacent** between the `cpu_ttb` assignment and `identityCachePA = cpu_ttb + L1_SIZE`),
  refuses drift. Verified to **FAIL** on a reverted tree.

## 4. What the build/press must show

Build side: the rebuilt `out/xnu_arm_obj_d13/arm_vm_init.o` must reference **`bcopy`** where it
referenced `bzero`, and the linked entry image's `arm_vm_init` must call the copy before the
`set_mmu_ttb`. Scope: **one statement in `osfmk/arm/arm_vm_init.c` alone**; the 4570 tree is untouched
(its copy is already correct).

**PROVEN BY COMPILATION (host, 2026-10-10).** Ran the tree's own `tools/build_xnu_arm_kernel.sh` with
the D13 tree and a **scratch** `XNU_KERNEL_OBJ_OUT=/tmp/…` (never the live pool): the **whole D13 kernel
compiles — 609 C + 96 C++, `fail: 0`** — with the patched `arm_vm_init.c`, and the resulting
`osfmk_arm_arm_vm_init.o` references **`bcopy`** (plus `bzero`, the L2 clear) where the unpatched live
object `out/xnu_arm_obj_d13/arm_vm_init.o` references **only `bzero`**. And the link resolves with **no
new closure**: `bcopy` is already **defined** in the live entry ELF (`out/stage90/xnu_arm_entry.elf`,
`T bcopy` at `0x80015d20`, from `osfmk/arm/bcopy.s`), and `bcopy_phys` at `0x8001849c`. So the patch
compiles, emits the call, and links.

**BUILT AND PARKED (host, 2026-10-10).** The real-pool object was produced by compiling
`osfmk/arm/arm_vm_init.c` with this tree's flags into a scratch root (no `MEM_TOTAL` — see §0) and
installing it at `out/xnu_kernel_obj_d13/osfmk_arm_arm_vm_init.o`; its undefined set went `bzero` →
`bzero + bcopy` and its defined set was unchanged. The entry image was then relinked
(`xnu_arm_entry.bin` `299ee994` → `89bcc6e3`) and the payload rebuilt around it (`stage90-qcdt.img`
`f3b082ba` → `89e86c77`). **By value in the LINKED image** `out/stage90/xnu_arm_entry.elf`:
`arm_vm_init @0x8001e928` now has `bl 80015d20 <bcopy>` at `0x8001e9d4`; the 299ee994 elf had `bl
…<bzero>` there. Parked as `armed-d13-89bcc6e3` (all members in
`out/stage90/frozen/armed-d13-89bcc6e3/`, recorded in `records/revert-set.txt`), `verify_press_ready`
5/5, `make check` 0 (incl. `check_d13_vm_init_staged`).

Press side (operator; the *second* D13 press): keys **after** `arm_vm_init` appear for the first time —
the SMC/USB/storage probes and, if the boot is clean, the Darwin banner. Contrast the 970g-only press,
which (if the split is as predicted) goes silent right after the pre-`arm_vm_init` keys.

**Falsification:** if 970g's press already reaches the full boot, 971 was not needed for the console —
recorded, not assumed. If a 970g+971 press *still* stops at `arm_vm_init`, the boot table is not the only
place the console's descriptors live and the model is wrong one rung further out.

## 5. Scope / non-goals

- **The 3 GB** is not touched (958's `max_mem` report and 915-B's low-bank pmap port are separate).
- **`gPhysSize` does not exist in D13** — there is no `V==P` clear to port, and none is needed (§2).
- **Not built now** by design: a build would overwrite `out/stage90/*`, which must stay as 970g's live
  bytes until the operator presses 970g (the press is resident — a black screen and a power-cycle — so
  it genuinely needs the operator at the device).

## 6. By-value verification (host)

- `arm_vm_init.c:352` `cpu_ttb = gTopOfKernel + L1_SIZE;` — the system table (4570's `cpu_ttep`).
- `arm_vm_init.c:353` (D13 HEAD) `bzero((void*)phys_to_virt(cpu_ttb), L1_SIZE);` — the defect.
- `arm_vm_init.c:365` (staged) the `bcopy` — ; `:368` `identityCachePA = cpu_ttb + L1_SIZE;` unchanged.
- 4570 `arm_vm_init.c:385-391` — `boot_ttep = args->topOfKernelData` … `bcopy(boot_tte, cpu_tte,
  ARM_PGBYTES * 4)`.
- `MANAGED_BASE == 0xC0000000` (D13), `gMemSize == 0x01000000` (16 MiB) → managed map
  `[0xC0000000, 0xC1000000)`, which does **not** contain `0xde500000`.
- `out/stage90/xnu_arm_entry.h`: `STAGE90_XNU_TOP_OF_KERNEL_DATA_OFFSET 10485760` (0x00A00000),
  `STAGE90_XNU_ENTRY_TABLE_BYTES 0x0000A000`.
- `make check` exit 0 (with `check_d13_vm_init_staged` wired in); the live 970g arm
  (`out/stage90/xnu_arm_entry.bin` = `321e3332…`) is unchanged.