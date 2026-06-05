#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage25

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
  -Wl,-Map,../out/stage25/stage25.map
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
  mmu.c
  xnu_kernel.c
  stage25_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage25/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage25/stage25.elf
$OBJCOPY -O binary ../out/stage25/stage25.elf ../out/stage25/stage25.bin
$OBJDUMP -d ../out/stage25/stage25.elf > ../out/stage25/stage25.disasm
$NM -n ../out/stage25/stage25.elf > ../out/stage25/stage25.symbols
$SIZE ../out/stage25/stage25.elf > ../out/stage25/stage25.size

: > ../out/stage25/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage25/stage25.bin \
  --ramdisk ../out/stage25/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage25 mi4ios6=stage25 c-runtime apple-dt' \
  --header_version 0 \
  --output ../out/stage25/stage25.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage25/stage25.bin \
    --ramdisk ../out/stage25/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage25 mi4ios6=stage25 c-runtime apple-dt' \
    --output ../out/stage25/stage25-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage25-qcdt.img" >&2
fi

sha256sum ../out/stage25/stage25.elf ../out/stage25/stage25.bin ../out/stage25/stage25.img \
  $(if [[ -f ../out/stage25/stage25-qcdt.img ]]; then printf '%s' ../out/stage25/stage25-qcdt.img; fi) \
  > ../out/stage25/SHA256SUMS.txt

ls -l ../out/stage25/stage25.elf ../out/stage25/stage25.bin ../out/stage25/stage25.img \
  $(if [[ -f ../out/stage25/stage25-qcdt.img ]]; then printf '%s' ../out/stage25/stage25-qcdt.img; fi)
cat ../out/stage25/stage25.size
cat ../out/stage25/SHA256SUMS.txt
