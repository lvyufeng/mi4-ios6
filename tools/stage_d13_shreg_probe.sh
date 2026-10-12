#!/bin/bash
# Stage 996's shared-region syscall probe into the D13 tree (experiment 996).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single entry point for 996's ONE tree edit - `bsd/vm/vm_unix.c` - and calls the patcher
# that owns it; it edits nothing itself:
#   * `bsd/vm/vm_unix.c` - `patch_d13_shreg_probe.py` (vmabi-recon lane's): adds bounded, fail-soft
#     `IOS7SHREG` printfs, each inside its own `#if defined(STAGE90_XNU_SHREG_PROBE) &&
#     STAGE90_XNU_SHREG_PROBE` region - the entry and every return of
#     `shared_region_check_np` (294) and `shared_region_map_and_slide_np` (438), plus the kr
#     immediately after `vm_shared_region_map_file()` inside `_shared_region_map` (its kr is not
#     visible at 438's exit sites - `_shared_region_map` folds it into the errno it returns). The
#     294/438 `sysent` rows, `syscalls.master` and every other lane's file are untouched.
#
# WHY (995's press, 2026-10-12, arm armed-d13-4efcabae, commit 947a75d, evidence
# out/stage90/captures/cancro-last_kmsg-995-armed-d13-4efcabae.txt:4111-4138): the witness READ dyld's
# real halt message by measurement - `Library not loaded: /usr/lib/libbsm.0.dylib`, launchd's FIRST
# LC_LOAD_DYLIB, no file on the volume but present in the shared cache (696 images). The minimal
# explanation is that dyld's shared cache was NEVER MAPPED: `mapSharedCache` failed silently and every
# cached dylib degraded to a file lookup, whose first miss halted dyld. dyld's two entry points are
# BSD syscalls 294/438, BOTH wired in this tree (`init_sysent.c:440` / `:649`) - so only the RUNTIME
# returns are unknown, and they are what 996 measures. It changes NO behaviour (printf only, the first
# 4 calls of each syscall). See docs/reference/d13-shreg-frontier.md.
#
# ONE build layer is touched: `vm_unix.c` is a POOL object (`bsd_vm_vm_unix.o`), so re-run
# `tools/build_xnu_arm_kernel.sh` with `XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_SHREG_PROBE=1` (it also
# gates the same define on the same-named ENVIRONMENT variable; pass BOTH or the two spellings can
# diverge - `mi4-one-value-two-definitions`). 996 does NOT touch the assembly layer and `build_entry.sh`
# only LINKS the objects, so without the rebuild the linked image keeps the old code and the probe is
# invisible.
#
# Switch OFF compiles the tree's original text byte for byte: the gate is
# `#if defined(STAGE90_XNU_SHREG_PROBE) && STAGE90_XNU_SHREG_PROBE` (`defined()` checked FIRST - the
# `mi4-off-option-two-spellings` guard), and the patcher PROVES the removability of its guard structure
# before it writes a single byte (`--verify-off`).
#
# Idempotent: the patcher decides all seven chunks before writing any, asserts each anchor window's
# occurrence count exactly, REFUSES (file untouched) on a partial/double/foreign edit, and a second run
# prints "nothing to do".
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

[[ -f $XNU/bsd/vm/vm_unix.c ]] || {
    echo "stage_d13_shreg_probe: no bsd/vm/vm_unix.c in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_shreg_probe.py" "$XNU"

echo "stage_d13_shreg_probe: done (XNU=$XNU)"
echo "stage_d13_shreg_probe: NOW RE-RUN tools/build_xnu_arm_kernel.sh (XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_SHREG_PROBE=1 + the same env var, for the pool's bsd_vm_vm_unix.o - the linked image only LINKS that object; 996 does NOT touch the assembly layer)"