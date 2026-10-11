#!/usr/bin/env bash
#
# Does the D13 boot's NEXT frontier really sit on the clock ladder, or on `coalitions_init`?
#
# WHY THIS EXISTS.  `src/entry/build_entry.sh`'s 286-step comment long predicted the boot stops at
# `coalitions_init`.  That prediction is stale: `coalition.c` landed, so `coalitions_init` is real,
# and the actual next ladder the boot climbs is the clock one, entered from `machine_init`:
#
#     machine_init                (osfmk/arm/model_dep.c)  `debug_log_init(); clock_config();`
#       -> clock_config           (osfmk/kern/clock.c)     tail-calls clock_oldconfig
#         -> clock_oldconfig      (osfmk/kern/clock_oldops.c) walks cl_ops->c_config  [indirect]
#     clock_init                  (osfmk/kern/clock.c)     calls clock_oldinit
#       -> clock_oldinit          (osfmk/kern/clock_oldops.c) walks cl_ops->c_init    [indirect]
#         -> rtclock_init         (osfmk/arm/rtclock.c)    <-- THE FRONTIER
#
# The `cl_ops->c_config`/`cl_ops->c_init` calls are **indirect** (`sysclk_ops = { rtclock_config,
# rtclock_init, ... }`, clock_oldops.c:144-145), so there is NO static `bl rtclock_*` anywhere in
# the image to grep for - which is exactly why a comment (not a check) let the prediction drift.
# A claim in a comment is not a check; this script is the check, and it corroborates the comment
# block in `src/entry/build_entry.sh` so the two cannot drift apart.
#
# What this checks, and what it does NOT:
#
#   - it reads the D13 SOURCE bodies by value and asserts the call edges above are present and in
#     order - a renamed or reordered step is refused;
#   - it asserts `is_clock_configured`, the *i386* `machine_init` body, is ABSENT from this tree, so
#     a re-vendored tree whose machine_init changed is caught rather than silently accepted;
#   - it corroborates against the linked entry image (`out/stage90/xnu_arm_entry.elf`) when that
#     artifact exists: `machine_init`'s own instructions must `bl <debug_log_init>` then
#     `bl <clock_config>`, and the ladder symbols must all be defined `T`. If the ELF is absent the
#     source half still runs (source half: no compiler, no device);
#   - it does NOT prove `rtclock_init` completes on the device, nor that `coalitions_init` is real
#     in ANY particular image - the negative it asserts is about the SOURCE tree.
#
# NOTE ON `pipefail`.  Every fact below is tested with a here-string (`grep ... <<<"$var"`), never a
# `printf | grep -q` pipeline: under `set -o pipefail` a `grep -q` that exits on first match SIGPIPEs
# its producer, the pipeline returns 141, and a true fact reads as a moved one. That bug is exactly
# the class this file is written to avoid.
#
# Usage: ./check_machine_init_body.sh [--tree <XNU_TREE>] [--elf <path>] [--selftest]
# Exit 0 when the clock ladder holds; 1 when a fact has moved; 2 usage.

set -euo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

TREE=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}
ELF=${ENTRY_ELF:-$REPO_ROOT/out/stage90/xnu_arm_entry.elf}
SELFTEST=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --tree) TREE=$2; shift 2 ;;
        --elf)  ELF=$2;  shift 2 ;;
        --selftest) SELFTEST=1; shift ;;
        *) echo "usage: $0 [--tree <XNU_TREE>] [--elf <path>] [--selftest]" >&2; exit 2 ;;
    esac
done

MODEL_DEP=$TREE/osfmk/arm/model_dep.c
CLOCK_C=$TREE/osfmk/kern/clock.c
CLOCK_OLD=$TREE/osfmk/kern/clock_oldops.c
RTCLOCK=$TREE/osfmk/arm/rtclock.c

refuse() { printf 'check_machine_init_body: %s\n' "$1" >&2; exit 1; }

# _body FILE FN  - print the C body of the top-level function FN (crude: brace-counted from the
# `FN(` line). Works for the flat, brace-matched bodies used here. `|| true` so a missing match is
# an empty string, not a `set -e` abort (the house's `set -e`/`awk` lesson).
_body() {
    awk -v fn="$1" '
        !inb && $0 ~ ("(^|[^A-Za-z0-9_])" fn "\\([^;]*\\)?[[:space:]]*$") { want=1 }
        want && !inb && index($0,"{") { inb=1 }
        inb { print; d=gsub(/\{/,"{"); u=gsub(/\}/,"}"); depth+=d-u; if (depth<=0 && seen) exit; if (depth>0) seen=1 }
    ' "$2" 2>/dev/null || true
}

# has TEXT PATTERN  - true if PATTERN matches TEXT (a here-string, so no pipe/SIGPIPE).
has() { grep -qE "$2" <<<"$1"; }

check_source() {
    [[ -f $MODEL_DEP ]] || refuse "no D13 model_dep.c at $MODEL_DEP"
    [[ -f $CLOCK_C   ]] || refuse "no D13 clock.c at $CLOCK_C"
    [[ -f $CLOCK_OLD ]] || refuse "no D13 clock_oldops.c at $CLOCK_OLD"
    [[ -f $RTCLOCK   ]] || refuse "no D13 rtclock.c at $RTCLOCK"

    local mi
    mi=$(_body machine_init "$MODEL_DEP")
    [[ -n $mi ]] || refuse "machine_init has no body in $MODEL_DEP - the frontier's entry moved or was renamed"
    has "$mi" 'debug_log_init[[:space:]]*\(' \
        || refuse "D13 machine_init no longer calls debug_log_init - the body is not the D13 one (model_dep.c:141)"
    has "$mi" 'clock_config[[:space:]]*\(' \
        || refuse "D13 machine_init no longer calls clock_config (model_dep.c:142) - the clock ladder is not entered"
    local dl cc
    dl=$(grep -nE 'debug_log_init[[:space:]]*\(' <<<"$mi" | head -1 | cut -d: -f1)
    cc=$(grep -nE 'clock_config[[:space:]]*\(' <<<"$mi" | head -1 | cut -d: -f1)
    [[ -n $dl && -n $cc && $dl -lt $cc ]] \
        || refuse "machine_init calls clock_config BEFORE debug_log_init - the D13 body order moved"

    # the i386 body must NOT be here: this is the exact staleness the comment carried.
    grep -q 'is_clock_configured' "$MODEL_DEP" \
        && refuse "D13 model_dep.c contains is_clock_configured - machine_init looks i386-shaped again; the clock-ladder claim no longer holds"

    local ccbody cibody cobody ribody
    ccbody=$(_body clock_config "$CLOCK_C")
    [[ -n $ccbody ]] || refuse "clock_config has no body in $CLOCK_C"
    has "$ccbody" 'clock_oldconfig[[:space:]]*\(' \
        || refuse "clock_config no longer calls clock_oldconfig - the c_config walk is gone"

    cibody=$(_body clock_init "$CLOCK_C")
    [[ -n $cibody ]] || refuse "clock_init has no body in $CLOCK_C"
    has "$cibody" 'clock_oldinit[[:space:]]*\(' \
        || refuse "clock_init no longer calls clock_oldinit - the ladder rung clock_init->clock_oldinit moved"

    cobody=$(_body clock_oldinit "$CLOCK_OLD")
    [[ -n $cobody ]] || refuse "clock_oldinit has no body in $CLOCK_OLD"
    has "$cobody" 'cl_ops[[:space:]]*->[[:space:]]*c_init' \
        || refuse "clock_oldinit no longer calls cl_ops->c_init - the indirect edge to rtclock_init is gone"

    grep -qE 'rtclock_config[[:space:]]*,[[:space:]]*rtclock_init' "$CLOCK_OLD" \
        || refuse "sysclk_ops no longer pairs { rtclock_config, rtclock_init } - the table-to-frontier mapping moved"

    ribody=$(_body rtclock_init "$RTCLOCK")
    [[ -n $ribody ]] || refuse "rtclock_init has no body in $RTCLOCK"
    has "$ribody" 'rtclock_bus_configure[[:space:]]*\(' \
        || refuse "rtclock_init no longer calls rtclock_bus_configure - the frontier body moved"
    has "$ribody" 'clock_timebase_init[[:space:]]*\(' \
        || refuse "rtclock_init no longer calls clock_timebase_init - the frontier body moved"
}

check_image() {
    [[ -f $ELF ]] || { printf 'check_machine_init_body: note: no ELF at %s - source half only\n' "$ELF"; return 0; }
    command -v arm-none-eabi-objdump >/dev/null 2>&1 \
        || { printf 'check_machine_init_body: note: arm-none-eabi-objdump absent - image half skipped\n'; return 0; }

    local n symtab
    symtab=$(arm-none-eabi-nm "$ELF" 2>/dev/null || true)
    for n in machine_init clock_config clock_oldconfig clock_init clock_oldinit rtclock_init; do
        has "$symtab" " T $n\$" \
            || refuse "the linked image has no defined '$n' - the ladder symbol moved (rebuild or re-read the frontier)"
    done

    local dis dump
    dump=$(arm-none-eabi-objdump -d "$ELF" 2>/dev/null || true)
    # machine_init's own block: from its header to the next `<sym>:` header line. It ends in a TAIL
    # branch (`b <clock_config>`, because the C body is `clock_config(); return;`), not a `bl`.
    dis=$(awk '/<machine_init>:/{f=1;print;next} f&&/^[0-9a-f]+ <[^>]+>:$/{exit} f{print}' <<<"$dump")
    [[ -n $dis ]] || refuse "the linked image disassembles no machine_init body"
    has "$dis" '(bl|b)[[:space:]]+[0-9a-f]+ <debug_log_init>$' \
        || refuse "linked machine_init has no 'bl <debug_log_init>' - the D13 body did not link"
    has "$dis" '(bl|b)[[:space:]]+[0-9a-f]+ <clock_config>$' \
        || refuse "linked machine_init has no 'b/bl <clock_config>' - the clock ladder is not entered in the image"

    printf 'check_machine_init_body: ok - machine_init -> clock_config/clock_init -> clock_oldinit -> rtclock_init; coalitions_init is not the frontier; image agrees\n'
}

if [[ $SELFTEST -eq 1 ]]; then
    # Mutation: a model_dep.c whose machine_init calls clock_config but NOT debug_log_init must be
    # refused. Copy the three clock sources unchanged into a scratch tree and mutate only model_dep.c.
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    mkdir -p "$tmp/osfmk/arm" "$tmp/osfmk/kern"
    cp "$CLOCK_C" "$tmp/osfmk/kern/clock.c"
    cp "$CLOCK_OLD" "$tmp/osfmk/kern/clock_oldops.c"
    cp "$RTCLOCK" "$tmp/osfmk/arm/rtclock.c"
    grep -v 'debug_log_init' "$MODEL_DEP" > "$tmp/osfmk/arm/model_dep.c" || true
    if "$0" --tree "$tmp" >/dev/null 2>&1; then
        refuse "selftest: a machine_init WITHOUT debug_log_init was accepted - the check does not bite"
    fi
    printf 'check_machine_init_body: selftest ok - a machine_init missing debug_log_init is refused\n'
    exit 0
fi

check_source
check_image