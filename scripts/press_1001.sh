#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE. 1001 = 1000's switch set VERBATIM (all checker/witness/
# USB-ladder switches, RESIDENT and USVC stay 1) + ONE NEW switch, STAGE90_XNU_SUBMAP_PMAP_SKIP=1 (the
# POOL define rides tools/build_xnu_arm_kernel.sh's DEFINES array - the SAME array as VMHINT_GUARD,
# because vm_map.c is a kernel-pool file - and build_entry.sh's EXPORTS records it). 1001 is the FIX
# rung for the 1000 press death.
#
# WHY (1000's press read, 2026-10-13, arm armed-d13-bbdf41bc, records/1000-press-read.txt).
#   1. The run DIED at XNU time 10.31 s: `panic(cpu 0 caller 0x80498058): hw_lock_lock(): PANIC: Lock
#      0xc0005b50 = 0xffb5be0b`, right after `attempting to start init of /sbin/launchd`. The locked
#      word - 8 = 0xc0005b48 = the "object" 999's VMHINT991 guard had already recorded (obj=0xc0005b48
#      / 0xc0005ab0, w04=0xbaddecaf = zalloc's free marker => a FREED vm_map-zone element passed as a
#      vm_object). The panic then waited on a serial KDP that never connected; the never-fed SoC
#      watchdog bit at its ~29 s bark and reset the SoC; the flashed ROM's Android came back by itself
#      51 s after the fastboot send. All five of the 1000 adjudicator's anomalies are death artifacts
#      (its own [5]), ZERO regressions.
#   2. ROOT CAUSE: vm_map_enter's pmap-enter tail (vm_map.c:2149-2161) calls `vm_map_pmap_enter` with
#      the `object` parameter - which on a SUBMAP enter IS the submap itself (:2063) - with NO
#      `is_submap` guard, so the helper locks the element's +8 word as if it were a vm_object's lock.
#      The same function guards the same parameter 30 lines later (:2192). 990 died on the same two
#      non-objects one field over (+0x18, vm_page_lookup); 991-999's guard caught the lookup; 1000's
#      +8 word held 0xffb5be0b (bit0 set) and hw_lock_lock panicked.
# 1001: (a) skip the tail call when `is_submap` (keeping `pmap_empty = FALSE`), behind the NEW switch;
# (b) the skip branch carries ONE bounded probe (`SUBMAPSKIP: obj=.. start=.. end=.. size=.. lr=..`,
# max 8) - the VMHINT991 pattern, naming the submap callers and their ranges by measurement;
# (c) EVERY 1000 instrument stays on - the boot that survives this point is the one that makes the
# USVC/pet/tick readings possible.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible. RESIDENT rung
# (POST_END_TICKS=0): 999 lived ~31 min fed and never returned; 1000 died at 10.31 s and the SoC
# watchdog returned the phone by itself. 1001 is expected to survive the 10.31 s point: if it then
# feeds the read door as 999 did, budget the same long survival and a power-press; if it dies
# somewhere else, the same watchdog path may return the phone. The arm carries the full USB ladder
# (EP1-IN): start the host reader FIRST - its capture is expected EMPTY again (the handed-off kernel
# never idles, so the ladder never fires); the PRIMARY channel is /proc/last_kmsg, read by
# scripts/recover_last_kmsg.sh after the device comes back (step 4 - a plain boot of the FLASHED ROM
# is the carrier, NEVER `fastboot boot`).
#
# Steps: (1) confirm the device is back, (2) write the REAL card image to userdata's head, (3) press,
# (4) recover the log. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot,
# NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")

# ====================================================================================================
# 1001 PRESS PRE-REGISTRATION (frozen; deviations reported, not reinterpreted)
# ====================================================================================================
# SWITCH SET. 1000's verbatim (STAGE90_XNU_DYLD_WITNESS=1, STAGE90_XNU_SHREG_PROBE=1,
# STAGE90_XNU_SHREG_WIRE=1, STAGE90_XNU_SHREG_GEOM=1, STAGE90_XNU_RESIDENT=1, STAGE90_XNU_USVC=1, USB
# ladder PROBE/DEV/ENUM/STREAM=1); CARD_COW=1, HDD_WRITE=0; RESIDENT rung (POST_END_TICKS=0). ONE new
# switch: STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - the POOL define rides tools/build_xnu_arm_kernel.sh's
# DEFINES array (the SAME array as VMHINT_GUARD; vm_map.c is a kernel-pool file) and build_entry.sh's
# EXPORTS records it in the entry image; the record must name it
# (`grep '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1'` below).
#
# THE POINTS.
# (1) SUBMAPSKIP - THE SKIP BRANCH REPORTS ITSELF. TEXT channel (a kprintf, NOT a xnu_live key):
#     `SUBMAPSKIP: obj=<ptr> start=<ptr> end=<ptr> size=<ptr> lr=<ptr>` from vm_map_enter's skip
#     branch, BOUNDED (max 8 hits). EXPECTED: >= 1; most likely EXACTLY 2, with `obj` equal to 999's
#     two VMHINT991 objects 0xc0005b48 and 0xc0005ab0 (the same two FREED vm_map-zone elements, now
#     identified as submaps BEFORE any lookup) - report by value; `lr` names the callers by
#     measurement and `start`/`end`/`size` range them. ABSENCE while the survival gate of (3) holds
#     = the probe did not reach the image (rebuild, do not re-adjudicate).
# (2) VMHINT991 - INVERTED EXPECTATION. 999 read exactly 2 (obj=0xc0005b48 / 0xc0005ab0,
#     lr=0x8003d8ec - the 991 guard catching the submap-as-object); 1000's press read **0**, and that
#     0 is the DEATH signature, not an absent guard (the guard is DOWNSTREAM of the lock in the same
#     call - records/1000-press-read.txt, deviations 3). In 1001 the call is SKIPPED before the
#     lookup, so the frozen reading is **0 WHILE THE BOOT SURVIVES** (the (3) gate). Any count > 0
#     while surviving = the skip did not reach the image, OR another caller path still arrives at
#     the lookup with a non-object - adjudicate it by value (which lr, which obj): a READING, never
#     noise.
# (3) THE SURVIVAL GATE (the 1000 adjudicator's lesson: guard absence counts ONLY when this gate
#     holds; tools/check_1001_capture.py carries the gate). The gate holds iff ALL of: (i) the text
#     channel carries NO panic stanza; (ii) the exec-done trio `xnu_live_exec_done_seq` / `_who` /
#     `_current` and the five `xnu_live_exec_done_probe_*` keys are present (999's @:28715-17 analog
#     - on the 1000 image the trio is DIRECT and did NOT print on the 1000 press: the exec never
#     returned); (iii) records from AFTER the exec-return point exist (the direct channel's own next
#     records). Only under this gate may any absence elsewhere in this block be read as a
#     regression.
# (4) THE USER-SVC FRONTIER - THE READING 1000 ARMED AND NEVER REACHED. KEYS: `xnu_live_usvc_n` /
#     `_num` / `_pid` / `_thr` / `_pc` / `_lr` / `_cpsr` / `_r0` (8 keys; the first 64 SVCs in FULL -
#     USVC_FULL_N 64u, `cmp #64` - then a powers-of-two ladder of `_n`/`_num`). READ THE FIRST 64 IN
#     FULL: the LAST `xnu_live_usvc_n` in the log is the last syscall the user side managed. Frozen
#     expectations (1000's, unchanged): `_n` strictly ascending 1, 2, 3...; `_pid` = 1 (launchd);
#     `_num` = saved_state.r[12]; `_pc` a user VA (0x2c000000..0x40000000 or launchd/dyld text -
#     never 0x8000xxxx); `_cpsr` a USER-mode CPSR (T set). `cs_invalid_page` (999's last measured
#     user-mode frontier) may recur - if it does, the dyld frontier MOVED rather than stopped.
# (5) THE PET - 1000's zero feeds was a DEATH ARTIFACT, not a door failure. The feed gate is
#     `count >= bark/2` = 14.5 s (entry_trace.c) and 1000 died at 10.31 s; the read door's calls
#     demonstrably happened (rd_ordinal x158). If 1001 lives past ~14.5 s with the door in use
#     (`xnu_live_storage_rd_ordinal` climbing), `xnu_live_wdt_pet_n` (the FIRST feed) MUST appear -
#     the first feedable moment is ~4.2 s past the 1000 death point. ZERO feeds past 14.5 s with the
#     door demonstrably used = the door stopped or the gate moved: a READING, not an assumed defect
#     (1000's (6)(b) - "zero feed with the door used => rebuild" - was REFUTED by the timeline, not
#     satisfied). `_calls` monotone, every value a power of two OR <= 4, and never above the last
#     preceding `xnu_live_storage_rd_ordinal`.
# (6) THE CAP AND THE TICK (frozen; neither may be read as new). Cap stays 24576; the read-dump
#     thinning ordinal stays 64 (committed 0504be5). 1000's whole boot FIT inside the cap (no
#     tombstone, `rd_ordinal` max 0x9e) - but 1000 never reached user mode; 1001 puts a LIVE pid 1 on
#     the same counted channel, so the cap MAY bite again (999 bit at :28714). A
#     `xnu_live_capped=0x6000` tombstone is NOT a regression: the direct channel (the exec trio +
#     probe, the pet feed, the tick anomalies and summaries, the SVC frontier) carries the reading
#     past it. The tick summaries continue (`xnu_live_tick_sum_*`; 1000: count 107 -> 121, maxd
#     2.58 s); the 2.58 s MASKED-WINDOW question stays OPEN (no second-writer signature in the four
#     sampled 1000 events - mechanism unnamed).
# (7) WHAT WOULD MAKE THIS RUNG WRONG (falsifiers, frozen): (a) death at the SAME call - a panic
#     naming hw_lock_lock / vm_map_pmap_enter / vm_map_enter - with SUBMAPSKIP ABSENT => the skip did
#     not reach the image: REBUILD, do not re-adjudicate; (b) VMHINT991 > 0 while the (3) gate holds
#     => the skip did not reach the image, OR another caller path still arrives at the lookup with a
#     non-object (adjudicate by lr/obj); (c) death ELSEWHERE, later than the 10.31 s point => the
#     frontier ADVANCED - record by value (last direct keys, then the text channel's last stanza).
#     The FROZEN REGRESSION GUARDS - the geometry line exactly once, `IOS7LAB cache mapping` x4, the
#     shared region x1, `check_np ret=12` x1, `IOS7SHREG map_file kr=0`, `IOS7SHREG map_slide ret=0`
#     - are re-verified only under the death gate of (3): if the run died before a guard's print
#     site, its absence is the death signature, NOT a regression (all five of the 1000 press's
#     anomalies were exactly that - records/1000-press-read.txt [5]). The 1000 capture
#     (records/1000-press-read.txt) is the comparison baseline by value.
#
# FAILURE SIGNATURES, distinguishable: (a) any NEW panic = stop, report verbatim (the same call as
# 1000 with no SUBMAPSKIP before it is (7)(a)); (b) the text channel ends in SUBMAPSKIP itself or in
# vm_map_enter = the skip branch is not transparent - read the skipped-block shape IN THE IMAGE
# (`!is_submap` around the call, `pmap_empty = FALSE` kept, the probe call on the else arm) before
# any judgement; (c) the log ends with a capped key and no direct key after it = the direct channel
# edit did not reach the image - rebuild (1000's (6)(b) pattern); (d) the log ends in a direct
# record = the machine did stop there, and that record's moment IS the frontier finding this rung
# was built to produce - read the last direct keys (usvc last, then pet, then anom) as the frontier
# triple.
if ! grep -q '^# 1001 PRESS PRE-REGISTRATION' "$SELF"; then
  echo "REFUSING: the 1001 pre-registration block is NOT embedded in this script yet."
  echo "          The team-lead writes it (the 997/998/999/1000 pattern: switch set / THE POINTS /"
  echo "          failure signatures / falsification / regression guards). The build side:"
  echo "          scripts/build_1001.sh + scripts/park_1001.sh + the 1001 clause in build_entry.sh."
  exit 1
fi

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the pressed
# 1000/999/998/997/996/995/994 and the confirmed 993 and the regressed 992 arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-bbdf41bc ]] && {
  echo "REFUSING: out/stage90 holds 1000's arm (bbdf41bc) - already pressed 2026-10-13 (death at"
  echo "10.31 s; adjudicated in records/1000-press-read.txt)."
  echo "Build 1001 (scripts/build_1001.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-467d59e4 ]] && {
  echo "REFUSING: out/stage90 holds 999's arm (467d59e4) - already pressed 2026-10-13."
  echo "Build 1001 (scripts/build_1001.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-dee822b0 ]] && {
  echo "REFUSING: out/stage90 holds 998's arm (dee822b0) - already pressed 2026-10-12."
  echo "Build 1001 (scripts/build_1001.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-518b6a94 ]] && {
  echo "REFUSING: out/stage90 holds 997's arm (518b6a94) - already pressed 2026-10-12."
  echo "Build 1001 (scripts/build_1001.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-cea0c1c2 ]] && {
  echo "REFUSING: out/stage90 holds 996's arm (cea0c1c2) - already pressed 2026-10-12."
  echo "Build 1001 (scripts/build_1001.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 1001 (scripts/build_1001.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 1001 (scripts/build_1001.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 1001 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 1001 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the ladder, the probes, the resident gate, the geometry, the USVC switch AND the submap skip =="
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
STAGE90_XNU_USVC the SVC frontier hook (1000's, kept)
STAGE90_XNU_SUBMAP_PMAP_SKIP the is_submap guard in vm_map_enter's tail (1001's NEW switch - the fix)
EOF
echo "   (record carries the 1000 verbatim switch set + STAGE90_XNU_SUBMAP_PMAP_SKIP=1)"

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
# 1001 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg of its own, so start the
# HOST reader FIRST with --wait; it attaches the moment the payload enumerates and captures LIVE -
# expected EMPTY again (the handed-off kernel never idles, so the ladder never fires; harmless).
# Primary channel: step 4 (-proc-last_kmsg via the flashed ROM).
USB_OUT="out/stage90/captures/usb-console-1001-$(date -u +%Y%m%d-%H%M%S).txt"
tools/usb_console_read.py --out "$USB_OUT" --wait 600 --seconds 0 &
USB_PID=$!
echo "   (USB console reader armed: pid $USB_PID -> $USB_OUT)"
trap 'kill "$USB_PID" 2>/dev/null || true' EXIT

scripts/preflight_boot_check.sh --allow-xnu-entry
# The runner exits 2 when a RESIDENT rung does not come back - that is a normal outcome here (999
# lived ~31 min fed; 1000 died at 10.31 s and the SoC watchdog reset it), not a failure, so do not
# let `set -e` abort before the reader is stopped and its capture is reported.
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
# 1000 died at 10.31 s and the never-fed SoC watchdog returned the phone BY ITSELF (Android 51 s after
# the fastboot send); 999 lived ~31 min (the read-door feed held) and never returned. 1001 feeds the
# same door, so BOTH outcomes are on the table: if it survives the 10.31 s point it may live long
# (budget a power-press), and if it dies anywhere the watchdog path may return it. The recovery is the
# same either way: the device must come back ONCE (on its own, or by the operator's power press) and
# the NEXT boot's /proc/last_kmsg holds the run as its previous. --read-only when Android is already
# up (do NOT reboot it - a further boot would overwrite the single-slot record); no flags when it sits
# in fastboot (the script reboots the installed ROM - NEVER `fastboot boot`).
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