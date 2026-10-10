#!/usr/bin/env bash
#
# Does the ARM de-underscore step still PRESERVE the D13 C-facing underscore names (the 972 fix)?
#
# WHY THIS EXISTS.  tools/assemble_arm_layer.sh renames `_x` -> `x` because this ELF build sets
# `__NO_UNDERSCORES__` and XNU's .s files write Apple's `_bcopy:` literally.  But D13's C compiler
# ALSO emits leading-underscore names for the identifiers that `osfmk/arm/cpu_data.h` defines as
# `#define name _name` - so `_disable_preemption` is the C identifier's own spelling, not a Darwin
# prefix.  Stripping it leaves the C call resolving against the entry's `entry_stub_hit` stub, which
# is exactly what stopped the FIRST D13 boot to reach post-arm_vm_init output (2026-10-10:
# `stub_hit=_disable_preemption`, caller=0x8002c140 = printf).  The fix derives, from the selected
# tree's own `osfmk/arm/*.h`, the set of `#define NAME _NAME` right-hand sides and skips them in the
# rename loop.  This check re-derives that property from the SOURCE and refuses drift, so a
# re-provisioned tree or a reverted builder cannot silently re-arm the trap.  Source half: no
# compiler, no device.
#
# 4570 IS THE CONTROL.  Its derived set is {_COMM_PAGE32_BASE_ADDRESS, _COMM_PAGE64_BASE_ADDRESS}
# (cpu_data.h:86 is a FUNCTION macro the anchored pattern does not match), and NO 4570 asm object
# defines either name - so the guard is INERT on 4570 and its objects are byte-identical with and
# without it.  This check asserts that inertness rather than assuming it.
#
# Exit 0 = the guard is present, the D13 set names _disable_preemption/_enable_preemption, and the
# 4570 set intersects no 4570 asm object.  Exit 1 = refused, naming what moved.
set -uo pipefail
SELF=$(readlink -f "${BASH_SOURCE[0]}")
ROOT=$(cd "$(dirname "$SELF")/.." && pwd)
BUILDER=$ROOT/tools/assemble_arm_layer.sh
D13=$ROOT/external/xnu-hd2-darwin13/xnu
T4570=$ROOT/external/xnu-4570.1.46

SELFTEST=0
[[ ${1:-} == --selftest ]] && SELFTEST=1

# The derivation, ONE copy, spelled exactly as the builder spells it.
derive() {  # derive TREE_DIR
    grep -rhoE '^#define[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]+_[a-zA-Z][A-Za-z0-9_]*[[:space:]]*$' \
        "$1/osfmk/arm/"*.h 2>/dev/null | awk '{print $NF}' | sort -u
}

refuse() { printf 'check_deunderscore_guard: %s\n' "$*" >&2; exit 1; }

# --- 0. selftest: a builder with the guard REMOVED must be refused --------------------------------
if [[ $SELFTEST -eq 1 ]]; then
    tmp=$(mktemp); trap 'rm -f "$tmp"' EXIT
    # strip the guard block (the `continue 2` skip) and assert the property test below would fail.
    awk '/for _k in \$UNDERSCORE_KEEP; do/{skip=1} skip&&/continue 2/{skip=0; next} !skip{print}' "$BUILDER" > "$tmp"
    if grep -q 'for _k in \$UNDERSCORE_KEEP' "$tmp"; then
        refuse "selftest: could not remove the guard from a copy of the builder"
    fi
    # the same presence test the real run uses must now FAIL on the stripped copy
    if grep -q 'UNDERSCORE_KEEP' "$tmp" && grep -q 'continue 2' "$tmp"; then
        refuse "selftest: the stripped copy still reads as guarded - the test does not bite"
    fi
    printf 'check_deunderscore_guard: selftest ok - a builder copy without the guard is refused\n'
    exit 0
fi

# --- 1. the guard is present in the builder --------------------------------------------------------
[[ -f $BUILDER ]] || refuse "no builder at $BUILDER"
grep -q 'UNDERSCORE_KEEP=' "$BUILDER" || refuse "the builder carries no UNDERSCORE_KEEP derivation - the 972 guard is absent"
grep -q 'for _k in \$UNDERSCORE_KEEP' "$BUILDER" || refuse "the builder derives UNDERSCORE_KEEP but does not iterate it in the rename loop"
grep -q 'continue 2' "$BUILDER" || refuse "the rename loop does not skip a preserved name (no 'continue 2')"
grep -qF '^#define[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]+_[a-zA-Z]' "$BUILDER" || refuse "the derivation pattern is not the anchored '#define NAME _NAME' form the guard derives from"

# --- 2. the D13 keep-set names the two symbols the press found ------------------------------------
d13set=$(derive "$D13")
[[ -n $d13set ]] || refuse "D13 derives an EMPTY keep-set - the tree or the pattern moved"
for need in _disable_preemption _enable_preemption; do
    printf '%s\n' "$d13set" | grep -qxF -- "$need" || refuse "D13's derived keep-set does not contain $need (the 972 symbol); the pattern or the tree moved"
done

# --- 3. the guard is INERT on 4570 -----------------------------------------------------------------
t70set=$(derive "$T4570")
hit=0
for k in $t70set; do
    for o in "$ROOT"/out/xnu_asm_obj/*.o; do
        [[ -e $o ]] || continue
        if arm-none-eabi-nm "$o" 2>/dev/null | grep -q " $k$"; then hit=1; printf '  note  4570 %s defines %s\n' "$o" "$k"; fi
    done
done
[[ $hit -eq 0 ]] || refuse "a 4570 asm object defines a name in 4570's keep-set - the guard is NOT inert on 4570"

printf 'check_deunderscore_guard: ok - the builder preserves the tree'"'"'s `#define NAME _NAME` underscore names; D13 keeps %s; 4570'"'"'s set (%s) intersects no 4570 asm object, so the guard is inert there\n' \
    "$(printf '%s' "$d13set" | tr '\n' ' ')" "$(printf '%s' "$t70set" | tr '\n' ' ')"
