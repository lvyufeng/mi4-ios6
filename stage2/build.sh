#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage2

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
  -Wl,-Map,../out/stage2/stage2.map
)

SOURCES=(
  start.S
  runtime.c
  ram_console.c
  apple_dt.c
  boot_args.c
  stage2_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage2/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage2/stage2.elf
$OBJCOPY -O binary ../out/stage2/stage2.elf ../out/stage2/stage2.bin
$OBJDUMP -d ../out/stage2/stage2.elf > ../out/stage2/stage2.disasm
$NM -n ../out/stage2/stage2.elf > ../out/stage2/stage2.symbols
$SIZE ../out/stage2/stage2.elf > ../out/stage2/stage2.size

: > ../out/stage2/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage2/stage2.bin \
  --ramdisk ../out/stage2/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage2 mi4ios6=stage2 c-runtime apple-dt' \
  --header_version 0 \
  --output ../out/stage2/stage2.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage2/stage2.bin \
    --ramdisk ../out/stage2/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage2 mi4ios6=stage2 c-runtime apple-dt' \
    --output ../out/stage2/stage2-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage2-qcdt.img" >&2
fi

sha256sum ../out/stage2/stage2.elf ../out/stage2/stage2.bin ../out/stage2/stage2.img \
  $(if [[ -f ../out/stage2/stage2-qcdt.img ]]; then printf '%s' ../out/stage2/stage2-qcdt.img; fi) \
  > ../out/stage2/SHA256SUMS.txt

ls -l ../out/stage2/stage2.elf ../out/stage2/stage2.bin ../out/stage2/stage2.img \
  $(if [[ -f ../out/stage2/stage2-qcdt.img ]]; then printf '%s' ../out/stage2/stage2-qcdt.img; fi)
cat ../out/stage2/stage2.size
cat ../out/stage2/SHA256SUMS.txt
