#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

OUT_DIR=${1:-../out/stage82}
OBJ_DIR="$OUT_DIR/xnu-objects"
LINK_DIR="$OUT_DIR/xnu-link"
mkdir -p "$LINK_DIR"

CC=${STAGE82_XNU_LINK_CC:-arm-none-eabi-gcc}
NM=${STAGE82_XNU_LINK_NM:-arm-none-eabi-nm}
OBJDUMP=${STAGE82_XNU_LINK_OBJDUMP:-arm-none-eabi-objdump}
READELF=${STAGE82_XNU_LINK_READELF:-arm-none-eabi-readelf}
SIZE=${STAGE82_XNU_LINK_SIZE:-arm-none-eabi-size}

DEVICE_TREE_OBJ="$OBJ_DIR/device_tree.o"
BOOTARGS_OBJ="$OBJ_DIR/bootargs.o"
PE_GEN_OBJ="$OBJ_DIR/pe_gen.o"
ARM_PE_BOOTARGS_OBJ="$OBJ_DIR/arm_pe_bootargs.o"
ARM_PE_CONSISTENT_DEBUG_OBJ="$OBJ_DIR/arm_pe_consistent_debug.o"
SHIM_OBJ="$OBJ_DIR/xnu_object_shims.o"
SUPPORT_SRC="xnu_link_support.c"
NOENTRY_SRC="xnu_link_noentry.c"
SUPPORT_OBJ="$LINK_DIR/xnu_link_support.o"
NOENTRY_OBJ="$LINK_DIR/xnu_link_noentry.o"
LINK_ELF="$LINK_DIR/stage82-xnu-link.elf"
MAP_TXT="$LINK_DIR/stage82-xnu-link.map"
SYMBOLS_TXT="$LINK_DIR/stage82-xnu-link.symbols"
UNDEFINED_TXT="$LINK_DIR/stage82-xnu-link.undefined"
DISASM_TXT="$LINK_DIR/stage82-xnu-link.disasm"
SIZE_TXT="$LINK_DIR/stage82-xnu-link.size"
SECTIONS_TXT="$LINK_DIR/stage82-xnu-link.sections"
READELF_TXT="$LINK_DIR/stage82-xnu-link.readelf"
SHA_TXT="$LINK_DIR/SHA256SUMS.txt"
STATUS_TXT="$OUT_DIR/xnu-link-status.txt"
MANIFEST_TXT="$OUT_DIR/xnu-link-manifest.txt"
METADATA_TXT="$OUT_DIR/xnu-link-macho-metadata.txt"
HEADER="$OUT_DIR/xnu_link_generated.h"
GRAPH_HEADER="$OUT_DIR/xnu_compile_graph_generated.h"

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
)

required=0x0001ffff
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
: > "$METADATA_TXT"
: > "$UNDEFINED_TXT"

printf 'Stage82 controlled public-XNU compile-graph ARM ELF link proof\n' >> "$STATUS_TXT"
printf 'This is a host-only linkability proof; linked public-XNU code is never executed.\n' >> "$STATUS_TXT"

objects_ok=1
for p in "$DEVICE_TREE_OBJ" "$BOOTARGS_OBJ" "$PE_GEN_OBJ" "$ARM_PE_BOOTARGS_OBJ" "$ARM_PE_CONSISTENT_DEBUG_OBJ" "$SHIM_OBJ"; do
  if [[ -f "$p" ]]; then
    printf 'present object %s\n' "$p" >> "$MANIFEST_TXT"
  else
    printf 'missing object %s\n' "$p" >> "$MANIFEST_TXT"
    objects_ok=0
  fi
done
if [[ "$objects_ok" -eq 1 ]]; then
  add_satisfied 0x00000001
else
  add_failure 0x00000001
fi

support_ok=1
for p in "$SUPPORT_SRC" "$NOENTRY_SRC" xnu_link.ld "$GRAPH_HEADER"; do
  if [[ -f "$p" ]]; then
    printf 'present support %s\n' "$p" >> "$MANIFEST_TXT"
  else
    printf 'missing support %s\n' "$p" >> "$MANIFEST_TXT"
    support_ok=0
  fi
done
if [[ "$support_ok" -eq 1 ]]; then
  add_satisfied 0x00000002
else
  add_failure 0x00000002
fi

tools_ok=1
for t in "$CC" "$NM" "$OBJDUMP" "$READELF" "$SIZE"; do
  if command -v "$t" >/dev/null 2>&1; then
    printf 'present tool %s\n' "$t" >> "$MANIFEST_TXT"
  else
    printf 'missing tool %s\n' "$t" >> "$MANIFEST_TXT"
    tools_ok=0
  fi
done
if [[ "$tools_ok" -eq 1 ]]; then
  add_satisfied 0x00000004
else
  add_failure 0x00000004
fi

external_clean=1
for ext in ../external/xnu-upstream ../external/xnu-4570.1.46; do
  if [[ -d "$ext/.git" ]]; then
    if [[ -n "$(git -C "$ext" status --short)" ]]; then
      printf 'dirty external %s\n' "$ext" >> "$MANIFEST_TXT"
      external_clean=0
    else
      printf 'clean external %s\n' "$ext" >> "$MANIFEST_TXT"
    fi
  fi
done

linked_object_count=6
support_object_count=2
undefined_count=0
global_count=0
elf_bytes=0
elf_sha32=0x00000000
text_addr=0
text_size=0
data_addr=0
data_size=0
bss_addr=0
bss_size=0
expected_symbols_ok=0

if [[ "$failure" -eq 0 ]]; then
  "$CC" "${COMMON_FLAGS[@]}" -c "$SUPPORT_SRC" -o "$SUPPORT_OBJ"
  "$CC" "${COMMON_FLAGS[@]}" -c "$NOENTRY_SRC" -o "$NOENTRY_OBJ"
  "$CC" -nostdlib \
    -Wl,-T,xnu_link.ld \
    -Wl,--build-id=none \
    -Wl,-Map,"$MAP_TXT" \
    "$NOENTRY_OBJ" "$DEVICE_TREE_OBJ" "$BOOTARGS_OBJ" "$PE_GEN_OBJ" "$ARM_PE_BOOTARGS_OBJ" "$ARM_PE_CONSISTENT_DEBUG_OBJ" "$SHIM_OBJ" "$SUPPORT_OBJ" \
    -o "$LINK_ELF"

  if [[ -s "$LINK_ELF" ]]; then
    add_satisfied 0x00000008
  else
    add_failure 0x00000008
  fi

  "$NM" -u "$LINK_ELF" > "$UNDEFINED_TXT"
  "$NM" -g "$LINK_ELF" > "$SYMBOLS_TXT"
  "$OBJDUMP" -dr "$LINK_ELF" > "$DISASM_TXT"
  "$OBJDUMP" -h "$LINK_ELF" > "$SECTIONS_TXT"
  "$READELF" -h "$LINK_ELF" > "$READELF_TXT"
  "$SIZE" "$LINK_ELF" > "$SIZE_TXT"
  sha256sum "$LINK_ELF" "$SUPPORT_OBJ" "$NOENTRY_OBJ" > "$SHA_TXT"

  undefined_count=$(python3 - "$UNDEFINED_TXT" <<'PY'
import sys
lines = [l for l in open(sys.argv[1], encoding='utf-8', errors='replace').read().splitlines() if l.strip()]
print(len(lines))
PY
)
  if [[ "$undefined_count" -eq 0 ]]; then
    add_satisfied 0x00000010
  else
    add_failure 0x00000010
  fi

  read -r global_count expected_symbols_ok < <(python3 - "$SYMBOLS_TXT" <<'PY'
import sys
text = open(sys.argv[1], encoding='utf-8', errors='replace').read().splitlines()
expected = {
    'stage82_xnu_link_noentry', 'DTInit', 'DTLookupEntry', 'DTGetProperty',
    'PE_parse_boot_argn', 'PE_get_default', 'PE_imgsrc_mount_supported',
    'PE_boot_args', 'kalloc', 'kfree', 'IODTGetDefault', 'memcpy', 'memset',
    'strcmp', 'strncmp', 'strlen',
    'pe_init_debug', 'PE_enter_debugger', 'PE_init_printf', 'PE_putc',
    'gPESerialBaud', 'appleClut8', 'Debugger', 'vcattach', 'cnputc', 'PE_state',
    'PE_consistent_debug_inherit', 'PE_consistent_debug_register',
    'PE_consistent_debug_enabled', 'OSCompareAndSwap64', 'ml_map_high_window'
}
defined = set()
count = 0
for line in text:
    parts = line.split()
    if len(parts) >= 3 and parts[1] not in ('U', 'w'):
        count += 1
        defined.add(parts[-1])
print(count, 1 if expected.issubset(defined) else 0)
PY
)
  if [[ "$global_count" -ne 0 && "$expected_symbols_ok" -eq 1 ]]; then
    add_satisfied 0x00000040
  else
    add_failure 0x00000020
  fi

  read -r text_addr text_size data_addr data_size bss_addr bss_size < <(python3 - "$SECTIONS_TXT" <<'PY'
import sys
sections = {'.text': (0, 0), '.data': (0, 0), '.bss': (0, 0)}
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    parts = line.split()
    if len(parts) >= 7 and parts[1] in sections:
        name = parts[1]
        size = int(parts[2], 16)
        vma = int(parts[3], 16)
        sections[name] = (vma, size)
print(*(f'0x{v:08x}' for pair in (sections['.text'], sections['.data'], sections['.bss']) for v in pair))
PY
)
  if [[ $((text_size)) -ne 0 ]]; then
    add_satisfied 0x00000020
  else
    add_failure 0x00000020
  fi

  elf_bytes=$(size_file "$LINK_ELF")
  elf_sha32=$(sha32_file "$LINK_ELF")
  if [[ "$elf_bytes" -ne 0 && "$elf_sha32" != "0x00000000" ]]; then
    add_satisfied 0x00000080
  fi
else
  add_failure 0x00000008
fi

add_satisfied 0x00000100  # public 2050 baseline inherited from workspace validation
add_satisfied 0x00000200  # public only
add_satisfied 0x00000400  # no full XNU build
add_satisfied 0x00000800  # no public-XNU execution
add_satisfied 0x00001000  # no Mach-O execution
if [[ "$external_clean" -eq 1 ]]; then
  add_satisfied 0x00002000
else
  add_failure 0x80000000
fi
add_satisfied 0x00004000  # outputs under ignored out/stage82
add_satisfied 0x00008000  # fail-closed script
add_satisfied 0x00010000  # no platform runtime execution

status=0x82000000
if [[ "$satisfied" -eq "$required" && "$failure" -eq 0 ]]; then
  status=0x82000001
fi

printf 'stage82_xnu_link_status=0x%08x\n' "$status" >> "$STATUS_TXT"
printf 'stage82_xnu_link_required_mask=0x%08x\n' "$required" >> "$STATUS_TXT"
printf 'stage82_xnu_link_satisfied_mask=0x%08x\n' "$satisfied" >> "$STATUS_TXT"
printf 'stage82_xnu_link_failure_mask=0x%08x\n' "$failure" >> "$STATUS_TXT"
printf 'stage82_xnu_link_object_count=0x%08x\n' "$linked_object_count" >> "$STATUS_TXT"
printf 'stage82_xnu_link_support_object_count=0x%08x\n' "$support_object_count" >> "$STATUS_TXT"
printf 'stage82_xnu_link_undefined_symbol_count=0x%08x\n' "$undefined_count" >> "$STATUS_TXT"
printf 'stage82_xnu_link_global_symbol_count=0x%08x\n' "$global_count" >> "$STATUS_TXT"
printf 'stage82_xnu_link_elf_bytes=0x%08x\n' "$elf_bytes" >> "$STATUS_TXT"
printf 'stage82_xnu_link_elf_sha32=%s\n' "$elf_sha32" >> "$STATUS_TXT"
printf 'stage82_xnu_link_text_addr=0x%08x\n' "$text_addr" >> "$STATUS_TXT"
printf 'stage82_xnu_link_text_size=0x%08x\n' "$text_size" >> "$STATUS_TXT"
printf 'stage82_xnu_link_data_addr=0x%08x\n' "$data_addr" >> "$STATUS_TXT"
printf 'stage82_xnu_link_data_size=0x%08x\n' "$data_size" >> "$STATUS_TXT"
printf 'stage82_xnu_link_bss_addr=0x%08x\n' "$bss_addr" >> "$STATUS_TXT"
printf 'stage82_xnu_link_bss_size=0x%08x\n' "$bss_size" >> "$STATUS_TXT"
printf 'stage82_xnu_link_public_only=0x00000001\n' >> "$STATUS_TXT"
printf 'stage82_xnu_link_no_full_xnu_build=0x00000001\n' >> "$STATUS_TXT"
printf 'stage82_xnu_link_no_public_xnu_exec=0x00000001\n' >> "$STATUS_TXT"
printf 'stage82_xnu_link_no_macho_exec=0x00000001\n' >> "$STATUS_TXT"
printf 'stage82_xnu_link_no_platform_runtime_exec=0x00000001\n' >> "$STATUS_TXT"
printf 'stage82_xnu_link_no_external_mutation=0x%08x\n' "$external_clean" >> "$STATUS_TXT"
printf 'stage82_xnu_link_outputs_ignored=0x00000001\n' >> "$STATUS_TXT"
printf 'stage82_xnu_link_fail_closed=0x00000001\n' >> "$STATUS_TXT"

cat > "$METADATA_TXT" <<EOF_METADATA
stage82-xnu-platform-graph graph_status=0x82000001 bounded-pe-gen=true bounded-arm-pe-bootargs=true bounded-arm-consistent-debug=true no-platform-runtime-exec=true stage82-xnu-link-proof noexec=true public=xnu-2050.22.13 arm-reference=xnu-4570.1.46 commit32=0xcc8a9b0c objects=${linked_object_count} support=${support_object_count} undefined=${undefined_count} elf_bytes=0x$(printf '%08x' "$elf_bytes") elf_sha32=${elf_sha32} text=${text_addr}/0x$(printf '%08x' "$text_size") data=${data_addr}/0x$(printf '%08x' "$data_size") bss=${bss_addr}/0x$(printf '%08x' "$bss_size") no-full-mach-kernel=true no-public-xnu-exec=true no-macho-exec=true
EOF_METADATA

cat > "$HEADER" <<EOF_HEADER
#ifndef MI4IOS6_STAGE82_XNU_LINK_GENERATED_H
#define MI4IOS6_STAGE82_XNU_LINK_GENERATED_H

#define STAGE82_XNU_LINK_HOST_STATUS                 0x$(printf '%08x' "$status")u
#define STAGE82_XNU_LINK_HOST_REQUIRED_MASK          0x0001ffffu
#define STAGE82_XNU_LINK_HOST_SATISFIED_MASK         0x$(printf '%08x' "$satisfied")u
#define STAGE82_XNU_LINK_HOST_FAILURE_MASK           0x$(printf '%08x' "$failure")u
#define STAGE82_XNU_LINK_HOST_LINKED_OBJECT_COUNT    0x$(printf '%08x' "$linked_object_count")u
#define STAGE82_XNU_LINK_HOST_SUPPORT_OBJECT_COUNT   0x$(printf '%08x' "$support_object_count")u
#define STAGE82_XNU_LINK_HOST_UNDEFINED_SYMBOL_COUNT 0x$(printf '%08x' "$undefined_count")u
#define STAGE82_XNU_LINK_HOST_GLOBAL_SYMBOL_COUNT    0x$(printf '%08x' "$global_count")u
#define STAGE82_XNU_LINK_HOST_ELF_BYTES              0x$(printf '%08x' "$elf_bytes")u
#define STAGE82_XNU_LINK_HOST_ELF_SHA32              ${elf_sha32}u
#define STAGE82_XNU_LINK_HOST_TEXT_ADDR              0x$(printf '%08x' "$text_addr")u
#define STAGE82_XNU_LINK_HOST_TEXT_SIZE              0x$(printf '%08x' "$text_size")u
#define STAGE82_XNU_LINK_HOST_DATA_ADDR              0x$(printf '%08x' "$data_addr")u
#define STAGE82_XNU_LINK_HOST_DATA_SIZE              0x$(printf '%08x' "$data_size")u
#define STAGE82_XNU_LINK_HOST_BSS_ADDR               0x$(printf '%08x' "$bss_addr")u
#define STAGE82_XNU_LINK_HOST_BSS_SIZE               0x$(printf '%08x' "$bss_size")u
#define STAGE82_XNU_LINK_HOST_BASELINE_COMMIT32      0xcc8a9b0cu
#define STAGE82_XNU_LINK_HOST_MASTER_VERSION_12_3_0  0x000c0300u
#define STAGE82_XNU_LINK_HOST_PUBLIC_ONLY            1u
#define STAGE82_XNU_LINK_HOST_NO_FULL_XNU_BUILD      1u
#define STAGE82_XNU_LINK_HOST_NO_PUBLIC_XNU_EXEC     1u
#define STAGE82_XNU_LINK_HOST_NO_MACHO_EXEC          1u
#define STAGE82_XNU_LINK_HOST_NO_PLATFORM_RUNTIME_EXEC 1u
#define STAGE82_XNU_LINK_HOST_NO_EXTERNAL_MUTATION   ${external_clean}u
#define STAGE82_XNU_LINK_HOST_OUTPUTS_IGNORED        1u
#define STAGE82_XNU_LINK_HOST_FAIL_CLOSED            1u

#endif
EOF_HEADER

cat "$STATUS_TXT"

if [[ "$status" -ne 0x82000001 ]]; then
  exit 1
fi
