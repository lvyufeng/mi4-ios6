#!/usr/bin/env bash
#
# Is the 914 board platform expert (src/platform/darwin13/pe_msm8974.c) actually WIRED INTO the entry
# link (the 973 rung)?
#
# WHY THIS EXISTS. The 914 experiment built `pe_msm8974.c` and MEASURED that it compiles 1/1 and
# defines `PE_init_SocSupport_stub` - but it was a standalone measurement tool, so the object landed in
# `out/xnu_arm_obj_d13/`, a directory `build_entry.sh`'s pool glob NEVER read. `PE_init_platform` calls
# `PE_init_SocSupport` (whose `pexpert_arm_common_pe_socsupport.o` leaves `PE_init_SocSupport_stub`
# undefined), so the entry build's stub generator fabricated a one-line stub and the FIRST D13 boot to
# reach it STOPPED THERE - the 2026-10-10 press of `armed-d13-0184b928`: `stub_hit=PE_init_SocSupport_stub`,
# caller `0x8048bd50` = `PE_init_platform+0x2c`. The whole defect was a DEFINITION that existed and was
# not linked: `mi4-one-value-two-definitions` with the value being "the board PE is compiled".
#
# WHAT THIS CHECKS (source half - no compiler, no device). The wiring has FOUR links, and each is a
# thing that, if reverted, re-arms the generating stub SILENTLY (the record would be unchanged):
#   (a) `build_xnu_arm_kernel.sh` COMPILES the file, with `-DBOARD_CONFIG_MSM8974=1` (the file's `#if`
#       gate) - so the object exists at all;
#   (b) `build_entry.sh` declares `STAGE90_PE_MSM8974_OBJ` and it points into the platform pool;
#   (c) that variable is NAMED in `LINK_OBJS` - the actual link, not merely a `require` (a `require`
#       alone proves the file exists, not that it is handed to the linker - the 914 failure exactly);
#   (d) `build_entry.sh` carries the LINKED-IMAGE clause that reads `PE_init_SocSupport_stub` BY VALUE
#       and refuses the generated stub (the body that tail-calls `entry_stub_hit`). Presence of a
#       symbol is not enough: a regression to the generated stub leaves the symbol present.
#
# The check works on FILE PATHS, not on captured text through a pipe: `set -o pipefail` plus `grep -q`
# on a multi-megabyte input turns a MATCH into a pipeline failure (grep exits early, the producer takes
# SIGPIPE), which reads as "the property is absent" when it is present. A check that goes red on the
# wrong thing is the defect it exists to catch, so the reads are file-direct.
#
# `--selftest` mutates each of the four and asserts each mutation is refused - a check that cannot go
# red is indistinguishable from one that holds ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
set -uo pipefail
SELF=$(readlink -f "${BASH_SOURCE[0]}")
ROOT=$(cd "$(dirname "$SELF")/.." && pwd)
KERNEL=$ROOT/tools/build_xnu_arm_kernel.sh
ENTRY=$ROOT/src/entry/build_entry.sh
SRC=$ROOT/src/platform/darwin13/pe_msm8974.c
VAR=STAGE90_PE_MSM8974_OBJ

SELFTEST=0
[[ ${1:-} == --selftest ]] && SELFTEST=1

refuse() { printf 'check_board_pe_wired: %s\n' "$*" >&2; exit 1; }

# need FILE PATTERN MESSAGE : grep -E a single file, one pattern, refuse with MESSAGE on miss.
need() {
    grep -qE "$2" "$1" && return 0
    printf '%s\n' "$3" >&2
    return 1
}
# needF FILE LITERAL MESSAGE : the same, fixed-string. The patterns holding `$XNU_PLATFORM_OBJ_OUT`
# are ERE-safe only by luck - glibc treats `$` before an ordinary char as literal, but a pattern with
# a leading `$` is an anchor and a near-miss would read as "the property is absent". The pool-path
# patterns are literals and are matched as such.
needF() {
    grep -qF -- "$2" "$1" && return 0
    printf '%s\n' "$3" >&2
    return 1
}

# check KERNEL_FILE ENTRY_FILE SRC_FILE : the four links, over file paths.
check() {
    local kf=$1 ef=$2 sf=$3

    # (a) the platform block compiles the file, with the board gate; the file's own #if selects on it.
    need "$kf" 'pe_msm8974\.c' \
        "the platform block does not name pe_msm8974.c - the object is never produced" || return 1
    need "$kf" 'BOARD_CONFIG_MSM8974=1' \
        "the platform block compiles pe_msm8974.c without -DBOARD_CONFIG_MSM8974=1 - the file's #if gate is off and the object defines nothing" || return 1
    need "$sf" 'defined\(BOARD_CONFIG_MSM8974\)' \
        "pe_msm8974.c no longer gates on BOARD_CONFIG_MSM8974 - the compile define selects nothing" || return 1

    # (b) the entry build declares the variable and points it into the platform pool.
    need "$ef" "^[[:space:]]*${VAR}=" \
        "build_entry.sh does not declare ${VAR} - the board PE has no link path" || return 1
    needF "$ef" "STAGE90_ENTRY_PE_MSM8974_OBJ:-\$XNU_PLATFORM_OBJ_OUT/pe_msm8974.o" \
        "${VAR} does not default into \$XNU_PLATFORM_OBJ_OUT/pe_msm8974.o - the compile lands elsewhere" || return 1

    # (c) the variable is NAMED in LINK_OBJS - the actual link. A `require` alone does not link it.
    needF "$ef" "\"\$${VAR}\"" \
        "\$${VAR} is not named in build_entry.sh (expected in LINK_OBJS) - the object is required but never LINKED, which is the 914 defect exactly" || return 1

    # (d) the linked-image clause reads the symbol BY VALUE (refuses the generated stub's entry_stub_hit
    # call) - otherwise the check is presence-only and a regression back to the stub would pass.
    need "$ef" 'PE_init_SocSupport_stub' \
        "build_entry.sh carries no linked-image clause naming PE_init_SocSupport_stub" || return 1
    need "$ef" 'bl.*<entry_stub_hit>' \
        "the linked-image clause does not test for the GENERATED stub body (no entry_stub_hit test) - a regression to the stub would pass" || return 1
    need "$ef" 'PE_init_SocSupport_msm8974' \
        "the linked-image clause does not require PE_init_SocSupport_msm8974 (the init the real body calls)" || return 1
    return 0
}

# --- selftest: each of the four links, mutated, must be refused -----------------------------------
if [[ $SELFTEST -eq 1 ]]; then
    [[ -f $KERNEL && -f $ENTRY && -f $SRC ]] || refuse "selftest: a source is missing ($KERNEL / $ENTRY / $SRC)"
    tmpd=$(mktemp -d); trap 'rm -rf "$tmpd"' EXIT
    # baseline must hold.
    check "$KERNEL" "$ENTRY" "$SRC" || refuse "selftest: the baseline does not hold, so a mutation cannot be judged"
    accepted=()
    # (a) the compile define removed.
    sed 's/BOARD_CONFIG_MSM8974=1/BOARD_CONFIG_MSM8974_OFF=1/' "$KERNEL" > "$tmpd/k"; cp "$ENTRY" "$tmpd/e"; cp "$SRC" "$tmpd/s"
    check "$tmpd/k" "$tmpd/e" "$tmpd/s" && accepted+=(the_board_gate_define_is_removed)
    # (a') the source gate removed.
    sed 's/defined(BOARD_CONFIG_MSM8974)/0/' "$SRC" > "$tmpd/s"
    check "$KERNEL" "$ENTRY" "$tmpd/s" && accepted+=(the_source_gate_is_removed)
    # (b) the variable's default pool moved.
    sed "s|STAGE90_ENTRY_PE_MSM8974_OBJ:-\$XNU_PLATFORM_OBJ_OUT/pe_msm8974\.o|STAGE90_ENTRY_PE_MSM8974_OBJ:-/tmp/nope.o|" "$ENTRY" > "$tmpd/e"
    check "$KERNEL" "$tmpd/e" "$SRC" && accepted+=(the_pool_default_moved)
    # (c) the LINK_OBJS entry removed.
    sed "s/\"\$${VAR}\"/\"\$STAGE90_GIC_OBJ\"/" "$ENTRY" > "$tmpd/e"
    check "$KERNEL" "$tmpd/e" "$SRC" && accepted+=(the_object_is_unlinked)
    # (d) the by-value test removed (presence only).
    sed 's/bl\.\*<entry_stub_hit>/xx/' "$ENTRY" > "$tmpd/e"
    check "$KERNEL" "$tmpd/e" "$SRC" && accepted+=(the_generated_stub_test_is_removed)
    if (( ${#accepted[@]} > 0 )); then
        printf 'FAIL: %d of 5 mutations were not refused: %s\n' "${#accepted[@]}" "$(IFS=', '; echo "${accepted[*]}")" >&2
        exit 1
    fi
    printf 'check_board_pe_wired: selftest ok - all 5 mutations (the compile gate, the source gate, the pool default, the link entry, the by-value test) were refused\n'
    exit 0
fi

# --- real run ------------------------------------------------------------------------------------
[[ -f $KERNEL ]] || refuse "no $KERNEL"
[[ -f $ENTRY ]]  || refuse "no $ENTRY"
[[ -f $SRC ]]    || refuse "no $SRC"
check "$KERNEL" "$ENTRY" "$SRC" || {
    printf 'check_board_pe_wired: run the 973 wiring (compile the board PE in the platform block, link it in build_entry.sh)\n' >&2
    exit 1
}
printf 'check_board_pe_wired: ok - the 914 board PE (src/platform/darwin13/pe_msm8974.c) is compiled with -DBOARD_CONFIG_MSM8974=1 and is LINKED (out/xnu_platform_obj_d13/pe_msm8974.o named in build_entry.sh), with a by-value linked-image clause that refuses the generated PE_init_SocSupport_stub\n'