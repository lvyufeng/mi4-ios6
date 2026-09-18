#!/usr/bin/env bash
#
# Measure how much of XNU's osfmk compiles, and what the remainder is waiting for.
#
#   ./tools/sweep_xnu_osfmk.sh                    # every directory, summary per directory
#   ./tools/sweep_xnu_osfmk.sh --dir kern         # one directory, with the failing files named
#   ./tools/sweep_xnu_osfmk.sh --blockers 30      # the distinct things blocking compilation
#
# Why this exists. `build_xnu_arm_layer.sh` answers "does osfmk/arm compile" — it does. This answers
# the next question, which is how much of the kernel proper is left, and it answers it the same way
# the ARM measurement was answered: by compiling, not by estimating.
#
# The output that matters is `--blockers`. A per-file error count is not actionable — one missing
# header produces dozens of cascading errors — but the list of distinct missing *names* is, because
# each one is either a header to supply, a value to choose, or a file to compile. That distinction
# is what turned osfmk/arm from 3 of 32 into 32 of 32, and the same method applies here.
#
# Uses the flag set from build_xnu_arm_layer.sh, which is the accumulated result of that work. It is
# a starting point for the rest of osfmk, not a claim that it is the right set for it.

set -uo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
SHIMS=$REPO_ROOT/stages/stage90/shims
SHIMS_ARM=$REPO_ROOT/stages/stage90/shims_arm
MIG_HEADERS=${MIG_HEADERS:-$REPO_ROOT/out/mach_headers}
OUT=${SWEEP_OUT:-$REPO_ROOT/out/osfmk_sweep}

ONLY_DIR=""
SHOW_BLOCKERS=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dir)      ONLY_DIR=$2; shift 2 ;;
        --blockers) SHOW_BLOCKERS=${2:-30}; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

[[ -d $MIG_HEADERS/mach ]] || {
    echo "no generated mach headers - run tools/gen_mach_headers.sh first" >&2; exit 2; }

mkdir -p "$OUT"; : > "$OUT/all.log"

FORCE_INCLUDES=(
    -include sys/_types/_u_int.h
    -include arm/simple_lock.h
    -include kern/queue.h
    -include kern/ast.h
    -include stdatomic.h
    -include mach/task_policy.h
    -include mach/thread_policy.h
    -include mi4ios6_build_config.h
)

DEFINES=(
    -DMACH_KERNEL=1 -DMACH_KERNEL_PRIVATE=1 -DXNU_KERNEL_PRIVATE=1 -DKERNEL_PRIVATE=1
    -DMACH_BSD=1 -DPRIVATE=1 -DKPC=1 -DMONOTONIC=1 -DXPR_DEBUG=0 -DLOCK_PRIVATE=1
    -DARMA7=1 -DKERNEL=1 -D__arm__=1 -DCONFIG_EMBEDDED=1 -D__ARM_L2CACHE_SIZE_LOG__=21
    -DCONFIG_SCHED_TIMESHARE_CORE=1 -DCONFIG_SCHED_TRADITIONAL=1
)

INCLUDES=(
    -I"$MIG_HEADERS" -I"$XNU/osfmk" -I"$XNU/iokit" -I"$XNU/bsd" -I"$XNU/libkern"
    -I"$XNU/EXTERNAL_HEADERS" -I"$XNU/pexpert" -I"$XNU/osfmk/arm" -I"$XNU/bsd/arm"
    -I"$SHIMS" -I"$SHIMS/kern" -I"$SHIMS/mach"
    -I"$SHIMS_ARM" -I"$SHIMS_ARM/kern" -I"$SHIMS_ARM/mach"
    -I"$SHIMS_ARM/sys" -I"$SHIMS_ARM/sys/_pthread"
)

CC_ARGS=(
    clang --target=$("$TOOLS_DIR/xnu_config/arm_target.sh") -mcpu=cortex-a15 -marm
    -mfpu=neon-vfpv4 -mfloat-abi=softfp
    -fsyntax-only -ferror-limit=0 -ffreestanding -w
)

DIRS=()
if [[ -n $ONLY_DIR ]]; then
    DIRS=("$ONLY_DIR")
else
    for d in "$XNU"/osfmk/*/; do
        name=$(basename "$d")
        # arm is already known-good and its measurement lives in build_xnu_arm_layer.sh; the stub
        # directories hold one placeholder file each and would distort a percentage.
        #
        # i386 and x86_64 are EXCLUDED, and that is not tidiness: they are a different architecture,
        # and including them polluted the first run of this script with 400-odd `_STRUCT_X86_*`
        # blockers that this kernel will never contain. A measurement that counts the wrong
        # architecture's failures is worse than no measurement, because it looks like a result.
        case "$name" in arm|arm64|i386|x86_64|libsa|conf|Makefile*|default_pager) continue ;; esac
        DIRS+=("$name")
    done
fi

total_ok=0
total=0
for dir in "${DIRS[@]}"; do
    ok=0
    count=0
    : > "$OUT/$dir.agg.log"
    for src in "$XNU/osfmk/$dir"/*.c; do
        [[ -f $src ]] || continue
        count=$((count + 1))
        # Per FILE, and it has to be re-set here: the outer loop's `name` is the directory, and
        # reusing it silently wrote every failing file in a directory to one log.
        name=$(basename "$src" .c)
        # One log per FILE, not per directory. The per-file error count is the measure that
        # matters: a file one error away is a config value or an include, a file three hundred
        # errors away is a subsystem, and averaging them by directory hides which is which.
        if "${CC_ARGS[@]}" "${FORCE_INCLUDES[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" "$src" \
             >"$OUT/$name.log" 2>&1; then
            ok=$((ok + 1))
            rm -f "$OUT/$name.log"
        else
            printf '%s\n' "$(basename "$src")" >> "$OUT/$dir.failed"
            cat "$OUT/$name.log" >> "$OUT/$dir.agg.log"
        fi
    done
    [[ -f $OUT/$dir.failed ]] || : > "$OUT/$dir.failed"
    cat "$OUT/$dir.agg.log" >> "$OUT/all.log"
    total_ok=$((total_ok + ok))
    total=$((total + count))
    printf '  %-14s %3d of %3d\n' "$dir" "$ok" "$count"

    if [[ -n $ONLY_DIR ]]; then
        echo "  failed:"
        sed 's/^/    /' "$OUT/$dir.failed"
    fi
    rm -f "$OUT/$dir.failed"
done

echo
echo "osfmk (excluding arm): $total_ok of $total parse"

if [[ $SHOW_BLOCKERS -gt 0 ]]; then
    echo
    echo "== distinct missing names, most common first =="
    # The three shapes a blocker takes. Counted, not listed, because a cascade of forty errors from
    # one missing name is one blocker.
    grep -hoE "unknown type name '[A-Za-z_][A-Za-z0-9_]*'|use of undeclared identifier '[A-Za-z_][A-Za-z0-9_]*'|file not found" "$OUT/all.log" \
        | sed "s/unknown type name '//;s/use of undeclared identifier '//;s/'//" \
        | sort | uniq -c | sort -rn | head -"$SHOW_BLOCKERS"
    echo
    echo "== missing headers =="
    grep -hoE "'[^']*\.h' file not found" "$OUT/all.log" | sort -u | head -"$SHOW_BLOCKERS"
fi

echo
echo "== how far each failing file is =="
# A count of failing files is not actionable; the distribution is. A file one error away is a
# config value or an include; a file three hundred errors away is a subsystem.
for log in "$OUT"/*.log; do
    case "$(basename "$log")" in all.log|one.log|*.agg.log) continue ;; esac
    n=$(grep -cE "error:" "$log" 2>/dev/null)
    [[ ${n:-0} -eq 0 ]] && continue
    printf '%d %s\n' "$n" "$(basename "$log" .log)"
done | sort -n | awk '
    { b = ($1 <= 2) ? "1-2 errors" : ($1 <= 10) ? "3-10" : ($1 <= 50) ? "11-50" : "51+"
      c[b]++ }
    END { for (k in c) printf "  %-12s %3d file(s)\n", k, c[k] }' | sort

echo
echo "raw logs in $OUT"
