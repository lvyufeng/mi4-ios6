#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage16

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
  -Wl,-Map,../out/stage16/stage16.map
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
  stage16_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage16/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage16/stage16.elf
$OBJCOPY -O binary ../out/stage16/stage16.elf ../out/stage16/stage16.bin
$OBJDUMP -d ../out/stage16/stage16.elf > ../out/stage16/stage16.disasm
$NM -n ../out/stage16/stage16.elf > ../out/stage16/stage16.symbols
$SIZE ../out/stage16/stage16.elf > ../out/stage16/stage16.size

: > ../out/stage16/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage16/stage16.bin \
  --ramdisk ../out/stage16/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage16 mi4ios6=stage16 c-runtime apple-dt' \
  --header_version 0 \
  --output ../out/stage16/stage16.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage16/stage16.bin \
    --ramdisk ../out/stage16/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage16 mi4ios6=stage16 c-runtime apple-dt' \
    --output ../out/stage16/stage16-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage16-qcdt.img" >&2
fi

sha256sum ../out/stage16/stage16.elf ../out/stage16/stage16.bin ../out/stage16/stage16.img \
  $(if [[ -f ../out/stage16/stage16-qcdt.img ]]; then printf '%s' ../out/stage16/stage16-qcdt.img; fi) \
  > ../out/stage16/SHA256SUMS.txt

ls -l ../out/stage16/stage16.elf ../out/stage16/stage16.bin ../out/stage16/stage16.img \
  $(if [[ -f ../out/stage16/stage16-qcdt.img ]]; then printf '%s' ../out/stage16/stage16-qcdt.img; fi)
cat ../out/stage16/stage16.size
cat ../out/stage16/SHA256SUMS.txt
