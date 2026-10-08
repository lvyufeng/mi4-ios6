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
## Appendix — the 86-object census, and why it is NOT a rename table

Of the 263 distinct objects `build_entry.sh` names through the derived pools, **86 are in no `_d13`
pool**. The tempting reading — "same basename, new directory" — is a **trap**, and comparing defined
symbols shows it:

| 4570 object | D13 same-basename object | symbols 4570 has that D13's lacks |
|---|---|---|
| `pexpert_arm_pe_init.o` | `pexpert_arm_common_pe_init.o` | **33** — `gPEClockFrequencyInfo`, `gPlatformECID`, `gTargetTypeBuffer`, `gPanicBase`, … |
| `pexpert_arm_pe_identify_machine.o` | `pexpert_arm_common_pe_identify_machine.o` | **34** — `gPESoCBasePhys`, `gPicBase`, `gTimerBase`, `gSocPhys`, … |
| `osfmk_console_serial_console.o` | `osfmk_console_arm_serial_console.o` | **11** — `console_ring_try_empty`, `nmi_counter`, … |
| `osfmk_arm_machine_task.o` | `osfmk_kern_task.o` | 5 — `machine_task_{get,set}_state`, … |

The D13 `common/` objects are the **generic** PE; the **ARM/board-specific symbols live in D13's
board PE** (`pexpert/arm/pe_qsd8250_leo.c`, the HD2 fork's board file — the analogue of our MSM8974
board PE). So the closure is **per-symbol**, not per-object, and it must be re-derived from D13's own
object graph.

What the per-symbol view already establishes:

- **re-point** (symbol has a D13 definer): `gVirtBase`/`gPhysBase` → `osfmk_arm_arm_vm_init.o`
  (`nm`: `B gPhysBase`, `B gVirtBase`); `ExceptionVectorsBase` → `exctramps.o`; `intstack`/`debstack`/
  `debstack_top` → `locore.o`.
- **supply** (no D13 definer): `RTClockData`, `CpuDataEntries`, `BootCpuData` — the rest of 4570
  `data.s`'s per-CPU data — appear **nowhere** in D13 (an index of all **31 849** defined D13 symbols
  has no entry). Likewise the `strlen`/`strncmp`/`strnlen`/`caches_asm` `.s` files (D13's ARM `.s`
  inventory is 38 files, none of them these) and the iOS-7-absent C objects
  (`osfmk_kern_{waitq,telemetry,coalition,work_interval}` etc.).

The next rung's first artifact is a `symbol -> defining D13 object` index over all `_d13` pools
(`arm-none-eabi-nm --defined-only … | sort -u`), and its first move is to seed the `entry_closure.py`
walk from **D13's own** `arm_init.o` undefined set rather than 4570's seed list — because the question
is not "where did this object go" but "what does D13's `arm_init` actually need".
