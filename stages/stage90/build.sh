#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

# This snapshot lives at stages/stageNN/, two levels below the repo root.
STAGE_DIR=$PWD
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)
mkdir -p $REPO_ROOT/out/stage90

./xnu_workspace_validate.sh $REPO_ROOT/out/stage90
./xnu_compile_graph_scan.py $REPO_ROOT/out/stage90
./xnu_object_subset_compile.sh $REPO_ROOT/out/stage90
./xnu_link_proof.sh $REPO_ROOT/out/stage90

PYTHON=${PYTHON:-python3}
MACHO_FIXTURE_GEN=${MACHO_FIXTURE_GEN:-$REPO_ROOT/tools/mkmacho_fixture.py}

# Regenerate the inert non-proprietary Mach-O fixture from the host tool so the
# checked-in macho_fixture.c stays reproducible. The raw fixture stays under
# the ignored out/ directory.
"$PYTHON" "$MACHO_FIXTURE_GEN" \
  --c-output macho_fixture.c \
  --bin-output $REPO_ROOT/out/stage90/stage90_fixture.macho \
  --symbol-prefix stage90 \
  --metadata-file $REPO_ROOT/out/stage90/xnu-link-macho-metadata.txt

CC=${CC:-arm-none-eabi-gcc}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}
OBJDUMP=${OBJDUMP:-arm-none-eabi-objdump}
NM=${NM:-arm-none-eabi-nm}
SIZE=${SIZE:-arm-none-eabi-size}
MKBOOTIMG=${MKBOOTIMG:-mkbootimg}
QCDT_PACKER=${QCDT_PACKER:-$REPO_ROOT/tools/mkbootimg_v0_qcdt.py}
QCDT_DT=${QCDT_DT:-$REPO_ROOT/xiaomi4-cancro-backup-20260604-112053/boot-unpacked/dt.img}

CFLAGS=(
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
  -std=c11
  -I$REPO_ROOT/out/stage90
)

# Build a switch variant without editing stage90.h, e.g.
#   STAGE90_EXTRA_CFLAGS='-DSTAGE90_EXCLUSIVE_PROBE=1' ./build.sh
# These land in CFLAGS, so the -dM config dump below records them and
# preflight_boot_check.sh gates on what the image was actually built with.
# shellcheck disable=SC2206
if [[ -n ${STAGE90_EXTRA_CFLAGS:-} ]]; then
  CFLAGS+=($STAGE90_EXTRA_CFLAGS)
fi

LDFLAGS=(
  -nostdlib
  -Wl,-T,linker.ld
  -Wl,--build-id=none
  -Wl,-Map,$REPO_ROOT/out/stage90/stage90.map
)

SOURCES=(
  start.S
  vectors.S
  runtime.c
  ram_console.c
  apple_dt.c
  boot_args.c
  probes.c
  timebase.c
  xnu_log.c
  xnu_timebase.c
  pexpert.c
  pe_state.c
  gic.c
  exclusive_probe.c
  hw_watchdog.c
  macho_fixture.c
  macho_probe.c
  mmu.c
  xnu_workspace.c
  xnu_compile_graph.c
  xnu_object_subset.c
  xnu_link.c
  xnu_bootstrap_contract.c
  xnu_pmap_bootstrap_contract.c
  xnu_pmap_table_dryrun_contract.c
  xnu_pmap_page_dryrun_contract.c
  xnu_pmap_attr_dryrun_contract.c
  xnu_pmap_multiwindow_dryrun_contract.c
  xnu_pmap_transition_dryrun_contract.c
  xnu_pexpert_hook_readiness_contract.c
  xnu_iokit_platform_scaffold_contract.c
  xnu_iokit_match_dryrun_contract.c
  xnu_iokit_registry_service_dryrun_contract.c
  xnu_iokit_provider_plane_dryrun_contract.c
  xnu_iokit_catalog_property_dryrun_contract.c
  xnu_iokit_property_inheritance_dryrun_contract.c
  xnu_iokit_registry_topology_dryrun_contract.c
  xnu_iokit_attach_start_readiness_dryrun_contract.c
  xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.c
  xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.c
  xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.c
  xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract.c
  xnu_entry_start.S
  xnu_entry_stub.c
  xnu_early_pmap_platform_init.c
  xnu_pe_init_platform_false.c
  xnu_arm_init_post_pe_bootstrap.c
  xnu_arm_vm_init_full_pmap.c
  xnu_arm_vm_init_high_va_code_exec.c
  xnu_arm_vm_init_high_va_irq_handler.c
  xnu_arm_vm_init_high_va_data_abort_handler.c
  xnu_arm_vm_init_high_va_undef_handler.c
  xnu_macho_loader.c
  xnu_handoff.c
  xnu_kernel.c
  stage90_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="$REPO_ROOT/out/stage90/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o $REPO_ROOT/out/stage90/stage90.elf
$OBJCOPY -O binary $REPO_ROOT/out/stage90/stage90.elf $REPO_ROOT/out/stage90/stage90.bin
$OBJDUMP -d $REPO_ROOT/out/stage90/stage90.elf > $REPO_ROOT/out/stage90/stage90.disasm
$NM -n $REPO_ROOT/out/stage90/stage90.elf > $REPO_ROOT/out/stage90/stage90.symbols
$SIZE $REPO_ROOT/out/stage90/stage90.elf > $REPO_ROOT/out/stage90/stage90.size

: > $REPO_ROOT/out/stage90/empty-ramdisk

$MKBOOTIMG \
  --kernel $REPO_ROOT/out/stage90/stage90.bin \
  --ramdisk $REPO_ROOT/out/stage90/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage90 mi4ios6=stage90 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved xnu-entry-stub xnu-early-init early-pmap-platform xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock xnu-armvm live-pmap ttbr-live tlb-live pmap-restore prevm-pexpert dtinit-facts peid-machine xnu-bs-contract xnu-pmap-bs-contract xnu-pmap-table-contract xnu-pmap-page-contract xnu-pmap-attr-contract xnu-pmap-mw-contract xnu-pmap-trans-contract xnu-pexpert-hook xnu-iokit-platform xnu-iokit-match-contract xnu-iokit-regsvc-contract xnu-iokit-provider xnu-iokit-catalog xnu-iokit-propinh xnu-iokit-regtop pexpert-hook-ready iokit-platform-scaffold iokit-match-local iokit-regsvc-local iokit-provider-local iokit-catalog-local iokit-propinh-local iokit-regtop-local irq-timer-hook pmap-bootstrap-ref public-xnu-workspace public-xnu-compile-graph public-xnu-platform-graph public-xnu-object-subset xnu-controlled-link xnu-bounded-pe-gen xnu-arm-pe-bootargs xnu-arm-consistent-debug stage90-xnu-link-proof inert-macho-fixture cancro-target arm-init-stub no-full-xnu-build no-pub-start no-pub-arm-init no-pub-thread no-pub-cpuboot no-pub-rtclock no-pub-pmap no-pub-pexpert no-pub-peinit no-pub-dtinit no-pub-peid no-pub-armvm no-iokit-runtime-exec no-macho-exec no-cache-change no-persist-write no-external-mutation' \
  --header_version 0 \
  --output $REPO_ROOT/out/stage90/stage90.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel $REPO_ROOT/out/stage90/stage90.bin \
    --ramdisk $REPO_ROOT/out/stage90/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage90 mi4ios6=stage90 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved xnu-entry-stub xnu-early-init early-pmap-platform xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock xnu-armvm live-pmap ttbr-live tlb-live pmap-restore prevm-pexpert dtinit-facts peid-machine xnu-bs-contract xnu-pmap-bs-contract xnu-pmap-table-contract xnu-pmap-page-contract xnu-pmap-attr-contract xnu-pmap-mw-contract xnu-pmap-trans-contract xnu-pexpert-hook xnu-iokit-platform xnu-iokit-match-contract xnu-iokit-regsvc-contract xnu-iokit-provider xnu-iokit-catalog xnu-iokit-propinh xnu-iokit-regtop pexpert-hook-ready iokit-platform-scaffold iokit-match-local iokit-regsvc-local iokit-provider-local iokit-catalog-local iokit-propinh-local iokit-regtop-local irq-timer-hook pmap-bootstrap-ref public-xnu-workspace public-xnu-compile-graph public-xnu-platform-graph public-xnu-object-subset xnu-controlled-link xnu-bounded-pe-gen xnu-arm-pe-bootargs xnu-arm-consistent-debug stage90-xnu-link-proof inert-macho-fixture cancro-target arm-init-stub no-full-xnu-build no-pub-start no-pub-arm-init no-pub-thread no-pub-cpuboot no-pub-rtclock no-pub-pmap no-pub-pexpert no-pub-peinit no-pub-dtinit no-pub-peid no-pub-armvm no-iokit-runtime-exec no-macho-exec no-cache-change no-persist-write no-external-mutation' \
    --output $REPO_ROOT/out/stage90/stage90-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage90-qcdt.img" >&2
fi

sha256sum $REPO_ROOT/out/stage90/stage90_fixture.macho $REPO_ROOT/out/stage90/stage90.elf $REPO_ROOT/out/stage90/stage90.bin $REPO_ROOT/out/stage90/stage90.img \
  $(if [[ -f $REPO_ROOT/out/stage90/stage90-qcdt.img ]]; then printf '%s' $REPO_ROOT/out/stage90/stage90-qcdt.img; fi) \
  > $REPO_ROOT/out/stage90/SHA256SUMS.txt

# Record the switches this image was actually built with, so preflight_boot_check.sh
# can gate a hardware run on them instead of on what the source is assumed to say.
$CC "${CFLAGS[@]}" -E -dM -include stage90.h - </dev/null \
  | grep -E '^#define STAGE90_(HANDOFF_MODE|ENTRY_LADDER_LEVEL|DEADMAN_ENABLE|DEADMAN_SELFTEST|BYPASS_ENTRY_STUB|EXCLUSIVE_PROBE|PMAP_ATTR_MODE|HW_WATCHDOG|HW_WATCHDOG_SELFTEST) ' \
  > $REPO_ROOT/out/stage90/stage90-build-config.txt
cat $REPO_ROOT/out/stage90/stage90-build-config.txt

ls -l $REPO_ROOT/out/stage90/stage90.elf $REPO_ROOT/out/stage90/stage90.bin $REPO_ROOT/out/stage90/stage90.img \
  $(if [[ -f $REPO_ROOT/out/stage90/stage90-qcdt.img ]]; then printf '%s' $REPO_ROOT/out/stage90/stage90-qcdt.img; fi)
cat $REPO_ROOT/out/stage90/stage90.size
cat $REPO_ROOT/out/stage90/SHA256SUMS.txt
