#!/usr/bin/env bash
#
# Compile the kernel Apple's own manifest says an ARM RELEASE build is made of.
#
#   ./tools/build_xnu_arm_kernel.sh                 # compile everything in the manifest
#   ./tools/build_xnu_arm_kernel.sh --limit 50      # the first 50, for a quick look
#   ./tools/build_xnu_arm_kernel.sh --dir osfmk     # one component
#   ./tools/build_xnu_arm_kernel.sh --blockers 25   # the distinct things blocking the rest
#
# This is the honest measurement. `build_xnu_arm_layer.sh` answers "does osfmk/arm compile" — all
# 32 files in the directory — and it does. But a kernel is not a directory: it is the 694 files
# `list_sources.py RELEASE` selects from Apple's `*/conf/files` lists under Apple's own
# configuration. This compiles that set.
#
# Prerequisites, both reproducible:
#   ./tools/build_mig.sh                        -> out/mig/build/migcom
#   ./tools/gen_mach_headers.sh                 -> out/mach_headers/ (the MIG output)
#   ./tools/xnu_config/list_sources.py RELEASE --write out/xnu_arm_manifest.txt
#
# What to expect: the ARM layer compiles and most of the kernel does not. The output that matters is
# `--blockers`: a per-file error count is not actionable, but the list of distinct missing names is,
# because each is either a header to supply, a value to choose, or a file to compile.

set -uo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
SHIMS=$REPO_ROOT/stages/stage90/shims
SHIMS_ARM=$REPO_ROOT/stages/stage90/shims_arm
MIG_HEADERS=${MIG_HEADERS:-$REPO_ROOT/out/mach_headers}
OUT=${XNU_KERNEL_OBJ_OUT:-$REPO_ROOT/out/xnu_kernel_obj}
MANIFEST=${MANIFEST:-$REPO_ROOT/out/xnu_arm_manifest.txt}

# The configuration to build. `RELEASE` is Apple's full iOS kernel; `STAGE90_BOOT` is the minimal
# one declared in tools/xnu_config/minimal/STAGE90_BOOT.local, and its manifest is built by passing
# the same XNU_MASTER_LOCAL to list_sources.py. Default is the full one, because the full one is
# what "does XNU compile" means; the minimal one is what "can this boot" means.
CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}

LIMIT=0
ONLY_DIR=""
SHOW_BLOCKERS=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --limit)    LIMIT=$2; shift 2 ;;
        --dir)      ONLY_DIR=$2; shift 2 ;;
        --blockers) SHOW_BLOCKERS=${2:-25}; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

[[ -f $MANIFEST ]] || {
    echo "no manifest at $MANIFEST - run:" >&2
    echo "  ./tools/xnu_config/list_sources.py RELEASE --write $MANIFEST" >&2
    exit 2
}
[[ -d $MIG_HEADERS/mach ]] || {
    echo "no generated mach headers - run tools/gen_mach_headers.sh first" >&2; exit 2; }

mkdir -p "$OUT"
# Truncate every output. A build script that appends leaves the previous run's failures in the
# list, and a count read from it is then a count of two runs - which is how a 397-file result
# became 582 here before anyone noticed.
: > "$OUT/all.log"
: > "$OUT/failed.txt"

# The force-include set and the values are build_xnu_arm_layer.sh's, which is where they were
# worked out and where each one's reason is recorded. Duplicated here rather than factored out
# because the two scripts want to stay independently runnable; if they drift, this comment is the
# pointer back.
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

# The configuration's own options, expanded from MASTER via the doconf pipeline. These are the
# values Apple's build would have; the block below is only the flags that configure the *toolchain*
# and the two that resolve collisions the config cannot express.
CONFIG_DEFINES=()
while IFS= read -r d; do
    [[ -n $d ]] && CONFIG_DEFINES+=("$d")
done < <("$TOOLS_DIR/xnu_config/make_defines.sh" "$CONFIG")

DEFINES=(
    "${CONFIG_DEFINES[@]}"
    -DMACH_KERNEL=1 -DMACH_KERNEL_PRIVATE=1 -DXNU_KERNEL_PRIVATE=1 -DKERNEL_PRIVATE=1
    -DMACH_BSD=1 -DPRIVATE=1 -DKPC=1 -DMONOTONIC=1 -DXPR_DEBUG=0 -DLOCK_PRIVATE=1
    -DARMA7=1 -DKERNEL=1 -D__arm__=1 -DCONFIG_EMBEDDED=1 -D__ARM_L2CACHE_SIZE_LOG__=21
    -DCONFIG_SCHED_TIMESHARE_CORE=1 -DCONFIG_SCHED_TRADITIONAL=1
    # _CLOCK_T: makes kern_types.h's `typedef struct clock *clock_t` the surviving definition.
    # bsd/sys/types.h:162 includes _clock_t.h unconditionally - the `#ifdef KERNEL` in that file
    # comes later - so without this the BSD userland `clock_t` (unsigned long) is typedef'd first
    # and the kernel's Mach clock object collides with it. One define, and iokit goes from 1
    # failing file to none. Same shape as the device-tree child count and the descriptor literals:
    # one name, two definitions, and the build has to say which one wins.
    -D_CLOCK_T=1
)

# Generated headers that are not MIG output: bsd/sys/sysproto.h comes from
# bsd/kern/makesyscalls.sh, and it is included by 55 of the failing files. Produced by
# tools/gen_bsd_headers.sh, and placed first so it wins over anything stale.
GENERATED=${XNU_GENERATED:-$REPO_ROOT/out/xnu_generated}

# The component order is Apple's, from makedefs/MakeInc.def:46-48:
#
#   COMPONENT_LIST        = osfmk bsd libkern iokit pexpert libsa security san
#   COMPONENT_IMPORT_LIST = $(filter-out $(COMPONENT),$(COMPONENT_LIST))
#
# so a component sees its own tree first and then the others in that order. A single build cannot
# vary the order per file the way that mechanism does, so this uses the list's own order and the
# components' headers are mostly in disjoint namespaces (sys/, kern/, mach/, iokit/, pexpert/,
# security/, san/, libkern/), which keeps the approximation honest.
#
# `-I$XNU` is what makes <security/_label.h> and <san/kasan.h> resolve. Both exist in the tarball
# and two hand-written shims used to shadow them - see docs/experiments/experiment-117.
#
# osfmk/libsa is a real component in COMPONENT_LIST and its types.h defines uint_t, but its *source*
# directory must NOT go on the include path: it holds `string.h`, `stdlib.h` and a `sys/` subdir for
# the bootloader context, and putting it there cost 4 files (191 -> 187) by shadowing the real ones.
# Apple exports a *selected list* from each component (EXPORT_MI_LIST in each Makefile) into
# EXPORT_HDRS; exposing the whole directory is not the same thing, and this is the fourth time in
# this project that a broad include path has been the bug rather than the fix.
INCLUDES=(
    -I"$GENERATED/bsd" -I"$GENERATED"
    -I"$MIG_HEADERS"
    -I"$XNU/osfmk"
    -I"$XNU/iokit"
    -I"$XNU/bsd"
    -I"$XNU/libkern"
    -I"$XNU/pexpert"
    -I"$XNU"
    -I"$XNU/osfmk/arm" -I"$XNU/bsd/arm"
    -I"$XNU/EXTERNAL_HEADERS"
    -I"$SHIMS" -I"$SHIMS/kern" -I"$SHIMS/mach"
    -I"$SHIMS_ARM" -I"$SHIMS_ARM/kern" -I"$SHIMS_ARM/mach"
    -I"$SHIMS_ARM/sys" -I"$SHIMS_ARM/sys/_pthread"
)

CC_ARGS=(
    clang --target=armv7-none-eabi -mcpu=cortex-a15 -marm
    -mfpu=neon-vfpv4 -mfloat-abi=softfp
    -ffreestanding -fno-builtin -fno-common -fno-pic -O2 -w -ferror-limit=0
)

PER_FILE_TIMEOUT=${PER_FILE_TIMEOUT:-60}
: > "$OUT/timedout.txt"

ok=0
fail=0
absent=0
skipped=0
tried=0
timedout=0

while read -r src; do
    if [[ ! -f $src ]]; then
        absent=$((absent + 1))
        continue
    fi
    # Only C. The .s files need the assembler flags from xnu_arm_assemble.sh, and the .cpp files
    # need libkern's C++ runtime, which is a separate and larger problem than this measures.
    case "$src" in
        *.cpp|*.s|*.S) skipped=$((skipped + 1)); continue ;;
    esac
    if [[ -n $ONLY_DIR && $src != *"/$ONLY_DIR/"* ]]; then
        continue
    fi

    tried=$((tried + 1))
    if [[ $LIMIT -gt 0 && $tried -gt $LIMIT ]]; then
        break
    fi

    name=$(basename "$src" .c)
    # Disambiguate: the manifest has files of the same name in different components.
    key=$(printf '%s' "$src" | sed "s|$XNU/||; s|/|_|g; s|\.c$||")
    # Bounded. A file that sends clang into a loop must cost seconds, not the whole session: one
    # did, for 45 minutes, because this had no timeout and its output was buffered behind a pipe.
    # A timeout is reported as its own outcome rather than as a compile failure, because "clang
    # hung" and "XNU does not compile" are different findings.
    if timeout "$PER_FILE_TIMEOUT" "${CC_ARGS[@]}" "${FORCE_INCLUDES[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" \
         -c "$src" -o "$OUT/$key.o" 2>"$OUT/$key.log"; then
        ok=$((ok + 1))
        rm -f "$OUT/$key.log"
    elif [[ $? -eq 124 ]]; then
        timedout=$((timedout + 1))
        printf '%s\n' "$src" >> "$OUT/timedout.txt"
        cat "$OUT/$key.log" >> "$OUT/all.log"
    else
        fail=$((fail + 1))
        cat "$OUT/$key.log" >> "$OUT/all.log"
        printf '%s\n' "$src" >> "$OUT/failed.txt"
    fi
done < "$MANIFEST"

echo
echo "== $CONFIG manifest for arm, compiled =="
echo "  C files tried:        $tried"
echo "  compile:              $ok"
echo "  fail:                 $fail"
echo "  absent from tarball:  $absent"
echo "  timed out (${PER_FILE_TIMEOUT}s): $timedout"
echo "  skipped (.s, .cpp):   $skipped"
echo "  objects in $OUT"

if [[ $SHOW_BLOCKERS -gt 0 ]]; then
    echo
    echo "== distinct missing names, most common first =="
    grep -hoE "unknown type name '[A-Za-z_][A-Za-z0-9_]*'|use of undeclared identifier '[A-Za-z_][A-Za-z0-9_]*'" "$OUT/all.log" \
        | sed "s/unknown type name '//;s/use of undeclared identifier '//;s/'//" \
        | sort | uniq -c | sort -rn | head -"$SHOW_BLOCKERS"
    if [[ -s $OUT/timedout.txt ]]; then
        echo
        echo "== clang did not finish in ${PER_FILE_TIMEOUT}s =="
        sed 's/^/  /' "$OUT/timedout.txt"
    fi
    echo
    echo "== missing headers =="
    grep -hoE "'[^']*\.h' file not found" "$OUT/all.log" | sort | uniq -c | sort -rn | head -"$SHOW_BLOCKERS"
fi
