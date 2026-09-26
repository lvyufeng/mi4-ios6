#!/usr/bin/env bash
#
# The first real link: partial-link the compiled kernel and report what the linker says.
#
#   ./tools/link_xnu_arm.sh                 # RELEASE, partial link, plus the report
#   ./tools/link_xnu_arm.sh --min           # the STAGE90_BOOT configuration
#   ./tools/link_xnu_arm.sh --final         # a full (non-relocatable) link against the ARM linker script
#
# Why this step and not another compile measurement. Everything this project has reported so far
# comes from `nm` and from clang, and neither can see two things a linker sees immediately:
#
#   * **duplicate definitions** across translation units - two files defining one symbol, which
#     compiles fine in both and fails at link time;
#   * **relocation and section errors** - a section too large for its place in the image, a branch
#     out of range, an object that cannot be placed.
#
# It found the first of those on its first run, and what it found was not where anyone was looking:
# 690 duplicate symbols from `bsd/net/net_stubs.c`, `bsd/kern/subr_xxx.c` and `bsd/netinet6/in6_cksum.c`
# - every one of which is guarded by an option macro (`#if !NETWORKING`) that was being supplied
# with the *other configuration's* value. See experiment-125.
#
# `ld -r` (a partial, relocatable link) is the right first step: it resolves what it can, reports
# duplicates, and keeps the result linkable. A full link needs a linker script and an address, which
# is a separate question from "do these objects fit together at all".

set -uo pipefail
cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
LD=${LD:-arm-none-eabi-ld}
NM=${NM:-arm-none-eabi-nm}
OUT=${LINK_OUT:-$REPO_ROOT/out/link}

CONFIG=RELEASE
FINAL=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --min)   CONFIG=STAGE90_BOOT; shift ;;
        --final) FINAL=1; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ $CONFIG == STAGE90_BOOT ]]; then
    OBJ=${MIN_OBJ:-$REPO_ROOT/out/xnu_min_obj}
else
    OBJ=${XNU_KERNEL_OBJ_OUT:-$REPO_ROOT/out/xnu_kernel_obj}
fi
[[ -d $OBJ ]] || { echo "no objects in $OBJ - run tools/build_xnu_arm_kernel.sh first" >&2; exit 2; }

mkdir -p "$OUT"
mapfile -t OBJS < <(ls "$OBJ"/*.o 2>/dev/null | sort)
[[ ${#OBJS[@]} -gt 0 ]] || { echo "no .o files in $OBJ" >&2; exit 2; }

# The EABI runtime and the compiler runtime: link inputs that are not in the manifest. Same two as
# tools/measure_link.sh, and for the same reason - see experiment-163. `-marm -mfloat-abi=soft`
# selects the ARM-state multilib of the host's arm-none-eabi-gcc; the build's callers are
# `-mfloat-abi=softfp` and the two agree on the `__aeabi_*` helpers' double arguments, which is
# measured rather than assumed.
RT_OBJ=${XNU_RT_OBJ_OUT:-$REPO_ROOT/out/xnu_rt_obj}
RTOBJS=()
if [[ -d $RT_OBJ ]]; then
    mapfile -t RTOBJS < <(ls "$RT_OBJ"/*.o 2>/dev/null | sort)
else
    echo "note: no EABI runtime objects in $RT_OBJ - run tools/build_xnu_arm_kernel.sh" >&2
fi
LIBGCC=()
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    LIBGCC=(-L"$(dirname "$(arm-none-eabi-gcc -marm -mfloat-abi=soft -print-libgcc-file-name)")" -lgcc)
fi

if [[ $FINAL -eq 0 ]]; then
    TARGET=$OUT/$CONFIG-partial.o
    "$LD" -r "${OBJS[@]}" "${RTOBJS[@]}" "${LIBGCC[@]}" -o "$TARGET" 2>"$OUT/$CONFIG-partial.err"
    rc=$?
else
    # A full link needs a script that says where everything goes. `src/xnu_link.ld` is the
    # one the Stage90 handoff uses for its entry image; it is the only address layout this project
    # has validated on hardware, so it is what a first full link is pointed at.
    TARGET=$OUT/$CONFIG-final.elf
    "$LD" -T "$REPO_ROOT/src/xnu_link.ld" --no-undefined -e _start \
          "${OBJS[@]}" "${RTOBJS[@]}" "${LIBGCC[@]}" -o "$TARGET" 2>"$OUT/$CONFIG-final.err"
    rc=$?
fi

# One file per run, so a plain grep -c works; the earlier glob form spanned several and `grep -c`
# prints one count per file, which then reached `[[ ... -gt 0 ]]` as "0\n0".
dups=$(grep -c "multiple definition" "$OUT/$CONFIG-partial.err" 2>/dev/null || true)
[[ -n $dups ]] || dups=0
echo "== $CONFIG link (${#OBJS[@]} objects) =="
echo "  exit:                 $rc"
echo "  multiple definition:  $dups"
if [[ $rc -ne 0 && $dups -eq 0 ]]; then
    echo "  first few errors:"
    grep -vE "^$" "$OUT/$CONFIG"-*.err | head -5 | sed 's/^/    /'
fi

if [[ $dups -gt 0 ]]; then
    echo
    echo "  the symbols defined twice, and by which pair of files:"
    grep -B0 -A0 "multiple definition" "$OUT/$CONFIG"-*.err \
        | sed 's/.*multiple definition of `\([^'"'"']*\)'"'"'; \(.*\):.*/\1\t\2/' \
        | sort -u | head -20 | sed 's/^/    /'
    echo "    ($dups in total)"
fi

if [[ -s $TARGET ]]; then
    def=$("$NM" --defined-only "$TARGET" 2>/dev/null | wc -l)
    undef_file=$OUT/$CONFIG-undefined.txt
    "$NM" --undefined-only "$TARGET" 2>/dev/null | awk '{print $NF}' | sort -u > "$undef_file"
    undef=$(wc -l < "$undef_file")
    rt=$(grep -cE '^(__aeabi_|__div|__udiv|__mod|__umod|__mul|__clz|__float|__fix|__trunc|__extend|_Unwind|__stack_chk)' "$undef_file" || true)

    echo
    echo "  image:                $(stat -c%s "$TARGET") bytes on disk"
    echo "  symbols defined:      $def"
    echo "  undefined after linking: $undef"
    echo "    compiler runtime:        $rt"
    echo "    a source file must provide: $((undef - rt))"
    echo "  full list: $undef_file"

    if [[ $FINAL -eq 1 ]]; then
        echo
        echo "  == section sizes =="
        arm-none-eabi-size -A "$TARGET" 2>/dev/null | head -12 | sed 's/^/    /'
    fi
fi

echo
echo "artifacts in $OUT/"
exit 0
