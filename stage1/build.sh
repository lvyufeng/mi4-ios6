#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage1

AS=${AS:-arm-none-eabi-as}
LD=${LD:-arm-none-eabi-ld}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}
OBJDUMP=${OBJDUMP:-arm-none-eabi-objdump}
MKBOOTIMG=${MKBOOTIMG:-mkbootimg}
QCDT_PACKER=${QCDT_PACKER:-../tools/mkbootimg_v0_qcdt.py}
QCDT_DT=${QCDT_DT:-../xiaomi4-cancro-backup-20260604-112053/boot-unpacked/dt.img}

$AS -march=armv7-a -mcpu=cortex-a15 -o ../out/stage1/stage1.o stage1.S
$LD -T linker.ld -o ../out/stage1/stage1.elf ../out/stage1/stage1.o
$OBJCOPY -O binary ../out/stage1/stage1.elf ../out/stage1/stage1.bin
$OBJDUMP -d ../out/stage1/stage1.elf > ../out/stage1/stage1.disasm

: > ../out/stage1/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage1/stage1.bin \
  --ramdisk ../out/stage1/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage1 mi4ios6=stage1 boot-wrapper' \
  --header_version 0 \
  --output ../out/stage1/stage1.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage1/stage1.bin \
    --ramdisk ../out/stage1/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage1 mi4ios6=stage1 boot-wrapper' \
    --output ../out/stage1/stage1-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage1-qcdt.img" >&2
fi

sha256sum ../out/stage1/stage1.elf ../out/stage1/stage1.bin ../out/stage1/stage1.img \
  $(if [[ -f ../out/stage1/stage1-qcdt.img ]]; then printf '%s' ../out/stage1/stage1-qcdt.img; fi) \
  > ../out/stage1/SHA256SUMS.txt

ls -l ../out/stage1/stage1.elf ../out/stage1/stage1.bin ../out/stage1/stage1.img \
  $(if [[ -f ../out/stage1/stage1-qcdt.img ]]; then printf '%s' ../out/stage1/stage1-qcdt.img; fi)
cat ../out/stage1/SHA256SUMS.txt
