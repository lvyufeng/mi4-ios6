#!/bin/bash
# Re-derive the staged 970h pre-switch TLB-flush removal (experiment 970h).  Source-half, no compiler,
# no device.
#
# 970h removes D13 `locore.s`'s whole-TLB invalidate at `:85`, which runs BEFORE the TTBR0 write and
# destroys the surviving identity TLB entry for VA 0x80a00000 (the boot table) that the payload's MMU-ON
# handoff leaves live.  4570's `_start` has no such pre-switch invalidate.  The property this check
# re-derives is BY POSITION, not by mere token presence: in the `mmu_reinitialize` boot path there must
# be NO `c8,c7,0` before the TTBR0 write (`c2,c0,0`), while the safe post-build one in `mmu_initialized`
# must still be present.
#
# It refuses DRIFT: if `tools/stage_d13_boot_path_970h.sh` is ever the source of truth for an edit no
# longer in the tree, or a hand edit removed it, this fails.  A re-provisioned `external/` must be
# re-staged for the 970h arm to be what its record says, and this is what says so.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}
F=$XNU/osfmk/arm/locore.s

# If the D13 tree is not present at all, this check has nothing to say - `make check` also runs where the
# fork is not provisioned.  Absent tree => skip, explicitly, not silently.
if [[ ! -f $F ]]; then
    echo "check_d13_boot_path_970h_staged: no $F - D13 tree not provisioned, skipping"
    exit 0
fi

fail=0

# (a) the edit token is present (the removal was staged).
if ! grep -q '970h edit 3: the TLB invalidate that stood here is REMOVED' "$F"; then
    echo "check_d13_boot_path_970h_staged: FAIL - locore.s carries no 970h edit token (not staged)" >&2
    fail=1
fi

# (b) BY POSITION: no `c8,c7,0` may precede the TTBR0 write (`c2,c0,0`) inside the boot path.  The boot
# path is the span from `mmu_reinitialize:` to `mmu_initialized:`.  We take line numbers, not tokens.
start_ln=$(grep -n '^mmu_reinitialize:' "$F" | head -1 | cut -d: -f1)
end_ln=$(grep -n '^mmu_initialized:' "$F" | head -1 | cut -d: -f1)
if [[ -z $start_ln || -z $end_ln || $start_ln -ge $end_ln ]]; then
    echo "check_d13_boot_path_970h_staged: FAIL - could not bound the boot path (mmu_reinitialize=$start_ln mmu_initialized=$end_ln)" >&2
    fail=1
else
    # first pre-switch TLB invalidate inside the boot path, if any
    inv_ln=$(awk -v s="$start_ln" -v e="$end_ln" 'NR>=s && NR<=e && /mcr +p15, *0, *r4, *c8, *c7, *0/ {print NR; exit}' "$F")
    ttbr_ln=$(awk -v s="$start_ln" -v e="$end_ln" 'NR>=s && NR<=e && /mcr +p15, *0, *r[0-9]+, *c2, *c0, *0$/ {print NR; exit}' "$F")
    if [[ -n $inv_ln ]]; then
        echo "check_d13_boot_path_970h_staged: FAIL - a whole-TLB invalidate is still in the boot path at line $inv_ln (the 970h edit is not effective)" >&2
        fail=1
    fi
    if [[ -z $ttbr_ln ]]; then
        echo "check_d13_boot_path_970h_staged: FAIL - no TTBR0 write found between mmu_reinitialize and mmu_initialized" >&2
        fail=1
    fi
fi

# (c) the SAFE post-build invalidate (in `mmu_initialized`) must still be present - the edit removes one
# instruction, not the memory-maintenance the kernel needs.  Exactly ONE `c8,c7,0` with r4 may remain,
# and it must sit after `mmu_initialized:`.
r4_count=$(grep -c 'mcr \+p15, 0, r4, c8, c7, 0' "$F" || true)
post_ln=$(awk -v e="$end_ln" 'NR>e && /mcr +p15, *0, *r4, *c8, *c7, *0/ {print NR; exit}' "$F" 2>/dev/null || true)
if [[ -z ${end_ln:-} || -z $post_ln ]]; then
    echo "check_d13_boot_path_970h_staged: FAIL - the post-build whole-TLB invalidate in mmu_initialized is GONE (the edit removed the wrong one)" >&2
    fail=1
fi
if [[ "$r4_count" != "1" ]]; then
    echo "check_d13_boot_path_970h_staged: FAIL - expected exactly ONE r4 whole-TLB invalidate (post-build), found $r4_count" >&2
    fail=1
fi

if (( fail )); then
    echo "check_d13_boot_path_970h_staged: run tools/stage_d13_boot_path_970h.sh to re-apply the edit" >&2
    exit 1
fi
echo "check_d13_boot_path_970h_staged: ok - the D13 boot path has no pre-switch TLB invalidate; the post-build one remains"