#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage5

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
  -Wl,-Map,../out/stage5/stage5.map
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
  xnu_kernel.c
  stage5_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage5/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage5/stage5.elf
$OBJCOPY -O binary ../out/stage5/stage5.elf ../out/stage5/stage5.bin
$OBJDUMP -d ../out/stage5/stage5.elf > ../out/stage5/stage5.disasm
$NM -n ../out/stage5/stage5.elf > ../out/stage5/stage5.symbols
$SIZE ../out/stage5/stage5.elf > ../out/stage5/stage5.size

: > ../out/stage5/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage5/stage5.bin \
  --ramdisk ../out/stage5/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage5 mi4ios6=stage5 c-runtime apple-dt' \
  --header_version 0 \
  --output ../out/stage5/stage5.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage5/stage5.bin \
    --ramdisk ../out/stage5/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage5 mi4ios6=stage5 c-runtime apple-dt' \
    --output ../out/stage5/stage5-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage5-qcdt.img" >&2
fi

sha256sum ../out/stage5/stage5.elf ../out/stage5/stage5.bin ../out/stage5/stage5.img \
  $(if [[ -f ../out/stage5/stage5-qcdt.img ]]; then printf '%s' ../out/stage5/stage5-qcdt.img; fi) \
  > ../out/stage5/SHA256SUMS.txt

ls -l ../out/stage5/stage5.elf ../out/stage5/stage5.bin ../out/stage5/stage5.img \
  $(if [[ -f ../out/stage5/stage5-qcdt.img ]]; then printf '%s' ../out/stage5/stage5-qcdt.img; fi)
cat ../out/stage5/stage5.size
cat ../out/stage5/SHA256SUMS.txt
