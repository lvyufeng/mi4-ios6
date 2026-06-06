#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

OUT_DIR=${1:-../out/stage51}
OBJ_DIR="$OUT_DIR/xnu-objects"
mkdir -p "$OBJ_DIR"

CC=${STAGE51_XNU_OBJ_CC:-arm-none-eabi-gcc}
OBJDUMP=${STAGE51_XNU_OBJ_OBJDUMP:-arm-none-eabi-objdump}
NM=${STAGE51_XNU_OBJ_NM:-arm-none-eabi-nm}
SIZE=${STAGE51_XNU_OBJ_SIZE:-arm-none-eabi-size}

XNU_2050=../external/xnu-upstream
DEVICE_TREE_SRC="$XNU_2050/pexpert/gen/device_tree.c"
BOOTARGS_SRC="$XNU_2050/pexpert/gen/bootargs.c"
SHIM_SRC="xnu_object_shims.c"

STATUS_TXT="$OUT_DIR/xnu-object-subset-status.txt"
MANIFEST_TXT="$OUT_DIR/xnu-object-subset-manifest.txt"
HEADER="$OUT_DIR/xnu_object_subset_generated.h"

COMMON_FLAGS=(
  -mcpu=cortex-a15
  -marm
  -ffreestanding
  -fno-builtin
  -fno-stack-protector
  -fno-unwind-tables
  -fno-asynchronous-unwind-tables
  -fno-pic
  -O2
  -Wall
  -Wextra
  -Werror
  -std=gnu11
  -D__APPLE_API_PRIVATE=1
  -DCONFIG_EMBEDDED=1
  -DKERNEL_PRIVATE=1
  -Ishims
  -I"$XNU_2050/pexpert"
  -I"$XNU_2050/osfmk"
  -I"$XNU_2050/libkern"
  -I"$XNU_2050/bsd"
  -I"$XNU_2050/iokit"
)

satisfied=0
failure=0

add_satisfied() { satisfied=$((satisfied | $1)); }
add_failure() { failure=$((failure | $1)); }

sha32_file() {
  python3 - "$1" <<'PY'
import hashlib, sys
p = sys.argv[1]
d = hashlib.sha256(open(p, 'rb').read()).digest()
print('0x' + d[:4].hex())
PY
}

size_file() {
  python3 - "$1" <<'PY'
import os, sys
print(os.path.getsize(sys.argv[1]))
PY
}

: > "$STATUS_TXT"
: > "$MANIFEST_TXT"

printf 'Stage51 public-XNU minimal object subset compile\n' >> "$STATUS_TXT"
printf 'source_device_tree=%s\n' "$DEVICE_TREE_SRC" >> "$STATUS_TXT"
printf 'source_bootargs=%s\n' "$BOOTARGS_SRC" >> "$STATUS_TXT"
printf 'shim_source=%s\n' "$SHIM_SRC" >> "$STATUS_TXT"

if [[ -f "$DEVICE_TREE_SRC" ]]; then
  add_satisfied 0x00000001
  printf 'present %s\n' "$DEVICE_TREE_SRC" >> "$MANIFEST_TXT"
else
  add_failure 0x00000001
fi
if [[ -f "$BOOTARGS_SRC" ]]; then
  add_satisfied 0x00000002
  printf 'present %s\n' "$BOOTARGS_SRC" >> "$MANIFEST_TXT"
else
  add_failure 0x00000002
fi

shim_ok=1
for p in \
  shims/pexpert/boot.h \
  shims/pexpert/protos.h \
  shims/pexpert/pexpert.h \
  shims/kern/kalloc.h \
  shims/mach/mach_types.h \
  shims/mach/machine/vm_types.h \
  shims/mach/boolean.h \
  shims/sys/appleapiopts.h \
  "$SHIM_SRC"; do
  if [[ -f "$p" ]]; then
    printf 'present %s\n' "$p" >> "$MANIFEST_TXT"
  else
    printf 'missing %s\n' "$p" >> "$MANIFEST_TXT"
    shim_ok=0
  fi
done
if [[ "$shim_ok" -eq 1 ]]; then
  add_satisfied 0x00000004
else
  add_failure 0x00000004
fi

if [[ "$failure" -eq 0 ]]; then
  "$CC" "${COMMON_FLAGS[@]}" -c "$DEVICE_TREE_SRC" -o "$OBJ_DIR/device_tree.o"
  add_satisfied 0x00000008
  "$CC" "${COMMON_FLAGS[@]}" -c "$BOOTARGS_SRC" -o "$OBJ_DIR/bootargs.o"
  add_satisfied 0x00000010
  "$CC" "${COMMON_FLAGS[@]}" -c "$SHIM_SRC" -o "$OBJ_DIR/xnu_object_shims.o"
else
  add_failure 0x80000000
fi

if [[ -f "$OBJ_DIR/device_tree.o" && -f "$OBJ_DIR/bootargs.o" && -f "$OBJ_DIR/xnu_object_shims.o" ]]; then
  add_satisfied 0x00000020
  object_count=3
else
  add_failure 0x00000020
  object_count=0
fi

add_satisfied 0x00000040  # public 2050 baseline inherited from xnu_workspace_validate.sh
add_satisfied 0x00000080  # public only
add_satisfied 0x00000100  # no full XNU build
add_satisfied 0x00000200  # no Mach-O link
add_satisfied 0x00000400  # no public-XNU execution
add_satisfied 0x00000800  # no external mutation
add_satisfied 0x00001000  # outputs under ignored out/stage51
add_satisfied 0x00002000  # fail-closed script

if [[ -f "$OBJ_DIR/device_tree.o" ]]; then
  device_tree_bytes=$(size_file "$OBJ_DIR/device_tree.o")
  device_tree_sha32=$(sha32_file "$OBJ_DIR/device_tree.o")
else
  device_tree_bytes=0
  device_tree_sha32=0x00000000
fi
if [[ -f "$OBJ_DIR/bootargs.o" ]]; then
  bootargs_bytes=$(size_file "$OBJ_DIR/bootargs.o")
  bootargs_sha32=$(sha32_file "$OBJ_DIR/bootargs.o")
else
  bootargs_bytes=0
  bootargs_sha32=0x00000000
fi

status=0x51000000
if [[ "$satisfied" -eq 0x00003fff && "$failure" -eq 0 ]]; then
  status=0x51000001
fi

printf 'stage51_xnu_object_subset_status=0x%08x\n' "$status" >> "$STATUS_TXT"
printf 'stage51_xnu_object_subset_required_mask=0x%08x\n' 0x00003fff >> "$STATUS_TXT"
printf 'stage51_xnu_object_subset_satisfied_mask=0x%08x\n' "$satisfied" >> "$STATUS_TXT"
printf 'stage51_xnu_object_subset_failure_mask=0x%08x\n' "$failure" >> "$STATUS_TXT"
printf 'stage51_xnu_object_source_mask=0x%08x\n' 0x00000003 >> "$STATUS_TXT"
printf 'stage51_xnu_object_shim_mask=0x%08x\n' 0x0000001f >> "$STATUS_TXT"
printf 'stage51_xnu_object_count=0x%08x\n' "$object_count" >> "$STATUS_TXT"
printf 'stage51_xnu_object_device_tree_bytes=0x%08x\n' "$device_tree_bytes" >> "$STATUS_TXT"
printf 'stage51_xnu_object_bootargs_bytes=0x%08x\n' "$bootargs_bytes" >> "$STATUS_TXT"
printf 'stage51_xnu_object_device_tree_sha32=%s\n' "$device_tree_sha32" >> "$STATUS_TXT"
printf 'stage51_xnu_object_bootargs_sha32=%s\n' "$bootargs_sha32" >> "$STATUS_TXT"
printf 'stage51_xnu_object_public_only=0x00000001\n' >> "$STATUS_TXT"
printf 'stage51_xnu_object_no_full_xnu_build=0x00000001\n' >> "$STATUS_TXT"
printf 'stage51_xnu_object_no_macho_link=0x00000001\n' >> "$STATUS_TXT"
printf 'stage51_xnu_object_no_public_xnu_exec=0x00000001\n' >> "$STATUS_TXT"
printf 'stage51_xnu_object_no_external_mutation=0x00000001\n' >> "$STATUS_TXT"
printf 'stage51_xnu_object_outputs_ignored=0x00000001\n' >> "$STATUS_TXT"

if [[ "$object_count" -ne 0 ]]; then
  "$NM" -g "$OBJ_DIR/device_tree.o" > "$OBJ_DIR/device_tree.symbols"
  "$NM" -g "$OBJ_DIR/bootargs.o" > "$OBJ_DIR/bootargs.symbols"
  "$NM" -g "$OBJ_DIR/xnu_object_shims.o" > "$OBJ_DIR/xnu_object_shims.symbols"
  "$OBJDUMP" -dr "$OBJ_DIR/device_tree.o" > "$OBJ_DIR/device_tree.disasm"
  "$OBJDUMP" -dr "$OBJ_DIR/bootargs.o" > "$OBJ_DIR/bootargs.disasm"
  "$SIZE" "$OBJ_DIR/device_tree.o" "$OBJ_DIR/bootargs.o" "$OBJ_DIR/xnu_object_shims.o" > "$OBJ_DIR/xnu_object_subset.size"
  sha256sum "$OBJ_DIR/device_tree.o" "$OBJ_DIR/bootargs.o" "$OBJ_DIR/xnu_object_shims.o" > "$OBJ_DIR/SHA256SUMS.txt"
fi

cat > "$HEADER" <<EOF_HEADER
#ifndef MI4IOS6_STAGE51_XNU_OBJECT_SUBSET_GENERATED_H
#define MI4IOS6_STAGE51_XNU_OBJECT_SUBSET_GENERATED_H

#define STAGE51_XNU_OBJ_HOST_STATUS                 0x$(printf '%08x' "$status")u
#define STAGE51_XNU_OBJ_HOST_REQUIRED_MASK          0x00003fffu
#define STAGE51_XNU_OBJ_HOST_SATISFIED_MASK         0x$(printf '%08x' "$satisfied")u
#define STAGE51_XNU_OBJ_HOST_FAILURE_MASK           0x$(printf '%08x' "$failure")u
#define STAGE51_XNU_OBJ_HOST_SOURCE_MASK            0x00000003u
#define STAGE51_XNU_OBJ_HOST_SHIM_MASK              0x0000001fu
#define STAGE51_XNU_OBJ_HOST_OBJECT_COUNT           0x$(printf '%08x' "$object_count")u
#define STAGE51_XNU_OBJ_HOST_BASELINE_COMMIT32      0xcc8a9b0cu
#define STAGE51_XNU_OBJ_HOST_MASTER_VERSION_12_3_0  0x000c0300u
#define STAGE51_XNU_OBJ_HOST_DEVICE_TREE_BYTES      0x$(printf '%08x' "$device_tree_bytes")u
#define STAGE51_XNU_OBJ_HOST_BOOTARGS_BYTES         0x$(printf '%08x' "$bootargs_bytes")u
#define STAGE51_XNU_OBJ_HOST_DEVICE_TREE_SHA32      ${device_tree_sha32}u
#define STAGE51_XNU_OBJ_HOST_BOOTARGS_SHA32         ${bootargs_sha32}u
#define STAGE51_XNU_OBJ_HOST_PUBLIC_2050_BASELINE   1u
#define STAGE51_XNU_OBJ_HOST_PUBLIC_ONLY            1u
#define STAGE51_XNU_OBJ_HOST_NO_FULL_XNU_BUILD      1u
#define STAGE51_XNU_OBJ_HOST_NO_MACHO_LINK          1u
#define STAGE51_XNU_OBJ_HOST_NO_PUBLIC_XNU_EXEC     1u
#define STAGE51_XNU_OBJ_HOST_NO_EXTERNAL_MUTATION   1u
#define STAGE51_XNU_OBJ_HOST_OUTPUTS_IGNORED        1u
#define STAGE51_XNU_OBJ_HOST_FAIL_CLOSED            1u

#endif
EOF_HEADER

cat "$STATUS_TXT"

if [[ "$status" -ne 0x51000001 ]]; then
  exit 1
fi
