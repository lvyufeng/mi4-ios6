#!/bin/bash
# Stage 997's 32-byte shared-region wire into the D13 tree (experiment 997 = G3).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single entry point for 997's ONE tree edit - `bsd/vm/vm_unix.c` - and calls the patcher
# that owns it; it edits nothing itself:
#   * `bsd/vm/vm_unix.c` - `patch_d13_shreg_wire.py` (vmabi-recon lane's): exactly three
#     `#if IOS7_COMPAT_PLATFORM` lines gain the 994 spelling
#     `|| (defined(STAGE90_XNU_SHREG_WIRE) && STAGE90_XNU_SHREG_WIRE)` and the file-local
#     `ios7lab_cache_abi()` body is gated to `return 1;` (the original
#     `return ios7_compat_vmabi_enabled();` as the `#else` half). The MACRO itself is NOT touched: 13
#     files reference `IOS7_COMPAT_PLATFORM` and widening it would wake exec/fork/ipc wholesale (the
#     994 "do not widen the macro" lesson). That opens exactly the file's whole
#     `IOS7_COMPAT_PLATFORM` footprint: the helper, 294's 8-byte copyout, and the 32 B branch at
#     :1190-1220 (`shared_region_copyin_mappings` + its per-mapping `IOS7LAB cache mapping ...`
#     printf). The 294/438 `sysent` rows, `syscalls.master` and every other lane's file are untouched.
#
# WHY (996's press, 2026-10-12, arm armed-d13-cea0c1c2, commit 821fb40, log
# out/stage90/captures/cancro-last_kmsg-996-armed-d13-cea0c1c2.txt:4101-4105): the probe MEASURED the
# misparse by value - `IOS7SHREG check_np ret=12`, `map_slide count=4 slide=0x3811000
# slide_start=0x3f9bd000 size=0x89000`, then `IOS7SHREG map_file kr=4` + `map_slide ret=22`. dyld 239
# builds the mapping array with a 32-byte stride (0x2be057ba `add.w r0, r4, r3, lsl #5`), while this
# kernel's `struct shared_file_mapping_np` is 20 bytes (`uint32_t` address/offset/size +
# `vm_prot_t`), so `shared_region_copyin_mappings` computed `count * 5 << 2`, m[0].size read 0, and
# `vm_map_enter_mem_object`'s `initial_size == 0` check (vm_map.c:2344) returned EINVAL before any
# slide work - the shared cache was never mapped and dyld halted on `Library not loaded:
# /usr/lib/libbsm.0.dylib`. See docs/reference/d13-shreg-frontier.md.
#
# ONE build layer is touched: `vm_unix.c` is a POOL object (`bsd_vm_vm_unix.o`), so re-run
# `tools/build_xnu_arm_kernel.sh` with `XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_SHREG_WIRE=1` (it also
# gates the same define on the same-named ENVIRONMENT variable; pass BOTH or the two spellings can
# diverge - `mi4-one-value-two-definitions`). 997 does NOT touch the assembly layer and `build_entry.sh`
# only LINKS the objects, so without the rebuild the linked image keeps the 20 B wire and the fix is
# invisible.
#
# Switch OFF compiles the tree's original text: the gate spelling is
# `#if IOS7_COMPAT_PLATFORM || (defined(STAGE90_XNU_SHREG_WIRE) && STAGE90_XNU_SHREG_WIRE)`
# (`defined()` checked FIRST - the `mi4-off-option-two-spellings` guard) and the helper half is
# `#if defined(STAGE90_XNU_SHREG_WIRE) && STAGE90_XNU_SHREG_WIRE` / `#else` original, so with the macro
# undefined the preprocessor condition is the original one and the body is the original text. The
# patcher PROVES the removability with `--verify-off` (strip(staged) == pre-image AND re-plan(pre) ==
# staged).
#
# Idempotent: the patcher decides all edits before writing, asserts each anchor's occurrence count
# exactly, REFUSES (file untouched) on a partial/double/foreign edit, and a second run prints
# "nothing to do".
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

[[ -f $XNU/bsd/vm/vm_unix.c ]] || {
    echo "stage_d13_shreg_wire: no bsd/vm/vm_unix.c in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_shreg_wire.py" "$XNU"

echo "stage_d13_shreg_wire: done (XNU=$XNU)"
echo "stage_d13_shreg_wire: NOW RE-RUN tools/build_xnu_arm_kernel.sh (XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_SHREG_WIRE=1 + the same env var, for the pool's bsd_vm_vm_unix.o - the linked image only LINKS that object; 997 does NOT touch the assembly layer)"