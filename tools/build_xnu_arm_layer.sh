#!/usr/bin/env bash
#
# Compile XNU's entire osfmk/arm layer, and report what it still needs to link.
#
#   ./tools/build_xnu_arm_layer.sh              # build into out/xnu_arm_obj/
#   ./tools/build_xnu_arm_layer.sh --syntax     # parse only, no objects (faster)
#   ./tools/build_xnu_arm_layer.sh --undefined N   # show N undefined symbols (default 0)
#
# Why this exists. The project spent several turns reporting `osfmk/arm: N of 32 compile` from an
# ad-hoc sweep. This is the same measurement as a build: it compiles every .c in the layer to a real
# object and then reports the shape of the link problem, which is the next thing after compiling.
#
# The flag set is the whole result. Every entry is recorded with why it is what it is, because
# several look arbitrary and are not — and because the difference between 3 of 32 and 32 of 32 is
# almost entirely these lines, not any code change.
#
# Prerequisites, both reproducible:
#   ./tools/build_mig.sh          generates out/mig/build/migcom
#   ./tools/gen_mach_headers.sh   generates out/mach_headers/ (the MIG output)
#
# What this does NOT do: link. 443 distinct symbols are undefined across the layer and most of them
# live outside osfmk/arm. Compiling is the first of three steps, and the script says so rather than
# letting a green run imply more than it does.

set -uo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
SHIMS=$REPO_ROOT/stages/stage90/shims
SHIMS_ARM=$REPO_ROOT/stages/stage90/shims_arm
MIG_HEADERS=${MIG_HEADERS:-$REPO_ROOT/out/mach_headers}
# The other two generated roots: MIG's output, and the headers the build generates from the
# configuration - the `OPTIONS/` macros (tools/gen_option_headers.py) plus `libkern/version.h`
# (tools/gen_libkern_version.sh). osfmk/arm reaches <mach_ldebug.h> through kern/thread.h:104, so
# without the second root this build reports 0 of 32.
GENERATED=${XNU_GENERATED:-$REPO_ROOT/out/xnu_generated}
OPTION_HEADERS=${XNU_OPTION_HEADERS_OUT:-$REPO_ROOT/out/xnu_options}
OUT=${XNU_ARM_OBJ_OUT:-$REPO_ROOT/out/xnu_arm_obj}

SYNTAX_ONLY=0
SHOW_UNDEFINED=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --syntax)    SYNTAX_ONLY=1; shift ;;
        --undefined) SHOW_UNDEFINED=${2:-20}; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

[[ -d $XNU/osfmk/arm ]] || { echo "no osfmk/arm in $XNU" >&2; exit 2; }
[[ -d $MIG_HEADERS/mach ]] || {
    echo "no generated mach headers in $MIG_HEADERS - run tools/gen_mach_headers.sh first" >&2
    exit 2
}
command -v clang >/dev/null 2>&1 || { echo "clang is required" >&2; exit 2; }

mkdir -p "$OUT"
rm -f "$OUT"/*.o

# ---------------------------------------------------------------------------------------------
# The force-include set. Each of these is a header the *build* arranges to be present, or a
# fragment that resolves a collision; none of them is a stand-in for missing XNU code.
#
#   sys/_types/_u_int.h     kern/sched.h uses `u_int` without including sys/types.h, and the whole
#                           of sys/types.h collides with kern_types.h (both define clock_t).
#   arm/simple_lock.h       defines decl_simple_lock_data and is included by NOTHING in the tree.
#   kern/queue.h, kern/ast.h  carry mpqueue_head_t and ast_t, which cpu_data_internal.h uses.
#   stdatomic.h             with -ffreestanding this is XNU's own, which defines the
#                           `enum memory_order` its atomics name.
#   mach/task_policy.h, mach/thread_policy.h  the QoS *_policy structs are members of task_t and
#                           thread_t but are declared in mach/, which neither kern/task.h nor
#                           kern/thread.h includes.
#   mi4ios6_build_config.h  the declarations this configuration supplies (currently `uint_t`) -
#                           see the file, it is not a stand-in for a missing header.
# ---------------------------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------------------------
# The values. These are what osfmk/conf/MASTER.XXX and the build's DEFINES would carry; the tarball
# ships neither, so each is chosen. Where the choice is *forced* by the source, that is noted.
# ---------------------------------------------------------------------------------------------
DEFINES=(
    -DARMA7=1
    -DKERNEL=1
    -DKERNEL_PRIVATE=1
    # MACH_KERNEL, not just MACH_KERNEL_PRIVATE. Getting this wrong is silent: kern/xpr.h:83 takes
    # its *userland* branch without it and asks for a header no kernel build has.
    -DMACH_KERNEL=1
    -DMACH_KERNEL_PRIVATE=1
    -DXNU_KERNEL_PRIVATE=1
    # The BSD side: task.h:236 and thread.h:467 put bsd_info and uthread behind `#ifdef MACH_BSD`.
    -DMACH_BSD=1
    -DPRIVATE=1
    # Forced by the source, not preferred: kpc_arm.c and monotonic_arm.c are files in this layer
    # and read cpu_kpc_* / cpu_monotonic, which those guards gate.
    -DKPC=1
    -DMONOTONIC=1
    # XPR_DEBUG=0 skips kern/xpr.h's `#include <xpr_debug.h>`; LOCK_PRIVATE gates
    # arm/locks.h:254-320, where LCK_MTX_THREAD_MASK lives.
    -DXPR_DEBUG=0
    -DLOCK_PRIVATE=1
    -D__arm__=1
    -DCONFIG_EMBEDDED=1
    -D__ARM_L2CACHE_SIZE_LOG__=21
    # The scheduler. struct run_queue is defined only under TIMESHARE_CORE or PROTO
    # (kern/sched.h:201), and MULTIQ wants a kern/sched_multiq.h the tarball does not ship — so the
    # source narrows MASTER.XXX's choice to two rather than leaving it open.
    -DCONFIG_SCHED_TIMESHARE_CORE=1
    -DCONFIG_SCHED_TRADITIONAL=1
)

INCLUDES=(
    -I"$GENERATED" -I"$GENERATED/bsd"
    -I"$MIG_HEADERS"
    -I"$OPTION_HEADERS"
    -I"$XNU/osfmk"
    -I"$XNU/iokit"
    -I"$XNU/bsd"
    -I"$XNU/libkern"
    -I"$XNU/EXTERNAL_HEADERS"
    -I"$XNU/pexpert"
    -I"$XNU/osfmk/arm"
    -I"$XNU/bsd/arm"
    # The tree root, which is what makes <security/_label.h> resolve - osfmk/kern/exception.h:39
    # includes it, and the file is at $XNU/security/_label.h rather than under osfmk/. Same entry as
    # in build_xnu_arm_kernel.sh, and the same reason (experiment-117).
    -I"$XNU"
    -I"$SHIMS" -I"$SHIMS/kern" -I"$SHIMS/mach"
    -I"$SHIMS_ARM" -I"$SHIMS_ARM/kern" -I"$SHIMS_ARM/mach"
    -I"$SHIMS_ARM/sys" -I"$SHIMS_ARM/sys/_pthread"
)

# -mfpu/-mfloat-abi: machine_cpuid.c reads the VFP identification registers, and without an FPU
# selected the assembler rejects them with "instruction requires: VFP2". Cortex-A15 is NEON/VFPv4.
CC_ARGS=(
    clang --target=armv7-none-eabi -mcpu=cortex-a15 -marm
    -mfpu=neon-vfpv4 -mfloat-abi=softfp
    -ffreestanding -fno-builtin -fno-common -fno-pic -O2 -w -ferror-limit=0
)

if [[ $SYNTAX_ONLY -eq 1 ]]; then
    CC_ARGS+=(-fsyntax-only)
fi

ok=0
fail=0
failed=()
for src in "$XNU"/osfmk/arm/*.c; do
    name=$(basename "$src" .c)
    if "${CC_ARGS[@]}" "${FORCE_INCLUDES[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" \
         -c "$src" -o "$OUT/$name.o" 2>"$OUT/$name.log"; then
        ok=$((ok + 1))
    else
        fail=$((fail + 1))
        failed+=("$name")
        printf '  FAIL %s: %s\n' "$name" "$(grep -m1 -E 'error:' "$OUT/$name.log" | cut -c1-120)"
    fi
done

echo
echo "osfmk/arm: $ok of $((ok + fail)) compile to objects"

if [[ $fail -gt 0 ]]; then
    exit 1
fi

if [[ $SYNTAX_ONLY -eq 1 ]]; then
    exit 0
fi

echo "text: $(arm-none-eabi-size "$OUT"/*.o 2>/dev/null | awk '{s+=$1} END {print s}') bytes across $ok objects"

echo
echo "== what it still needs to link =="
arm-none-eabi-nm -u "$OUT"/*.o 2>/dev/null | awk '{print $2}' | sort -u > "$OUT/undefined.txt"
total=$(wc -l < "$OUT/undefined.txt")
echo "$total distinct undefined symbols"

if [[ $SHOW_UNDEFINED -gt 0 ]]; then
    head -"$SHOW_UNDEFINED" "$OUT/undefined.txt" | sed 's/^/  /'
fi

echo
echo "most of those live outside osfmk/arm: the pmap, the kernel proper, libkern and bsd. Compiling"
echo "this layer is the first of three steps; linking and running are the other two."
echo "full list: $OUT/undefined.txt"
