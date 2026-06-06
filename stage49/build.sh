#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p ../out/stage49

PYTHON=${PYTHON:-python3}
MACHO_FIXTURE_GEN=${MACHO_FIXTURE_GEN:-../tools/mkmacho_fixture.py}

# Regenerate the inert non-proprietary Mach-O fixture from the host tool so the
# checked-in macho_fixture.c stays reproducible. The raw fixture stays under
# the ignored out/ directory.
"$PYTHON" "$MACHO_FIXTURE_GEN" \
  --c-output macho_fixture.c \
  --bin-output ../out/stage49/stage49_fixture.macho \
  --symbol-prefix stage49

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
  -Wl,-Map,../out/stage49/stage49.map
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
  stage49_main.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="../out/stage49/${src%.*}.o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o ../out/stage49/stage49.elf
$OBJCOPY -O binary ../out/stage49/stage49.elf ../out/stage49/stage49.bin
$OBJDUMP -d ../out/stage49/stage49.elf > ../out/stage49/stage49.disasm
$NM -n ../out/stage49/stage49.elf > ../out/stage49/stage49.symbols
$SIZE ../out/stage49/stage49.elf > ../out/stage49/stage49.size

: > ../out/stage49/empty-ramdisk

$MKBOOTIMG \
  --kernel ../out/stage49/stage49.bin \
  --ramdisk ../out/stage49/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'stage49 mi4ios6=stage49 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved no-xnu-jump no-macho-exec no-proposed-phys-write no-proposed-tte-write no-persist-write' \
  --header_version 0 \
  --output ../out/stage49/stage49.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel ../out/stage49/stage49.bin \
    --ramdisk ../out/stage49/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline 'stage49 mi4ios6=stage49 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved no-xnu-jump no-macho-exec no-proposed-phys-write no-proposed-tte-write no-persist-write' \
    --output ../out/stage49/stage49-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage49-qcdt.img" >&2
fi

sha256sum ../out/stage49/stage49_fixture.macho ../out/stage49/stage49.elf ../out/stage49/stage49.bin ../out/stage49/stage49.img \
  $(if [[ -f ../out/stage49/stage49-qcdt.img ]]; then printf '%s' ../out/stage49/stage49-qcdt.img; fi) \
  > ../out/stage49/SHA256SUMS.txt

ls -l ../out/stage49/stage49.elf ../out/stage49/stage49.bin ../out/stage49/stage49.img \
  $(if [[ -f ../out/stage49/stage49-qcdt.img ]]; then printf '%s' ../out/stage49/stage49-qcdt.img; fi)
cat ../out/stage49/stage49.size
cat ../out/stage49/SHA256SUMS.txt
