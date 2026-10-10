# Experiment 971 — the console dies at `arm_vm_init`: D13 builds a fresh system table where 4570 copies the boot table

**Status:** host-side COMPLETE and STAGED in the D13 tree (`tools/stage_d13_vm_init.sh` +
`check_d13_vm_init_staged.sh`, wired into `make check`). **NOT BUILT, NOT PARKED, NOT PRESSED** — it
must follow 970g's press, which is the operator's. The D13 tree now carries **970g + 971**; nothing
references 971 until a build runs.

Supersedes nothing; continues `experiment-970-the-window-is-ram-not-just-address.md` §6g (970g).

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