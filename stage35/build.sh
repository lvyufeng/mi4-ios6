#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage35

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
  -Wl,-Map,../out/stage35/stage35.map
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
  stage35_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage35/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage35/stage35.elf
$OBJCOPY -O binary ../out/stage35/stage35.elf ../out/stage35/stage35.bin
$OBJDUMP -d ../out/stage35/stage35.elf > ../out/stage35/stage35.disasm
$NM -n ../out/stage35/stage35.elf > ../out/stage35/stage35.symbols
$SIZE ../out/stage35/stage35.elf > ../out/stage35/stage35.size

: > ../out/stage35/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage35/stage35.bin \
  --ramdisk ../out/stage35/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage35 mi4ios6=stage35 c-runtime apple-dt' \
  --header_version 0 \
  --output ../out/stage35/stage35.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage35/stage35.bin \
    --ramdisk ../out/stage35/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage35 mi4ios6=stage35 c-runtime apple-dt' \
    --output ../out/stage35/stage35-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage35-qcdt.img" >&2
fi

sha256sum ../out/stage35/stage35.elf ../out/stage35/stage35.bin ../out/stage35/stage35.img \
  $(if [[ -f ../out/stage35/stage35-qcdt.img ]]; then printf '%s' ../out/stage35/stage35-qcdt.img; fi) \
  > ../out/stage35/SHA256SUMS.txt

ls -l ../out/stage35/stage35.elf ../out/stage35/stage35.bin ../out/stage35/stage35.img \
  $(if [[ -f ../out/stage35/stage35-qcdt.img ]]; then printf '%s' ../out/stage35/stage35-qcdt.img; fi)
cat ../out/stage35/stage35.size
cat ../out/stage35/SHA256SUMS.txt
