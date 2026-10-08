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

# Every out-root follows the selected tree (experiment 933): the rule lives once in
# tools/xnu_tree_roots.sh, so `XNU_TREE=<d13>` reads and writes the `_d13` roots and a plain run is
# byte-identical on 4570. See that file.
. "$TOOLS_DIR/xnu_tree_roots.sh"

XNU=$XNU_TREE
SHIMS=$REPO_ROOT/src/shims
SHIMS_ARM=$REPO_ROOT/src/shims_arm
CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}
# The configuration's own options, which this script did not use to pass at all - see
# `tools/xnu_config/arm_asm_defines.sh` for what that cost and for the one exception. The toolchain
# flags below are the assembler's; these are the kernel's.
# **488: read once, with the status seen.** The form this replaces was `done < <(...)`, and a process
# substitution hides its command's failure: when the expansion fails, the loop runs zero times, the
# script assembles every file with none of the configuration's options, and nothing says so. That is
# exactly the defect 488 chased into `locore.o` - the object that stopped the boot - so both consumers
# of this list now fail loudly instead of quietly getting `CONFIG_DEFINES=()`.
CONFIG_DEFINES=()
if ! defines=$("$REPO_ROOT/tools/xnu_config/arm_asm_defines.sh" "$CONFIG"); then
    echo "assemble_arm_layer.sh: arm_asm_defines.sh failed for $CONFIG" >&2
    exit 1
fi
if [[ -z $defines ]]; then
    echo "assemble_arm_layer.sh: $CONFIG expanded to no options - refusing to assemble without them" >&2
    exit 1
fi
while IFS= read -r d; do
    [[ -n $d ]] && CONFIG_DEFINES+=("$d")
done <<<"$defines"
OUT=$XNU_ASM_OBJ_OUT
# **519: where the idle thread runs.** `cswitch.s`'s `Idle_context` takes the idle thread's stack from
# `cpu_data->istackptr` - the same field the exception vectors read to place a handler's stack - so the
# idle thread and every handler share one stack, and the handler's 5th and 6th pushed words land on the
# idle code's own saved return address (experiment 518's run caught the idle exit popping one of them
# into the PC). Moving the field cannot fix that, because both readers read that one field; the idle
# thread needs a stack of its own, and `Idle_context` is the one place that can be said. The patch is
# `tools/patch_idle_stack.py`, bounded to `Idle_context` (`Shutdown_context` keeps the original two
# instructions), and it is a *semantic* change rather than a dialect translation - so it is its own
# switch, it is reported, and a source it does not match stops the build.
#
# The size is spelled here and in `entry_stubs.c` (the array) and the build compares the two against the
# object's own immediate, because a mismatch would be an array that does not cover the region the idle
# thread actually uses.
#
# **The default reads the tree, not this project.** The collision the patch fixes is a property of a
# `cswitch.s` whose `Idle_context` borrows `cpu_data->istackptr`; a tree with no `CPU_ISTACKPTR`
# anywhere has no interrupt stack to borrow from, and its `cswitch.s` has no `Idle_context` for the
# patch to match. Darwin-13 is that tree: it transplants a context-switch scheme that runs the idle
# thread on its own `TH_PCB_ISS` (the idle thread is an ordinary `kernel_thread_create` thread). So the
# default is `1` for the 4570 line and `0` for D13, decided by the tree rather than by a hand-set
# value that would silently make the build stop on "did not match Idle_context".
if grep -rq 'CPU_ISTACKPTR' "$XNU/osfmk/arm" 2>/dev/null; then
    IDLE_STACK_DEFAULT=1
else
    IDLE_STACK_DEFAULT=0
fi
IDLE_STACK=${STAGE90_XNU_IDLE_STACK:-$IDLE_STACK_DEFAULT}
IDLE_STACK_SIZE=${STAGE90_XNU_IDLE_STACK_SIZE:-16384}
case "$IDLE_STACK" in
    0|1) ;;
    *) echo "STAGE90_XNU_IDLE_STACK must be 0 or 1, not [$IDLE_STACK]" >&2; exit 1 ;;
esac
case "$IDLE_STACK_SIZE" in
    ''|*[!0-9]*) echo "STAGE90_XNU_IDLE_STACK_SIZE must be a decimal byte count, not [$IDLE_STACK_SIZE]" >&2; exit 1 ;;
esac
PATCH_IDLE_STACK=$TOOLS_DIR/patch_idle_stack.py
idle_patched=0
ASSYM=$XNU_ASSYM_OUT/$CONFIG
OPTION_HEADERS=$XNU_OPTION_HEADERS_OUT/$CONFIG
DEVICE_HEADERS=$XNU_DEVICE_HEADERS_OUT/$CONFIG
# 933 left this out: the INCLUDES below still spell `$GENERATED` while the rules file exports
# `XNU_GENERATED`, so a plain run died on `GENERATED: unbound variable` under `set -u`. The two names
# are the same root; this is the one-line assignment its two sibling builders (`build_xnu_arm_kernel.sh`
# `:72`, `build_xnu_arm_layer.sh` `:43`) already had.
GENERATED=$XNU_GENERATED
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
# payload's `external_clean` gate — `scripts/xnu_compile_graph_scan.py` runs `git status` in
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
# 519's size, for `patch_idle_stack.py`'s replacement text. It is appended here and not above because
# the array is assigned, not extended, further down the file - an `ASFLAGS+=(...)` before this line is
# discarded by this line, which the first version of this change did.
ASFLAGS+=("-DSTAGE90_IDLE_STACK_SIZE=$IDLE_STACK_SIZE")
INCLUDES=(
    -I"$ASSYM" -I"$REPO_ROOT/src/entry"
    -I"$OPTION_HEADERS" -I"$DEVICE_HEADERS"
    -I"$GENERATED" -I"$MIG_HEADERS"
    -I"$XNU/osfmk" -I"$XNU/bsd" -I"$XNU/libkern" -I"$XNU/EXTERNAL_HEADERS"
    -I"$XNU/pexpert" -I"$XNU/osfmk/arm" -I"$XNU/bsd/arm" -I"$XNU"
    -I"$SHIMS" -I"$SHIMS/kern" -I"$SHIMS/mach"
    -I"$SHIMS_ARM" -I"$SHIMS_ARM/kern" -I"$SHIMS_ARM/mach"
)

# The translation is its own tool: it is a parser over `.macro` blocks rather than a sed, and it
# reports how many substitutions it made so a file needing none is used unchanged.
TRANSLATE=$TOOLS_DIR/translate_arm_asm.py

# **934: an object in the pool that the *selected manifest* does not name is not an object of this
# layer, and it must be removed rather than left.** The pool directory is per-tree (`_d13`), so a run
# against a tree does not mix trees - but a *manifest* change does not remove the previous run's
# products, and the consumer is a glob: `build_entry.sh`'s 436 step adopts every `*.o` in
# `$XNU_ASM_OBJ_OUT` (930), gated only on a `.log` sitting beside it. So a manifest that was once
# built against the wrong tree, or an object assembled by hand, survives every later run and is
# linked. Measured cost on this D13 build: `out/xnu_asm_obj_d13/` held `strncmp.o`/`strnlen.o`/
# `strlen.o` - the 4570 `osfmk/arm/*.s` files, which D13 does not have (its `subrs.c` and
# `loose_ends.c` define `strncmp`/`strnlen`/`strlen` in C, so the `.s` copies are duplicate
# definitions) - left by an earlier run into the same root. The three survived the manifest switch,
# passed the `.log` gate, and produced pass 1's `multiple definition of 'strncmp'`.
#
# Pruning the pool to the manifest's own set is the one rule that makes "the pool" mean "what this
# manifest builds", independently of what any earlier configuration left behind. `.log` files are
# removed with the `.o` (the entry build keys on their existence), and the `translated/` mirror is
# left alone because it is keyed by path and re-materialized as needed.
pruned=0
kept=()
while IFS= read -r src; do
    case "$src" in *.s|*.S) ;; *) continue ;; esac
    kept+=("$(basename "$src" .s)")
done < "$MANIFEST"
for _o in "$OUT"/*.o; do
    [[ -e $_o ]] || continue
    _n=$(basename "$_o" .o)
    _know=0
    for _k in "${kept[@]}"; do [[ $_k == "$_n" ]] && { _know=1; break; }; done
    if [[ $_know -eq 0 ]]; then
        rm -f "$_o" "${_o%.o}.log"
        pruned=$((pruned + 1))
    fi
done
[[ $pruned -gt 0 ]] && echo "  pruned $pruned object(s) not named by $(basename "$MANIFEST")"

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
    if [[ $IDLE_STACK -eq 1 && $name == cswitch ]]; then
        # The patch runs on whichever text is about to be assembled (translated or the tree's own) and
        # writes to the mirror, and a success cannot then be assembled from the wrong file.
        #
        # **What keeps this from writing the tree is `patch_idle_stack.py`'s own `os.replace`, and not the
        # fact that it writes into `$OUT`.** That mirror is *directories that are real and files that are
        # symlinks into the tree*, so `$OUT/translated/cswitch.s` is a symlink to
        # `external/xnu-4570.1.46/osfmk/arm/cswitch.s` whenever the translate step has not replaced it,
        # and an `open(..., 'w')` on it writes the tree. The first version of this comment claimed a
        # failure here could not leave the tree changed; the first run of the tool did exactly that, and
        # the file was restored from git. The translate step above is safe by the same mechanism (`mv -f`
        # renames over the symlink), which is the mechanism this one now uses too.
        #
        # `--count-only` first, so a source the patch does not match is reported as the build's failure
        # rather than as a compiler error about `LOAD_ADDR`.
        if [[ $("$PATCH_IDLE_STACK" "$use" --count-only) != 1 ]]; then
            fail=$((fail + 1))
            printf '  FAIL %-24s %s\n' "$name" "patch_idle_stack did not match Idle_context"
            continue
        fi
        materialize_for "$rel"
        "$PATCH_IDLE_STACK" "$use" "$OUT/translated/$rel" || { fail=$((fail + 1)); continue; }
        use=$OUT/translated/$rel
        idle_patched=$((idle_patched + 1))
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
    #
    # **926: this covers the object's UNDEFINED references too, not just its definitions.** A D13 `.s`
    # file does not only define `_x` names it then calls; it also *refers to* C symbols the C build
    # emits unprefixed, written with Apple's underscore (`.extern _sleh_abort`, `bl _panic`). Renaming
    # only the definitions left `traps_lo.o` with `U _sleh_abort` against `trap.o`'s `T sleh_abort`,
    # `cache.o` with `U _arm_dcache_inv_all` against `cpufunc-v7.o`'s `T arm_dcache_inv_all`, and 12
    # more - visible only when those objects are pulled into the entry closure, which is why the
    # defect surfaced at the D13 link and not at the assembly step. An undefined symbol carries its
    # own name, so `--redefine-sym` fixes the reference with no change to what is defined. Same rule,
    # both directions. (`nm` prints the type a `--defined-only` listing would suppress, so one pass.)
    args=()
    while IFS= read -r sym; do
        [[ $sym == "_start" ]] && continue
        args+=(--redefine-sym "$sym=${sym#_}")
    done < <("$NM" "$OUT/$name.o" 2>/dev/null | awk '($1 == "U" && $2 ~ /^_[a-zA-Z]/) || ($2 ~ /^[TDBR]$/ && $3 ~ /^_[a-zA-Z]/) { print ($1 == "U") ? $2 : $3 }' | sort -u)
    if [[ ${#args[@]} -gt 0 ]]; then
        "$OBJCOPY" "${args[@]}" "$OUT/$name.o"
        renamed=$((renamed + ${#args[@]}))
    fi
done < "$MANIFEST"

echo
echo "assemble: $ok ok, $fail failed; $translated file(s) translated, $renamed symbol(s) de-underscored"
echo "          519: STAGE90_XNU_IDLE_STACK=$IDLE_STACK, size $IDLE_STACK_SIZE; Idle_context patched into $idle_patched object(s) (expected $IDLE_STACK)"
echo "objects in $OUT"
