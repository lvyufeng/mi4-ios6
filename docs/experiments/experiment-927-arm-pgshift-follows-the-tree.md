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