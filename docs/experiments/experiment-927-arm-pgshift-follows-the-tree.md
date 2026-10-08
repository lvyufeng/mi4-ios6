# 927 — `ARM_PGSHIFT` follows the tree (2026-10-08)

926 retargeted the entry link's object pool. The next step of the D13 link stopped on the same
class of defect, one level down: a **header path** pinned to 4570.

## The defect

`tools/host_ramdisk_macho_check.py` (the ramdisk Mach-O check, run for the entry image) reads the
kernel's page shift from the kernel's own source:

    return hdr_define(os.path.join(XNU, "osfmk/arm/proc_reg.h"), "ARM_PGSHIFT")

`XNU` is already `XNU_TREE`-selected, so the *tree* was fine — but the **header name** was not.
4570 defines `ARM_PGSHIFT` in `osfmk/arm/proc_reg.h:605`; **Darwin-13 moved it to
`osfmk/mach/arm/vm_param.h:22`** (a file both trees ship, but which 4570 only consumes through
`PAGE_SHIFT`). On D13 the read fails with `proc_reg.h does not define ARM_PGSHIFT - the header this
check reads has moved` — which then surfaces as `the RAM disk Mach-O (entry_ramdisk.s) is not what
parse_machfile reads`, a message about the *fixture* for a defect in the *reader*.

This is exactly the recurring class — an asset pinned to one tree — with the asset being a header
path rather than a value or an object.

## The fix

Try both known locations, in order, and take the one that actually defines the macro:

    for rel in ("osfmk/arm/proc_reg.h", "osfmk/mach/arm/vm_param.h"):
        ...if the tree's header defines `ARM_PGSHIFT`, return hdr_define(it)...
    sys.exit("ARM_PGSHIFT is defined in neither ... - the header it lives in has moved again")

`XNU` is `os.environ.get("XNU_TREE", …4570…)` (:52), so selecting the tree selects the header; the
loop is the "follow the selected tree" rule stated as a search over the places the macro has lived,
and the `sys.exit` keeps the failure loud if it moves a third time.

## Verification

| check | result |
|---|---|
| `arm_pgshift()` on 4570 | 12 |
| `host_ramdisk_macho_check.py` with no env (4570) | exit 0 |
| `host_ramdisk_macho_check.py` with `XNU_TREE=<d13>` | exit 0 (was: the "moved" failure) |
| `make check` | 0 |

The D13 ramdisk check now passes, and the D13 entry link proceeds to the next wall: the entry
closure names **86 objects** the D13 pools do not contain (`data.o`, `strlen.o`, `cswitch` vs
`caches_asm.o`, `pexpert_arm_pe_init.o` at a new path, and 82 more) — Darwin-13's source layout and
its HD2 fork's `.s` set differ from 4570's, so which object each closure slot names has to be
re-derived name by name. That is the next rung.

## What moved

`tools/host_ramdisk_macho_check.py`, one function. No tree edit, no device, no other file.
## Appendix — the 86-object census (for the next rung)

Of the 263 distinct objects `build_entry.sh` names through the derived pools, **86 are in no `_d13`
pool**. Censused against D13's own 744 objects:

- **~20 are ARM/PE seed objects with a clean D13 path**: `pexpert_arm_pe_{init,identify_machine,
  bootargs,kprintf,serial}.o` → `pexpert_arm_common_pe_*.o`; `osfmk_arm_machine_task.o` →
  `osfmk_kern_task.o`; console → `osfmk_console_arm_serial_console.o`.
- **~53 are genuinely absent from Darwin-13** — iOS 7 predates them or restructured them:
  `osfmk_kern_{waitq,telemetry,coalition,kern_stackshot,work_interval,sched_multiq,kern_monotonic}.o`,
  `osfmk_ipc_ipc_voucher.o`, `osfmk_corpses_corpse.o`, the whole `osfmk_corecrypto_*` and
  `osfmk_prng_*` trees (moved to `bsd/dev/random/` and `libkern/crypto/corecrypto/`),
  `osfmk_arm_{caches,cpu_common,cpuid,lowmem_vectors,io_map,strlcpy,strncpy}.o`, and the assembly
  leaves `data.o`, `caches_asm.o`, `strlen.o`, `strncmp.o`, `strnlen.o`.

So the closure re-derivation is **three** kinds, not one:

1. **Clean path renames (~20).** The object exists under a new path: `pexpert_arm_pe_{init,
   identify_machine,bootargs,kprintf,serial}.o` → `pexpert_arm_common_pe_*.o`; `osfmk_arm_machine_task.o`
   → `osfmk_kern_task.o`; `osfmk_console_{serial,video}_console.o` →
   `osfmk_console_arm_serial_console.o`.
2. **Symbols that moved into a C object.** `data.s` does not exist in D13; `gVirtBase`/`gPhysBase`
   are now `B` symbols in `osfmk_arm_arm_vm_init.o` (`nm`: `0000000c B gPhysBase`, `00000010 B
   gVirtBase`). The closure slot for `data.o` splits: some of its symbols have a new object, some do
   not.
3. **Symbols with no D13 home at all (~53).** `RTClockData`, `CpuDataEntries`, `BootCpuData` — the
   rest of 4570 `data.s`'s per-CPU data — appear **nowhere** in Darwin-13 (`grep -rn` empty). Nor do
   `data.s`, `caches_asm.s`, `strlen.s`, `strncmp.s`, `strnlen.s` exist (D13's ARM `.s` inventory is
   38 files, none of them these). D13's per-CPU data and timer model genuinely differs, so these must
   be **supplied by the project** (a stub or a D13-appropriate definition), not re-pointed.

Which of kind 3 need a stub and which the D13 kernel defines under another name is the next rung's
first question — and it is the rung's size, because kind 3 is where the two models diverge.
