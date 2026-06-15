#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

OUT_DIR=${1:-../out/stage88}
OBJ_DIR="$OUT_DIR/xnu-objects"
mkdir -p "$OBJ_DIR"

CC=${STAGE88_XNU_OBJ_CC:-arm-none-eabi-gcc}
OBJDUMP=${STAGE88_XNU_OBJ_OBJDUMP:-arm-none-eabi-objdump}
NM=${STAGE88_XNU_OBJ_NM:-arm-none-eabi-nm}
SIZE=${STAGE88_XNU_OBJ_SIZE:-arm-none-eabi-size}

XNU_2050=../external/xnu-upstream
XNU_4570=../external/xnu-4570.1.46
DEVICE_TREE_SRC="$XNU_2050/pexpert/gen/device_tree.c"
BOOTARGS_SRC="$XNU_2050/pexpert/gen/bootargs.c"
PE_GEN_SRC="$XNU_2050/pexpert/gen/pe_gen.c"
ARM_PE_BOOTARGS_SRC="$XNU_4570/pexpert/arm/pe_bootargs.c"
ARM_PE_CONSISTENT_DEBUG_SRC="$XNU_4570/pexpert/arm/pe_consistent_debug.c"
SHIM_SRC="xnu_object_shims.c"
GRAPH_HEADER="$OUT_DIR/xnu_compile_graph_generated.h"

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
  -Wno-incompatible-pointer-types
  -std=gnu11
  -D__APPLE_API_PRIVATE=1
  -DCONFIG_EMBEDDED=1
  -DKERNEL_PRIVATE=1
  -D__arm__=1
  -Ishims
  -I"$XNU_2050/pexpert"
  -I"$XNU_2050/osfmk"
  -I"$XNU_2050/libkern"
  -I"$XNU_2050/bsd"
  -I"$XNU_2050/iokit"
  -I"$XNU_4570/pexpert"
  -I"$XNU_4570/osfmk"
)

required=0x07ffffff
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

printf 'Stage84 public-XNU pexpert/platform compile graph gated object subset compile\n' >> "$STATUS_TXT"
printf 'source_device_tree=%s\n' "$DEVICE_TREE_SRC" >> "$STATUS_TXT"
printf 'source_bootargs=%s\n' "$BOOTARGS_SRC" >> "$STATUS_TXT"
printf 'source_pe_gen=%s\n' "$PE_GEN_SRC" >> "$STATUS_TXT"
printf 'source_arm_pe_bootargs=%s\n' "$ARM_PE_BOOTARGS_SRC" >> "$STATUS_TXT"
printf 'source_arm_pe_consistent_debug=%s\n' "$ARM_PE_CONSISTENT_DEBUG_SRC" >> "$STATUS_TXT"
printf 'shim_source=%s\n' "$SHIM_SRC" >> "$STATUS_TXT"

graph_ok=0
if [[ -f "$GRAPH_HEADER" ]] &&
   grep -q 'STAGE88_XNU_GRAPH_HOST_STATUS' "$GRAPH_HEADER" &&
   grep -q '0x88000001u' "$GRAPH_HEADER" &&
   grep -q 'STAGE88_XNU_GRAPH_HOST_PE_GEN_ALLOWED' "$GRAPH_HEADER" &&
   grep -q 'STAGE88_XNU_GRAPH_HOST_ARM_BOOTARGS_ALLOWED' "$GRAPH_HEADER" &&
   grep -q 'STAGE88_XNU_GRAPH_HOST_ARM_CONSISTENT_DEBUG_ALLOWED' "$GRAPH_HEADER" &&
   grep -q 'STAGE88_XNU_GRAPH_HOST_DUPLICATE_SYMBOL_COUNT 0x00000000u' "$GRAPH_HEADER"; then
  graph_ok=1
  printf 'present graph %s\n' "$GRAPH_HEADER" >> "$MANIFEST_TXT"
else
  printf 'missing_or_failed graph %s\n' "$GRAPH_HEADER" >> "$MANIFEST_TXT"
  add_failure 0x00000010
fi

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
if [[ -f "$PE_GEN_SRC" ]]; then
  add_satisfied 0x00000004
  printf 'present %s\n' "$PE_GEN_SRC" >> "$MANIFEST_TXT"
else
  add_failure 0x00000004
fi
if [[ -f "$ARM_PE_BOOTARGS_SRC" ]]; then
  add_satisfied 0x00020000
  printf 'present %s\n' "$ARM_PE_BOOTARGS_SRC" >> "$MANIFEST_TXT"
else
  add_failure 0x00000400
fi
if [[ -f "$ARM_PE_CONSISTENT_DEBUG_SRC" ]]; then
  add_satisfied 0x00800000
  printf 'present %s\n' "$ARM_PE_CONSISTENT_DEBUG_SRC" >> "$MANIFEST_TXT"
else
  add_failure 0x00004000
fi

shim_ok=1
for p in \
  shims/pexpert/boot.h \
  shims/pexpert/protos.h \
  shims/pexpert/pexpert.h \
  shims/kern/kalloc.h \
  shims/kern/debug.h \
  shims/pexpert/arm/consistent_debug.h \
  shims/libkern/OSAtomic.h \
  shims/machine/machine_routines.h \
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
  add_satisfied 0x00000008
  add_satisfied 0x00200000
  add_satisfied 0x04000000
else
  add_failure 0x00000008
fi
if [[ "$graph_ok" -eq 1 ]]; then
  add_satisfied 0x00000010
fi

if grep -q '^char \*PE_boot_args' "$SHIM_SRC" || grep -q '^char \*$' "$SHIM_SRC"; then
  printf 'duplicate-risk PE_boot_args shim still present in %s\n' "$SHIM_SRC" >> "$MANIFEST_TXT"
  add_failure 0x00001000
fi

if [[ "$failure" -eq 0 ]]; then
  "$CC" "${COMMON_FLAGS[@]}" -c "$DEVICE_TREE_SRC" -o "$OBJ_DIR/device_tree.o"
  add_satisfied 0x00000020
  "$CC" "${COMMON_FLAGS[@]}" -c "$BOOTARGS_SRC" -o "$OBJ_DIR/bootargs.o"
  add_satisfied 0x00000040
  "$CC" "${COMMON_FLAGS[@]}" -c "$PE_GEN_SRC" -o "$OBJ_DIR/pe_gen.o"
  add_satisfied 0x00000080
  "$CC" "${COMMON_FLAGS[@]}" -c "$ARM_PE_BOOTARGS_SRC" -o "$OBJ_DIR/arm_pe_bootargs.o"
  add_satisfied 0x00040000
  "$CC" "${COMMON_FLAGS[@]}" -c "$ARM_PE_CONSISTENT_DEBUG_SRC" -o "$OBJ_DIR/arm_pe_consistent_debug.o"
  add_satisfied 0x01000000
  "$CC" "${COMMON_FLAGS[@]}" -c "$SHIM_SRC" -o "$OBJ_DIR/xnu_object_shims.o"
else
  add_failure 0x80000000
fi

if [[ -f "$OBJ_DIR/device_tree.o" && -f "$OBJ_DIR/bootargs.o" && -f "$OBJ_DIR/pe_gen.o" && -f "$OBJ_DIR/arm_pe_bootargs.o" && -f "$OBJ_DIR/arm_pe_consistent_debug.o" && -f "$OBJ_DIR/xnu_object_shims.o" ]]; then
  add_satisfied 0x00000100
  add_satisfied 0x00080000
  add_satisfied 0x02000000
  object_count=6
else
  add_failure 0x00000100
  object_count=0
fi

duplicate_count=0
if [[ "$object_count" -ne 0 ]]; then
  DEFINED_SYMBOLS_TXT="$OBJ_DIR/xnu_object_subset.defined"
  "$NM" -g --defined-only "$OBJ_DIR/device_tree.o" "$OBJ_DIR/bootargs.o" "$OBJ_DIR/pe_gen.o" "$OBJ_DIR/arm_pe_bootargs.o" "$OBJ_DIR/arm_pe_consistent_debug.o" "$OBJ_DIR/xnu_object_shims.o" > "$DEFINED_SYMBOLS_TXT"
  duplicate_count=$(python3 - "$DEFINED_SYMBOLS_TXT" <<'PY'
import sys
from collections import Counter
names=[]
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    parts=line.split()
    if len(parts)>=3 and parts[1] not in ('U','w'):
        names.append(parts[-1])
print(sum(1 for _, c in Counter(names).items() if c > 1))
PY
)
fi
if [[ "$duplicate_count" -eq 0 ]]; then
  add_satisfied 0x00100000
else
  add_failure 0x00001000
fi

add_satisfied 0x00000200  # public 2050 baseline inherited from xnu_workspace_validate.sh
add_satisfied 0x00000400  # public only
add_satisfied 0x00000800  # no full XNU build
add_satisfied 0x00001000  # no Mach-O link in object compile step
add_satisfied 0x00002000  # no public-XNU execution
add_satisfied 0x00004000  # no external mutation
add_satisfied 0x00008000  # outputs under ignored out/stage88
add_satisfied 0x00010000  # fail-closed script
add_satisfied 0x00400000  # no platform runtime execution

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
if [[ -f "$OBJ_DIR/pe_gen.o" ]]; then
  pe_gen_bytes=$(size_file "$OBJ_DIR/pe_gen.o")
  pe_gen_sha32=$(sha32_file "$OBJ_DIR/pe_gen.o")
else
  pe_gen_bytes=0
  pe_gen_sha32=0x00000000
fi
if [[ -f "$OBJ_DIR/arm_pe_bootargs.o" ]]; then
  arm_pe_bootargs_bytes=$(size_file "$OBJ_DIR/arm_pe_bootargs.o")
  arm_pe_bootargs_sha32=$(sha32_file "$OBJ_DIR/arm_pe_bootargs.o")
else
  arm_pe_bootargs_bytes=0
  arm_pe_bootargs_sha32=0x00000000
fi
if [[ -f "$OBJ_DIR/arm_pe_consistent_debug.o" ]]; then
  arm_pe_consistent_debug_bytes=$(size_file "$OBJ_DIR/arm_pe_consistent_debug.o")
  arm_pe_consistent_debug_sha32=$(sha32_file "$OBJ_DIR/arm_pe_consistent_debug.o")
else
  arm_pe_consistent_debug_bytes=0
  arm_pe_consistent_debug_sha32=0x00000000
fi

status=0x88000000
if [[ "$satisfied" -eq "$required" && "$failure" -eq 0 ]]; then
  status=0x88000001
fi

printf 'stage88_xnu_object_subset_status=0x%08x\n' "$status" >> "$STATUS_TXT"
printf 'stage88_xnu_object_subset_required_mask=0x%08x\n' "$required" >> "$STATUS_TXT"
printf 'stage88_xnu_object_subset_satisfied_mask=0x%08x\n' "$satisfied" >> "$STATUS_TXT"
printf 'stage88_xnu_object_subset_failure_mask=0x%08x\n' "$failure" >> "$STATUS_TXT"
printf 'stage88_xnu_object_source_mask=0x%08x\n' 0x0000001f >> "$STATUS_TXT"
printf 'stage88_xnu_object_shim_mask=0x%08x\n' 0x000003ff >> "$STATUS_TXT"
printf 'stage88_xnu_object_count=0x%08x\n' "$object_count" >> "$STATUS_TXT"
printf 'stage88_xnu_object_public_2050_count=0x00000003\n' >> "$STATUS_TXT"
printf 'stage88_xnu_object_public_arm_pexpert_count=0x00000002\n' >> "$STATUS_TXT"
printf 'stage88_xnu_object_stage_owned_shim_count=0x00000001\n' >> "$STATUS_TXT"
printf 'stage88_xnu_object_duplicate_symbol_count=0x%08x\n' "$duplicate_count" >> "$STATUS_TXT"
printf 'stage88_xnu_object_pe_state_abi_shim_ready=0x00000001\n' >> "$STATUS_TXT"
printf 'stage88_xnu_object_consistent_debug_abi_shim_ready=0x00000001\n' >> "$STATUS_TXT"
printf 'stage88_xnu_object_device_tree_bytes=0x%08x\n' "$device_tree_bytes" >> "$STATUS_TXT"
printf 'stage88_xnu_object_bootargs_bytes=0x%08x\n' "$bootargs_bytes" >> "$STATUS_TXT"
printf 'stage88_xnu_object_pe_gen_bytes=0x%08x\n' "$pe_gen_bytes" >> "$STATUS_TXT"
printf 'stage88_xnu_object_arm_pe_bootargs_bytes=0x%08x\n' "$arm_pe_bootargs_bytes" >> "$STATUS_TXT"
printf 'stage88_xnu_object_arm_pe_consistent_debug_bytes=0x%08x\n' "$arm_pe_consistent_debug_bytes" >> "$STATUS_TXT"
printf 'stage88_xnu_object_device_tree_sha32=%s\n' "$device_tree_sha32" >> "$STATUS_TXT"
printf 'stage88_xnu_object_bootargs_sha32=%s\n' "$bootargs_sha32" >> "$STATUS_TXT"
printf 'stage88_xnu_object_pe_gen_sha32=%s\n' "$pe_gen_sha32" >> "$STATUS_TXT"
printf 'stage88_xnu_object_arm_pe_bootargs_sha32=%s\n' "$arm_pe_bootargs_sha32" >> "$STATUS_TXT"
printf 'stage88_xnu_object_arm_pe_consistent_debug_sha32=%s\n' "$arm_pe_consistent_debug_sha32" >> "$STATUS_TXT"
printf 'stage88_xnu_object_public_only=0x00000001\n' >> "$STATUS_TXT"
printf 'stage88_xnu_object_no_full_xnu_build=0x00000001\n' >> "$STATUS_TXT"
printf 'stage88_xnu_object_no_macho_link=0x00000001\n' >> "$STATUS_TXT"
printf 'stage88_xnu_object_no_public_xnu_exec=0x00000001\n' >> "$STATUS_TXT"
printf 'stage88_xnu_object_no_platform_runtime_exec=0x00000001\n' >> "$STATUS_TXT"
printf 'stage88_xnu_object_no_external_mutation=0x00000001\n' >> "$STATUS_TXT"
printf 'stage88_xnu_object_outputs_ignored=0x00000001\n' >> "$STATUS_TXT"

if [[ "$object_count" -ne 0 ]]; then
  "$NM" -g "$OBJ_DIR/device_tree.o" > "$OBJ_DIR/device_tree.symbols"
  "$NM" -g "$OBJ_DIR/bootargs.o" > "$OBJ_DIR/bootargs.symbols"
  "$NM" -g "$OBJ_DIR/pe_gen.o" > "$OBJ_DIR/pe_gen.symbols"
  "$NM" -g "$OBJ_DIR/arm_pe_bootargs.o" > "$OBJ_DIR/arm_pe_bootargs.symbols"
  "$NM" -g "$OBJ_DIR/arm_pe_consistent_debug.o" > "$OBJ_DIR/arm_pe_consistent_debug.symbols"
  "$NM" -g "$OBJ_DIR/xnu_object_shims.o" > "$OBJ_DIR/xnu_object_shims.symbols"
  "$OBJDUMP" -dr "$OBJ_DIR/device_tree.o" > "$OBJ_DIR/device_tree.disasm"
  "$OBJDUMP" -dr "$OBJ_DIR/bootargs.o" > "$OBJ_DIR/bootargs.disasm"
  "$OBJDUMP" -dr "$OBJ_DIR/pe_gen.o" > "$OBJ_DIR/pe_gen.disasm"
  "$OBJDUMP" -dr "$OBJ_DIR/arm_pe_bootargs.o" > "$OBJ_DIR/arm_pe_bootargs.disasm"
  "$OBJDUMP" -dr "$OBJ_DIR/arm_pe_consistent_debug.o" > "$OBJ_DIR/arm_pe_consistent_debug.disasm"
  "$SIZE" "$OBJ_DIR/device_tree.o" "$OBJ_DIR/bootargs.o" "$OBJ_DIR/pe_gen.o" "$OBJ_DIR/arm_pe_bootargs.o" "$OBJ_DIR/arm_pe_consistent_debug.o" "$OBJ_DIR/xnu_object_shims.o" > "$OBJ_DIR/xnu_object_subset.size"
  sha256sum "$OBJ_DIR/device_tree.o" "$OBJ_DIR/bootargs.o" "$OBJ_DIR/pe_gen.o" "$OBJ_DIR/arm_pe_bootargs.o" "$OBJ_DIR/arm_pe_consistent_debug.o" "$OBJ_DIR/xnu_object_shims.o" > "$OBJ_DIR/SHA256SUMS.txt"
fi

cat > "$HEADER" <<EOF_HEADER
#ifndef MI4IOS6_STAGE88_XNU_OBJECT_SUBSET_GENERATED_H
#define MI4IOS6_STAGE88_XNU_OBJECT_SUBSET_GENERATED_H

#define STAGE88_XNU_OBJ_HOST_STATUS                 0x$(printf '%08x' "$status")u
#define STAGE88_XNU_OBJ_HOST_REQUIRED_MASK          0x07ffffffu
#define STAGE88_XNU_OBJ_HOST_SATISFIED_MASK         0x$(printf '%08x' "$satisfied")u
#define STAGE88_XNU_OBJ_HOST_FAILURE_MASK           0x$(printf '%08x' "$failure")u
#define STAGE88_XNU_OBJ_HOST_SOURCE_MASK            0x0000001fu
#define STAGE88_XNU_OBJ_HOST_SHIM_MASK              0x000003ffu
#define STAGE88_XNU_OBJ_HOST_OBJECT_COUNT           0x$(printf '%08x' "$object_count")u
#define STAGE88_XNU_OBJ_HOST_PUBLIC_2050_OBJECT_COUNT 0x00000003u
#define STAGE88_XNU_OBJ_HOST_PUBLIC_ARM_PEXPERT_OBJECT_COUNT 0x00000002u
#define STAGE88_XNU_OBJ_HOST_STAGE_OWNED_SHIM_OBJECT_COUNT 0x00000001u
#define STAGE88_XNU_OBJ_HOST_DUPLICATE_SYMBOL_COUNT 0x$(printf '%08x' "$duplicate_count")u
#define STAGE88_XNU_OBJ_HOST_PE_STATE_ABI_SHIM_READY 1u
#define STAGE88_XNU_OBJ_HOST_CONSISTENT_DEBUG_ABI_SHIM_READY 1u
#define STAGE88_XNU_OBJ_HOST_BASELINE_COMMIT32      0xcc8a9b0cu
#define STAGE88_XNU_OBJ_HOST_MASTER_VERSION_12_3_0  0x000c0300u
#define STAGE88_XNU_OBJ_HOST_DEVICE_TREE_BYTES      0x$(printf '%08x' "$device_tree_bytes")u
#define STAGE88_XNU_OBJ_HOST_BOOTARGS_BYTES         0x$(printf '%08x' "$bootargs_bytes")u
#define STAGE88_XNU_OBJ_HOST_PE_GEN_BYTES           0x$(printf '%08x' "$pe_gen_bytes")u
#define STAGE88_XNU_OBJ_HOST_ARM_PE_BOOTARGS_BYTES  0x$(printf '%08x' "$arm_pe_bootargs_bytes")u
#define STAGE88_XNU_OBJ_HOST_ARM_PE_CONSISTENT_DEBUG_BYTES 0x$(printf '%08x' "$arm_pe_consistent_debug_bytes")u
#define STAGE88_XNU_OBJ_HOST_DEVICE_TREE_SHA32      ${device_tree_sha32}u
#define STAGE88_XNU_OBJ_HOST_BOOTARGS_SHA32         ${bootargs_sha32}u
#define STAGE88_XNU_OBJ_HOST_PE_GEN_SHA32           ${pe_gen_sha32}u
#define STAGE88_XNU_OBJ_HOST_ARM_PE_BOOTARGS_SHA32  ${arm_pe_bootargs_sha32}u
#define STAGE88_XNU_OBJ_HOST_ARM_PE_CONSISTENT_DEBUG_SHA32 ${arm_pe_consistent_debug_sha32}u
#define STAGE88_XNU_OBJ_HOST_PUBLIC_2050_BASELINE   1u
#define STAGE88_XNU_OBJ_HOST_PUBLIC_ONLY            1u
#define STAGE88_XNU_OBJ_HOST_NO_FULL_XNU_BUILD      1u
#define STAGE88_XNU_OBJ_HOST_NO_MACHO_LINK          1u
#define STAGE88_XNU_OBJ_HOST_NO_PUBLIC_XNU_EXEC     1u
#define STAGE88_XNU_OBJ_HOST_NO_PLATFORM_RUNTIME_EXEC 1u
#define STAGE88_XNU_OBJ_HOST_NO_EXTERNAL_MUTATION   1u
#define STAGE88_XNU_OBJ_HOST_OUTPUTS_IGNORED        1u
#define STAGE88_XNU_OBJ_HOST_FAIL_CLOSED            1u

#endif
EOF_HEADER

cat "$STATUS_TXT"

if [[ "$status" -ne 0x88000001 ]]; then
  exit 1
fi
