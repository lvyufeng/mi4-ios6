#!/usr/bin/env bash
# Park 981 (armed-d13-7fbd7009) and append its block to records/revert-set.txt.
#
# 981 = 980's switch set, REBUILT on the fixed kernel sources so the entry image and the source agree.
# Two kernel fixes landed after 980's embed and are NOT in 980's image:
#
#   EDIT A (pe_msm8974.c, msm8974_get_timebase). The function returned NANOSECONDS
#   (`cntvct * NSEC_PER_SEC / cntfrq`), but the generic RTC layer ALREADY does that conversion:
#   rtclock_timer_scale_convert (osfmk/arm/rtclock.c:82-93) computes
#   `ticks = pe_arm_get_timebase(NULL) * NSEC_PER_SEC / tscFreq`. So the value was converted TWICE -
#   mach_absolute_time came out NSEC_PER_SEC/19200000 = 52.0833x too large - and every scheduler/idle
#   deadline derived from it was pushed past the SoC watchdog's ~25 s reset. That is why 980's run
#   reached IOKit, idled, and was reset with NO idle wake (the 980 capture's last live record is
#   `xnu_live_idle_seq=1`, no `xnu_live_wdt_*`). The body is now `return (uint64_t)msm8974_read_cntvct();`
#   - RAW ticks, like the apq8060 peer - and the ELF shows `mrrc p15,0,r0,r1,c14; bx lr`.
#
#   EDIT B (bsd_init.c:405). `bsd_init_kprintf` was a no-op comment; it is now `kprintf("bsd_init: " x)`,
#   lighting ~48 markers through bsd_init (vfs_mountroot, VFS_ROOT, bsd_utaskbootstrap, launchd, ...) so
#   the next stop is legible instead of silent. 58 `bsd_init:` strings are in the linked ELF.
#
# THE SWITCH SET IS 980's, unchanged: window 0x1e400000 (484 MiB), RESIDENT=1, USB_PROBE/DEV/ENUM/
# STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1, HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD/CARD_COW=1,
# HDD_WRITE=0, SMEM_PROBE=1, IDLE_NO_SLEEP=0. Only the ENTRY BIN moved (fa301bd7 -> 7fbd7009).
#
# VERIFIED BY VALUE: `msm8974_get_timebase` in the linked ELF is `mrrc ... c14; bx lr` (no multiply);
# 58 `bsd_init:` markers; make check 0. Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1,
# HDD_WRITE=0, the base is never written - fully reversible. fastboot boot only, NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

ARM=armed-d13-7fbd7009
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
  printf '# %s  —  981: 980 rebuilt on the fixed kernel sources, so the embedded entry image agrees with\n' "$ARM"
  printf '# the source. Two kernel fixes 980 predates: the get_timebase ns-vs-ticks double conversion\n'
  printf '# (EDIT A) and the bsd_init markers (EDIT B).\n#\n'
  printf '# EDIT A (pe_msm8974.c msm8974_get_timebase). It returned NANOSECONDS, but rtclock_timer_scale_convert\n'
  printf '# (osfmk/arm/rtclock.c:82-93) converts again: `ticks = get_timebase() * NSEC_PER_SEC / tscFreq`. So\n'
  printf '# mach_absolute_time was 52.0833x too large and every scheduler/idle deadline derived from it was\n'
  printf '# pushed past the SoC watchdog ~25 s reset - the reason 980 idled and was reset with no idle wake\n'
  printf '# (980 capture: last live record xnu_live_idle_seq=1, no xnu_live_wdt_*). Now returns RAW CNTVCT\n'
  printf '# ticks, like the apq8060 peer; ELF shows `mrrc p15,0,r0,r1,c14; bx lr`.\n#\n'
  printf '# EDIT B (bsd_init.c:405). bsd_init_kprintf was a no-op; now kprintf("bsd_init: " x), lighting ~48\n'
  printf '# markers through bsd_init so the next stop is legible. 58 bsd_init: strings in the ELF.\n#\n'
  printf '# Switch set is 980 s UNCHANGED; only the ENTRY BIN moved (fa301bd7 -> 7fbd7009). VERIFIED BY\n'
  printf '# VALUE: get_timebase is `mrrc ... c14; bx lr`; 58 bsd_init markers; make check 0. Medium: 968\n'
  printf '# real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never written - fully\n'
  printf '# reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=981-980-switch-set-on-fixed-kernel-get_timebase-raw-ticks-and-bsd_init-markers;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3