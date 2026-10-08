#!/bin/bash
# **Where a build's outputs go, as one function of the tree it builds (experiment 933).**
#
# The recurring defect class of this pivot is an *asset pinned to 4570* that must follow the selected
# tree ([[mi4-913-ios7-rebase-decision]]): a path, a header, an option, an object pool. `build_entry.sh`
# fixed its half in 925/926 by deriving `XNU_OBJ_SUFFIX` from the tree's own discriminator - Darwin 13
# ships the legacy private header `osfmk/sys/types.h`, Darwin 17 does not - so `XNU_TREE=<d13>` alone
# selects the `_d13` pools, and a plain run is byte-identical on 4570.
#
# The **build tools did not**. Six of them (`build_xnu_arm_kernel.sh`, `gen_mach_headers.sh`,
# `gen_assym.sh`, `build_xnu_arm_layer.sh`, `build_xnu_arm_macho.sh`, `assemble_arm_layer.sh`) each
# defaulted their output root to a tree-independent literal (`$REPO_ROOT/out/xnu_kernel_obj`,
# `.../out/mach_headers`, ...). On 4570 that is right because 4570 *is* the default tree; on Darwin 13
# it means the build writes - and, worse, **reads** - the 4570 outputs. Measured cost: a D13 kernel
# build had to be driven by a hand-typed twelve-variable environment (recorded nowhere), and when it
# was re-run without it the platform block compiled `MSM8974PlatformExpert.cpp` against **4570's**
# `out/mach_headers/mach/mach_host.h`, whose voucher types (absent from D13's MIG output) failed the
# compile. A `mach_host.h` that belongs to another tree is exactly the shape this class is named for,
# and it is silent: the header exists, so nothing says it is the wrong one.
#
# **This file is the one definition.** Every builder sources it right after it sets `REPO_ROOT`; the
# rule (`XNU_TREE` -> `XNU_OBJ_SUFFIX` -> every root) is written down once. Every variable uses
# `${VAR:-...}`, so a caller's explicit value still wins and a controlled comparison is unchanged.
#
# 4570-neutrality is **structural**: 4570 ships no `osfmk/sys/types.h`, so `XNU_OBJ_SUFFIX` is empty
# and every root below is character-for-character the literal it replaced.
#
# It is meant to be *sourced*, never executed.

# **Every root is exported, not just set.** A builder does not only *name* these paths - it *passes
# them down*: `build_xnu_arm_kernel.sh` runs `gen_option_headers.py`, `gen_device_headers.py` and
# `list_sources.py` as children, and those read `XNU_TREE`/`XNU_*_OUT` **from the environment**. A
# variable that is set but not exported is invisible to them, so they fall back to *their own* 4570
# default - the exact silent-wrong-tree failure this file exists to stop, one level down. Exporting is
# behaviour-preserving on 4570: each child's default equals the value exported here.
XNU_TREE=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
if [[ -z ${XNU_OBJ_SUFFIX:-} ]]; then
    if [[ -f $XNU_TREE/osfmk/sys/types.h ]]; then XNU_OBJ_SUFFIX=_d13; else XNU_OBJ_SUFFIX=; fi
fi
export XNU_TREE XNU_OBJ_SUFFIX

# The base is spelled `$REPO_ROOT/out` and the pool name separately, on purpose: the tools that
# re-root paths by textual substitution of a contiguous prefix must not become victims of their own
# rule, so no root below is written as one contiguous literal.
_OUT_BASE=$REPO_ROOT/out

# --- the object pools and generated roots, all `<name><suffix>`. ---
XNU_KERNEL_OBJ_OUT=${XNU_KERNEL_OBJ_OUT:-$_OUT_BASE/xnu_kernel_obj$XNU_OBJ_SUFFIX}
XNU_ASM_OBJ_OUT=${XNU_ASM_OBJ_OUT:-$_OUT_BASE/xnu_asm_obj$XNU_OBJ_SUFFIX}
XNU_ARM_OBJ_OUT=${XNU_ARM_OBJ_OUT:-$_OUT_BASE/xnu_arm_obj$XNU_OBJ_SUFFIX}
XNU_MACHO_OBJ_OUT=${XNU_MACHO_OBJ_OUT:-$_OUT_BASE/xnu_macho_obj$XNU_OBJ_SUFFIX}
XNU_PLATFORM_OBJ_OUT=${XNU_PLATFORM_OBJ_OUT:-$_OUT_BASE/xnu_platform_obj$XNU_OBJ_SUFFIX}
MACH_HEADERS_OUT=${MACH_HEADERS_OUT:-$_OUT_BASE/mach_headers$XNU_OBJ_SUFFIX}
# The build tools read the MIG root as `MIG_HEADERS`; `gen_mach_headers.sh` writes it as
# `MACH_HEADERS_OUT`. They are one directory (one value, one definition), so both names point at it.
# The kserver variant goes BESIDE it, not at a fixed root: the two are outputs of the SAME `.defs` set
# (the `-DKERNEL_SERVER` half), and pairing one tree's headers with another tree's kserver is the same
# defect one level down.
MIG_HEADERS=${MIG_HEADERS:-$MACH_HEADERS_OUT}
MIG_KSERVER_OUT=${MIG_KSERVER_OUT:-$MACH_HEADERS_OUT/kserver}
XNU_GENERATED=${XNU_GENERATED:-$_OUT_BASE/xnu_generated$XNU_OBJ_SUFFIX}
XNU_OPTION_HEADERS_OUT=${XNU_OPTION_HEADERS_OUT:-$_OUT_BASE/xnu_options$XNU_OBJ_SUFFIX}
XNU_DEVICE_HEADERS_OUT=${XNU_DEVICE_HEADERS_OUT:-$_OUT_BASE/xnu_device$XNU_OBJ_SUFFIX}
XNU_ASSYM_OUT=${XNU_ASSYM_OUT:-$_OUT_BASE/xnu_assym$XNU_OBJ_SUFFIX}
# The suffix goes *before* the extension for these two, so they cannot use the `<name><suffix>` shape.
XNU_DEVICE_TABLE=${XNU_DEVICE_TABLE:-$_OUT_BASE/device_table$XNU_OBJ_SUFFIX.txt}
MANIFEST=${MANIFEST:-$_OUT_BASE/xnu_arm_manifest$XNU_OBJ_SUFFIX.txt}

# --- the two roots that are *this project's* sources, not the tree's, and so carry no suffix. ---
# The EABI runtime and the firehose block are `src/` files; their contents do not change when the
# selected tree does, and the D13 build wrote them at the unsuffixed paths (experiment 921c).
XNU_RT_OBJ_OUT=${XNU_RT_OBJ_OUT:-$_OUT_BASE/xnu_rt_obj}
XNU_FIREHOSE_OBJ_OUT=${XNU_FIREHOSE_OBJ_OUT:-$_OUT_BASE/xnu_firehose_obj}

# Export the lot, for the reason at the top: the generators are children.
export XNU_KERNEL_OBJ_OUT XNU_ASM_OBJ_OUT XNU_ARM_OBJ_OUT XNU_MACHO_OBJ_OUT XNU_PLATFORM_OBJ_OUT
export MACH_HEADERS_OUT MIG_HEADERS MIG_KSERVER_OUT
export XNU_GENERATED XNU_OPTION_HEADERS_OUT XNU_DEVICE_HEADERS_OUT XNU_ASSYM_OUT
export XNU_DEVICE_TABLE MANIFEST
export XNU_RT_OBJ_OUT XNU_FIREHOSE_OBJ_OUT