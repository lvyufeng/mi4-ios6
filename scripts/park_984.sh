#!/usr/bin/env bash
# Park 984 (armed-d13-6435778a) and append its block to records/revert-set.txt.
#
# 984 = 983's switch set UNCHANGED, rebuilt with LIVE instrumentation of the IOKitInitializeTime
# frontier. The ENTRY BIN moved (887d60ea -> 6435778a).
#
# WHY (983's press, 2026-10-11, commit 2a5cee2): 983 cleared 982's GIC fault - no far=0xf9000104, no
# sleh, no panic, runner exit 0 - and then went quiet at `bsd_init: calling IOKitInitializeTime`
# (bsd/kern/bsd_init.c:673) with the NEXT stage, `ubc_init` (:676), never printed. The live log kept
# arming the deadline metronome (xnu_live_tmr_enter_seq -> 64) and the IOKit matching threads ran
# (finddrv x319), so the boot thread is parked INSIDE IOKitInitializeTime
# (iokit/Kernel/IOStartIOKit.cpp:61) = waitForService(resourceMatching("IORTC"), 2s) then
# clock_initialize_calendar(). Everything readable is an ABSENCE (no ubc_init, no tick after ~4.767s,
# timerdrv_isr_calls=0, no dec_* keys) - and an absence is a reading only if its publisher would have
# been reached on success.
#
# WHAT 984 ADDS: four one-line live wrappers, behaviour-neutral (the real function runs and its result
# is returned as it came). xnu_live_wait_enter/_exit make the wait's return a line in the log;
# xnu_live_iokitinit_enter/_exit bracket IOKitInitializeTime; xnu_live_cal_enter/_exit and
# xnu_live_cal_hostnotify bracket clock_initialize_calendar and its host notification.
#
# READ: a log ending in xnu_live_wait_enter with no _exit => the WAIT is the frontier (timer ticks
# cannot fire -> the 2 s timeout cannot expire -> only the publisher notification could wake it).
# xnu_live_cal_enter present => the wait returned and the block is the calendar.
#
# VERIFIED BY VALUE: bsd_init calls __wrap_IOKitInitializeTime (bl 8049e770), which writes the enter
# key, calls the real IOKitInitializeTime (bl 800c1dc8), then writes the exit key; the three --wrap=
# flags are at src/entry/build_entry.sh:194-195. make check 0.
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible. fastboot boot only, NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

ARM=armed-d13-6435778a
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
  printf '# %s  -  984: 983 + LIVE instrumentation of the IOKitInitializeTime frontier.\n' "$ARM"
  printf '# Switch set is 983-s UNCHANGED; the ENTRY BIN moved (887d60ea -> 6435778a).\n#\n'
  printf '# 983 (pressed 2026-10-11, commit 2a5cee2) cleared 982-s GIC fault - no far=0xf9000104, no\n'
  printf '# sleh, no panic, runner exit 0 - and went quiet at bsd_init: calling IOKitInitializeTime\n'
  printf '# (bsd/kern/bsd_init.c:673); the NEXT stage, ubc_init (:676), never printed. The live log\n'
  printf '# kept arming the deadline metronome (xnu_live_tmr_enter_seq -> 64) and the matching threads\n'
  printf '# ran (finddrv x319), so the boot thread is parked INSIDE IOKitInitializeTime\n'
  printf '# (iokit/Kernel/IOStartIOKit.cpp:61) = waitForService(resourceMatching("IORTC"), 2s) then\n'
  printf '# clock_initialize_calendar(). Every reading is an ABSENCE (no ubc_init, no tick after\n'
  printf '# ~4.767s, timerdrv_isr_calls=0, no dec_* keys) - and an absence is a reading only if its\n'
  printf '# publisher would have been reached on success.\n#\n'
  printf '# 984 ADDS four behaviour-neutral live wrappers: xnu_live_wait_enter/_exit (the wait),\n'
  printf '# xnu_live_iokitinit_enter/_exit, xnu_live_cal_enter/_exit + xnu_live_cal_hostnotify.\n'
  printf '# READ: log ending in _wait_enter with no _exit => the WAIT is the frontier; _cal_enter\n'
  printf '# present => the wait returned and the block is the calendar.\n#\n'
  printf '# VERIFIED BY VALUE: bsd_init bl 8049e770 __wrap_IOKitInitializeTime -> writes enter key ->\n'
  printf '# bl 800c1dc8 (real) -> writes exit key; the --wrap= flags are build_entry.sh:194-195.\n'
  printf '# make check 0. Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1,\n'
  printf '# HDD_WRITE=0, base never written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=984-iortc-wait-and-calendar-live-instrumentation;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3