#!/usr/bin/env bash
# Park 980 (armed-d13-fa301bd7) and append its block to records/revert-set.txt.
#
# 980 = 979 (armed-d13-97d6a511) with the ONE functional addition L2.1 installs: the msm8974 board
# PE now PUBLISHES the timebase and CALLS rtc_configure. Before this, rtc_configure was DEFINED in the
# linked image (0x8001b5e0) but had ZERO callers, so gPEClockFrequencyInfo.timebase_frequency_hz and
# rtclock_sec_divisor stayed 0; rtclock_bus_configure (osfmk/arm/rtclock.c:294/:297) panics on either,
# at the next reachable stop after ipc_bootstrap (clock_init -> clock_oldinit -> rtclock_init).
# pe_msm8974.c now sets gPEClockFrequencyInfo.timebase_frequency_hz = 19.2 MHz (0x124f800, offset 0x24,
# confirmed by value) and calls rtc_configure(cntfrq) unconditionally, from ONE value.
#
# WHY 979's arm could not simply be pressed instead. The L2.1 kernel edit makes the source newer than
# the entry image 979 embedded, so preflight_boot_check.sh (correctly) refuses 979's live bytes as
# STALE - a source edit and a checkout are not distinguishable by mtime. This arm is 979's switch set
# rebuilt so the source and the image agree, and it carries BOTH fixes: 979's page-aligned .text/data
# seam (which retired the last-text-page NX fault at lock_set_init) and L2.1's timebase publication.
#
# THE SWITCH SET IS 979's, unchanged: window 0x1e400000 (484 MiB), RESIDENT=1, USB_PROBE/DEV/ENUM/
# STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1, HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD/CARD_COW=1,
# HDD_WRITE=0, SMEM_PROBE=1, IDLE_NO_SLEEP=0. Only the ENTRY BIN moved (97d6a511 -> fa301bd7).
#
# VERIFIED BY VALUE: the linked entry ELF contains 3 `bl <rtc_configure>` call sites where 979 had 0;
# tools/check_rtc_published.sh passes on this image (it FAILs on 979's); tools/check_machine_init_body.sh
# ok; make check 0. Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0,
# the base is never written - fully reversible. fastboot boot only, NEVER flash. PRESS IS THE OPERATOR S.
set -euo pipefail
cd "$(dirname "$0")/.."

ARM=armed-d13-fa301bd7
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
  printf '# %s  —  980: 979 rebuilt so the L2.1 kernel edit and the entry image agree, carrying BOTH
' "$ARM"
  printf '# the 979 page-alignment fix and the L2.1 timebase publication.\n#\n'
  printf '# WHY. 979 = 977 with the entry.ld PAGE-ALIGNMENT fix. That fix retired the `ipc_bootstrap`\n'
  printf '# panic (pc=lock_set_init+0, fsr=0x0f) experiment-979 recovered: entry `.text` had ended at\n'
  printf '# __entry_text_end = 0x805699a0 (ALIGN(32), NOT page-aligned) while the real `.data` output\n'
  printf '# section sat on the next page 0x8056a000, so pmap_init (osfmk/arm/pmap.c:3620) started its\n'
  printf '# data-NX loop at __entry_data_start = 0x805699a0 and set L2_NX_BIT on the LAST TEXT PAGE\n'
  printf '# [0x80569000,0x8056a000) - holding lock_set_init. entry.ld now pads `.text` to a page.\n#\n'
  printf '# L2.1 (the reason THIS arm exists). rtc_configure was DEFINED in the linked image but had\n'
  printf '# ZERO callers, so gPEClockFrequencyInfo.timebase_frequency_hz and rtclock_sec_divisor stayed\n'
  printf '# 0; rtclock_bus_configure (osfmk/arm/rtclock.c:294/:297) panics on either, at the next\n'
  printf '# reachable stop after ipc_bootstrap (clock_init -> clock_oldinit -> rtclock_init).\n'
  printf '# pe_msm8974.c now sets gPEClockFrequencyInfo.timebase_frequency_hz = 19.2 MHz (0x124f800,\n'
  printf '# offset 0x24) and calls rtc_configure(cntfrq) unconditionally, from ONE value.\n#\n'
  printf '# 979 arm could not simply be pressed: the L2.1 edit makes the source newer than 979 s\n'
  printf '# embedded image, so preflight_boot_check.sh refuses 979 s live bytes as STALE. This arm is\n'
  printf '# 979 s switch set rebuilt so source and image agree. The switch set is 979 s UNCHANGED; only\n'
  printf '# the ENTRY BIN moved (97d6a511 -> fa301bd7).\n#\n'
  printf '# VERIFIED BY VALUE: the linked entry ELF has 3 `bl <rtc_configure>` sites (979 had 0);\n'
  printf '# tools/check_rtc_published.sh passes here and FAILs on 979 s image; check_machine_init_body.sh\n'
  printf '# ok; make check 0. Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1,\n'
  printf '# HDD_WRITE=0, the base is never written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# PRESS IS THE OPERATOR S.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=980-979-rebuilt-with-the-L2.1-timebase-publication-the-entry-image-agrees-with-the-source;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3