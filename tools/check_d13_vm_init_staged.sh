#!/bin/bash
# Re-derive the staged D13 arm_vm_init boot-table copy (experiment 971).  Source-half, no compiler,
# no device.
#
# The 971 arm needs D13's `arm_vm_init` to CARRY the boot table (which 970g fills with the entry's
# console descriptors) into the system table the MMU walks after `set_mmu_ttb(cpu_ttb)`.  4570 does
# `bcopy(boot_tte, cpu_tte, ARM_PGBYTES*4)` at the address D13 bzeroes; D13 bzeroed a FRESH table, so
# the console VA 0xde500000 - outside D13's managed map [MANAGED_BASE 0xC0000000, +gMemSize) - was lost
# the instant the table switched and every key after `arm_vm_init` was dropped.  The edit is applied by
# `tools/stage_d13_vm_init.sh`.
#
# The property this check re-derives: the `bcopy` is PRESENT and ADJACENT to the `cpu_ttb` assignment it
# replaces - because the copy only carries the console if it lands exactly where the fresh table was
# built (the address `identityCachePA = cpu_ttb + L1_SIZE` is derived from it two lines later).  A copy
# that is present but moved away would leave the marker present and the property false - the class a
# presence-only check cannot see ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
#
# It refuses DRIFT: if `tools/stage_d13_vm_init.sh` is ever the source of truth for an edit no longer in
# the tree, or a hand edit removed it, this fails.  A re-provisioned `external/` must be re-staged
# (`tools/stage_d13_vm_init.sh`) for the 971 arm to be what its record says, and this is what says so.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}
F=$XNU/osfmk/arm/arm_vm_init.c

# If the D13 tree is not present at all, this check has nothing to say - `make check` also runs where the
# fork is not provisioned.  Absent tree => skip, explicitly, not silently.
if [[ ! -f $F ]]; then
    echo "check_d13_vm_init_staged: no $F - D13 tree not provisioned, skipping"
    exit 0
fi

fail=0

# (a) the fresh-table bzero is GONE and the boot-table copy is PRESENT.
if grep -q 'bzero((void\*)phys_to_virt(cpu_ttb), L1_SIZE);' "$F"; then
    echo "check_d13_vm_init_staged: FAIL - arm_vm_init.c still bzeroes a fresh cpu_ttb (the 971 edit is not staged)" >&2
    fail=1
fi
if ! grep -q 'bcopy((void\*)phys_to_virt(gTopOfKernel), (void\*)phys_to_virt(cpu_ttb), L1_SIZE);' "$F"; then
    echo "check_d13_vm_init_staged: FAIL - arm_vm_init.c carries no boot-table bcopy" >&2
    fail=1
fi

# (b) ADJACENCY, by line: the copy must sit immediately after the `cpu_ttb = gTopOfKernel + L1_SIZE;`
# assignment (the table address it fills) and before the `identityCachePA = cpu_ttb + L1_SIZE;` line
# that derives from it.  A hand edit that moved it away leaves the marker present and the property false.
assign_ln=$(grep -n 'cpu_ttb = gTopOfKernel + L1_SIZE;' "$F" | head -1 | cut -d: -f1)
copy_ln=$(grep -n 'bcopy((void\*)phys_to_virt(gTopOfKernel), (void\*)phys_to_virt(cpu_ttb), L1_SIZE);' "$F" | head -1 | cut -d: -f1)
cache_ln=$(grep -n 'identityCachePA = cpu_ttb + L1_SIZE;' "$F" | head -1 | cut -d: -f1)

if [[ -z $assign_ln || -z $copy_ln || -z $cache_ln ]]; then
    echo "check_d13_vm_init_staged: FAIL - one of the assign/copy/identityCachePA anchors is missing" >&2
    fail=1
else
    # assign < copy < identityCachePA, and the copy within 32 lines of the assignment (the 971 insertion
    # is a 16-line comment block plus the statement).
    if (( assign_ln < copy_ln && copy_ln < cache_ln && copy_ln - assign_ln <= 32 )); then
        echo "check_d13_vm_init_staged: $F - the boot-table copy (line $copy_ln) is between the cpu_ttb assignment ($assign_ln) and identityCachePA ($cache_ln)"
    else
        echo "check_d13_vm_init_staged: FAIL - the boot-table copy is NOT adjacent to the cpu_ttb assignment" >&2
        echo "        assign=$assign_ln copy=$copy_ln identityCachePA=$cache_ln (want assign < copy < identityCachePA and copy-assign <= 32)" >&2
        fail=1
    fi
fi

if (( fail )); then
    echo "check_d13_vm_init_staged: run tools/stage_d13_vm_init.sh to re-apply the edit" >&2
    exit 1
fi
echo "check_d13_vm_init_staged: ok - the D13 arm_vm_init carries the boot-table copy the 971 arm needs"