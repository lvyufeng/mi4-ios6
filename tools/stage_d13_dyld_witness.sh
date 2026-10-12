#!/bin/bash
# Stage 995's dyld halt-message witness into the D13 tree (experiment 995).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single entry point for 995's ONE tree edit - `osfmk/arm/trap.c` - and calls the
# patcher that owns it; it edits nothing itself:
#   * `osfmk/arm/trap.c` - `patch_d13_dyld_witness.py` (udf-retry lane's): adds
#     `ios7wit_dyld_message` after the LEO block's `/* IOS7LEO_DYLD_END */` (a noinline, bounded,
#     fail-soft reader: `copyin(task->all_image_info_addr + 0x20, &msgp, 4)` then
#     `copyinstr(msgp, text, 256, &copied)`, at most 2 print slots claimed BEFORE any I/O, every
#     failure branch printing its reason) and a call to it in `sleh_undef` immediately after the
#     `dyld_all_image_info_addr` printf - the SAME path that prints A, inside the `udf #0xdefe` branch
#     (`trap.c:877` returns early for every other undefined instruction).
#
# WHY (994's press, 2026-10-12, arm armed-d13-8b4c9a3e, commit a4a2532, evidence
# out/stage90/captures/cancro-last_kmsg-994-armed-d13-8b4c9a3e.txt): the trap-10 wire fix HELD
# (`IOS7LAB vm_allocate64 size=00000000:00001000 flags=1 result=0 address=000e1000`, log:4101) and the
# frontier MOVED - dyld still halts, but now at its REAL error: 235x `usermode undefined instruction,
# EXC_BAD_INSTRUCTION at 0x2beb90c4` (slide 0xB8000, so pc 0x2be010c4 = `_dyld_fatal_error` =
# `udf #0xdefe`), r5 a heap string in dyld's malloc pool. `dyld::halt` formats a message and NOTHING
# on CANCRO can print it: the tree's own reader (the LEO witness, `trap.c:71-126`) is compiled out
# (`#if defined(BOARD_CONFIG_QSD8250_LEO)`). See docs/reference/d13-dyld-witness-995.md.
#
# ONE build layer is touched: `trap.c` is a POOL object (`osfmk_arm_trap.o`), so re-run
# `tools/build_xnu_arm_kernel.sh` with `XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_DYLD_WITNESS=1` (it
# also gates the same define on the same-named ENVIRONMENT variable; pass BOTH or the two spellings
# can diverge - `mi4-one-value-two-definitions`). 995 does NOT touch the assembly layer (`traps_lo.s`
# is 994's, unchanged) and `build_entry.sh` only LINKS the objects, so without the rebuild the linked
# image keeps the old code and the patch is invisible.
#
# Switch OFF compiles the tree's original text byte for byte: the gate is
# `#if defined(STAGE90_XNU_DYLD_WITNESS) && STAGE90_XNU_DYLD_WITNESS` (a fresh-block spelling, not
# 994's `|| (defined(...) && ...)` gate-opening one), so `defined()` is checked FIRST and an unset
# switch leaves the file's preprocessed text identical to HEAD - the `mi4-off-option-two-spellings`
# guard. The patcher PROVES that with the real preprocessor over the whole file (four `clang -E -P`
# runs) BEFORE it writes a single byte.
#
# Idempotent: the patcher decides both chunks before writing either, asserts each anchor window's
# occurrence count exactly, REFUSES (file untouched) on a partial/double/foreign edit, and a second
# run prints "nothing to do".
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

[[ -f $XNU/osfmk/arm/trap.c ]] || {
    echo "stage_d13_dyld_witness: no osfmk/arm/trap.c in $XNU (D13 tree not provisioned?)" >&2; exit 2; }

python3 "$HERE/patch_d13_dyld_witness.py" "$XNU"

echo "stage_d13_dyld_witness: done (XNU=$XNU)"
echo "stage_d13_dyld_witness: NOW RE-RUN tools/build_xnu_arm_kernel.sh (XNU_KERNEL_EXTRA_DEFINES=-DSTAGE90_XNU_DYLD_WITNESS=1 + the same env var, for the pool's osfmk_arm_trap.o - the linked image only LINKS that object; 995 does NOT touch the assembly layer)"