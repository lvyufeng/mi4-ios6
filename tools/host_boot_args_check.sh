#!/usr/bin/env bash
#
# Execute a payload module on the host, 32-bit, with the linker symbols it needs supplied.
#
# Why this exists
# ---------------
# Several payload modules take exactly one input the host cannot know: a linker symbol. For
# `xnu_boot_args_conformant.c` it is `__stage90_image_end`, whose value only exists once the
# image is linked. That made the module unrunnable anywhere except the device — and the device
# has not run it, because it sits behind a default-off switch.
#
# `--defsym=__stage90_image_end=<value>` fixes that, and it is faithful rather than a
# substitution: the symbol is only ever used as an address *value* in the module, never
# dereferenced, so an absolute symbol with the right value is exactly the same computation.
# The value is read from the built ELF, so the module is exercised against the real image
# layout rather than an invented one.
#
# The build is 32-bit on purpose: the module's own `_Static_assert`s encode the ARM ILP32 ABI,
# so making them hold is part of the check.
#
# Usage: ./host_boot_args_check.sh
# Exit 0 if the module accepts the configuration, 1 if it rejects it, 2 on a setup problem.

set -euo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)
ELF=$REPO_ROOT/out/stage90/stage90.elf
WORK=$REPO_ROOT/out/host-boot-args

CC=${CC:-cc}
NM=${NM:-arm-none-eabi-nm}

[[ -f $ELF ]] || { echo "host_boot_args_check: no $ELF - run scripts/build.sh first" >&2; exit 2; }

mkdir -p "$WORK"

# The one host-unknowable input, taken from the real image.
IMAGE_END=$("$NM" "$ELF" | awk '$3 == "__stage90_image_end" { print "0x" $1 }')
[[ -n $IMAGE_END ]] || { echo "host_boot_args_check: __stage90_image_end not in $ELF" >&2; exit 2; }

echo "host_boot_args_check: __stage90_image_end = $IMAGE_END (from $ELF)"

# -static because this host has no 32-bit dynamic loader; -nostdlib because the payload is
# freestanding and tools/host_32bit_runtime.c supplies the handful of symbols it wants.
"$CC" -m32 -static -ffreestanding -nostdlib -fno-builtin -std=gnu11 -O1 \
  -Wno-unused-function -Wno-unused-parameter \
  -I "$REPO_ROOT/src" \
  -o "$WORK/host_boot_args" \
  "$TOOLS_DIR/host_32bit_runtime.c" \
  "$TOOLS_DIR/host_boot_args_harness.c" \
  "$REPO_ROOT/src/xnu_boot_args_conformant.c" \
  -Wl,--defsym=__stage90_image_end="$IMAGE_END" \
  -Wl,-e,main

"$WORK/host_boot_args"
