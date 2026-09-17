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
STAGE_DIR=$PWD
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)

XNU=$REPO_ROOT/external/xnu-4570.1.46
if [[ ! -d $XNU ]]; then
  echo "xnu-4570.1.46 not present at $XNU" >&2
  exit 2
fi

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
  # assym.s first: start.s and locore.s both do #include "assym.s".
  -I$STAGE_DIR/xnu_arm_boot
  -I$XNU/osfmk
  -I$XNU/bsd
  -I$XNU/libkern
  -I$XNU/EXTERNAL_HEADERS
  -I$XNU/pexpert
  -I$XNU/osfmk/arm
  -I$XNU/bsd/arm
  # Shims last, as a fallback rather than an override.
  -I$STAGE_DIR/shims
  -I$STAGE_DIR/shims/kern
  -I$STAGE_DIR/shims/mach
  -I$STAGE_DIR/shims_arm
  -I$STAGE_DIR/shims_arm/kern
  -I$STAGE_DIR/shims_arm/mach
  -I$STAGE_DIR/shims_arm/sys
  -I$STAGE_DIR/shims_arm/sys/_pthread
)

assemble_one() {
  local src=$1 out=$2
  [[ $VERBOSE -eq 1 ]] && echo "${CC_CMD[*]} ${TARGET_FLAGS[*]} ${DEFINES[*]} ... -c $src -o $out"
  "${CC_CMD[@]}" "${TARGET_FLAGS[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" -c "$src" -o "$out"
}

report() {
  local obj=$1
  echo "defined:"
  arm-none-eabi-nm "$obj" 2>/dev/null | grep -E ' [TtDdBb] ' | awk '{print "  " $2 " " $3}'
  echo
  echo "undefined:"
  arm-none-eabi-nm -u "$obj" 2>/dev/null | awk '{print "  " $2}'
}

echo "== osfmk/arm/start.s =="
if assemble_one "$XNU/osfmk/arm/start.s" "$OUT_DIR/xnu_arm_start.o" 2>"$OUT_DIR/xnu_arm_start.log"; then
  echo "assembles: yes"
  echo
  report "$OUT_DIR/xnu_arm_start.o"
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
