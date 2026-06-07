#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage55

./xnu_workspace_validate.sh ../out/stage55
./xnu_compile_graph_scan.py ../out/stage55
./xnu_object_subset_compile.sh ../out/stage55
./xnu_link_proof.sh ../out/stage55

PYTHON=${PYTHON:-python3}
MACHO_FIXTURE_GEN=${MACHO_FIXTURE_GEN:-../tools/mkmacho_fixture.py}

# Regenerate the inert non-proprietary Mach-O fixture from the host tool so the
# checked-in macho_fixture.c stays reproducible. The raw fixture stays under
# the ignored out/ directory.
"$PYTHON" "$MACHO_FIXTURE_GEN" \
  --c-output macho_fixture.c \
  --bin-output ../out/stage55/stage55_fixture.macho \
  --symbol-prefix stage55 \
  --metadata-file ../out/stage55/xnu-link-macho-metadata.txt

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
  -I../out/stage55
)

LDFLAGS=(
  -nostdlib
  -Wl,-T,linker.ld
  -Wl,--build-id=none
  -Wl,-Map,../out/stage55/stage55.map
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
  xnu_kernel.c
  stage55_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage55/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage55/stage55.elf
$OBJCOPY -O binary ../out/stage55/stage55.elf ../out/stage55/stage55.bin
$OBJDUMP -d ../out/stage55/stage55.elf > ../out/stage55/stage55.disasm
$NM -n ../out/stage55/stage55.elf > ../out/stage55/stage55.symbols
$SIZE ../out/stage55/stage55.elf > ../out/stage55/stage55.size

: > ../out/stage55/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage55/stage55.bin \
  --ramdisk ../out/stage55/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage55 mi4ios6=stage55 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved public-xnu-workspace public-xnu-compile-graph public-xnu-platform-graph public-xnu-object-subset public-xnu-controlled-link public-xnu-bounded-pe-gen public-xnu-arm-pe-bootargs public-xnu-arm-consistent-debug stage55-xnu-link-proof inert-macho-link-fixture cancro-target-scaffold no-full-xnu-build no-xnu-jump no-public-xnu-exec no-platform-runtime-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-cache-policy-change no-persist-write no-external-mutation' \
  --header_version 0 \
  --output ../out/stage55/stage55.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage55/stage55.bin \
    --ramdisk ../out/stage55/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage55 mi4ios6=stage55 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved public-xnu-workspace public-xnu-compile-graph public-xnu-platform-graph public-xnu-object-subset public-xnu-controlled-link public-xnu-bounded-pe-gen public-xnu-arm-pe-bootargs public-xnu-arm-consistent-debug stage55-xnu-link-proof inert-macho-link-fixture cancro-target-scaffold no-full-xnu-build no-xnu-jump no-public-xnu-exec no-platform-runtime-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-cache-policy-change no-persist-write no-external-mutation' \
    --output ../out/stage55/stage55-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage55-qcdt.img" >&2
fi

sha256sum ../out/stage55/stage55_fixture.macho ../out/stage55/stage55.elf ../out/stage55/stage55.bin ../out/stage55/stage55.img \
  $(if [[ -f ../out/stage55/stage55-qcdt.img ]]; then printf '%s' ../out/stage55/stage55-qcdt.img; fi) \
  > ../out/stage55/SHA256SUMS.txt

ls -l ../out/stage55/stage55.elf ../out/stage55/stage55.bin ../out/stage55/stage55.img \
  $(if [[ -f ../out/stage55/stage55-qcdt.img ]]; then printf '%s' ../out/stage55/stage55-qcdt.img; fi)
cat ../out/stage55/stage55.size
cat ../out/stage55/SHA256SUMS.txt
