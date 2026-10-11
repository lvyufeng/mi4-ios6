#!/usr/bin/env bash
# Build 981: 980's arm, REBUILT on the fixed kernel sources.
#
# WHY: 980 embedded the entry image at the moment L2.1 published the timebase, but two kernel fixes
# landed AFTER that embed and are NOT in 980's image:
#   * EDIT A (pe_msm8974.c): `msm8974_get_timebase` now returns RAW CNTVCT TICKS, not nanoseconds.
#     rtclock_timer_scale_convert (osfmk/arm/rtclock.c:85-91) multiplies by NSEC_PER_SEC/tscFreq
#     itself, so the old ns-returning body double-converted and made mach_absolute_time 52.0833x too
#     large - every idle/scheduler deadline derived from it pushed past the SoC watchdog's ~25 s reset.
#   * EDIT B (bsd_init.c:405): `bsd_init_kprintf` is now a real `kprintf` (was a no-op), lighting ~48
#     markers through bsd_init so the NEXT stop is legible instead of silent.
#
# These change POOL/platform objects, which the entry image links, so the entry bin moves - the arm
# name comes from the rebuilt qcdt and the new entry hash. Nothing about the SWITCH SET changes vs 980
# (window 0x1e400000 = 484 MiB, RESIDENT=1, USB ladder, MEM_TOTAL/CARD_TOTAL=1, HFS root from card,
# HDD_WRITE=0 = base never written). This script re-reads 980's record and re-exports it verbatim,
# overrides nothing (mi4-build-variant-comes-from-an-env-default).
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 981: rebuild the entry image with 980's switch set on the fixed kernel =="

EXPORTS=()
while IFS= read -r line; do
  line=${line%%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  k=${line%%=*}; v=${line#*=}
  [[ $v == '(unset)' ]] && continue
  case $k in
    STAGE90_XNU_ENTRY_SHA256|STAGE90_XNU_ENTRY_BYTES) continue ;;  # outputs, not inputs
  esac
  EXPORTS+=("$k=$v")
done < "$REC"
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "  980 was fa301bd7 - a DIFFERENT value here means the kernel fix moved the image, as it must."
grep -E "ENTRY_WINDOW" out/stage90/xnu_arm_entry-config.txt