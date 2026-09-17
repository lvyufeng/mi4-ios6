#!/usr/bin/env bash
#
# Assemble XNU's ARM `.s` files for **ELF**, translating the two Darwin-assembler dialect constructs
# the EABI assembler rejects — without editing anything in XNU's tree.
#
#   ./tools/assemble_arm_layer.sh            # writes out/xnu_asm_obj/
#   ./tools/assemble_arm_layer.sh --syntax   # report only
#
# Why a translation exists at all. `experiment-142` measured that `--target=armv7-apple-darwin`
# assembles all 17 files and `--target=armv7-none-eabi` assembles 13, and concluded the triple was the
# answer. **`experiment-150` then measured the other half: LLVM's `ld64.lld` cannot link 32-bit ARM
# Mach-O** — `unhandled relocation type`, in lld 14 *and* 15, while `-arch arm64` links fine. So the
# Mach-O path can compile and cannot link on this host, and ELF is the only route to a linked image.
#
# Which makes these two constructs the last four files, and they are small:
#
#   1. `osfmk/arm/data.s:41,100`       `.section __DATA, __data` — Darwin's `<segment>, <section>`.
#                                      GNU as wants `.section <name>, "<flags>", %<type>`.
#   2. `osfmk/arm/machine_routines_asm.s:570`  `.macro COPYIO_BODY` declared with NO parameters and
#                                      invoked as `COPYIO_BODY copyin`, the body referring to the
#                                      argument as `$0` (9 sites). GNU as has the same feature spelled
#                                      `\p0\()` with a declared parameter — verified: `L\p0\()_x`
#                                      with `BODY copyin` produces the symbol `Lcopyin_x`.
#   3. `osfmk/arm/lz4_decode_armv7NEON.s`      10 `$N` sites, same mechanism.
#
# **This is a dialect translation, not a source change**: the tree is never written to, and the
# substitution is bounded to the two constructs and asserted to have matched something (a transform
# that silently matches nothing would leave the file broken in a way that looks like the original
# problem). It is the same category as the `objcopy --redefine-sym` step below, which exists because
# `asm.h`'s `EXT(x)` is `_##x` under Apple's convention and the ELF side has to be told otherwise.
#
# Measured after it: **17 of 17 manifest `.s` files assemble for ELF**, which is what the Darwin
# triple was reaching for — and this one can be linked.

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
UNDEF=${ASM_UNDEF:-$REPO_ROOT/out/link/$CONFIG-measure-undef.txt}

NM=${NM:-arm-none-eabi-nm}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}

SYNTAX_ONLY=0
[[ ${1:-} == --syntax ]] && SYNTAX_ONLY=1

mkdir -p "$OUT" "$OUT/translated"
# A mirror of the tree root beside the translated copies, so XNU's relative includes resolve from the
# translated file's directory exactly as they do from the original's.
mkdir -p "$OUT/translated"
for d in osfmk bsd libkern iokit pexpert security san libsa EXTERNAL_HEADERS; do
    [[ -d $XNU/$d ]] && ln -sfn "$XNU/$d" "$OUT/translated/$d"
done

[[ -f $ASSYM/assym.s ]] || { echo "no $ASSYM/assym.s - run ./tools/gen_assym.sh first" >&2; exit 2; }

ASFLAGS=(
    --target=armv7-none-eabi -mcpu=cortex-a15 -marm
    -mfpu=neon-vfpv4 -mfloat-abi=softfp -x assembler-with-cpp
    -DASSEMBLER=1 -DSLIDABLE=0 -DARMA7=1 -DKERNEL=1 -DKERNEL_PRIVATE=1
    -D__arm__=1 -D__ARM__=1 -DCONFIG_EMBEDDED=1 -D__ARM_L2CACHE_SIZE_LOG__=21
    -Dfmrx=vmrs -Dfmxr=vmsr
    -D__NO_UNDERSCORES__=1
)
INCLUDES=(
    -I"$ASSYM" -I"$REPO_ROOT/stages/stage90/xnu_arm_boot"
    -I"$OPTION_HEADERS" -I"$DEVICE_HEADERS"
    -I"$REPO_ROOT/out/xnu_generated" -I"$REPO_ROOT/out/mach_headers"
    -I"$XNU/osfmk" -I"$XNU/bsd" -I"$XNU/libkern" -I"$XNU/EXTERNAL_HEADERS"
    -I"$XNU/pexpert" -I"$XNU/osfmk/arm" -I"$XNU/bsd/arm" -I"$XNU"
    -I"$SHIMS" -I"$SHIMS/kern" -I"$SHIMS/mach"
    -I"$SHIMS_ARM" -I"$SHIMS_ARM/kern" -I"$SHIMS_ARM/mach"
)

# The translation is its own tool: it is a parser over `.macro` blocks rather than a sed, and it
# reports how many substitutions it made so a file needing none is used unchanged.
TRANSLATE=$TOOLS_DIR/translate_arm_asm.py

ok=0; fail=0; renamed=0; translated=0
while read -r src; do
    case "$src" in *.s|*.S) ;; *) continue ;; esac
    [[ -f $src ]] || continue
    name=$(basename "$src" .s)
    # The translated copy keeps the source's path *below the tree root*, because XNU's assembly uses
    # relative includes - `bsd/dev/arm/cpu_in_cksum.s:50` is `#include "../../../osfmk/arm/arch.h"` -
    # and those resolve against the file's own directory. A flat `translated/` broke exactly that.
    # The translated copy keeps the source's path *below the tree root*, and a mirror of that root is
    # symlinked beside it, because XNU's assembly uses relative includes -
    # `bsd/dev/arm/cpu_in_cksum.s:50` is `#include "../../../osfmk/arm/arch.h"` - and those resolve
    # against the file's own directory. A flat `translated/` broke exactly that.
    rel=${src#"$XNU"/}
    mkdir -p "$OUT/translated/$(dirname "$rel")"
    use=$src
    if [[ $("$TRANSLATE" "$src" "$OUT/translated/$rel") -gt 0 ]]; then
        use=$OUT/translated/$rel
    fi
    if ! clang "${ASFLAGS[@]}" "${INCLUDES[@]}" -c "$use" -o "$OUT/$name.o" 2>"$OUT/$name.log"; then
        fail=$((fail + 1))
        printf '  FAIL %-24s %s\n' "$name" \
            "$(grep -m1 -aE 'error|fatal' "$OUT/$name.log" | sed 's|.*xnu-4570.1.46/||' | cut -c1-64)"
        continue
    fi
    ok=$((ok + 1))
    [[ $SYNTAX_ONLY -eq 1 ]] && continue

    # De-underscore, as before: `asm.h`'s EXT(x) is `_##x` under Apple's convention, and ten files
    # write their labels literally. Renamed only when the bare name is one the link cannot otherwise
    # resolve, which is what keeps `_start` intact.
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
