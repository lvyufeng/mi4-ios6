#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage77

./xnu_workspace_validate.sh ../out/stage77
./xnu_compile_graph_scan.py ../out/stage77
./xnu_object_subset_compile.sh ../out/stage77
./xnu_link_proof.sh ../out/stage77

PYTHON=${PYTHON:-python3}
MACHO_FIXTURE_GEN=${MACHO_FIXTURE_GEN:-../tools/mkmacho_fixture.py}

# Regenerate the inert non-proprietary Mach-O fixture from the host tool so the
# checked-in macho_fixture.c stays reproducible. The raw fixture stays under
# the ignored out/ directory.
"$PYTHON" "$MACHO_FIXTURE_GEN" \
  --c-output macho_fixture.c \
  --bin-output ../out/stage77/stage77_fixture.macho \
  --symbol-prefix stage77 \
  --metadata-file ../out/stage77/xnu-link-macho-metadata.txt

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
  -I../out/stage77
)

LDFLAGS=(
  -nostdlib
  -Wl,-T,linker.ld
  -Wl,--build-id=none
  -Wl,-Map,../out/stage77/stage77.map
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
  xnu_kernel.c
  stage77_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage77/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage77/stage77.elf
$OBJCOPY -O binary ../out/stage77/stage77.elf ../out/stage77/stage77.bin
$OBJDUMP -d ../out/stage77/stage77.elf > ../out/stage77/stage77.disasm
$NM -n ../out/stage77/stage77.elf > ../out/stage77/stage77.symbols
$SIZE ../out/stage77/stage77.elf > ../out/stage77/stage77.size

: > ../out/stage77/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage77/stage77.bin \
  --ramdisk ../out/stage77/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage77 mi4ios6=stage77 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved xnu-entry-stub xnu-bs-contract xnu-pmap-bs-contract xnu-pmap-table-contract xnu-pmap-page-contract xnu-pmap-attr-contract xnu-pmap-mw-contract xnu-pmap-trans-contract xnu-pexpert-hook xnu-iokit-platform xnu-iokit-match-contract xnu-iokit-regsvc-contract xnu-iokit-provider xnu-iokit-catalog xnu-iokit-propinh xnu-iokit-regtop xnu-iokit-attachstart xnu-iokit-lifecycle xnu-iokit-pn-open pexpert-hook-ready iokit-platform-scaffold iokit-match-local iokit-regsvc-local iokit-provider-local iokit-catalog-local iokit-propinh-local iokit-regtop-local iokit-attachstart-local lifecycle-regsvc-local pn-open-local irq-timer-hook pmap-table-local pmap-page-local pmap-attr-local pmap-mw-local pmap-transition-local pmap-bootstrap-ref public-xnu-workspace public-xnu-compile-graph public-xnu-platform-graph public-xnu-object-subset xnu-controlled-link xnu-bounded-pe-gen xnu-arm-pe-bootargs xnu-arm-consistent-debug stage77-xnu-link-proof inert-macho-fixture cancro-target arm-init-stub no-full-xnu-build no-pub-start no-pub-arm-init no-platform-runtime-exec no-iokit-runtime-exec no-iokit-propinh-exec no-regtop-exec no-attachstart-exec no-lifecycle-exec no-register-exec no-notify-exec no-int-del-open-exec no-macho-exec no-phys-write no-tte-write no-pmap-write no-live-pmap-install no-tlb-invalidate no-cache-change no-persist-write no-external-mutation' \
  --header_version 0 \
  --output ../out/stage77/stage77.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage77/stage77.bin \
    --ramdisk ../out/stage77/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage77 mi4ios6=stage77 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved xnu-entry-stub xnu-bs-contract xnu-pmap-bs-contract xnu-pmap-table-contract xnu-pmap-page-contract xnu-pmap-attr-contract xnu-pmap-mw-contract xnu-pmap-trans-contract xnu-pexpert-hook xnu-iokit-platform xnu-iokit-match-contract xnu-iokit-regsvc-contract xnu-iokit-provider xnu-iokit-catalog xnu-iokit-propinh xnu-iokit-regtop xnu-iokit-attachstart xnu-iokit-lifecycle xnu-iokit-pn-open pexpert-hook-ready iokit-platform-scaffold iokit-match-local iokit-regsvc-local iokit-provider-local iokit-catalog-local iokit-propinh-local iokit-regtop-local iokit-attachstart-local lifecycle-regsvc-local pn-open-local irq-timer-hook pmap-table-local pmap-page-local pmap-attr-local pmap-mw-local pmap-transition-local pmap-bootstrap-ref public-xnu-workspace public-xnu-compile-graph public-xnu-platform-graph public-xnu-object-subset xnu-controlled-link xnu-bounded-pe-gen xnu-arm-pe-bootargs xnu-arm-consistent-debug stage77-xnu-link-proof inert-macho-fixture cancro-target arm-init-stub no-full-xnu-build no-pub-start no-pub-arm-init no-platform-runtime-exec no-iokit-runtime-exec no-iokit-propinh-exec no-regtop-exec no-attachstart-exec no-lifecycle-exec no-register-exec no-notify-exec no-int-del-open-exec no-macho-exec no-phys-write no-tte-write no-pmap-write no-live-pmap-install no-tlb-invalidate no-cache-change no-persist-write no-external-mutation' \
    --output ../out/stage77/stage77-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage77-qcdt.img" >&2
fi

sha256sum ../out/stage77/stage77_fixture.macho ../out/stage77/stage77.elf ../out/stage77/stage77.bin ../out/stage77/stage77.img \
  $(if [[ -f ../out/stage77/stage77-qcdt.img ]]; then printf '%s' ../out/stage77/stage77-qcdt.img; fi) \
  > ../out/stage77/SHA256SUMS.txt

ls -l ../out/stage77/stage77.elf ../out/stage77/stage77.bin ../out/stage77/stage77.img \
  $(if [[ -f ../out/stage77/stage77-qcdt.img ]]; then printf '%s' ../out/stage77/stage77-qcdt.img; fi)
cat ../out/stage77/stage77.size
cat ../out/stage77/SHA256SUMS.txt
