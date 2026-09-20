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
# The configuration's own options, which this script did not use to pass at all - see
# `tools/xnu_config/arm_asm_defines.sh` for what that cost and for the one exception. The toolchain
# flags below are the assembler's; these are the kernel's.
CONFIG_DEFINES=()
while IFS= read -r d; do
    [[ -n $d ]] && CONFIG_DEFINES+=("$d")
done < <("$REPO_ROOT/tools/xnu_config/arm_asm_defines.sh" "$CONFIG")
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
# The translated copies live in a mirror of the tree, because XNU's assembly uses *relative*
# includes — `bsd/dev/arm/cpu_in_cksum.s:50` is `#include "../../../osfmk/arm/arch.h"` — so a file
# has to keep its position under the tree root for them to resolve.
#
# **And the mirror must not be the tree.** The first version symlinked each top-level component in
# (`ln -sfn "$XNU/$d" "$OUT/translated/$d"`) and then wrote the translated text to
# `$OUT/translated/<path>`. Through the symlink that is `$XNU/<path>`: **every translated copy was a
# write into Apple's source**, four files were rewritten in place on every run of this script, and
# the tool's own comment said "the tree is never written to" while it did exactly that. Found by the
# payload's `external_clean` gate — `stages/stage90/xnu_compile_graph_scan.py` runs `git status` in
# `external/xnu-4570.1.46` and had been failing `stage90_xnu_compile_graph_no_external_mutation` on a
# checkout this project believes it never touches. See experiment-155.
#
# So the mirror is built the other way round: **directories are real, files are symlinks into the
# tree**, and only the directories on the path to a file that actually needs translating stop being
# symlinks. Nothing here can write to the tree even if the translation changes, because no path this
# script opens for writing passes through a symlink.
materialize_dir() {   # $1 = relative directory, "" for the tree root
    local rel=$1 src dst e b
    src=$XNU${rel:+/$rel}
    dst=$OUT/translated${rel:+/$rel}
    [[ -L $dst ]] && rm -f "$dst"
    mkdir -p "$dst"
    for e in "$src"/*; do
        [[ -e $e ]] || continue
        b=$(basename "$e")
        [[ -e $dst/$b || -L $dst/$b ]] && continue
        ln -sfn "$e" "$dst/$b"
    done
}

materialize_for() {   # $1 = relative path of a file: make every directory above it real
    local rel=$1 p cur=""
    local -a parts=()
    IFS=/ read -r -a parts <<< "$(dirname "$rel")"
    materialize_dir ""
    for p in "${parts[@]}"; do
        [[ $p == . ]] && continue
        cur=${cur:+$cur/}$p
        materialize_dir "$cur"
    done
}

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
    # Translated to a scratch file first, and only moved into the mirror when it needed translating:
    # making a directory real is the one thing here with a cost, and there is no reason to pay it for
    # a file that will be assembled from the tree unchanged.
    use=$src
    n=$("$TRANSLATE" "$src" "$OUT/translated.tmp")
    if [[ $n -gt 0 ]]; then
        materialize_for "$rel"
        mv -f "$OUT/translated.tmp" "$OUT/translated/$rel"
        use=$OUT/translated/$rel
        translated=$((translated + 1))
    fi
    if ! clang "${ASFLAGS[@]}" "${INCLUDES[@]}" "${CONFIG_DEFINES[@]}" -c "$use" -o "$OUT/$name.o" 2>"$OUT/$name.log"; then
        fail=$((fail + 1))
        printf '  FAIL %-24s %s\n' "$name" \
            "$(grep -m1 -aE 'error|fatal' "$OUT/$name.log" | sed 's|.*xnu-4570.1.46/||' | cut -c1-64)"
        continue
    fi
    ok=$((ok + 1))
    [[ $SYNTAX_ONLY -eq 1 ]] && continue

    # De-underscore. `asm.h`:88-98 gives two conventions behind one flag —
    #
    #     #ifndef __NO_UNDERSCORES__
    #     #define EXT(x)  _ ## x      Darwin: a C symbol `bcopy` is `_bcopy` in assembly
    #     #else
    #     #define EXT(x)  x           ELF:    it is `bcopy` in both
    #     #endif
    #
    # — and this build sets `__NO_UNDERSCORES__`, because the toolchain is ELF. **But ten of the
    # manifest's assembly files do not use `EXT()`**: they write `_bcopy:` literally. So under this
    # flag every `_x` they define is one underscore too many.
    #
    # The rule is therefore the flag's own: rename `_x` to `x`, except `_start`, which the linker
    # script enters at by that exact name.
    #
    # **The first version keyed this on the linker's undefined list instead, and that was wrong in a
    # way worth recording**: it made the assembly step depend on the *previous* `measure_link.sh`
    # run, so on a clean tree the list was stale and only 2 of the 22 symbols were renamed - and on
    # the *next* run the same command renamed 22. A build step whose result depends on how many times
    # it has been run is the project's oldest defect. The exclusion is now a name, not a list.
    args=()
    while IFS= read -r sym; do
        [[ $sym == "_start" ]] && continue
        args+=(--redefine-sym "$sym=${sym#_}")
    done < <("$NM" --defined-only "$OUT/$name.o" 2>/dev/null | awk '$2 ~ /^[TDBR]$/ && $3 ~ /^_[a-zA-Z]/ {print $3}')
    if [[ ${#args[@]} -gt 0 ]]; then
        "$OBJCOPY" "${args[@]}" "$OUT/$name.o"
        renamed=$((renamed + ${#args[@]}))
    fi
done < "$MANIFEST"

echo
echo "assemble: $ok ok, $fail failed; $translated file(s) translated, $renamed symbol(s) de-underscored"
echo "objects in $OUT"
