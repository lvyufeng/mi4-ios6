#!/bin/bash
# Stage 958's memory-total port into the D13 tree (experiment 958).
#
# `external/` is a re-provisionable, gitignored checkout, so every edit this makes to it must be a
# script a re-checkout can re-apply, not a hand edit it would lose (`stage_d13_board.sh`'s rule). This
# script is the single source of truth for the ONE edit that makes D13 recognise the device's full
# RAM: `tools/patch_d13_memory_total.py` turns `max_mem = mem_size = sane_size = gMemSize;` into a
# guarded read of `/defaults hw.memsize`, so `hw.memsize` reports the device's 3 GB while the linear
# map (gMemSize/mem_size/sane_size) stays the payload's boot bank.
#
# Idempotent: a second run re-applies the (idempotent) patch and prints the same summary.
#
# The numbers this port binds - the device total 0xC0000000, the boot bank 0x5e500000, D13's 1 GiB
# linear-map ceiling - live in `src/stage90.h` (RAM_DEVICE_TOTAL / RAM_BOOT_BANK_SIZE) and the D13
# tree; `tools/check_d13_memory_total_staged.sh` (in `make check`) re-derives every one and refuses
# drift.  No separate record file: the check reads the sources, which is the only thing that cannot
# drift from them.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}

[[ -f $XNU/osfmk/arm/arm_vm_init.c ]] || {
    echo "stage_d13_memory_total: no arm_vm_init.c in $XNU (D13 tree not provisioned?)" >&2; exit 2; }
[[ -f $REPO_ROOT/src/stage90.h ]] || {
    echo "stage_d13_memory_total: no src/stage90.h at $REPO_ROOT" >&2; exit 2; }

python3 "$HERE/patch_d13_memory_total.py" "$XNU"

echo "stage_d13_memory_total: done (XNU=$XNU)"