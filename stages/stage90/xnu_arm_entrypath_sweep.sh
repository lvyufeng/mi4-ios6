#!/usr/bin/env bash
#
# Measure how far XNU's ARM entry path is from compiling, with the best flag set known.
#
#   ./xnu_arm_entrypath_sweep.sh            # measure arm_init.c and a few neighbours
#   ./xnu_arm_entrypath_sweep.sh --detail arm_init.c
#
# Why this exists separately from xnu_arm_sweep.sh. That script sweeps all 32 files in osfmk/arm
# and reports 3 of 32 - a number dominated by files that are nowhere near the entry path. The
# entry path is a handful of files, and measuring them specifically is what turned "the build
# configuration is absent" into "four errors, and here they are".
#
# The flag set below is the accumulated result of every earlier attempt. Each line records why it
# is there, because several of them look like arbitrary build flags and are not.
#
# What is NOT here, and cannot be: osfmk/conf/MASTER.XXX. XNU's scheduler-algorithm selection lives
# in that file - the build system generates it, and osfmk/kern/sched_prim.h:574 is an `#error`
# naming it when no algorithm is selected. The tarball has osfmk/conf/files.* and Makefile.* but no
# MASTER.*, so the value has to be chosen rather than read. CONFIG_SCHED_MULTIQ is the plausible
# one for a Darwin-14 iOS ARM kernel and is what the measurement uses.

set -uo pipefail

cd "$(dirname "$0")"
STAGE_DIR=$PWD
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)

XNU=$REPO_ROOT/external/xnu-4570.1.46
[[ -d $XNU ]] || { echo "xnu-4570.1.46 not present at $XNU" >&2; exit 2; }
command -v clang >/dev/null 2>&1 || { echo "clang is required (see xnu_arm_assemble.sh)" >&2; exit 2; }

# XNU's own paths first, shims after. This order is load-bearing and was the single fix that took
# arm_init.c from a wall of "unknown type name 'natural_t'" to ten real errors: with shims first,
# shims/mach/machine/vm_types.h shadows osfmk/mach/arm/vm_types.h, which is where natural_t is
# defined. A shim is a fallback, not an override.
INCLUDES=(
  -I$XNU/osfmk
  -I$XNU/iokit
  -I$XNU/bsd
  -I$XNU/libkern
  -I$XNU/EXTERNAL_HEADERS
  -I$XNU/pexpert
  -I$XNU/osfmk/arm
  -I$XNU/bsd/arm
  -I$STAGE_DIR/shims
  -I$STAGE_DIR/shims/kern
  -I$STAGE_DIR/shims/mach
  -I$STAGE_DIR/shims_arm
  -I$STAGE_DIR/shims_arm/kern
  -I$STAGE_DIR/shims_arm/mach
  -I$STAGE_DIR/shims_arm/sys
  -I$STAGE_DIR/shims_arm/sys/_pthread
)

# The force-included set. osfmk/arm/simple_lock.h defines decl_simple_lock_data and is included by
# NOTHING in the tree, so the build must inject it; osfmk/kern/queue.h and kern/ast.h carry
# mpqueue_head_t and ast_t, which cpu_data_internal.h uses without including them. These are
# exactly the "force-included header set is not public" half of the build-configuration finding.
FORCE_INCLUDES=(
  -include arm/simple_lock.h
  -include kern/queue.h
  -include kern/ast.h
  # Also tried and NOT effective: -include kern/call_entry.h -include kern/timer_call.h. The two
  # ordering errors below survive force-including the headers that define the types, so the cause
  # is the include graph rather than the set of headers present. Recorded so it is not re-tried.
)

DEFINES=(
  -DARMA7=1
  -DKERNEL=1
  -DKERNEL_PRIVATE=1
  -DMACH_KERNEL_PRIVATE=1
  -D__arm__=1
  -DCONFIG_EMBEDDED=1
  -D__ARM_L2CACHE_SIZE_LOG__=21
  -DCONFIG_SCHED_MULTIQ=1
)

TARGET=(clang --target=armv7-none-eabi -mcpu=cortex-a15 -marm -fsyntax-only)

measure_one() {
  local rel=$1
  local out
  out=$("${TARGET[@]}" "${FORCE_INCLUDES[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" "$XNU/$rel" 2>&1)
  local n
  n=$(printf '%s\n' "$out" | grep -cE "error:")
  printf '  %-28s %s\n' "$(basename "$rel")" "$n error(s)"
  printf '%s\n' "$out" | grep -E "error:" | sed 's|.*/external/xnu-4570.1.46/||' | sed 's/^/      /' | head -6
}

if [[ ${1:-} == --detail ]]; then
  "${TARGET[@]}" "${FORCE_INCLUDES[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" "$XNU/osfmk/arm/${2:-arm_init.c}" 2>&1 | head -40
  exit 0
fi

echo "== XNU's ARM entry path, with the best flag set known =="
echo
measure_one osfmk/arm/arm_init.c
measure_one osfmk/arm/arm_vm_init.c
measure_one osfmk/arm/machine_routines.c
echo
echo "Compare: ./xnu_arm_sweep.sh reports 3 of 32 for osfmk/arm as a whole."
echo "MASTER.XXX, which the scheduler #error names, is not in the tarball - see the header comment."
