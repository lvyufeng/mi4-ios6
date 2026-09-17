#!/usr/bin/env bash
#
# Sweep external/xnu-4570.1.46/osfmk/arm/*.c and report how many compile, and with which
# compiler. This is the Phase 4 measurement: its job is to answer "how far is real XNU ARM code
# from compiling here" with a number, and to let the effect of one change be seen.
#
# Usage:
#   ./xnu_arm_sweep.sh              # clang, the toolchain XNU's ARM layer requires
#   ./xnu_arm_sweep.sh --gcc        # arm-none-eabi-gcc, for the comparison
#   ./xnu_arm_sweep.sh --detail FILE  # full output for one file
#
# Why clang is the default. XNU's atomic layer is clang-only, and says so outright:
# EXTERNAL_HEADERS/stdatomic.h:24 is `#ifndef __clang__ / #error unsupported compiler`. The
# project's ARM toolchain has been arm-none-eabi-gcc throughout, which cannot get past that line
# at all. The sweep's job is to say what changes when that is no longer the toolchain's fault.
#
# Three configuration facts, each found earlier by changing one thing and reading what moved.
# They are not headers to write, and without them the sweep reports header gaps that are not
# there:
#   1. -DKERNEL=1, or EXTERNAL_HEADERS/stdint.h falls through to the compiler's stdint.h and
#      int32_t is defined twice.
#   2. -Ibsd/arm, or osfmk/mach/arm/vm_types.h's <arm/_types.h> is not found and __darwin_natural_t
#      cascades into everything built on it.
#   3. the real XNU -I paths come BEFORE shims/. A shim is a fallback, not an override; a shim
#      sys/types.h shadowing the real one makes correctly-defined types look missing.

set -uo pipefail

cd "$(dirname "$0")"
STAGE_DIR=$PWD
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)

XNU=$REPO_ROOT/external/xnu-4570.1.46
if [[ ! -d $XNU ]]; then
  echo "xnu-4570.1.46 not present at $XNU" >&2
  exit 2
fi

USE_GCC=0
DETAIL=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --gcc) USE_GCC=1; shift ;;
    --detail) DETAIL=${2:-}; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [[ $USE_GCC -eq 1 ]]; then
  CC_CMD=(arm-none-eabi-gcc)
  TARGET_FLAGS=(-mcpu=cortex-a15 -marm)
  echo "compiler: arm-none-eabi-gcc (the project's payload toolchain)"
else
  CC_CMD=(clang --target=armv7-none-eabi)
  TARGET_FLAGS=(-mcpu=cortex-a15 -marm)
  echo "compiler: clang --target=armv7-none-eabi (what the ARM layer requires)"
fi
if ! command -v "${CC_CMD[0]}" >/dev/null 2>&1; then
  echo "compiler not found: ${CC_CMD[0]}" >&2
  exit 2
fi

INCLUDES=(
  -I"$XNU/osfmk"
  -I"$XNU/bsd"
  -I"$XNU/libkern"
  -I"$XNU/EXTERNAL_HEADERS"
  -I"$XNU/pexpert"
  -I"$XNU/iokit"
  -I"$XNU/osfmk/arm"
  -I"$XNU/bsd/arm"
  # Shims last: a fallback, not an override. Subdirectories because these are named the way
  # the includes are (kern/debug.h, sys/_pthread/_pthread_types.h), unlike the project's own
  # flat shims/ tree.
  -Ishims
  -Ishims/kern
  -Ishims/mach
  -Ishims_arm
  -Ishims_arm/kern
  -Ishims_arm/mach
  -Ishims_arm/sys
  -Ishims_arm/sys/_pthread
)

FLAGS=(
  "${TARGET_FLAGS[@]}"
  # ARMA7 is the 32-bit ARMv7 machine configuration, and it is *in the source* -
  # osfmk/arm/proc_reg.h:73 is `#if defined (ARMA7)` followed by __ARM_ARCH__ 7, __ARM_VMSA__ 7.
  # Without one of the processor macros that chain reaches its `#else / #error processor not
  # supported` at :161, which is what every ARM file was dying on before this flag was found.
  # The other branches are ASC/APPLECYCLONE/APPLETYPHOON/APPLETWISTER/APPLEHURRICANE, all 64-bit
  # Apple parts; ARMA7 is the only one that describes a 32-bit ARMv7 core like Krait.
  -DARMA7=1
  -ffreestanding
  -fno-builtin
  -fno-stack-protector
  -fno-common
  -fno-pic
  -std=gnu11
  -fsyntax-only
  -Wno-everything
  -D__APPLE_API_PRIVATE=1
  -DCONFIG_EMBEDDED=1
  -DKERNEL=1
  -DKERNEL_PRIVATE=1
  -D__arm__=1
)

if [[ -n $DETAIL ]]; then
  "${CC_CMD[@]}" "${FLAGS[@]}" "${INCLUDES[@]}" "$XNU/osfmk/arm/$DETAIL" 2>&1 | head -60
  exit 0
fi

OUT_DIR=$REPO_ROOT/out/stage90
mkdir -p "$OUT_DIR"
RAW=$OUT_DIR/xnu-arm-sweep-raw.txt
: > "$RAW"

ok=0
fail=0
declare -a FAILED=()

for src in "$XNU"/osfmk/arm/*.c; do
  name=$(basename "$src")
  if "${CC_CMD[@]}" "${FLAGS[@]}" "${INCLUDES[@]}" "$src" > /tmp/sweep-one.txt 2>&1; then
    ok=$((ok + 1))
    echo "  ok   $name"
  else
    fail=$((fail + 1))
    FAILED+=("$name")
    echo "  FAIL $name" >> "$RAW"
    cat /tmp/sweep-one.txt >> "$RAW"
  fi
done

total=$((ok + fail))
echo
echo "osfmk/arm: $ok of $total compile"

if [[ $fail -gt 0 ]]; then
  echo
  echo "errors by distinct missing name (cascades, so the name count is the honest one):"
  grep -oE "[A-Za-z_][A-Za-z0-9_]* undeclared|unknown type name '[A-Za-z_][A-Za-z0-9_]*'|definition of [A-Za-z_][A-Za-z0-9_]*|error: #[a-z ]+ '#[a-z]+'" "$RAW" \
    | sed "s/unknown type name '//;s/'//;s/ undeclared//;s/definition of //" \
    | sort | uniq -c | sort -rn | head -20
  echo
  echo "full output: $RAW"
fi
