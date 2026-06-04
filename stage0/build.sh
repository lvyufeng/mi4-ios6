#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage0

AS=${AS:-arm-none-eabi-as}
LD=${LD:-arm-none-eabi-ld}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}
OBJDUMP=${OBJDUMP:-arm-none-eabi-objdump}
MKBOOTIMG=${MKBOOTIMG:-mkbootimg}

$AS -march=armv7-a -mcpu=cortex-a15 -o ../out/stage0/stage0.o stage0.S
$LD -T linker.ld -o ../out/stage0/stage0.elf ../out/stage0/stage0.o
$OBJCOPY -O binary ../out/stage0/stage0.elf ../out/stage0/stage0.bin
$OBJDUMP -d ../out/stage0/stage0.elf > ../out/stage0/stage0.disasm

: > ../out/stage0/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage0/stage0.bin \
  --ramdisk ../out/stage0/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage0 mi4ios6=stage0' \
  --header_version 0 \
  --output ../out/stage0/stage0.img

sha256sum ../out/stage0/stage0.elf ../out/stage0/stage0.bin ../out/stage0/stage0.img > ../out/stage0/SHA256SUMS.txt
ls -l ../out/stage0/stage0.elf ../out/stage0/stage0.bin ../out/stage0/stage0.img
cat ../out/stage0/SHA256SUMS.txt
