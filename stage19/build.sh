#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage19

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
  -Wl,-Map,../out/stage19/stage19.map
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
  stage19_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage19/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage19/stage19.elf
$OBJCOPY -O binary ../out/stage19/stage19.elf ../out/stage19/stage19.bin
$OBJDUMP -d ../out/stage19/stage19.elf > ../out/stage19/stage19.disasm
$NM -n ../out/stage19/stage19.elf > ../out/stage19/stage19.symbols
$SIZE ../out/stage19/stage19.elf > ../out/stage19/stage19.size

: > ../out/stage19/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage19/stage19.bin \
  --ramdisk ../out/stage19/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage19 mi4ios6=stage19 c-runtime apple-dt' \
  --header_version 0 \
  --output ../out/stage19/stage19.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage19/stage19.bin \
    --ramdisk ../out/stage19/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage19 mi4ios6=stage19 c-runtime apple-dt' \
    --output ../out/stage19/stage19-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage19-qcdt.img" >&2
fi

sha256sum ../out/stage19/stage19.elf ../out/stage19/stage19.bin ../out/stage19/stage19.img \
  $(if [[ -f ../out/stage19/stage19-qcdt.img ]]; then printf '%s' ../out/stage19/stage19-qcdt.img; fi) \
  > ../out/stage19/SHA256SUMS.txt

ls -l ../out/stage19/stage19.elf ../out/stage19/stage19.bin ../out/stage19/stage19.img \
  $(if [[ -f ../out/stage19/stage19-qcdt.img ]]; then printf '%s' ../out/stage19/stage19-qcdt.img; fi)
cat ../out/stage19/stage19.size
cat ../out/stage19/SHA256SUMS.txt
