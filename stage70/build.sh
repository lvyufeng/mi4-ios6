#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage70

./xnu_workspace_validate.sh ../out/stage70
./xnu_compile_graph_scan.py ../out/stage70
./xnu_object_subset_compile.sh ../out/stage70
./xnu_link_proof.sh ../out/stage70

PYTHON=${PYTHON:-python3}
MACHO_FIXTURE_GEN=${MACHO_FIXTURE_GEN:-../tools/mkmacho_fixture.py}

# Regenerate the inert non-proprietary Mach-O fixture from the host tool so the
# checked-in macho_fixture.c stays reproducible. The raw fixture stays under
# the ignored out/ directory.
"$PYTHON" "$MACHO_FIXTURE_GEN" \
  --c-output macho_fixture.c \
  --bin-output ../out/stage70/stage70_fixture.macho \
  --symbol-prefix stage70 \
  --metadata-file ../out/stage70/xnu-link-macho-metadata.txt

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
  -I../out/stage70
)

LDFLAGS=(
  -nostdlib
  -Wl,-T,linker.ld
  -Wl,--build-id=none
  -Wl,-Map,../out/stage70/stage70.map
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
  xnu_kernel.c
  stage70_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage70/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage70/stage70.elf
$OBJCOPY -O binary ../out/stage70/stage70.elf ../out/stage70/stage70.bin
$OBJDUMP -d ../out/stage70/stage70.elf > ../out/stage70/stage70.disasm
$NM -n ../out/stage70/stage70.elf > ../out/stage70/stage70.symbols
$SIZE ../out/stage70/stage70.elf > ../out/stage70/stage70.size

: > ../out/stage70/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage70/stage70.bin \
  --ramdisk ../out/stage70/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage70 mi4ios6=stage70 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved xnu-bs-contract xnu-pmap-bs-contract xnu-pmap-table-contract xnu-pmap-page-contract xnu-pmap-attr-contract xnu-pmap-mw-contract xnu-pmap-trans-contract xnu-pexpert-hook xnu-iokit-platform xnu-iokit-match-contract xnu-iokit-regsvc-contract xnu-iokit-provider xnu-iokit-catalog xnu-iokit-propinh xnu-iokit-regtop pexpert-hook-ready iokit-platform-scaffold iokit-match-local-only iokit-regsvc-local-only iokit-provider-local-only iokit-catalog-local-only iokit-propinh-local-only iokit-regtop-local irq-timer-hook pmap-table-local-only pmap-page-local-only pmap-attr-local-only pmap-mw-local-only pmap-transition-local-only pmap-bootstrap-ref-only public-xnu-workspace public-xnu-compile-graph public-xnu-platform-graph public-xnu-object-subset xnu-controlled-link xnu-bounded-pe-gen xnu-arm-pe-bootargs xnu-arm-consistent-debug stage70-xnu-link-proof inert-macho-fixture cancro-target no-full-xnu-build no-xnu-jump no-public-xnu-exec no-platform-runtime-exec no-iokit-runtime-exec no-iokit-propinh-exec no-regtop-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-proposed-pmap-write no-live-pmap-install no-tlb-invalidate no-cache-change no-persist-write no-external-mutation' \
  --header_version 0 \
  --output ../out/stage70/stage70.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage70/stage70.bin \
    --ramdisk ../out/stage70/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage70 mi4ios6=stage70 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved xnu-bs-contract xnu-pmap-bs-contract xnu-pmap-table-contract xnu-pmap-page-contract xnu-pmap-attr-contract xnu-pmap-mw-contract xnu-pmap-trans-contract xnu-pexpert-hook xnu-iokit-platform xnu-iokit-match-contract xnu-iokit-regsvc-contract xnu-iokit-provider xnu-iokit-catalog xnu-iokit-propinh xnu-iokit-regtop pexpert-hook-ready iokit-platform-scaffold iokit-match-local-only iokit-regsvc-local-only iokit-provider-local-only iokit-catalog-local-only iokit-propinh-local-only iokit-regtop-local irq-timer-hook pmap-table-local-only pmap-page-local-only pmap-attr-local-only pmap-mw-local-only pmap-transition-local-only pmap-bootstrap-ref-only public-xnu-workspace public-xnu-compile-graph public-xnu-platform-graph public-xnu-object-subset xnu-controlled-link xnu-bounded-pe-gen xnu-arm-pe-bootargs xnu-arm-consistent-debug stage70-xnu-link-proof inert-macho-fixture cancro-target no-full-xnu-build no-xnu-jump no-public-xnu-exec no-platform-runtime-exec no-iokit-runtime-exec no-iokit-propinh-exec no-regtop-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-proposed-pmap-write no-live-pmap-install no-tlb-invalidate no-cache-change no-persist-write no-external-mutation' \
    --output ../out/stage70/stage70-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage70-qcdt.img" >&2
fi

sha256sum ../out/stage70/stage70_fixture.macho ../out/stage70/stage70.elf ../out/stage70/stage70.bin ../out/stage70/stage70.img \
  $(if [[ -f ../out/stage70/stage70-qcdt.img ]]; then printf '%s' ../out/stage70/stage70-qcdt.img; fi) \
  > ../out/stage70/SHA256SUMS.txt

ls -l ../out/stage70/stage70.elf ../out/stage70/stage70.bin ../out/stage70/stage70.img \
  $(if [[ -f ../out/stage70/stage70-qcdt.img ]]; then printf '%s' ../out/stage70/stage70-qcdt.img; fi)
cat ../out/stage70/stage70.size
cat ../out/stage70/SHA256SUMS.txt
