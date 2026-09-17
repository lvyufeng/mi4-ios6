#!/usr/bin/env bash
#
# Measure the link gap: which symbols the compiled objects need and none of them defines.
#
#   ./tools/link_gap.sh                      # summary + the missing set, grouped
#   ./tools/link_gap.sh --list 60            # the first 60 missing symbols
#   ./tools/link_gap.sh --attribute          # map each missing symbol to the file that defines it
#
# Why this and not a failure count. `build_xnu_arm_kernel.sh` reports "88 of 587 do not compile",
# which says nothing about *what is missing* - and most of the manifest's 587 files do not matter for
# a given symbol. What matters is the other direction: take every symbol the compiled objects
# reference and do not define, subtract every symbol any of them defines, and the remainder is
# exactly the set a link would fail on. Each remaining symbol is then attributable: either to a file
# that fails to compile (a known, measurable piece of work) or to something no file in the manifest
# provides (a compiler runtime, a shim, or a file outside the manifest).
#
# This is the measurement `build_xnu_arm_layer.sh --undefined` does for one directory
# (443 symbols for osfmk/arm alone). This does it for the whole compiled kernel.
#
# It needs no linker: nm on the objects is enough, and it is the same question. Apple's kernel links
# with `ld -kext` into Mach-O; this works on the ELF objects clang emits here, and the *symbol* view
# is identical either way.

set -uo pipefail
cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
NM=${NM:-arm-none-eabi-nm}
FULL=${XNU_KERNEL_OBJ_OUT:-$REPO_ROOT/out/xnu_kernel_obj}
ARM=${XNU_ARM_OBJ_OUT:-$REPO_ROOT/out/xnu_arm_obj}
MANIFEST=${MANIFEST:-$REPO_ROOT/out/xnu_arm_manifest.txt}
OUT=${LINK_GAP_OUT:-$REPO_ROOT/out/link_gap}

SHOW_LIST=25
ATTRIBUTE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --list)      SHOW_LIST=$2; shift 2 ;;
        --attribute) ATTRIBUTE=1; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

command -v "$NM" >/dev/null 2>&1 || { echo "$NM is required" >&2; exit 2; }

mapfile -t OBJS < <(ls "$FULL"/*.o "$ARM"/*.o 2>/dev/null)
[[ ${#OBJS[@]} -gt 0 ]] || { echo "no objects in $FULL or $ARM" >&2; exit 2; }

mkdir -p "$OUT"

# Defined: every symbol any object provides. Weak symbols count as defined - the link resolves them.
"$NM" --defined-only "${OBJS[@]}" 2>/dev/null \
    | awk '$2 ~ /^[A-Za-z]$/ { print $3 }' | sort -u > "$OUT/defined.txt"
# Undefined: every symbol any object references and does not define itself.
"$NM" --undefined-only "${OBJS[@]}" 2>/dev/null \
    | awk 'NF >= 2 { print $NF }' | sort -u > "$OUT/undefined.txt"

comm -23 "$OUT/undefined.txt" "$OUT/defined.txt" > "$OUT/missing.txt"

n_objs=${#OBJS[@]}
n_def=$(wc -l < "$OUT/defined.txt")
n_und=$(wc -l < "$OUT/undefined.txt")
n_mis=$(wc -l < "$OUT/missing.txt")

# The compiler runtime is not a source problem and must not be counted as one. clang emits calls to
# the ARM EABI helpers for 64-bit division, soft-float conversion and bulk memory operations; they
# come from libgcc/compiler-rt at link time and from nowhere in XNU.
grep -E '^(__aeabi_|__div|__udiv|__mod|__umod|__mul|__clz|__float|__fix|__trunc|__extend|_Unwind|__stack_chk)' \
     "$OUT/missing.txt" > "$OUT/compiler_rt.txt" || true
comm -23 "$OUT/missing.txt" "$OUT/compiler_rt.txt" > "$OUT/missing_source.txt"
n_rt=$(wc -l < "$OUT/compiler_rt.txt")
n_ms=$(wc -l < "$OUT/missing_source.txt")

echo "== the link gap =="
echo "  objects:                       $n_objs"
echo "  symbols defined:               $n_def"
echo "  symbols referenced, undefined: $n_und"
echo "  MISSING (referenced by every, defined by none): $n_mis"
echo "    of which compiler runtime (libgcc/compiler-rt): $n_rt"
echo "    of which a source file must provide:            $n_ms"
echo

# Attribute each missing symbol to a file, if the tree defines it. This is what turns the number
# into a work list: the files that appear are the ones whose compilation unblocks the most symbols.
if [[ $ATTRIBUTE -eq 1 ]]; then
    echo "== attributing $n_ms symbols to files (grep over the tree; slow) =="
    : > "$OUT/attribution.txt"
    : > "$OUT/by_file.txt"
    # One pass over the tree is much cheaper than one pass per symbol.
    declare -A DEFINES_SYMBOL=()
    while IFS= read -r sym; do
        # A definition looks like a C function definition, a data definition, or a macro-free
        # declaration in a header. Searching for the bare name at a definition site:
        #   ^<type> name(   |  ^name(  |  } name;  |  name = …;   |  asm label
        hit=$(grep -rlnE "(^|[^A-Za-z0-9_])${sym}([[:space:]]*\(|[[:space:]]*=[^=]|[[:space:]]*;)" \
                "$XNU"/osfmk "$XNU"/bsd "$XNU"/libkern "$XNU"/iokit "$XNU"/pexpert "$XNU"/security \
                --include=*.c --include=*.s 2>/dev/null | head -3)
        [[ -z $hit ]] && continue
        for f in $hit; do
            printf '%s\t%s\n' "$sym" "${f#"$XNU"/}" >> "$OUT/attribution.txt"
        done
    done < "$OUT/missing_source.txt"

    awk -F'\t' '{print $2}' "$OUT/attribution.txt" | sort | uniq -c | sort -rn > "$OUT/by_file.txt"
    echo "  attributable to a source file in the tree: $(wc -l < "$OUT/attribution.txt") of $n_ms"
    echo
    echo "== the files that would close the most missing symbols =="
    head -25 "$OUT/by_file.txt" | awk '{printf "  %-4s %s\n", $1, $2}'
    echo
fi

echo "== the missing set, first $SHOW_LIST =="
head -"$SHOW_LIST" "$OUT/missing_source.txt" | sed 's/^/  /'
echo
echo "  full lists in $OUT/"
