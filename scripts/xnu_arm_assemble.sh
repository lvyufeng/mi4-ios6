#!/usr/bin/env bash
#
# Assemble XNU's ARM entry point — osfmk/arm/start.s — unmodified, and report what it defines and
# what it still needs. This is the Phase 4 measurement for the entry path.
#
#   ./xnu_arm_assemble.sh                 # assemble start.s, report symbols
#   ./xnu_arm_assemble.sh --locore        # try osfmk/arm/locore.s as well
#   ./xnu_arm_assemble.sh --verbose       # show the compiler command and any diagnostics
#
# Why this exists. The roadmap says "the source tree is complete and the build configuration is
# absent", which is true and unhelpful: it does not say which configuration, or how much of it. This
# script supplies the configuration for one target file and reports the result, so the answer is a
# symbol list instead of an adjective.
#
# Every flag below was found by assembling and reading the error, and each one is recorded with the
# reason it is what it is. Two are not obvious and cost the most time:
#
#   -DASSEMBLER=1     Without it, osfmk/arm/asm.h's entire macro block from line 160 onward is
#                     preprocessed away, so LOAD_ADDR, EXT, LEXT and the LOAD_ADDR_GEN_DEF
#                     machinery do not exist and every use of them reads as a syntax error. That
#                     one flag was behind every "expected ')'" in the first attempt.
#   -Dfmrx=vmrs       Apple's assembler accepts the FPA-era mnemonics fmrx/fmxr for the VFP system
#   -Dfmxr=vmsr       registers; LLVM's does not, and wants vmrs/vmsr. A macro substitution does
#                     the translation without touching the source.
#
# clang is required rather than arm-none-eabi-gcc, and for a reason that is not about quality:
# EXTERNAL_HEADERS/stdatomic.h:24 is `#ifndef __clang__ / #error unsupported compiler`. GNU as also
# rejects asm.h's `.macro`/`.endmacro` (it wants `.endm`), so the C sources and the assembly want
# the same compiler.

set -uo pipefail

cd "$(dirname "$0")"
SCRIPT_DIR=$PWD
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
SRC_DIR=$REPO_ROOT/src

# **936: the tree follows the build, and the file that defines `_start` follows the tree.**
# This script hard-pinned `XNU=external/xnu-4570.1.46` and always assembled `osfmk/arm/start.s` into
# `out/stage90/xnu_arm_start.o` - the object the entry link takes `_start` from. On Darwin 13 that is
# doubly wrong: D13 **ships no `start.s`** (its `_start` is `osfmk/arm/locore.s`'s `EnterARM(_start)`),
# and the pinned 4570 `start.s` drags in a symbol D13 defines nowhere - `gPhysSize`, via 4570's
# `globals_asm.h`, which is `LOAD_ADDR_GEN_DEF`'d but never used by the `_start` path. The entry link
# then had a spurious undefined object with no safe stand-in. Sourcing `tools/xnu_tree_roots.sh` - the
# one place `XNU_TREE -> XNU_OBJ_SUFFIX -> every root` is written down (933) - makes `XNU` follow the
# build; `XNU_OBJ_SUFFIX` empty on 4570 makes this byte-identical there.
. "$REPO_ROOT/tools/xnu_tree_roots.sh"
XNU=$XNU_TREE
# The two trees put `_start` in different files and name it differently (`start.s`'s `_start` vs
# `locore.s`'s `_start`, which D13's `asm_help.h`'s `EnterARM` writes as the same ELF name). The
# discriminator is the pivot's own (`osfmk/sys/types.h`), and the entry file is chosen from it.
if [[ -f $XNU/osfmk/sys/types.h ]]; then
  ENTRY_SRC=$XNU/osfmk/arm/locore.s
  ENTRY_NAME=locore
  D13_ENTRY=1
else
  ENTRY_SRC=$XNU/osfmk/arm/start.s
  ENTRY_NAME=start
  D13_ENTRY=
fi
if [[ ! -f $ENTRY_SRC ]]; then
  echo "no $ENTRY_SRC - the tree at $XNU has no entry file for its kind" >&2
  exit 2
fi
ENTRY_OBJ_NAME=xnu_arm_start.o   # the name the entry link has always taken; kept so `LINK_OBJS` is not respelled

DO_LOCORE=0
VERBOSE=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --locore) DO_LOCORE=1; shift ;;
    --verbose) VERBOSE=1; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

OUT_DIR=$REPO_ROOT/out/stage90
mkdir -p "$OUT_DIR"

# The generated `OPTIONS/` headers for one configuration. `start.s` includes `<mach_kdp.h>`, which
# is one of the 91 headers `tools/gen_option_headers.py` generates - nothing in XNU's tree provides
# it, and neither did this script's include list, so the entry image could not be reassembled from
# the committed tree. (It was built once with copies of ten of them dropped in by hand, which is why
# the gap survived: the artifact existed, so nothing failed.)
#
# RELEASE by default because that is the configuration the kernel objects this image links against
# are built with; `XNU_KERNEL_CONFIG` overrides it, the same variable the kernel build reads.
CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}
# **466: the configuration's own options, which this script did not use to pass either.** `start.s`
# is assembled here and `locore.s` by `tools/assemble_arm_layer.sh`, and both were giving the
# assembler the toolchain flags only - so `#if CONFIG_TELEMETRY` and friends in the tree's `.s` files
# were false while the same macros were 1 in every C object. The one source for them is
# `tools/xnu_config/arm_asm_defines.sh`; see it for the measurements and for the one exception.
# **488: read once, with the status seen**, for the reason `tools/assemble_arm_layer.sh` records at its
# own copy of this loop: a process substitution hides the failure that matters most here.
CONFIG_DEFINES=()
if ! defines=$("$REPO_ROOT/tools/xnu_config/arm_asm_defines.sh" "$CONFIG"); then
    echo "xnu_arm_assemble.sh: arm_asm_defines.sh failed for $CONFIG" >&2
    exit 1
fi
if [[ -z $defines ]]; then
    echo "xnu_arm_assemble.sh: $CONFIG expanded to no options - refusing to assemble without them" >&2
    exit 1
fi
while IFS= read -r d; do
    [[ -n $d ]] && CONFIG_DEFINES+=("$d")
done <<<"$defines"
OPTION_HEADERS=${XNU_OPTION_HEADERS_OUT:-$REPO_ROOT/out/xnu_options}/$CONFIG
if [[ ! -f $OPTION_HEADERS/mach_kdp.h ]]; then
  echo "no $OPTION_HEADERS/mach_kdp.h - run ./tools/gen_option_headers.py first" >&2
  echo "(XNU_KERNEL_CONFIG=$CONFIG; start.s includes <mach_kdp.h> unconditionally)" >&2
  exit 2
fi

CC_CMD=(clang --target=armv7-none-eabi)
if ! command -v clang >/dev/null 2>&1; then
  echo "clang not found - it is required, see the header comment" >&2
  exit 2
fi

# -mfpu/-mfloat-abi: without these the assembler rejects the VFP system-register transfers in
#   start.s's `#if __ARM_VFP__` block with "instruction requires: VFP2". Cortex-A15 implies
#   NEON/VFPv4, which is what the part actually has.
# -D__ARM_L2CACHE_SIZE_LOG__: osfmk/arm/proc_reg.h's L2_CSIZE is this, and the tarball never
#   defines it - it is per-SoC. 21 is 2 MB, which is the Krait 400 L2 in MSM8974.
TARGET_FLAGS=(
  -mcpu=cortex-a15 -marm -mfpu=neon-vfpv4 -mfloat-abi=softfp
  -x assembler-with-cpp
)

DEFINES=(
  -DASSEMBLER=1
  -DSLIDABLE=0
  -DARMA7=1
  -DKERNEL=1
  -DKERNEL_PRIVATE=1
  -D__arm__=1
  -DCONFIG_EMBEDDED=1
  -D__ARM_L2CACHE_SIZE_LOG__=21
  -Dfmrx=vmrs
  -Dfmxr=vmsr
  # osfmk/arm/asm.h's EXT(x) is `_ ## x` unless this is set, so without it every XNU assembly
  # reference comes out underscore-prefixed (`_arm_init`, `__start`) and cannot resolve against
  # C definitions or against a linker script naming `_start`. Turning it off is the Darwin-ELF
  # convention and is what makes the entry image linkable at all.
  -D__NO_UNDERSCORES__=1
)

INCLUDES=(
  # **936: on D13 the generated `assym.s` must come first, and on 4570 it must not be here at all.**
  # D13's `locore.s` does `#include <assym.s>` (angle brackets) and reads `BOOT_ARGS_*` - names only
  # the *generated* assym carries; `src/entry/assym.s` carries 4570's `BA_*` spelling, not these. On
  # 4570 the entry file is `start.s`, which does `#include "assym.s"` and reads `BA_*` from
  # `src/entry/assym.s`; adding the generated dir would change which file it resolves and is not done,
  # so 4570's include list is character-for-character what it was.
  ${D13_ENTRY:+-I$XNU_ASSYM_OUT/$CONFIG}
  -I$SRC_DIR/entry
  # The generated OPTIONS headers. `<mach_kdp.h>` is one of them and start.s includes it
  # unconditionally, so without this the entry image does not assemble at all.
  -I$OPTION_HEADERS
  -I$XNU/osfmk
  -I$XNU/bsd
  -I$XNU/libkern
  -I$XNU/EXTERNAL_HEADERS
  -I$XNU/pexpert
  -I$XNU/osfmk/arm
  -I$XNU/bsd/arm
  # Shims last, as a fallback rather than an override.
  -I$SRC_DIR/shims
  -I$SRC_DIR/shims/kern
  -I$SRC_DIR/shims/mach
  -I$SRC_DIR/shims_arm
  -I$SRC_DIR/shims_arm/kern
  -I$SRC_DIR/shims_arm/mach
  -I$SRC_DIR/shims_arm/sys
  -I$SRC_DIR/shims_arm/sys/_pthread
)

assemble_one() {
  local src=$1 out=$2
  [[ $VERBOSE -eq 1 ]] && echo "${CC_CMD[*]} ${TARGET_FLAGS[*]} ${DEFINES[*]} ${CONFIG_DEFINES[*]} ... -c $src -o $out"
  "${CC_CMD[@]}" "${TARGET_FLAGS[@]}" "${DEFINES[@]}" "${CONFIG_DEFINES[@]}" "${INCLUDES[@]}" -c "$src" -o "$out"
}

report() {
  local obj=$1
  echo "defined:"
  arm-none-eabi-nm "$obj" 2>/dev/null | grep -E ' [TtDdBb] ' | awk '{print "  " $2 " " $3}'
  echo
  echo "undefined:"
  arm-none-eabi-nm -u "$obj" 2>/dev/null | awk '{print "  " $2}'
}

# **936: D13's entry file is written with Apple's underscore convention, and this build is ELF.**
# `-D__NO_UNDERSCORES__` makes `EXT()`/`LEXT()` emit unprefixed names (so 4570's `start.s`, which uses
# them, is already right), but it does **not** touch `EnterARM` in D13's `asm_help.h` - which always
# writes `_ ## function` - nor the literal `_intstack`/`_debstack` globals D13's `locore.s` names by
# hand. So D13's `_start` comes out as `__start` and its stack symbols as `_intstack`, none of which
# resolve against the C definitions or the script's `ENTRY(_start)`. `tools/assemble_arm_layer.sh` has
# the same pass for the same reason ("ten of the manifest's assembly files do not use `EXT()`"); this
# is its rule applied to the entry file: rename `_x` -> `x`, **except** `__start`, which is D13's
# `_start` and must become `_start`. 4570's file is de-underscored from the outset, so this is a
# no-op there (`nm` finds no `_`-prefixed globals) and its object is byte-identical.
deunderscore() {
  local obj=$1 args=() sym
  while IFS= read -r sym; do
    case "$sym" in
      __start)  args+=(--redefine-sym "$sym=_start") ;;
      _[a-zA-Z]*) args+=(--redefine-sym "$sym=${sym#_}") ;;
    esac
  done < <(arm-none-eabi-nm "$obj" 2>/dev/null | awk '($1 == "U" && $2 ~ /^_[a-zA-Z]/) || ($2 ~ /^[TDBR]$/ && $3 ~ /^_[a-zA-Z]/) { print ($1 == "U") ? $2 : $3 }' | sort -u)
  if [[ ${#args[@]} -gt 0 ]]; then
    arm-none-eabi-objcopy "${args[@]}" "$obj"
    echo "de-underscored ${#args[@]} symbol(s) for the ELF link"
  fi
}

echo "== osfmk/arm/$ENTRY_NAME.s (this tree's \`_start\`) =="
if assemble_one "$ENTRY_SRC" "$OUT_DIR/$ENTRY_OBJ_NAME" 2>"$OUT_DIR/xnu_arm_start.log"; then
  echo "assembles: yes"
  deunderscore "$OUT_DIR/$ENTRY_OBJ_NAME"
  echo
  report "$OUT_DIR/$ENTRY_OBJ_NAME"
else
  echo "assembles: no"
  grep -E "error" "$OUT_DIR/xnu_arm_start.log" | head -10
  echo "(full output: $OUT_DIR/xnu_arm_start.log)"
  exit 1
fi

if [[ $DO_LOCORE -eq 1 ]]; then
  echo
  echo "== osfmk/arm/locore.s =="
  if assemble_one "$XNU/osfmk/arm/locore.s" "$OUT_DIR/xnu_arm_locore.o" 2>"$OUT_DIR/xnu_arm_locore.log"; then
    echo "assembles: yes"
    echo
    report "$OUT_DIR/xnu_arm_locore.o"
  else
    echo "assembles: no - still missing assym constants"
    grep -E "error" "$OUT_DIR/xnu_arm_locore.log" | head -10
    echo "(full output: $OUT_DIR/xnu_arm_locore.log)"
  fi
fi
