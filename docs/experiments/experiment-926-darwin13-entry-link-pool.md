# 926 — the entry link's object pool follows the selected tree (2026-10-08)

925 closed the last C walls: the Darwin-13 kernel build is complete (C 606/606, C++ 96/96, assembly
33/33) and its objects sit in `_d13` pools. The next step is the **image link** — and the first
thing it hits is that `src/entry/build_entry.sh`, the script that links the XNU entry image, had no
notion of a tree at all.

## The defect

`build_entry.sh` read **one** hard-pinned tree and **one** hard-pinned object pool, 289 times:

    XNU=$REPO_ROOT/external/xnu-4570.1.46
    ARM_INIT_OBJ=...$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_arm_init.o   # ×265
    ARM_DATA_OBJ=...$REPO_ROOT/out/xnu_asm_obj/data.o                    # ×10
    STAGE90_CONFIG_TABLES_OBJ=...$REPO_ROOT/out/xnu_platform_obj/...     # ×11
    local p="$REPO_ROOT/out/mach_headers/kserver/$1"                     # ×1

This is the recurring class of the whole D13 pivot — an asset pinned to 4570 that must follow the
selected tree — and it is the largest single instance of it: the D13 kernel build writes
`out/xnu_kernel_obj_d13` and friends, so a link that reads `out/xnu_kernel_obj` links the **wrong
kernel's** objects into the image.

## The fix

The pool is now **derived**, not pinned, and derived the same way the two scripts already agree on
a tree:

    XNU_TREE=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
    if [[ -f $XNU_TREE/osfmk/sys/types.h ]]; then XNU_OBJ_SUFFIX=_d13; else XNU_OBJ_SUFFIX=; fi
    XNU_KERNEL_OBJ_OUT=${XNU_KERNEL_OBJ_OUT:-$_OUT_BASE/xnu_kernel_obj$XNU_OBJ_SUFFIX}
    ...

Three properties make it correct rather than merely working:

1. **The default tree is 4570, matching `tools/build_xnu_arm_kernel.sh`'s own default**
   (`XNU=${XNU_TREE:-.../xnu-4570.1.46}`). Darwin 13 is always selected explicitly, the same way its
   kernel build is, so entry and kernel cannot disagree about which tree they mean.
2. **The suffix is derived from the tree's own header**, not a second hand-set switch.
   `osfmk/sys/types.h` is the discriminator `tools/check_d13_board_staged.sh` already uses: Darwin 13
   ships the legacy private header, Darwin 17 does not. So `XNU_TREE=<d13>` alone selects `_d13`.
3. **The pool directories are out-parameterized with the exact names `build_xnu_arm_kernel.sh` uses**
   (`XNU_KERNEL_OBJ_OUT`, `XNU_ASM_OBJ_OUT`, `XNU_PLATFORM_OBJ_OUT`, `MACH_HEADERS_OUT`), so pointing
   the kernel build at a pool and pointing the entry link at the same pool is **one variable**.

`XNU` becomes `$XNU_TREE` (it was used in one live place: the HFS `proc_prepareexit` grep). The
`VM_MIN/MAX_KERNEL_ADDRESS` bound read for the 521 guard (`osfmk/mach/arm/vm_param.h`) follows
`$XNU` too. Every per-file object reference — 265 + 10 + 11 + 1 of them — is re-rooted through the
derived directory by a textual substitution of the contiguous prefix.

## 4570 neutrality — proved, not assumed

The replacement is **exactly** the old text for 4570. Evaluated with no environment (the plain
`./build_entry.sh` invocation):

    XNU                 = /…/external/xnu-4570.1.46        # was $REPO_ROOT/external/xnu-4570.1.46
    XNU_KERNEL_OBJ_OUT  = /…/out/xnu_kernel_obj            # was $REPO_ROOT/out/xnu_kernel_obj
    XNU_ASM_OBJ_OUT     = /…/out/xnu_asm_obj
    XNU_PLATFORM_OBJ_OUT= /…/out/xnu_platform_obj
    MACH_HEADERS_OUT    = /…/out/mach_headers

Because 4570 ships no `osfmk/sys/types.h`, `XNU_OBJ_SUFFIX` is empty and the pool names are
character-for-character the ones the script named before. This is a **source-level** proof, and it
is the right kind here: the 2026-10-08 operator decision retired the per-rung 4570 object
rebuild+`cmp` ([[mi4-two-branch-split-4570-vs-master]]), and the working `out/` has operator-side
drift the pristine script hits too (see below), so a full-object reproduction is neither available
nor required. What the proof establishes is the thing the edit could have broken: **the 4570 path
is unchanged.**

## D13 selection

With `XNU_TREE=external/xnu-hd2-darwin13/xnu`, the same evaluation gives `XNU_OBJ_SUFFIX=_d13` and
`out/xnu_kernel_obj_d13` (706 objects), `out/xnu_asm_obj_d13` (67), `out/xnu_platform_obj_d13` (9),
`out/mach_headers_d13` (116) — the pools the D13 build wrote.

## The check

`tools/check_entry_tree_pools.sh` (in `make check`, with a `--selftest`) re-derives the property
from `build_entry.sh` alone, no compiler, no device, and refuses drift: (a) the `_d13` pool is keyed
on the tree's own header; (b) **no live object-path literal remains** — a re-pin would silently read
the 4570 pool on D13; (c) `XNU` is `$XNU_TREE`; (d) the default tree is 4570. Its `--selftest` feeds
the pre-926 text and asserts it is refused.

(The check is written to read the live lines **once** rather than `live | grep -q`: under
`set -o pipefail` a `grep -q` that exits at the first match sends the producer SIGPIPE and the
pipeline's status becomes 141, so a passing check reads as a failure. That is the defect class
`check_mem_size_max.py` names, and the selftest is what caught it here.)

## What moved, and what did not

- **Moved**: `build_entry.sh` only. 320 insertions, 289 deletions, of which 288 are the mechanical
  substitution and the rest are the header block.
- **Did not move**: `make check` is 0; the six other scripts that read the entry image are untouched.

## The next wall

The D13 link now gets past the pool and stops one step in: `build_entry.sh` reads `ARM_PGSHIFT` from
`$XNU/osfmk/arm/proc_reg.h`, and **D13 moved that macro out of that header** (`proc_reg.h does not
define ARM_PGSHIFT - the header this check reads has moved`). That is the next instance of the same
class, and it is the next rung.

## A note on the working `out/`

A plain `./build_entry.sh` in this working tree does **not** reproduce the arm on disk
(`STAGE90_XNU_ENTRY_SHA256=cb4e17f1…`), and neither does the pristine script: `out/xnu_assym/STAGE90_XNU`
and `out/xnu_asm_obj` hold artifacts from the D13 experiments (assym's thread offsets shifted 16
bytes: `TH_CTH_SELF` 1496 vs 1480), and `out/xnu_kernel_obj` holds a `RELEASE`-config pool. Both the
edited and the pristine script refuse it identically. This is operator-side `out/` state, not the
edit — regenerating it is the next build, not a condition of this one.