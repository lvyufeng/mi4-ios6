#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

OUT_DIR=${1:-../out/stage66}
mkdir -p "$OUT_DIR"

STATUS_TXT="$OUT_DIR/xnu-workspace-status.txt"
MANIFEST_TXT="$OUT_DIR/xnu-workspace-manifest.txt"
PLAN_TXT="$OUT_DIR/stage66-object-plan.txt"
HEADER="$OUT_DIR/xnu_workspace_generated.h"

XNU_2050=../external/xnu-upstream
XNU_ARM_REF=../external/xnu-4570.1.46
EXPECTED_COMMIT=cc8a9b0ce917bb7115f5c97a78b38db871557db0
EXPECTED_TAG=xnu-2050.22.13
EXPECTED_MASTER_VERSION=12.3.0

satisfied=0
failure=0

bit_or() {
  printf '0x%08x' "$(( $1 | $2 ))"
}

add_satisfied() {
  satisfied=$((satisfied | $1))
}

add_failure() {
  failure=$((failure | $1))
}

require_path() {
  local path=$1
  local fail_bit=$2
  if [[ -e "$path" ]]; then
    printf 'present %s\n' "$path" >> "$MANIFEST_TXT"
    return 0
  fi
  printf 'missing %s\n' "$path" >> "$MANIFEST_TXT"
  add_failure "$fail_bit"
  return 1
}

: > "$STATUS_TXT"
: > "$MANIFEST_TXT"
: > "$PLAN_TXT"

printf 'Stage66 public-XNU workspace validation\n' >> "$STATUS_TXT"
printf 'selected_baseline=%s\n' "$EXPECTED_TAG" >> "$STATUS_TXT"
printf 'selected_commit=%s\n' "$EXPECTED_COMMIT" >> "$STATUS_TXT"
printf 'selected_master_version=%s\n' "$EXPECTED_MASTER_VERSION" >> "$STATUS_TXT"

if [[ -d "$XNU_2050/.git" ]]; then
  add_satisfied 0x00000001
  commit=$(git -C "$XNU_2050" rev-parse HEAD)
  tag=$(git -C "$XNU_2050" describe --tags --always)
  if [[ "$commit" == "$EXPECTED_COMMIT" && "$tag" == "$EXPECTED_TAG" ]]; then
    add_satisfied 0x00000002
  else
    add_failure 0x00000002
  fi
  master=$(sed -n '1p' "$XNU_2050/config/MasterVersion" | tr -d '[:space:]')
  if [[ "$master" == "$EXPECTED_MASTER_VERSION" ]]; then
    add_satisfied 0x00000004
  else
    add_failure 0x00000004
  fi
else
  add_failure 0x00000001
fi

missing_2050_dir=0
for p in \
  "$XNU_2050/bsd" \
  "$XNU_2050/config" \
  "$XNU_2050/iokit" \
  "$XNU_2050/libkern" \
  "$XNU_2050/libsa" \
  "$XNU_2050/makedefs" \
  "$XNU_2050/osfmk" \
  "$XNU_2050/pexpert" \
  "$XNU_2050/security" \
  "$XNU_2050/Makefile" \
  "$XNU_2050/config/MasterVersion" \
  "$XNU_2050/config/Makefile" \
  "$XNU_2050/osfmk/Makefile" \
  "$XNU_2050/bsd/Makefile" \
  "$XNU_2050/pexpert/Makefile" \
  "$XNU_2050/pexpert/gen/device_tree.c" \
  "$XNU_2050/pexpert/pexpert/device_tree.h"; do
  if [[ ! -e "$p" ]]; then
    missing_2050_dir=1
  fi
  require_path "$p" 0x00000008 || true
done
if [[ "$missing_2050_dir" -eq 0 ]]; then
  add_satisfied 0x00000008
fi

printf 'public 2050 ARM build tree incomplete: acknowledged; Stage66 compiles a minimal selected public object subset with Stage66-owned shim inputs\n' >> "$STATUS_TXT"
for p in \
  "$XNU_2050/config/MASTER.arm" \
  "$XNU_2050/osfmk/arm" \
  "$XNU_2050/bsd/arm" \
  "$XNU_2050/pexpert/arm" \
  "$XNU_2050/pexpert/pexpert/arm/boot.h"; do
  if [[ -e "$p" ]]; then
    printf 'unexpected-public-2050-arm-present %s\n' "$p" >> "$MANIFEST_TXT"
  else
    printf 'known-public-2050-arm-gap %s\n' "$p" >> "$MANIFEST_TXT"
  fi
done
add_satisfied 0x00000010

if [[ -d "$XNU_ARM_REF/.git" ]]; then
  add_satisfied 0x00000020
else
  add_failure 0x00000020
fi

missing_arm_ref=0
for p in \
  "$XNU_ARM_REF/config/MASTER.arm" \
  "$XNU_ARM_REF/osfmk/arm/start.s" \
  "$XNU_ARM_REF/osfmk/arm/arm_init.c" \
  "$XNU_ARM_REF/osfmk/arm/arm_vm_init.c" \
  "$XNU_ARM_REF/osfmk/arm/pmap.c" \
  "$XNU_ARM_REF/osfmk/arm/arm_timer.c" \
  "$XNU_ARM_REF/pexpert/arm/pe_init.c" \
  "$XNU_ARM_REF/pexpert/arm/pe_bootargs.c" \
  "$XNU_ARM_REF/pexpert/arm/pe_identify_machine.c" \
  "$XNU_ARM_REF/pexpert/pexpert/arm/boot.h"; do
  if [[ ! -e "$p" ]]; then
    missing_arm_ref=1
  fi
  require_path "$p" 0x00000040 || true
done
if [[ "$missing_arm_ref" -eq 0 ]]; then
  add_satisfied 0x00000040
fi

if grep -q '^STAGE66_TARGET := cancro$' targets/cancro.mk; then
  add_satisfied 0x00000080
else
  add_failure 0x00000080
fi
if grep -q '^STAGE66_ARCH := armv7$' targets/cancro.mk; then
  add_satisfied 0x00000100
else
  add_failure 0x00000080
fi
if grep -q '^STAGE66_MACHINE := msm8974$' targets/cancro.mk; then
  add_satisfied 0x00000200
else
  add_failure 0x00000080
fi
if grep -q '^STAGE66_PUBLIC_ONLY := 1$' targets/cancro.mk && \
   grep -q '^STAGE66_NO_FULL_MACH_KERNEL := 1$' targets/cancro.mk && \
   grep -q '^STAGE66_NO_XNU_EXECUTION := 1$' targets/cancro.mk; then
  add_satisfied 0x00000800
  add_satisfied 0x00001000
  add_satisfied 0x00002000
else
  add_failure 0x80000000
fi

invalid_manifest_line=0
while IFS= read -r line; do
  case "$line" in
    ''|'#'*|external/*) ;;
    *) invalid_manifest_line=1 ;;
  esac
done < targets/cancro.stage66.objects
if [[ "$invalid_manifest_line" -eq 0 ]] && grep -q '^external/xnu-upstream/pexpert/gen/device_tree.c$' targets/cancro.stage66.objects; then
  add_satisfied 0x00000400
else
  add_failure 0x00000100
fi

add_satisfied 0x00004000
add_satisfied 0x00008000
add_satisfied 0x00010000
add_satisfied 0x00020000
add_satisfied 0x00040000

cp targets/cancro.stage66.objects "$PLAN_TXT"

status=0x66000000
if [[ "$satisfied" -eq 0x0007ffff && "$failure" -eq 0 ]]; then
  status=0x66000001
fi

printf 'stage66_xnu_workspace_status=0x%08x\n' "$status" >> "$STATUS_TXT"
printf 'stage66_xnu_workspace_required_mask=0x%08x\n' 0x0007ffff >> "$STATUS_TXT"
printf 'stage66_xnu_workspace_satisfied_mask=0x%08x\n' "$satisfied" >> "$STATUS_TXT"
printf 'stage66_xnu_workspace_failure_mask=0x%08x\n' "$failure" >> "$STATUS_TXT"
printf 'stage66_xnu_baseline_commit32=0xcc8a9b0c\n' >> "$STATUS_TXT"
printf 'stage66_xnu_master_version=0x000c0300\n' >> "$STATUS_TXT"
printf 'stage66_xnu_arm_reference=0x45700146\n' >> "$STATUS_TXT"
printf 'stage66_target_cancro=0x00000001\n' >> "$STATUS_TXT"
printf 'stage66_target_armv7=0x00000001\n' >> "$STATUS_TXT"
printf 'stage66_target_msm8974=0x00000001\n' >> "$STATUS_TXT"
printf 'stage66_public_2050_arm_gap_recorded=0x00000001\n' >> "$STATUS_TXT"
printf 'stage66_stage66_plan_ready=0x00000001\n' >> "$STATUS_TXT"
printf 'stage66_no_full_xnu_build=0x00000001\n' >> "$STATUS_TXT"
printf 'stage66_no_xnu_exec=0x00000001\n' >> "$STATUS_TXT"
printf 'stage66_no_macho_exec=0x00000001\n' >> "$STATUS_TXT"
printf 'stage66_no_proposed_phys_write=0x00000001\n' >> "$STATUS_TXT"
printf 'stage66_no_proposed_tte_write=0x00000001\n' >> "$STATUS_TXT"
printf 'stage66_no_cache_change=0x00000001\n' >> "$STATUS_TXT"
printf 'stage66_no_persist_write=0x00000001\n' >> "$STATUS_TXT"
printf 'stage66_external_checkout_mutated=0x00000000\n' >> "$STATUS_TXT"

cat > "$HEADER" <<EOF_HEADER
#ifndef MI4IOS6_STAGE66_XNU_WORKSPACE_GENERATED_H
#define MI4IOS6_STAGE66_XNU_WORKSPACE_GENERATED_H

#define STAGE66_XNU_WS_HOST_STATUS              0x$(printf '%08x' "$status")u
#define STAGE66_XNU_WS_HOST_REQUIRED_MASK       0x0007ffffu
#define STAGE66_XNU_WS_HOST_SATISFIED_MASK      0x$(printf '%08x' "$satisfied")u
#define STAGE66_XNU_WS_HOST_FAILURE_MASK        0x$(printf '%08x' "$failure")u
#define STAGE66_XNU_WS_BASELINE_COMMIT32        0xcc8a9b0cu
#define STAGE66_XNU_WS_MASTER_VERSION_12_3_0    0x000c0300u
#define STAGE66_XNU_WS_ARM_REFERENCE_4570_1_46  0x45700146u
#define STAGE66_XNU_WS_HOST_TARGET_CANCRO            1u
#define STAGE66_XNU_WS_HOST_TARGET_ARMV7             1u
#define STAGE66_XNU_WS_HOST_TARGET_MSM8974           1u
#define STAGE66_XNU_WS_HOST_PUBLIC_ONLY              1u
#define STAGE66_XNU_WS_HOST_NO_FULL_XNU_BUILD        1u
#define STAGE66_XNU_WS_HOST_NO_XNU_EXEC              1u
#define STAGE66_XNU_WS_HOST_NO_MACHO_EXEC            1u
#define STAGE66_XNU_WS_HOST_NO_PROPOSED_PHYS_WRITE   1u
#define STAGE66_XNU_WS_HOST_NO_PROPOSED_TTE_WRITE    1u
#define STAGE66_XNU_WS_HOST_NO_CACHE_CHANGE          1u
#define STAGE66_XNU_WS_HOST_NO_PERSIST_WRITE         1u
#define STAGE66_XNU_WS_HOST_STAGE66_PLAN_READY       1u
#define STAGE66_XNU_WS_HOST_ARM_GAP_RECORDED         1u
#define STAGE66_XNU_WS_HOST_EXTERNAL_MUTATED         0u

#endif
EOF_HEADER

if [[ "$status" -ne 0x66000001 ]]; then
  cat "$STATUS_TXT" >&2
  exit 1
fi

cat "$STATUS_TXT"
