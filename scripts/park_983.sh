#!/usr/bin/env bash
# Park 983 (armed-d13-887d60ea) and append its block to records/revert-set.txt.
#
# 983 = 982's switch set, rebuilt with the GIC-WINDOW fix. 982 fixed the CPU-interface half (the PE
# handler mapped GICC and read it through the VA) and that fix WORKED: the 982 press ran past 981's
# stop through PE_init_iokit, IOKitInitializeTime, the MSM8974Timer driver, and reached bsd_init.
# It then hit the SAME defect on the OTHER half of the window:
#
#   xnu_live_sleh_seq=1 type=4 fsr=5 far=0xf9000104 pc=0x8000c874 lr=0x801ac87c
#   -> pc = entry_irq_enable_line+0x54, lr = MSM8974Timer::start
#
# `entry_irq_enable_line` (the timer driver's own arming call at MSM8974Timer.cpp:1831, intid 40)
# reads GICD_ISENABLER0 through `gicd_read`, whose base was the RAW PHYSICAL 0xf9000000. XNU's pmap
# does not translate that either - it maps only [physBase, physBase+gMemSize) and the managed window,
# and the entry image's 0xf9000000 boot-L1 section is dropped once the kernel owns the tables. Same
# one-value-two-definitions class as 981, one register over.
#
# EDIT (entry_irq.c + entry_gic.h + pe_msm8974.c): the four GIC accessors now add ONE base each -
# `g_irq_gic_dist_va` / `g_irq_gic_cpu_va` - that is zero while physBase == virtBase (every pre-XNU
# caller: entry_irq_arm, the storage probe, unchanged) and is set to `ml_io_map(phys) minus PHYS` by the
# new `entry_irq_gic_map`, which the board PE calls once the kernel owns the tables. The PE's
# `msm8974_gic_map` now maps BOTH windows (the distributor it did not map before) and hands the image
# the two VAs. One window, one definition, no second copy of the arithmetic.
#
# VERIFIED BY VALUE in the linked ELF: `gicd_read` is `movw/movt r3,#0x8061; ldr r3,[r3,#0x18];
# add r3,r3,#-117440512 (0xf9000000); ldr r0,[r3,r0]` - it loads `g_irq_gic_dist_va`, NOT a bare
# 0xf9000000; `entry_irq_gic_map` at 0x8000cef8; `g_irq_gic_{dist,cpu}_va` in .bss.
#
# THE SWITCH SET IS 982's, unchanged (window 0x1e400000 = 484 MiB, RESIDENT=1, USB_PROBE/DEV/ENUM/
# STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1, HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD/CARD_COW=1,
# HDD_WRITE=0, SMEM_PROBE=1, IDLE_NO_SLEEP=0). The ENTRY BIN MOVED (8cfb7ca8 -> 887d60ea) and the
# payload did too. Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0,
# the base is never written - fully reversible. fastboot boot only, NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

ARM=armed-d13-887d60ea
LIVE=out/stage90
PARK=$LIVE/frozen/$ARM
REC=records/revert-set.txt
MEMBERS=(xnu_arm_entry.bin xnu_arm_entry.elf xnu_arm_entry.h xnu_arm_entry-config.txt \
         xnu_arm_entry-sources.txt stage90.bin stage90.elf stage90.img stage90-qcdt.img \
         stage90_fixture.macho stage90-build-config.txt SHA256SUMS.txt)

[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  —  983: 982 rebuilt with the GIC-WINDOW fix. 982 fixed the CPU interface\n' "$ARM"
  printf '# (the PE handler ml_io_map-d GICC) and ran past 981 to bsd_init; then faulted on the\n'
  printf '# OTHER half - the distributor, read raw by entry_irq_enable_line from the timer driver.\n#\n'
  printf '# THE FAULT (982 press): xnu_live_sleh_seq=1 type=4 fsr=5 far=0xf9000104 pc=0x8000c874\n'
  printf '# lr=0x801ac87c -> pc = entry_irq_enable_line+0x54, lr = MSM8974Timer::start. The driver s\n'
  printf '# arming call (MSM8974Timer.cpp:1831, intid 40) reads GICD_ISENABLER0 through gicd_read,\n'
  printf '# whose base was the RAW 0xf9000000. XNU maps only [physBase,+gMemSize) and the managed\n'
  printf '# window, so the entry 0xf9000000 boot-L1 section is dropped - the 981 class, one register\n'
  printf '# over ([[mi4-one-value-two-definitions]]).\n#\n'
  printf '# THE FIX: the four accessors in entry_irq.c add ONE base each - g_irq_gic_dist_va /\n'
  printf '# g_irq_gic_cpu_va - zero while physBase == virtBase (every pre-XNU caller unchanged), set\n'
  printf '# to ml_io_map(phys) minus PHYS by the new entry_irq_gic_map, which the board PE\n'
  printf '# (pe_msm8974.c msm8974_gic_map) calls once the kernel owns the tables. The PE now maps\n'
  printf '# BOTH windows (the distributor it did not map before) and hands the image the two VAs.\n#\n'
  printf '# VERIFIED BY VALUE: gicd_read loads g_irq_gic_dist_va (ldr r3,[r3,#0x18]; add\n'
  printf '# r3,#0xf9000000), NOT a bare 0xf9000000; entry_irq_gic_map @0x8000cef8; the two VAs in .bss.\n#\n'
  printf '# Switch set is 982-s UNCHANGED; the ENTRY BIN moved (8cfb7ca8 -> 887d60ea). VERIFIED:\n'
  printf '# the four build checkers pass, make check 0. Medium: 968 real iOS 7.1.2 HFSX rootfs over\n'
  printf '# hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never written - fully reversible. fastboot boot\n'
  printf '# only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=983-gic-window-both-dist-and-cpu-mapped;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3