#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage47

PYTHON=${PYTHON:-python3}
MACHO_FIXTURE_GEN=${MACHO_FIXTURE_GEN:-../tools/mkmacho_fixture.py}

# Regenerate the inert non-proprietary Mach-O fixture from the host tool so the
# checked-in macho_fixture.c stays reproducible. The raw fixture stays under
# the ignored out/ directory.
"$PYTHON" "$MACHO_FIXTURE_GEN" \
  --c-output macho_fixture.c \
  --bin-output ../out/stage47/stage47_fixture.macho \
  --symbol-prefix stage47

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
)

LDFLAGS=(
  -nostdlib
  -Wl,-T,linker.ld
  -Wl,--build-id=none
  -Wl,-Map,../out/stage47/stage47.map
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
  xnu_kernel.c
  stage47_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage47/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage47/stage47.elf
$OBJCOPY -O binary ../out/stage47/stage47.elf ../out/stage47/stage47.bin
$OBJDUMP -d ../out/stage47/stage47.elf > ../out/stage47/stage47.disasm
$NM -n ../out/stage47/stage47.elf > ../out/stage47/stage47.symbols
$SIZE ../out/stage47/stage47.elf > ../out/stage47/stage47.size

: > ../out/stage47/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage47/stage47.bin \
  --ramdisk ../out/stage47/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage47 mi4ios6=stage47 c-runtime apple-dt macho-fixture load-plan materialize tte-dryrun tte-verify vtop-dryrun xnu-preflight' \
  --header_version 0 \
  --output ../out/stage47/stage47.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage47/stage47.bin \
    --ramdisk ../out/stage47/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage47 mi4ios6=stage47 c-runtime apple-dt macho-fixture load-plan materialize tte-dryrun tte-verify vtop-dryrun xnu-preflight' \
    --output ../out/stage47/stage47-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage47-qcdt.img" >&2
fi

sha256sum ../out/stage47/stage47_fixture.macho ../out/stage47/stage47.elf ../out/stage47/stage47.bin ../out/stage47/stage47.img \
  $(if [[ -f ../out/stage47/stage47-qcdt.img ]]; then printf '%s' ../out/stage47/stage47-qcdt.img; fi) \
  > ../out/stage47/SHA256SUMS.txt

ls -l ../out/stage47/stage47.elf ../out/stage47/stage47.bin ../out/stage47/stage47.img \
  $(if [[ -f ../out/stage47/stage47-qcdt.img ]]; then printf '%s' ../out/stage47/stage47-qcdt.img; fi)
cat ../out/stage47/stage47.size
cat ../out/stage47/SHA256SUMS.txt
