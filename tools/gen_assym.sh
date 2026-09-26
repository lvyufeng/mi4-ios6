#!/usr/bin/env bash
#
# Generate `assym.s` — the constants XNU's ARM assembly needs — with Apple's own generator.
#
#   ./tools/gen_assym.sh                 # RELEASE
#   ./tools/gen_assym.sh --min           # STAGE90_BOOT
#
# This is the sixth generator in the tree, alongside `mkheaders.c`'s OPTIONS headers,
# `makesyscalls.sh`, MIG, `newvers.pl` and `config(8)`'s device headers. And it is the same shape as
# the other five: **the official generator is in the tarball and this project was supplying its
# output by hand.**
#
# `src/entry/assym.s` is 48 lines with 17 defines, and its own header comment says
# the real one "is absent from the OSS tarball" and is "the single largest piece of the build
# configuration that osfmk/arm's assembly needs". **Both halves of that are wrong.**
# `osfmk/arm/genassym.c` is in the tarball, it is the generator, and it produces **268 lines** —
# including `ASSIST_RESET_HANDLER`, `CPU_DATA_ENTRIES` and `CPU_DATA_PADDR`, which are exactly what
# `locore.s:92,96,103` was failing on with "register expected". That error reads like an assembler
# syntax problem and is not: the name was simply undefined.
#
# The pipeline is Apple's, from osfmk/conf/Makefile.template:184-189, unmodified:
#
#   genassym.o: clang -S -o genassym.o ... genassym.c        # the C file compiles to ASSEMBLY
#   assym.s:    sed -e '/^DEFINITION__define__/!d;{N;s/\n//;}' ... genassym.o > assym.s
#
# `genassym.c` uses the classic trick: `DECLARE("NAME", offsetof(...))` emits a symbol whose name
# carries the value as an ASCII string, and the sed scrapes it back out. Two forms come out of each
# entry and both are used:
#
#   #define NAME     3     -- note the `#`: ARM's immediate syntax is `[r4, #3]`, and the assembly
#                            writes `[r4, NAME]`, so the `#` has to be inside the define
#   #define NAME_NUM 3     -- the bare number, for uses that supply their own `#`
#
# The compile needs the same flag set as the rest of the ARM layer — in particular a scheduler,
# because `kern/thread.h` is in the include chain and `sched_prim.h:574`'s `#error` fires without one.

set -uo pipefail
cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}
OUT=${XNU_ASSYM_OUT:-$REPO_ROOT/out/xnu_assym}/$CONFIG

SHIMS=$REPO_ROOT/src/shims
SHIMS_ARM=$REPO_ROOT/src/shims_arm
GENERATED=${XNU_GENERATED:-$REPO_ROOT/out/xnu_generated}
OPTION_HEADERS=${XNU_OPTION_HEADERS_OUT:-$REPO_ROOT/out/xnu_options}/$CONFIG
DEVICE_HEADERS=${XNU_DEVICE_HEADERS_OUT:-$REPO_ROOT/out/xnu_device}/$CONFIG
MIG_HEADERS=${MIG_HEADERS:-$REPO_ROOT/out/mach_headers}

[[ -f $XNU/osfmk/arm/genassym.c ]] || { echo "no genassym.c in $XNU" >&2; exit 2; }

mkdir -p "$OUT"

# **488: read once, with the status seen.** `done < <(...)` hides the expansion's failure and leaves an
# empty list, which assembles the whole ARM layer with none of the configuration's options - the defect
# this step exists to not reproduce. See `tools/xnu_config/arm_asm_defines.sh`.
CONFIG_DEFINES=()
if ! defines=$("$TOOLS_DIR/xnu_config/make_defines.sh" "$CONFIG"); then
    echo "gen_assym.sh: make_defines.sh failed for $CONFIG" >&2
    exit 2
fi
if [[ -z $defines ]]; then
    echo "gen_assym.sh: $CONFIG expanded to no options - refusing to generate constants without them" >&2
    exit 2
fi
while IFS= read -r d; do
    [[ -n $d ]] && CONFIG_DEFINES+=("$d")
done <<<"$defines"

# The same values build_xnu_arm_kernel.sh uses, and for the same reasons — see that script. Kept in
# step by intent rather than by construction, which is the one place in this pipeline where a
# divergence would be silent; the check at the end catches the case that matters (an empty assym.s).
CFLAGS=(
    clang --target=$("$TOOLS_DIR/xnu_config/arm_target.sh") -mcpu=cortex-a15 -marm
    -mfpu=neon-vfpv4 -mfloat-abi=softfp
    -ffreestanding -fno-builtin -fno-common -fno-pic -O2 -w -ferror-limit=0
    "${CONFIG_DEFINES[@]}"
    -DMACH_KERNEL=1 -DMACH_KERNEL_PRIVATE=1 -DXNU_KERNEL_PRIVATE=1 -DKERNEL_PRIVATE=1
    -DMACH_BSD=1 -DPRIVATE=1 -DKPC=1 -DMONOTONIC=1 -DLOCK_PRIVATE=1
    -DARMA7=1 -DKERNEL=1 -D__arm__=1 -D__APPLE__=1 -DCONFIG_EMBEDDED=1
    -D__ARM_L2CACHE_SIZE_LOG__=21
    -DCONFIG_SCHED_TIMESHARE_CORE=1 -DCONFIG_SCHED_TRADITIONAL=1
    -include sys/_types/_u_int.h -include arm/simple_lock.h
    -include kern/queue.h -include kern/ast.h -include stdatomic.h
    -include mach/task_policy.h -include mach/thread_policy.h
    -include mi4ios6_build_config.h -include sys/_types/_caddr_t.h
    -include meta_features.h
    -I"$GENERATED/bsd" -I"$GENERATED"
    -I"$OPTION_HEADERS" -I"$DEVICE_HEADERS" -I"$MIG_HEADERS"
    -I"$XNU/osfmk" -I"$XNU/iokit" -I"$XNU/bsd" -I"$XNU/libkern" -I"$XNU/pexpert" -I"$XNU"
    -I"$XNU/osfmk/arm" -I"$XNU/bsd/arm" -I"$XNU/EXTERNAL_HEADERS"
    -I"$SHIMS" -I"$SHIMS/kern" -I"$SHIMS/mach"
    -I"$SHIMS_ARM" -I"$SHIMS_ARM/kern" -I"$SHIMS_ARM/mach"
)

# 1. The C file compiles to ASSEMBLY, not to an object.
if ! "${CFLAGS[@]}" -S -o "$OUT/genassym.s" "$XNU/osfmk/arm/genassym.c" 2>"$OUT/genassym.log"; then
    echo "genassym.c did not compile:" >&2
    grep -m5 -E "error|fatal" "$OUT/genassym.log" >&2
    exit 2
fi

# 2. Apple's scrape, character for character (osfmk/conf/Makefile.template:189).
sed -e '/^[[:space:]]*DEFINITION__define__/!d;{N;s/\n//;}' \
    -e 's/^[[:space:]]*DEFINITION__define__\([^:]*\):.*ascii.*"[\$$]*\([-0-9\#]*\)".*$/#define \1 \2/' \
    -e 'p' \
    -e 's/#//2' \
    -e 's/^[[:space:]]*#define \([A-Za-z0-9_]*\)[[:space:]]*[\$$#]*\([-0-9]*\).*$/#define \1_NUM \2/' \
    "$OUT/genassym.s" > "$OUT/assym.s"

n=$(grep -c '^#define' "$OUT/assym.s" || true)
# A scrape that matched nothing leaves an empty file, which would make every assembly file fail with
# an undefined name — a failure that reads like the problem it is replacing. So it is checked.
if [[ ${n:-0} -lt 100 ]]; then
    echo "the scrape produced $n defines from $(wc -l < "$OUT/genassym.s") lines of assembly - too few to be right" >&2
    exit 1
fi

# The three names locore.s fails on when assym.s is absent, checked by name rather than by count.
for sym in ASSIST_RESET_HANDLER CPU_DATA_ENTRIES CPU_DATA_PADDR; do
    grep -q "^#define $sym " "$OUT/assym.s" || { echo "assym.s has no $sym" >&2; exit 1; }
done

echo "generated $n defines in $OUT/assym.s"
echo "  from $(wc -l < "$OUT/genassym.s") lines of genassym.s (Apple's sed, osfmk/conf/Makefile.template:189)"
