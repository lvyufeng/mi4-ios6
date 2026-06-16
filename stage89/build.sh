#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage89

./xnu_workspace_validate.sh ../out/stage89
./xnu_compile_graph_scan.py ../out/stage89
./xnu_object_subset_compile.sh ../out/stage89
./xnu_link_proof.sh ../out/stage89

PYTHON=${PYTHON:-python3}
MACHO_FIXTURE_GEN=${MACHO_FIXTURE_GEN:-../tools/mkmacho_fixture.py}

# Regenerate the inert non-proprietary Mach-O fixture from the host tool so the
# checked-in macho_fixture.c stays reproducible. The raw fixture stays under
# the ignored out/ directory.
"$PYTHON" "$MACHO_FIXTURE_GEN" \
  --c-output macho_fixture.c \
  --bin-output ../out/stage89/stage89_fixture.macho \
  --symbol-prefix stage89 \
  --metadata-file ../out/stage89/xnu-link-macho-metadata.txt

CC=${CC:-arm-none-eabi-gcc}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}
OBJDUMP=${OBJDUMP:-arm-none-eabi-objdump}
NM=${NM:-arm-none-eabi-nm}
SIZE=${SIZE:-arm-none-eabi-size}
MKBOOTIMG=${MKBOOTIMG:-mkbootimg}
QCDT_PACKER=${QCDT_PACKER:-../tools/mkbootimg_v0_qcdt.py}
QCDT_DT=${QCDT_DT:-../xiaomi4-cancro-backup-20260604-112053/boot-unpacked/dt.img}

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
  -I../out/stage89
)

LDFLAGS=(
  -nostdlib
  -Wl,-T,linker.ld
  -Wl,--build-id=none
  -Wl,-Map,../out/stage89/stage89.map
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
  xnu_kernel.c
  stage89_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage89/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage89/stage89.elf
$OBJCOPY -O binary ../out/stage89/stage89.elf ../out/stage89/stage89.bin
$OBJDUMP -d ../out/stage89/stage89.elf > ../out/stage89/stage89.disasm
$NM -n ../out/stage89/stage89.elf > ../out/stage89/stage89.symbols
$SIZE ../out/stage89/stage89.elf > ../out/stage89/stage89.size

: > ../out/stage89/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage89/stage89.bin \
  --ramdisk ../out/stage89/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage89 mi4ios6=stage89 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved xnu-entry-stub xnu-early-init early-pmap-platform xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock xnu-armvm live-pmap ttbr-live tlb-live pmap-restore prevm-pexpert dtinit-facts peid-machine xnu-bs-contract xnu-pmap-bs-contract xnu-pmap-table-contract xnu-pmap-page-contract xnu-pmap-attr-contract xnu-pmap-mw-contract xnu-pmap-trans-contract xnu-pexpert-hook xnu-iokit-platform xnu-iokit-match-contract xnu-iokit-regsvc-contract xnu-iokit-provider xnu-iokit-catalog xnu-iokit-propinh xnu-iokit-regtop pexpert-hook-ready iokit-platform-scaffold iokit-match-local iokit-regsvc-local iokit-provider-local iokit-catalog-local iokit-propinh-local iokit-regtop-local irq-timer-hook pmap-bootstrap-ref public-xnu-workspace public-xnu-compile-graph public-xnu-platform-graph public-xnu-object-subset xnu-controlled-link xnu-bounded-pe-gen xnu-arm-pe-bootargs xnu-arm-consistent-debug stage89-xnu-link-proof inert-macho-fixture cancro-target arm-init-stub no-full-xnu-build no-pub-start no-pub-arm-init no-pub-thread no-pub-cpuboot no-pub-rtclock no-pub-pmap no-pub-pexpert no-pub-peinit no-pub-dtinit no-pub-peid no-pub-armvm no-iokit-runtime-exec no-macho-exec no-cache-change no-persist-write no-external-mutation' \
  --header_version 0 \
  --output ../out/stage89/stage89.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage89/stage89.bin \
    --ramdisk ../out/stage89/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage89 mi4ios6=stage89 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved xnu-entry-stub xnu-early-init early-pmap-platform xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock xnu-armvm live-pmap ttbr-live tlb-live pmap-restore prevm-pexpert dtinit-facts peid-machine xnu-bs-contract xnu-pmap-bs-contract xnu-pmap-table-contract xnu-pmap-page-contract xnu-pmap-attr-contract xnu-pmap-mw-contract xnu-pmap-trans-contract xnu-pexpert-hook xnu-iokit-platform xnu-iokit-match-contract xnu-iokit-regsvc-contract xnu-iokit-provider xnu-iokit-catalog xnu-iokit-propinh xnu-iokit-regtop pexpert-hook-ready iokit-platform-scaffold iokit-match-local iokit-regsvc-local iokit-provider-local iokit-catalog-local iokit-propinh-local iokit-regtop-local irq-timer-hook pmap-bootstrap-ref public-xnu-workspace public-xnu-compile-graph public-xnu-platform-graph public-xnu-object-subset xnu-controlled-link xnu-bounded-pe-gen xnu-arm-pe-bootargs xnu-arm-consistent-debug stage89-xnu-link-proof inert-macho-fixture cancro-target arm-init-stub no-full-xnu-build no-pub-start no-pub-arm-init no-pub-thread no-pub-cpuboot no-pub-rtclock no-pub-pmap no-pub-pexpert no-pub-peinit no-pub-dtinit no-pub-peid no-pub-armvm no-iokit-runtime-exec no-macho-exec no-cache-change no-persist-write no-external-mutation' \
    --output ../out/stage89/stage89-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage89-qcdt.img" >&2
fi

sha256sum ../out/stage89/stage89_fixture.macho ../out/stage89/stage89.elf ../out/stage89/stage89.bin ../out/stage89/stage89.img \
  $(if [[ -f ../out/stage89/stage89-qcdt.img ]]; then printf '%s' ../out/stage89/stage89-qcdt.img; fi) \
  > ../out/stage89/SHA256SUMS.txt

ls -l ../out/stage89/stage89.elf ../out/stage89/stage89.bin ../out/stage89/stage89.img \
  $(if [[ -f ../out/stage89/stage89-qcdt.img ]]; then printf '%s' ../out/stage89/stage89-qcdt.img; fi)
cat ../out/stage89/stage89.size
cat ../out/stage89/SHA256SUMS.txt
