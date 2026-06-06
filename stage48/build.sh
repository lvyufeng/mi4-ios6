#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage48

PYTHON=${PYTHON:-python3}
MACHO_FIXTURE_GEN=${MACHO_FIXTURE_GEN:-../tools/mkmacho_fixture.py}

# Regenerate the inert non-proprietary Mach-O fixture from the host tool so the
# checked-in macho_fixture.c stays reproducible. The raw fixture stays under
# the ignored out/ directory.
"$PYTHON" "$MACHO_FIXTURE_GEN" \
  --c-output macho_fixture.c \
  --bin-output ../out/stage48/stage48_fixture.macho \
  --symbol-prefix stage48

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
  -Wl,-Map,../out/stage48/stage48.map
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
  macho_fixture.c
  macho_probe.c
  mmu.c
  xnu_kernel.c
  stage48_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage48/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage48/stage48.elf
$OBJCOPY -O binary ../out/stage48/stage48.elf ../out/stage48/stage48.bin
$OBJDUMP -d ../out/stage48/stage48.elf > ../out/stage48/stage48.disasm
$NM -n ../out/stage48/stage48.elf > ../out/stage48/stage48.symbols
$SIZE ../out/stage48/stage48.elf > ../out/stage48/stage48.size

: > ../out/stage48/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage48/stage48.bin \
  --ramdisk ../out/stage48/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage48 mi4ios6=stage48 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table safe-table-materialize stage-owned-tables tte-dryrun tte-verify vtop-dryrun xnu-preflight no-ttbr-write no-cache-change no-xnu-jump' \
  --header_version 0 \
  --output ../out/stage48/stage48.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage48/stage48.bin \
    --ramdisk ../out/stage48/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage48 mi4ios6=stage48 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table safe-table-materialize stage-owned-tables tte-dryrun tte-verify vtop-dryrun xnu-preflight no-ttbr-write no-cache-change no-xnu-jump' \
    --output ../out/stage48/stage48-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage48-qcdt.img" >&2
fi

sha256sum ../out/stage48/stage48_fixture.macho ../out/stage48/stage48.elf ../out/stage48/stage48.bin ../out/stage48/stage48.img \
  $(if [[ -f ../out/stage48/stage48-qcdt.img ]]; then printf '%s' ../out/stage48/stage48-qcdt.img; fi) \
  > ../out/stage48/SHA256SUMS.txt

ls -l ../out/stage48/stage48.elf ../out/stage48/stage48.bin ../out/stage48/stage48.img \
  $(if [[ -f ../out/stage48/stage48-qcdt.img ]]; then printf '%s' ../out/stage48/stage48-qcdt.img; fi)
cat ../out/stage48/stage48.size
cat ../out/stage48/SHA256SUMS.txt
