#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage4

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
  -Wl,-Map,../out/stage4/stage4.map
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
  stage4_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage4/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage4/stage4.elf
$OBJCOPY -O binary ../out/stage4/stage4.elf ../out/stage4/stage4.bin
$OBJDUMP -d ../out/stage4/stage4.elf > ../out/stage4/stage4.disasm
$NM -n ../out/stage4/stage4.elf > ../out/stage4/stage4.symbols
$SIZE ../out/stage4/stage4.elf > ../out/stage4/stage4.size

: > ../out/stage4/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage4/stage4.bin \
  --ramdisk ../out/stage4/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage4 mi4ios6=stage4 c-runtime apple-dt' \
  --header_version 0 \
  --output ../out/stage4/stage4.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage4/stage4.bin \
    --ramdisk ../out/stage4/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage4 mi4ios6=stage4 c-runtime apple-dt' \
    --output ../out/stage4/stage4-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage4-qcdt.img" >&2
fi

sha256sum ../out/stage4/stage4.elf ../out/stage4/stage4.bin ../out/stage4/stage4.img \
  $(if [[ -f ../out/stage4/stage4-qcdt.img ]]; then printf '%s' ../out/stage4/stage4-qcdt.img; fi) \
  > ../out/stage4/SHA256SUMS.txt

ls -l ../out/stage4/stage4.elf ../out/stage4/stage4.bin ../out/stage4/stage4.img \
  $(if [[ -f ../out/stage4/stage4-qcdt.img ]]; then printf '%s' ../out/stage4/stage4-qcdt.img; fi)
cat ../out/stage4/stage4.size
cat ../out/stage4/SHA256SUMS.txt
