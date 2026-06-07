#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage53

./xnu_workspace_validate.sh ../out/stage53
./xnu_compile_graph_scan.py ../out/stage53
./xnu_object_subset_compile.sh ../out/stage53
./xnu_link_proof.sh ../out/stage53

PYTHON=${PYTHON:-python3}
MACHO_FIXTURE_GEN=${MACHO_FIXTURE_GEN:-../tools/mkmacho_fixture.py}

# Regenerate the inert non-proprietary Mach-O fixture from the host tool so the
# checked-in macho_fixture.c stays reproducible. The raw fixture stays under
# the ignored out/ directory.
"$PYTHON" "$MACHO_FIXTURE_GEN" \
  --c-output macho_fixture.c \
  --bin-output ../out/stage53/stage53_fixture.macho \
  --symbol-prefix stage53 \
  --metadata-file ../out/stage53/xnu-link-macho-metadata.txt

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
  -I../out/stage53
)

LDFLAGS=(
  -nostdlib
  -Wl,-T,linker.ld
  -Wl,--build-id=none
  -Wl,-Map,../out/stage53/stage53.map
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
  stage53_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage53/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage53/stage53.elf
$OBJCOPY -O binary ../out/stage53/stage53.elf ../out/stage53/stage53.bin
$OBJDUMP -d ../out/stage53/stage53.elf > ../out/stage53/stage53.disasm
$NM -n ../out/stage53/stage53.elf > ../out/stage53/stage53.symbols
$SIZE ../out/stage53/stage53.elf > ../out/stage53/stage53.size

: > ../out/stage53/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage53/stage53.bin \
  --ramdisk ../out/stage53/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage53 mi4ios6=stage53 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved public-xnu-workspace public-xnu-compile-graph public-xnu-object-subset public-xnu-controlled-link public-xnu-bounded-pe-gen stage53-xnu-link-proof inert-macho-link-fixture cancro-target-scaffold no-full-xnu-build no-xnu-jump no-public-xnu-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-cache-policy-change no-persist-write no-external-mutation' \
  --header_version 0 \
  --output ../out/stage53/stage53.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage53/stage53.bin \
    --ramdisk ../out/stage53/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage53 mi4ios6=stage53 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved public-xnu-workspace public-xnu-compile-graph public-xnu-object-subset public-xnu-controlled-link public-xnu-bounded-pe-gen stage53-xnu-link-proof inert-macho-link-fixture cancro-target-scaffold no-full-xnu-build no-xnu-jump no-public-xnu-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-cache-policy-change no-persist-write no-external-mutation' \
    --output ../out/stage53/stage53-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage53-qcdt.img" >&2
fi

sha256sum ../out/stage53/stage53_fixture.macho ../out/stage53/stage53.elf ../out/stage53/stage53.bin ../out/stage53/stage53.img \
  $(if [[ -f ../out/stage53/stage53-qcdt.img ]]; then printf '%s' ../out/stage53/stage53-qcdt.img; fi) \
  > ../out/stage53/SHA256SUMS.txt

ls -l ../out/stage53/stage53.elf ../out/stage53/stage53.bin ../out/stage53/stage53.img \
  $(if [[ -f ../out/stage53/stage53-qcdt.img ]]; then printf '%s' ../out/stage53/stage53-qcdt.img; fi)
cat ../out/stage53/stage53.size
cat ../out/stage53/SHA256SUMS.txt
