#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage8

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
  -Wl,-Map,../out/stage8/stage8.map
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
  xnu_kernel.c
  stage8_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage8/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage8/stage8.elf
$OBJCOPY -O binary ../out/stage8/stage8.elf ../out/stage8/stage8.bin
$OBJDUMP -d ../out/stage8/stage8.elf > ../out/stage8/stage8.disasm
$NM -n ../out/stage8/stage8.elf > ../out/stage8/stage8.symbols
$SIZE ../out/stage8/stage8.elf > ../out/stage8/stage8.size

: > ../out/stage8/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage8/stage8.bin \
  --ramdisk ../out/stage8/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage8 mi4ios6=stage8 c-runtime apple-dt' \
  --header_version 0 \
  --output ../out/stage8/stage8.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage8/stage8.bin \
    --ramdisk ../out/stage8/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage8 mi4ios6=stage8 c-runtime apple-dt' \
    --output ../out/stage8/stage8-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage8-qcdt.img" >&2
fi

sha256sum ../out/stage8/stage8.elf ../out/stage8/stage8.bin ../out/stage8/stage8.img \
  $(if [[ -f ../out/stage8/stage8-qcdt.img ]]; then printf '%s' ../out/stage8/stage8-qcdt.img; fi) \
  > ../out/stage8/SHA256SUMS.txt

ls -l ../out/stage8/stage8.elf ../out/stage8/stage8.bin ../out/stage8/stage8.img \
  $(if [[ -f ../out/stage8/stage8-qcdt.img ]]; then printf '%s' ../out/stage8/stage8-qcdt.img; fi)
cat ../out/stage8/stage8.size
cat ../out/stage8/SHA256SUMS.txt
