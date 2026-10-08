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
#
# Both edits carry the `MSM8974_CANCRO` sentinel and are idempotent: a second run is a no-op.
#
# `tools/check_d13_board_staged.sh` (in `make check`) re-derives these two facts and refuses drift.
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

echo "stage_d13_board: done (XNU=$XNU)"