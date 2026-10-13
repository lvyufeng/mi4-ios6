#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE. 1000 = 999's switch set VERBATIM (all checker/witness/
# USB-ladder switches and RESIDENT stay 1) + ONE NEW switch, STAGE90_XNU_USVC=1 (the assembly layer's
# ASFLAGS half + build_entry.sh's EXPORTS; no pool object reads it). The rung answers the THREE GIVENS
# of the 999 press read by changing WHERE the evidence is recorded, not what the kernel does.
#
# WHY (999's press read, 2026-10-13, arm armed-d13-467d59e4, records/999-press-read.txt).
#   1. The run reached USER MODE: the exec-done trio byte-closed at :28715 and launchd's dyld mapped
#      the 438 shared cache (`IOS7SHREG map_slide ret=0` :4113) - then the COUNTED channel goes quiet
#      after `cs_invalid_page` (:4114). The machine stayed ALIVE (31 min, no USB return at all, fed
#      by the read door), so the silence is a READING GAP, not a stop of the machine.
#   2. The tick RATE COLLAPSED: 122 deliveries with 24 of them across the >= 2.4 s the rest of the
#      boot took (the powers-of-two ladder ends :5468; direct probes reach :28723). 999 could say
#      "rate collapsed" but not WHY - the two models (a second CNTV writer / a long masked window)
#      need the raw counter pair read AT the delivery.
#   3. The cap bit AGAIN (`xnu_live_capped=0x6000` @:28714): the counted channel saturated before
#      pid 1's user-mode work, so any record whose ABSENCE is read as evidence cannot ride it.
# 1000: (a) `msm8974_tick_sample` samples every delivery's lateness at the handler's entry (ONE call,
# before the EOI) and publishes the d/tval/ctl triple past one full period; (b) the pet's feed
# records, the tick anomalies and the tick summaries move to the DIRECT (cap-exempt) channel; (c)
# `entry_note_user_svc` hooks `fleh_swi` right after `cpsie i` and publishes launchd's syscall
# frontier - the first 64 SVCs with all eight fields, then a powers-of-two ladder.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible. RESIDENT rung
# (POST_END_TICKS=0): 999 lived ~31 min and never returned; budget the same, with a power-press and
# a `--read-only` recovery as the likely path. The arm carries the full USB ladder (EP1-IN): start
# the host reader FIRST - its capture is expected EMPTY again (the handed-off kernel never idles);
# the PRIMARY channel is /proc/last_kmsg, read by scripts/recover_last_kmsg.sh after the device comes
# back (step 4 - a plain boot of the FLASHED ROM is the carrier, NEVER `fastboot boot`).
#
# Steps: (1) confirm the device is back, (2) write the REAL card image to userdata's head, (3) press,
# (4) recover the log. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot,
# NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")

# ====================================================================================================
# 1000 PRESS PRE-REGISTRATION (frozen; deviations reported, not reinterpreted)
# ====================================================================================================
# SWITCH SET. 999's verbatim (STAGE90_XNU_DYLD_WITNESS=1, STAGE90_XNU_SHREG_PROBE=1,
# STAGE90_XNU_SHREG_WIRE=1, STAGE90_XNU_SHREG_GEOM=1, STAGE90_XNU_RESIDENT=1, USB ladder
# PROBE/DEV/ENUM/STREAM=1); CARD_COW=1, HDD_WRITE=0; RESIDENT rung (POST_END_TICKS=0). ONE new
# switch: STAGE90_XNU_USVC=1 - the ASFLAGS half puts the hook into traps_lo.o and build_entry.sh's
# EXPORTS half gates the C reader; the record must name it (`grep '^STAGE90_XNU_USVC=1'` below).
#
# THE POINTS.
# (1) TICK - THE DELIVERY IS SAMPLED AT ITS SOURCE. KEYS (all DIRECT/cap-exempt):
#     xnu_live_tick_anom_n / _d / _tval / _ctl (first FOUR events in full; `_n` incremented before
#     anything is written), and xnu_live_tick_sum_count / _sum_anom / _sum_maxd (written when asked -
#     two call sites: the exec-done point and the pet's sparse gate). `_d` is CNTVCT ticks from the
#     last re-arm to the handler's entry: HEALTHY = ~19,200 (one period) + the vector's own latency
#     (tens to hundreds); 999's collapse = 24 events near whole multiples of 19,200.
#     READING (frozen): `_tval` consistent with `_d` (the countdown ran through the whole gap) =
#     MASKED DELIVERY; `_tval` a value the gap could not have produced = a SECOND WRITER armed the
#     deadline; `_ctl` bit 1 (IMASK) set or bit 0 (ENABLE) clear = the second writer's signature.
#     Aggregates: `sum_count` at the last moment the log was taken - past 122 iff the tick RESUMED
#     after 999's collapse; still 122 with 0 anomaly events and no later ladder values = deliveries
#     stopped entirely (a different finding than the collapse). Absence of anomaly records WITH
#     `sum_count` climbing = a healthy tick - the negative cell this instrument is built to make
#     readable.
# (2) THE ABSENCE-IS-EVIDENCE CHANNEL IS NOW DIRECT (999's cap bit and swallowed the pet's evidence).
#     KEYS: xnu_live_wdt_pet_n / _calls / _countdown - first four feeds in full, then a powers-of-two
#     ladder; `_calls` is the monotone st_read_count at the feed, so ATTRIBUTION (frozen): every
#     `_calls` value is a power of two OR <= 4 AND <= the last preceding
#     xnu_live_storage_rd_ordinal. The exec-done trio + its probe keys (999's, still direct) must
#     appear again AND - the point of the rung - the direct keys must appear EVEN IF a
#     `xnu_live_capped=0x6000` tombstone appears: a direct record cannot be swallowed by the cap, so
#     a capped key with NO direct record after it means the channel edit did not reach the image
#     (rebuild, do not judge). The summaries must appear at BOTH claimed moments: beside the
#     exec-done trio, and at the pet's first sparse-gate publish.
# (3) THE USER-SVC FRONTIER. KEYS: xnu_live_usvc_n / _num / _pid / _thr / _pc / _lr / _cpsr / _r0,
#     all DIRECT; the first 64 SVCs publish ALL EIGHT fields (USVC_FULL_N 64u, `cmp #64` in the
#     image); past 64 a powers-of-two ladder publishes `_n`/`_num` only. `num` = saved_state.r[12]
#     (STAGE90_SS_R12 - the frame word the ARM SWI convention puts the syscall number in), `pc`/`lr`
#     the user PC/LR of the SVC, `r0` the first argument. FROZEN EXPECTATIONS: `_n` = 1, 2, 3...
#     strictly ascending, one record set per value (the ordinal is incremented before anything is
#     written); `_pid` = 1 (launchd - pid 1 is the process the exec ran); `_cpsr` = a USER-mode CPSR
#     (bit 4 (T) set; 0x00000010 / 0x20000010 range - NOT a kernel CPSR with I/F masked); `_pc` in a
#     user VA (the 0x2c000000..0x40000000 shared window or launchd/dyld text - never 0x8000xxxx).
#     THE READING: the LAST `xnu_live_usvc_n` in the log IS the last syscall the user side managed -
#     the frontier after `cs_invalid_page`, by value, whether or not the counted channel survives it.
#     A ladder value with `_num` = 0 or `_pid` != 1 or `_cpsr` a kernel CPSR = a DEVIATION: the state
#     pointer or an offset is wrong (the saved-state checker derives R0/R12 from the assym sources -
#     re-run it, then rebuild; do not reinterpret the values as kernel behaviour).
# (4) THE CAP AND THE THINNING (context, not a new reading). The cap stays 24576 and the read-dump
#     thinning ordinal moved 256 -> 64 (committed 0504be5; 999 measured 218 reads in the WHOLE boot,
#     so 256 never fired and the full-width dump spent the cap before exec). If
#     `xnu_live_capped=0x6000` appears again, the COUNTED channel still saturates - that is expected
#     and no longer load-bearing: the direct channel carries the trio, the pet feed, the anomalies,
#     the summaries and the SVC frontier past it. A capped key followed by direct records = the rung
#     working as designed; a capped key followed by NOTHING (no direct key of any kind) is the
#     failure in (2)'s last sentence.
# (5) REGRESSION GUARDS (must hold; a miss = the new build regressed the old reading, report the
#     guard, not the new keys): `IOS7LAB check_np ret=12` exactly once; the four `IOS7LAB cache
#     mapping` lines; the geometry line exactly once (:4098's shape); `IOS7SHREG map_file kr=0`;
#     `IOS7SHREG map_slide ret=0` (999's success - if it is not 0, the slide regressed, STOP); the
#     exec-done trio present; VMHINT991 exactly 2x. USB/`pcx` keys 0 = EXPECTED (the kernel never
#     idles); `IOS7LAB vm_allocate64` absent = EXPECTED (997 proved it is the failure path's buffer
#     alloc). The 999 capture (records/999-press-read.txt) is the comparison baseline by value.
# (6) WHAT WOULD MAKE THIS RUNG WRONG (falsifiers, frozen): (a) `xnu_live_usvc_n` ABSENT while the
#     exec trio and `map_slide ret=0` are present => the hook did not reach the image or the state
#     pointer is wrong - REBUILD, the press cannot adjudicate it; (b) zero pet-feed records with the
#     storage door demonstrably used (`rd_ordinal` climbing) => the direct edit did not reach the
#     image - same verdict; (c) all forty-plus usvc records with `_num` = 0 => SS_R12 points at the
#     wrong frame word - the checker's territory, not the kernel's.
#
# FAILURE SIGNATURES, distinguishable: (a) any NEW panic = stop, report verbatim (the hook's frame
# math is a prime suspect - see (c)); (b) the log ends in the SWI path itself (a fault whose far/pc
# names `locore_fleh_swi` or the hook window) = the hook is not transparent - the four-word window
# (push {r0,r1,r8,lr} / mov r0,r8 / bl / pop) must be read IN THE IMAGE before any judgement;
# (c) log ends with a capped key and no direct key after it = (2)'s last sentence - rebuild;
# (d) log ends in a direct record = the machine did stop there, and that record's moment IS the
# frontier finding this rung was built to produce - read the last direct keys (usvc last, then pet,
# then anom) as the frontier triple.
if ! grep -q '^# 1000 PRESS PRE-REGISTRATION' "$SELF"; then
  echo "REFUSING: the 1000 pre-registration block is NOT embedded in this script yet."
  echo "          The team-lead writes it (the 997/998/999 pattern: switch set / THE POINTS /"
  echo "          failure signatures / falsification / regression guards). The build side:"
  echo "          scripts/build_1000.sh + scripts/park_1000.sh + the 1000 clause in build_entry.sh."
  exit 1
fi

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the pressed
# 999/998/997/996/995/994 and the confirmed 993 and the regressed 992 arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-467d59e4 ]] && {
  echo "REFUSING: out/stage90 holds 999's arm (467d59e4) - already pressed 2026-10-13."
  echo "Build 1000 (scripts/build_1000.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-dee822b0 ]] && {
  echo "REFUSING: out/stage90 holds 998's arm (dee822b0) - already pressed 2026-10-12."
  echo "Build 1000 (scripts/build_1000.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-518b6a94 ]] && {
  echo "REFUSING: out/stage90 holds 997's arm (518b6a94) - already pressed 2026-10-12."
  echo "Build 1000 (scripts/build_1000.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-cea0c1c2 ]] && {
  echo "REFUSING: out/stage90 holds 996's arm (cea0c1c2) - already pressed 2026-10-12."
  echo "Build 1000 (scripts/build_1000.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 1000 (scripts/build_1000.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 1000 (scripts/build_1000.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 1000 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 1000 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the ladder, the probes, the resident gate, the geometry AND the new USVC switch =="
while read -r key why; do
  grep -q "^${key}=1" out/stage90/xnu_arm_entry-config.txt || {
    echo "REFUSING: record has no ${key}=1 - ${why}"; exit 1; }
done <<'EOF'
STAGE90_XNU_USB_PROBE the USB ladder stage 1 (probe)
STAGE90_XNU_USB_DEV the USB device stage (the reader below would capture an empty file)
STAGE90_XNU_USB_ENUM the USB enumeration stage
STAGE90_XNU_USB_STREAM the USB streaming stage
STAGE90_XNU_DYLD_WITNESS the message/cacheBase readings
STAGE90_XNU_SHREG_PROBE the IOS7SHREG ladder lines
STAGE90_XNU_SHREG_WIRE the IOS7LAB cache mapping lines (997's reading)
STAGE90_XNU_SHREG_GEOM the geometry/stack half (kept from 998/999; G1 held)
STAGE90_XNU_RESIDENT the WDT door feed's gate AND the tick probes (999's, kept)
STAGE90_XNU_USVC the SVC frontier hook (1000's NEW switch - the ASFLAGS half and this run's own)
EOF
echo "   (record carries the 999 verbatim switch set + STAGE90_XNU_USVC=1)"

echo "== 2a. stage the REAL 896 MiB iOS 7.1.2 rootfs as the card image (the COW base) =="
tools/build_root_volumes.sh --real

echo "== 2b. write it to userdata's head (partition offset 0) =="
# **STREAMED, not pushed.** /data on this device is a 512 MB tmpfs, not the userdata partition, so the
# image is dd'd in over adb stdin to the unmounted userdata block device.
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b shell "dd of=/dev/block/mmcblk0p25 bs=1M conv=fsync" < out/stage90/xnu_card_hfs.img
echo "read-back (the card's first 8 MiB):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=1M count=8 2>/dev/null" | sha256sum
echo "expected (first 8 MiB of the built card):"
head -c 8388608 out/stage90/xnu_card_hfs.img | sha256sum

echo "== 3. the press (gate + runner, fastboot boot only) =="
# 1000 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg of its own, so start the
# HOST reader FIRST with --wait; it attaches the moment the payload enumerates and captures LIVE -
# expected EMPTY again (the handed-off kernel never idles, so the ladder never fires; harmless).
# Primary channel: step 4 (-proc-last_kmsg via the flashed ROM).
USB_OUT="out/stage90/captures/usb-console-1000-$(date -u +%Y%m%d-%H%M%S).txt"
tools/usb_console_read.py --out "$USB_OUT" --wait 600 --seconds 0 &
USB_PID=$!
echo "   (USB console reader armed: pid $USB_PID -> $USB_OUT)"
trap 'kill "$USB_PID" 2>/dev/null || true' EXIT

scripts/preflight_boot_check.sh --allow-xnu-entry
# The runner exits 2 when a RESIDENT rung does not come back - that is the EXPECTED outcome here, not a
# failure, so do not let `set -e` abort before the reader is stopped and its capture is reported.
set +e
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm="$EXPECT_ARM"
RUN_RC=$?
set -e
echo "   (runner exit $RUN_RC; a resident rung not returning shows as 2)"

kill "$USB_PID" 2>/dev/null || true
echo "== USB console capture: $USB_OUT =="
if [ -s "$USB_OUT" ]; then echo "   bytes read: $(wc -c < "$USB_OUT")"; else
  echo "   (empty - the ladder did not reach the idle path, or the payload stopped before it)"
fi

echo "== 4. recovery: the run's own log via /proc/last_kmsg (the carrier is the FLASHED ROM) =="
# 999's rung stayed alive ~31 min (the read-door feed held) and never returned; 1000 feeds the same
# door, so the same is expected. The silent-reset case (~29 s net) and the fed case both end the same
# way for the RECOVERY: the device must come back ONCE (on its own, or by the operator's power press)
# and the NEXT boot's /proc/last_kmsg holds the run as its previous. --read-only when Android is
# already up (do NOT reboot it - a further boot would overwrite the single-slot record); no flags
# when it sits in fastboot (the script reboots the installed ROM - NEVER `fastboot boot`).
for _ in $(seq 1 90); do
  if adb devices 2>/dev/null | grep -q 4a2fe00b || fastboot devices 2>/dev/null | grep -q 4a2fe00b; then break; fi
  sleep 5
done
if adb devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh --read-only
elif fastboot devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh
else
  echo "   (device not visible yet - 999 lived ~31 min fed; if it is still dark, the operator"
  echo "    power-press is the next step, then: scripts/recover_last_kmsg.sh --read-only"
  echo "    while Android is up; do not boot anything else first)"
fi
echo "== DONE =="