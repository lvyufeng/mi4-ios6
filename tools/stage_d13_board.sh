#!/bin/bash
# Stage the Darwin-13 board macro (experiment 923).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script that a re-checkout can re-apply, not a hand edit it would lose.  This is the smallest such
# script: two marked in-place edits that give the fork a second board, `MSM8974_CANCRO` (Xiaomi Mi 4),
# without touching the fork's own `QSD8250_LEO`.
#
# WHY A NEW BOARD, not `-DBOARD_CONFIG_QSD8250_LEO`.  The fork's `nokextd/IOS7NoKextd035.h` requires
# *a* board (`#error` otherwise), and `QSD8250_LEO` satisfies it - but LEO is also the switch for the
# fork's HD2 lab instrumentation: `osfmk/arm/trap.c:56` (`#include "ios7lab_fault_witness.h"`),
# `osfmk/kern/thread_act.c:82` and `pexpert/arm/pe_qsd8250_leo.c` all open HD2-only headers under LEO,
# and `trap.c` is load-bearing.  Compiling as LEO would put the wrong board's tracing in the fault
# path.  A new name keeps MSM8974 free of it while satisfying the gate.
#
# WHAT IT DOES
#   1. `osfmk/arm/PlatformConfigs.h` - add a `BOARD_CONFIG_MSM8974_CANCRO` block mapping it to the
#      Qualcomm-Krait processor class (the MSM8960_TOUCHPAD precedent, one generation on).
#   2. `../nokextd/IOS7NoKextd035.h` - widen its guard from "must be LEO" to "must be a board"
#      (LEO or MSM8974_CANCRO), so the gate's `NO_KEXTD=1` reaches the new board.
#   3. `osfmk/mach/arm/asm.h` - guard the `#define SLIDABLE 1` (experiment 924), so the ELF build's
#      `-DSLIDABLE=0` selects the non-slidable `LOAD_ADDR_GEN_DEF` the EABI assembler can take.
#   4. `libkern/kxld/kxld_object.h` - add the comma the upstream attribute clause is missing
#      (experiment 925), so the four kxld files compile.
#
# All three edits carry the `MSM8974_CANCRO` sentinel and are idempotent: a second run is a no-op.
#
# `tools/check_d13_board_staged.sh` (in `make check`) re-derives these facts and refuses drift.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}
NOKEXTD=$(cd "$XNU/.." && pwd)/nokextd

[[ -f $XNU/osfmk/arm/PlatformConfigs.h ]] || { echo "stage_d13_board: no PlatformConfigs.h in $XNU" >&2; exit 2; }
[[ -f $NOKEXTD/IOS7NoKextd035.h ]]        || { echo "stage_d13_board: no IOS7NoKextd035.h in $NOKEXTD" >&2; exit 2; }

MARK="MSM8974_CANCRO"

# --- 1. PlatformConfigs.h: the board -> processor-class mapping ------------------------------------
if grep -q "$MARK" "$XNU/osfmk/arm/PlatformConfigs.h"; then
    echo "PlatformConfigs.h: $MARK already present (no-op)"
else
    python3 - "$XNU/osfmk/arm/PlatformConfigs.h" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
anchor = "#ifdef BOARD_CONFIG_MSM8960_TOUCHPAD"
i = s.index(anchor)
# insert just after that block's own `#endif`, so the new board reads next to its precedent
j = s.index("#endif", i) + len("#endif")
block = """

/* MSM8974 (Xiaomi Mi 4, `cancro`) is Qualcomm Krait 400: the same architectural path the
 * MSM8960_TOUCHPAD precedent uses for Krait, one generation on. Follow that mapping. */
#ifdef BOARD_CONFIG_MSM8974_CANCRO
#define __ARM_PROCESSOR_CLASS_CORTEX_A9__		1
#define __ARM_PROCESSOR_CLASS_QUALCOMM_A9__		1
#endif"""
open(p, "w").write(s[:j] + block + s[j:])
print("PlatformConfigs.h: added BOARD_CONFIG_MSM8974_CANCRO")
PY
fi

# --- 2. nokextd/IOS7NoKextd035.h: widen the board guard --------------------------------------------
if grep -q "$MARK" "$NOKEXTD/IOS7NoKextd035.h"; then
    echo "IOS7NoKextd035.h: $MARK already present (no-op)"
else
    python3 - "$NOKEXTD/IOS7NoKextd035.h" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = "#if !defined(BOARD_CONFIG_QSD8250_LEO)\n#error IOS7NoKextd035 requires the LEO compilation target\n#endif"
new = ("#if !defined(BOARD_CONFIG_QSD8250_LEO) && !defined(BOARD_CONFIG_MSM8974_CANCRO)\n"
       "#error IOS7NoKextd035 requires a board compilation target (LEO or MSM8974_CANCRO)\n#endif")
assert old in s, "the nokextd guard is not the expected text - refusing to widen it blind"
open(p, "w").write(s.replace(old, new))
print("IOS7NoKextd035.h: widened the board guard to LEO or MSM8974_CANCRO")
PY
fi

# --- 3. mach/arm/asm.h: let `-DSLIDABLE=0` win (experiment 924) ------------------------------------
# The ARM `.s` files are assembled here with clang's **ELF** assembler, which has no
# `.section __DATA,__nl_symbol_ptr` / `.indirect_symbol` — the SLIDABLE `LOAD_ADDR_GEN_DEF` form. The
# non-slidable branch is selected by `-DSLIDABLE=0` (in `tools/assemble_arm_layer.sh`), but the tree's
# `#ifdef _ARM_ARCH_7 / #define SLIDABLE 1` overrode that flag (clang sets `__ARM_ARCH_7A__`, which
# `arm/arch.h:15` turns into `_ARM_ARCH_7`). The `#ifndef` lets a Darwin build keep the default (1)
# and an ELF build choose 0. This is the same class of fix as the two above: a tree that assumes
# Apple's toolchain, given the one guard the ELF toolchain needs.
if grep -q "$MARK" "$XNU/osfmk/mach/arm/asm.h"; then
    echo "asm.h: $MARK already present (no-op)"
else
    python3 - "$XNU/osfmk/mach/arm/asm.h" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = "#ifdef _ARM_ARCH_7\n#define SLIDABLE 1\n#endif"
new = ("/* MSM8974_CANCRO (924): a command-line `-DSLIDABLE=0` must win. The Darwin assembler wants the\n"
       " * SLIDABLE non-lazy-pointer form, so the default stays 1, but this project assembles the ARM layer\n"
       " * with clang's ELF assembler, which has no `.section __DATA,__nl_symbol_ptr` / `.indirect_symbol`,\n"
       " * and takes the position-dependent branch below by passing `-DSLIDABLE=0`. Previously the\n"
       " * unconditional `#define` here overrode that flag; the `#ifndef` lets a Darwin build be unchanged\n"
       " * (default 1) and an ELF build choose 0. */\n"
       "#ifdef _ARM_ARCH_7\n#ifndef SLIDABLE\n#define SLIDABLE 1\n#endif\n#endif")
assert old in s, "the asm.h SLIDABLE guard is not the expected text - refusing to guard it blind"
assert s.count(old) == 1, "the asm.h SLIDABLE guard appears more than once - refusing to guess"
open(p, "w").write(s.replace(old, new))
print("asm.h: guarded the SLIDABLE default so -DSLIDABLE=0 wins")
PY
fi

# --- 4. libkern/kxld/kxld_object.h: the missing comma (experiment 925) ---------------------------------
# `__attribute__((nonnull(1,2,4) visibility("hidden")))` is missing the comma between its two clauses,
# so clang stops at `expected ')'` and the four kxld files cannot compile. It is an **upstream**
# transcription defect: `xnu-2050.18.24`, `apple-xnu-rel-2050` and `xnu-upstream` all carry it
# un-comma'd at their own `:59`, while Darwin-17 (4570) fixed it to `nonnull(1,2,4), visibility(...)`.
# So the fix is the one Apple made, applied one release early: add the comma.
if grep -q "$MARK" "$XNU/libkern/kxld/kxld_object.h"; then
    echo "kxld_object.h: $MARK already present (no-op)"
else
    python3 - "$XNU/libkern/kxld/kxld_object.h" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = "__attribute__((nonnull(1,2,4) visibility(\"hidden\")))"
new = "/* MSM8974_CANCRO (925): the comma between the two attribute clauses was missing upstream (it is\n" \
      " * un-comma'd in xnu-2050 and xnu-upstream too); Darwin-17 fixed it. Add it, so kxld compiles. */\n" \
      "    __attribute__((nonnull(1,2,4), visibility(\"hidden\")))"
assert old in s, "the kxld_object.h attribute clause is not the expected text - refusing to patch blind"
assert s.count(old) == 1, "the attribute clause appears more than once - refusing to guess"
open(p, "w").write(s.replace(old, new))
print("kxld_object.h: added the missing comma in the nonnull/visibility attribute")
PY
fi

echo "stage_d13_board: done (XNU=$XNU)"