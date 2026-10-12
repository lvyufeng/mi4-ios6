#!/bin/bash
# Stage 994's iOS 7 VM wire-width fix + the -3 SWI timebase into the D13 tree (experiment 994).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single entry point for 994's THREE tree edits and calls the two lanes' patchers that
# own them; it edits nothing itself:
#   * `osfmk/ipc/mach_kernelrpc.c` + `osfmk/arm/ios7_compat.h` - `patch_d13_ios7_vmwire.py` (vmwire's):
#     adds `ios7_traps_enabled()`, opens the file-wide `#if IOS7_COMPAT_PLATFORM` (:39) under the
#     switch, keeps the port/vm_map lab adapters behind an inner gate, adds the traps -12/-14 wire
#     structs + adapters, and routes the three `_kernelrpc_*` VM trap bodies (10/12/14) through their
#     real-width wire adapters (5/5/7 words). `osfmk/kern/syscall_sw.c` needs NO edit: traps 10/12/14
#     already carry arg counts 5/5/7 and on `__arm__` the mungers are ignored; traps 15/24/25/41/42
#     stay `kern_invalid` (their bodies are the LEO lab reimplementations, out of proportion for a
#     wire-width fix).
#   * `osfmk/arm/traps_lo.s` - `patch_d13_ios7_traps.py` (swi's): the :116 gate closes the
#     `cmn r12, #3` / `swi_trap_tb: movs pc, lr` fast path and :162/:264 open
#     `ios7lab_timebase: blx _mach_absolute_time; b swi_exit64`, so a r12 == -3 SWI returns the real
#     64-bit ns clock instead of r0 = 0 / r1 = stale.
#
# WHY (993's press, 2026-10-12, arm armed-d13-1d5cf20e, commit 160650b, evidence
# /tmp/cancro-last_kmsg.txt): the alias + high-vector L2 zeroing HELD (0 commpage faults, no panic),
# launchd's dyld ran from the shared cache and then HALTED at `_dyld_fatal_error` (`udf #0xdefe`, pc
# 0x2be7e0c4 - slide 0x7d000) reached from `dyld::throwf`'s out-of-memory path. Root cause: this
# 32-bit build's `mach_vm_size_t` is `uint32_t`, so the native trap-10 args struct is ONE WORD
# SHORTER than the iOS 7 wire dyld builds; `args->flags` reads size_hi (0) and the allocation runs
# without VM_FLAGS_ANYWHERE. See docs/reference/d13-ios7-vm-wire-defect.md.
#
# TWO build layers are touched: the two C files are POOL objects (`osfmk_ipc_mach_kernelrpc.o`), so
# re-run `tools/build_xnu_arm_kernel.sh` with `XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_IOS7_TRAPS=1`
# (it also gates the same define on the same-named ENVIRONMENT variable; pass BOTH or the two
# spellings can diverge - `mi4-one-value-two-definitions`), and `traps_lo.s` is the ASSEMBLY layer,
# so re-run `tools/assemble_arm_layer.sh` with `STAGE90_XNU_IOS7_TRAPS=1` exported
# (`ASFLAGS+=(-DSTAGE90_XNU_IOS7_TRAPS=$IOS7_TRAPS)`). `build_entry.sh` only LINKS the objects, so
# without the rebuilds the linked image keeps the old code and the patch is invisible.
#
# Switch OFF compiles the tree's original text byte for byte: every appended clause is
# ` || (defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS)`, so `defined()` is checked FIRST
# and an unset switch leaves the `#if` exactly as before - the `mi4-off-option-two-spellings` guard.
#
# Idempotent: both patchers decide every gate before writing any, assert each anchor's occurrence
# count exactly, REFUSE (file untouched) on a partial/double edit, and a second run prints
# "already staged" and writes nothing.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

for f in osfmk/ipc/mach_kernelrpc.c osfmk/arm/ios7_compat.h osfmk/arm/traps_lo.s; do
    [[ -f $XNU/$f ]] || {
        echo "stage_d13_ios7_traps: no $f in $XNU (D13 tree not provisioned?)" >&2; exit 2; }
done

python3 "$HERE/patch_d13_ios7_vmwire.py" "$XNU"
python3 "$HERE/patch_d13_ios7_traps.py" "$XNU"

echo "stage_d13_ios7_traps: done (XNU=$XNU)"
echo "stage_d13_ios7_traps: NOW RE-RUN tools/build_xnu_arm_kernel.sh (XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_IOS7_TRAPS=1 + the same env var, for the pool's osfmk_ipc_mach_kernelrpc.o) AND tools/assemble_arm_layer.sh (STAGE90_XNU_IOS7_TRAPS=1 exported -> ASFLAGS, for traps_lo.o)"