#!/bin/bash
# Re-derive the staged D13 tree edits: `BOARD_CONFIG_MSM8974_CANCRO` is accepted at the board gates
# (923), and `asm.h` guards the `SLIDABLE` default so `-DSLIDABLE=0` wins (924).  Source-half, no
# compiler, no device - it reads the tree files.
#
# The property: the build defines `-DBOARD_CONFIG_MSM8974_CANCRO` and the tree must both (a) map it
# to a processor class (else `__ARM_PROCESSOR_CLASS_*` is undefined and locore.s takes the wrong
# branch) and (b) accept it at the nokextd gate (else `IOService.cpp` and three siblings `#error`,
# and the link has no `IOService`).  Either half alone is a silent wrong answer: a class with no gate
# is dead, a gate with no class is an unclassified board.
#
# It refuses DRIFT: if `tools/stage_d13_board.sh` is ever the source of truth for an edit that is no
# longer in the tree, or the tree gains the macro by hand, this fails.  A re-provisioned `external/`
# must be re-staged (`tools/stage_d13_board.sh`) for the build to work, and this is what says so.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}
NOKEXTD=$(cd "$XNU/.." && pwd)/nokextd

pc=$XNU/osfmk/arm/PlatformConfigs.h
nk=$NOKEXTD/IOS7NoKextd035.h

# If the D13 tree is not present at all, this check has nothing to say - the 4570 line does not carry
# these files and `make check` runs on both.  Absent tree => skip, explicitly, not silently.
if [[ ! -f $pc ]]; then
    echo "check_d13_board_staged: no $pc - D13 tree not provisioned, skipping"
    exit 0
fi

fail=0
note() { printf '  %s\n' "$*"; }

# (a) the board -> processor-class mapping.
if grep -q 'BOARD_CONFIG_MSM8974_CANCRO' "$pc" &&
   awk '/BOARD_CONFIG_MSM8974_CANCRO/{f=1} f&&/__ARM_PROCESSOR_CLASS_/{print; exit}' "$pc" | grep -q .; then
    echo "PlatformConfigs.h: BOARD_CONFIG_MSM8974_CANCRO maps to __ARM_PROCESSOR_CLASS_*"
else
    echo "check_d13_board_staged: FAIL - PlatformConfigs.h does not map MSM8974_CANCRO to a class" >&2
    note "re-run tools/stage_d13_board.sh (external/ is re-provisionable)"
    fail=1
fi

# (b) the nokextd gate accepts it.
if grep -q 'BOARD_CONFIG_MSM8974_CANCRO' "$nk" &&
   grep -q 'board compilation target' "$nk"; then
    echo "IOS7NoKextd035.h: the board gate accepts MSM8974_CANCRO"
else
    echo "check_d13_board_staged: FAIL - the nokextd gate does not accept MSM8974_CANCRO" >&2
    note "re-run tools/stage_d13_board.sh (external/ is re-provisionable)"
    fail=1
fi

# (c) the build defines the macro the two edits above are about.  A rename on either side is the
#     defect this catches: the tree would accept a board the compiler never names.
if grep -q -- '-DBOARD_CONFIG_MSM8974_CANCRO' "$REPO_ROOT/tools/build_xnu_arm_kernel.sh"; then
    echo "build_xnu_arm_kernel.sh: defines -DBOARD_CONFIG_MSM8974_CANCRO"
else
    echo "check_d13_board_staged: FAIL - the build does not define BOARD_CONFIG_MSM8974_CANCRO" >&2
    fail=1
fi

# (d) asm.h guards the SLIDABLE default (experiment 924).  Without it the tree's `#define SLIDABLE 1`
#     forces the Mach-O non-lazy-pointer `LOAD_ADDR_GEN_DEF` and every ARM `.s` that loads a global
#     fails in clang's ELF assembler.  The two halves are checked together: the guard must be present
#     *and* the assembler must still pass `-DSLIDABLE=0`, or the guard protects nothing.
asm=$XNU/osfmk/mach/arm/asm.h
if grep -q '#ifndef SLIDABLE' "$asm" &&
   awk '/#ifdef _ARM_ARCH_7/{f=1} f&&/#ifndef SLIDABLE/{print; exit}' "$asm" | grep -q . ; then
    echo "asm.h: the SLIDABLE default is guarded so -DSLIDABLE=0 can win"
else
    echo "check_d13_board_staged: FAIL - asm.h forces SLIDABLE under _ARM_ARCH_7" >&2
    note "re-run tools/stage_d13_board.sh (external/ is re-provisionable)"
    fail=1
fi
if grep -q -- '-DSLIDABLE=0' "$REPO_ROOT/tools/assemble_arm_layer.sh"; then
    echo "assemble_arm_layer.sh: passes -DSLIDABLE=0"
else
    echo "check_d13_board_staged: FAIL - the assembler does not pass -DSLIDABLE=0" >&2
    fail=1
fi

# (e) kxld_object.h carries the comma the upstream attribute clause is missing (experiment 925).
#     Without it all four kxld files fail at `expected ')'`.
kxld=$XNU/libkern/kxld/kxld_object.h
if grep -q 'nonnull(1,2,4), visibility' "$kxld"; then
    echo "kxld_object.h: the nonnull/visibility attribute clause is comma-corrected"
else
    echo "check_d13_board_staged: FAIL - kxld_object.h is missing the attribute comma" >&2
    note "re-run tools/stage_d13_board.sh (external/ is re-provisionable)"
    fail=1
fi

[[ $fail -eq 0 ]] || exit 1
echo "check_d13_board_staged: ok"