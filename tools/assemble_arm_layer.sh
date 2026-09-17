#!/usr/bin/env bash
#
# Strip the leading underscore from assembled symbols that C code refers to without one.
#
#   ./tools/assemble_arm_layer.sh            # assemble osfmk/arm's .s into out/xnu_asm_obj/
#   ./tools/assemble_arm_layer.sh --syntax   # try every manifest .s and report, writing nothing
#
# Why this exists. `osfmk/arm/asm.h:88-98` gives two conventions behind one flag:
#
#   #ifndef __NO_UNDERSCORES__
#   #define EXT(x)  _ ## x        Darwin: a C symbol `bcopy` is `_bcopy` in assembly
#   #else
#   #define EXT(x)  x            ELF:    it is `bcopy` in both
#   #endif
#
# and this project assembles with `-D__NO_UNDERSCORES__=1` because the toolchain is
# `armv7-none-eabi` (ELF), where clang emits `bcopy` for `void bcopy(void)`. **But ten of the
# manifest's assembly files do not use `EXT()`** — they write their labels literally, with the
# underscore Apple's convention expects:
#
#   osfmk/arm/bcopy.s:40      _bcopy:      /* void bcopy(const void *src, void *dest, size_t len); */
#   osfmk/arm/bcopy.s:38      .globl _memcpy
#
# So those objects assemble cleanly and define the *wrong names* — `_bcopy` where everything else
# wants `bcopy` — and the failure appears at link time as "undefined reference to `bcopy'" from
# files that plainly have an implementation. That is the same one-value-two-definitions shape this
# project keeps meeting, in the assembler's own convention.
#
# What this does is apply the flag's own intent to the files that do not use the macro: for each
# assembled object, rename `_x` to `x` **when `x` is a symbol the link cannot otherwise resolve**.
# The condition is what keeps it safe — `start.s` defines `_start`, `start` is not undefined, so
# `_start` is left alone. Stripping unconditionally turned the entry point into `start` and the link
# lost its entry.
#
# This is not a source change: XNU's files are untouched, and the rename is a property of the
# object, which is what `EXT()` is for in the first place.

set -uo pipefail
cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
SHIMS=$REPO_ROOT/stages/stage90/shims
SHIMS_ARM=$REPO_ROOT/stages/stage90/shims_arm
CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}
OUT=${XNU_ASM_OBJ:-$REPO_ROOT/out/xnu_asm_obj}
ASSYM=${XNU_ASSYM_OUT:-$REPO_ROOT/out/xnu_assym}/$CONFIG
OPTION_HEADERS=${XNU_OPTION_HEADERS_OUT:-$REPO_ROOT/out/xnu_options}/$CONFIG
DEVICE_HEADERS=${XNU_DEVICE_HEADERS_OUT:-$REPO_ROOT/out/xnu_device}/$CONFIG
MANIFEST=${MANIFEST:-$REPO_ROOT/out/xnu_arm_manifest.txt}
UNDEF=${ASM_UNDEF:-$REPO_ROOT/out/link/RELEASE-measure-undef.txt}

NM=${NM:-arm-none-eabi-nm}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}

SYNTAX_ONLY=0
[[ ${1:-} == --syntax ]] && SYNTAX_ONLY=1

mkdir -p "$OUT"

[[ -f $ASSYM/assym.s ]] || { echo "no $ASSYM/assym.s - run ./tools/gen_assym.sh first" >&2; exit 2; }

ASFLAGS=(
    --target=armv7-none-eabi -mcpu=cortex-a15 -marm
    -mfpu=neon-vfpv4 -mfloat-abi=softfp -x assembler-with-cpp
    -DASSEMBLER=1 -DSLIDABLE=0 -DARMA7=1 -DKERNEL=1 -DKERNEL_PRIVATE=1
    -D__arm__=1 -DCONFIG_EMBEDDED=1 -D__ARM_L2CACHE_SIZE_LOG__=21
    -Dfmrx=vmrs -Dfmxr=vmsr
    -D__NO_UNDERSCORES__=1
)
INCLUDES=(
    -I"$ASSYM"
    -I"$REPO_ROOT/stages/stage90/xnu_arm_boot"
    -I"$OPTION_HEADERS" -I"$DEVICE_HEADERS"
    -I"$REPO_ROOT/out/xnu_generated" -I"$REPO_ROOT/out/mach_headers"
    -I"$XNU/osfmk" -I"$XNU/bsd" -I"$XNU/libkern" -I"$XNU/EXTERNAL_HEADERS"
    -I"$XNU/pexpert" -I"$XNU/osfmk/arm" -I"$XNU/bsd/arm" -I"$XNU"
    -I"$SHIMS" -I"$SHIMS/kern" -I"$SHIMS/mach"
    -I"$SHIMS_ARM" -I"$SHIMS_ARM/kern" -I"$SHIMS_ARM/mach"
)

ok=0; fail=0; renamed=0
while read -r src; do
    case "$src" in *.s|*.S) ;; *) continue ;; esac
    [[ -f $src ]] || continue
    name=$(basename "$src" .s)
    if ! clang "${ASFLAGS[@]}" "${INCLUDES[@]}" -c "$src" -o "$OUT/$name.o" 2>"$OUT/$name.log"; then
        fail=$((fail + 1))
        [[ $SYNTAX_ONLY -eq 1 ]] && printf '  FAIL %-24s %s\n' "$name" \
            "$(grep -m1 -aE 'error|fatal' "$OUT/$name.log" | sed 's|.*xnu-4570.1.46/||' | cut -c1-64)"
        continue
    fi
    ok=$((ok + 1))

    # The rename, conditioned on the link needing the bare name.
    [[ -s $UNDEF ]] || continue
    args=()
    while IFS= read -r sym; do
        bare=${sym#_}
        [[ $bare == "$sym" ]] && continue
        grep -qx "$bare" "$UNDEF" || continue
        args+=(--redefine-sym "$sym=$bare")
    done < <("$NM" --defined-only "$OUT/$name.o" 2>/dev/null | awk '$2 ~ /^[TDBR]$/ && $3 ~ /^_[a-zA-Z]/ {print $3}')
    if [[ ${#args[@]} -gt 0 ]]; then
        "$OBJCOPY" "${args[@]}" "$OUT/$name.o"
        renamed=$((renamed + ${#args[@]}))
    fi
done < "$MANIFEST"

echo
echo "assemble: $ok ok, $fail failed; $renamed symbol(s) de-underscored"
echo "objects in $OUT"
