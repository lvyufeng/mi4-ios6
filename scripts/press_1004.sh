#!/usr/bin/env bash
# ====================================================================================================
# SKELETON - press_1004.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/press_1003.sh.
#
# Run this WHEN YOU ARE BACK AT THE DEVICE.  1004 = 1003's switch set VERBATIM (all checker/witness/
# USB-ladder switches, RESIDENT, USVC, SUBMAP_PMAP_SKIP and STALL stay 1) plus ONE new switch
# STAGE90_XNU_USER_PC.  1004 is a MEASUREMENT rung (no kernel behaviour change): it samples the
# interrupted pid-1 FRAME (user pc + cpsr + frame word count) per tick, generalising 1003's sampler,
# so SPIN-vs-ASLEEP can be read as a CHANGING user pc (naming a function) vs a KERNEL cpsr.
#
# WHY (1003's rung, the baseline).  1003's stall sampler published `xnu_live_stall_*` on the tick's
# powers-of-two ladder to settle the question the 1002 press read left open: after `IOS7SHREG
# check_np ret=12` the boot printed `map_slide`, ran 22 user SVCs (the last three num=-3 =
# mach_absolute_time from pc 0x2be97328), then made NO further syscall while the kernel stayed alive
# ~23 min.  cs_invalid_page(0x2f7f6000) is a TOLERATED warning (retval 0), NOT the frontier.
# 1004 GENERALISES the frame sample into a pid-1 instrument: the two candidate call sites (both
# currently UNWRITTEN - 1004-TODO) are
#   (D) src/entry/entry_trace.c near the Idle_load_context wrapper (~l.3040-3080), on the idle
#       pass-count ladder, reading the raw interrupted frame (safe, always-run);
#   (B) src/platform/darwin13/pe_msm8974.c in the tick handler, gated on the thread being pid 1,
#       reading the syscall frame's user pc/cpsr.
#
# 1004-TODO: THE PRE-REGISTRATION BLOCK BELOW IS A PLACEHOLDER.  The team-lead writes the real one
# (the switch set / THE POINTS / failure signatures / regression guards) before the press.  The
# switch set, the record check (step 2), the byte-structural press flow (steps 2a/2b/3/4) and the
# recovery are real; the POINTS and the by-value image reads for the NEW instrument are placeholders.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible.  RESIDENT rung
# (POST_END_TICKS=0): the primary channel is /proc/last_kmsg, read by scripts/recover_last_kmsg.sh
# after the device comes back (step 4 - a plain boot of the FLASHED ROM is the carrier, NEVER
# `fastboot boot`).
#
# Steps: (1) confirm the device is back, (2) write the REAL card image to userdata's head, (3) press,
# (4) recover the log.  Steps 2 and 3 run the project's own gate + runner - never a raw fastboot,
# NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")

# ====================================================================================================
# 1004 PRESS PRE-REGISTRATION (SKELETON - TO BE REPLACED BY THE TEAM-LEAD)
# 1004-SKELETON  <-- the team-lead REMOVES this sentinel line when the real block lands; its presence
#                    makes the guard below REFUSE the press (the POINTS are placeholders).
# ====================================================================================================
# 1004-TODO: The POINTS (0)-(8) below are the 1003 block's SHAPE, carried over with STALL -> USER_PC
# substitutions so the skeleton is structurally complete.  The team-lead replaces the wording, the
# by-value quantities and the key names before this rung is pressed.  Do NOT press this skeleton as
# the frozen pre-registration.
#
# SWITCH SET.  1003's verbatim (STAGE90_XNU_STALL=1, STAGE90_XNU_DYLD_WITNESS=1,
# STAGE90_XNU_SHREG_PROBE=1, STAGE90_XNU_SHREG_WIRE=1, STAGE90_XNU_SHREG_GEOM=1,
# STAGE90_XNU_RESIDENT=1, STAGE90_XNU_USVC=1, STAGE90_XNU_SUBMAP_PMAP_SKIP=1, USB ladder
# PROBE/DEV/ENUM/STREAM=1); CARD_COW=1, HDD_WRITE=0; RESIDENT rung (POST_END_TICKS=0).
# NEW switch STAGE90_XNU_USER_PC=1 (the pid-1 frame instrument).  STALL=1, USVC=1, SUBMAP_PMAP_SKIP=1
# AND STAGE90_XNU_USER_PC=1 must ALL be in the record (grepped below).
#
# (0) THE 1004 CHANGE IS IN THE IMAGE - READ IT BEFORE ANY JUDGEMENT.  By value: the recorder symbol
#     (1004-TODO: `entry_note_user_pc` PROVISIONAL) present (T) in out/stage90/xnu_arm_entry.elf AND
#     the 4 `xnu_live_user_pc*` key literals embedded, exactly as build_1004.sh's delta clause and
#     scripts/park_1004.sh assert.  If the instrument is NOT in the image, NOTHING below may be read
#     as a test - rebuild.  (Step 2-0 below reads it before the press.)
# (0b) THE 1003 STALL INSTRUMENT IS A REGRESSION: entry_note_stall_sample (T) + the 7
#     `xnu_live_stall_*` literals must STILL be in the image (1004's set is 1003's verbatim).
# (0c) THE 1002 r3-SAVE FIX IS A REGRESSION: locore_fleh_swi's prologue must STILL be the six-word
#     window `push {r0, r1, r2, r3, r8, lr}` / `bl <entry_note_user_svc>` / `pop {r0, r1, r2, r3, r8,
#     lr}`.  A FOUR-word window means the fix regressed - rebuild.
# (F) THE FRONTIER UNDER TEST - THE PID-1 USER-SPACE STALL, AND THE pid-1 FRAME SAMPLE IS THE READING.
#     1004-TODO: THE READING (the keys xnu_live_user_pc / _n / _nz / _cpsr):
#       * a USER cpsr (low 5 bits = 0x10) with a CHANGING pc across samples = pid 1 is SPINNING in
#         user space, and the pc value NAMES THE FUNCTION (dyld's image, the shared region, ...);
#       * a USER cpsr with the SAME pc = wedged at one instruction;
#       * every sample a KERNEL cpsr = pid 1 is ASLEEP/blocked.
#     Either way this is a pure MEASUREMENT: the run is EXPECTED to still stall (1004 changes no kernel
#     behaviour).  FALSIFIER: no `xnu_live_user_pc*` keys at all => the instrument did not reach the
#     image (rebuild); the samples carry nz=0 => the frame reader read the wrong struct (a MEASUREMENT
#     defect - adjudicate the offset against entry_saved_state.h).
# (1) THE 1002 SELF-TEST STILL HOLDS - THE 230x ABORT DOES NOT RECUR.  After `IOS7SHREG check_np
#     ret=12`, the count of `launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0x00000004` in the
#     whole capture must STILL be 0 (1002 confirmed it; a recurrence is a REGRESSION - rebuild).
# (2) SUBMAPSKIP - UNCHANGED FROM 1001.  TEXT channel `SUBMAPSKIP: obj=<ptr> start=<ptr> end=<ptr>
#     size=<ptr> lr=<ptr>`, BOUNDED (max 8).  EXPECTED >= 1; most likely EXACTLY 2, obj = 999's two
#     VMHINT991 objects 0xc0005b48 / 0xc0005ab0.  ABSENCE while the (4) gate holds = rebuild.
# (3) VMHINT991 - UNCHANGED: 0 WHILE THE BOOT SURVIVES.  Any count > 0 while surviving = a READING.
# (4) THE SURVIVAL GATE (tools/check_1004_capture.py carries the gate): no panic + the exec-done trio
#     + its five probe keys + records after the exec-returned point.
# (5) THE USER-SVC FRONTIER continues (keys xnu_live_usvc_n / _num / _pid / _thr / _pc / _lr / _cpsr
#     / _r0; first 64 in FULL then a powers-of-two ladder).  `_pid` = 1; `_pc` a user VA; `_cpsr` a
#     USER-mode CPSR.  Read by value against 1003's count.
# (6) THE PET.  The feed gate is `count >= bark/2` = 14.5 s.  If 1004 lives past ~14.5 s with the read
#     door in use (`xnu_live_storage_rd_ordinal` climbing), `xnu_live_wdt_pet_n` MUST appear.
# (7) THE CAP AND THE TICK (frozen; neither may be read as new).  Cap stays 24576; the read-dump
#     thinning ordinal stays 64.  A `xnu_live_capped=0x6000` tombstone is NOT a regression.
# (8) WHAT WOULD MAKE THIS RUNG WRONG (falsifiers, frozen): (a) the (0) change is NOT in the image =>
#     REBUILD; (b) the 230x `usermode data abort at 0x4` recurs past `check_np ret=12` => the 1002
#     r3-save fix REGRESSED - rebuild; (c) the boot does NOT stall and instead advances - record the
#     new wall by value; a stall that reproduces EXACTLY is the EXPECTED outcome; (d) death ELSEWHERE,
#     later => the frontier ADVANCED - record by value.  The FROZEN REGRESSION GUARDS - the geometry
#     line exactly once, `IOS7LAB cache mapping` x4, the shared region x1, `check_np ret=12` x1,
#     `IOS7SHREG map_file kr=0`, `IOS7SHREG map_slide ret=0` - are re-verified only under the gate (4).
#
# FAILURE SIGNATURES, distinguishable: (a) any NEW panic = stop, report verbatim; (b) the text channel
# ends in SUBMAPSKIP itself or in vm_map_enter = the skip branch is not transparent - read the
# skipped-block shape IN THE IMAGE before any judgement; (c) the log ends with a capped key and no
# direct key after it = rebuild; (d) the log ends in a direct record = that record's moment IS the
# frontier finding this rung was built to produce - read the last direct keys (user_pc last, then
# usvc, then pet) as the frontier; (e) the (0) change is not in the image = the fix is not in the
# image - rebuild, nothing else is a test.
# ====================================================================================================
if ! grep -q '^# 1004 PRESS PRE-REGISTRATION' "$SELF"; then
  echo "REFUSING: the 1004 pre-registration block is NOT embedded in this script yet."
  echo "          The team-lead writes it (the 997/998/999/1000/1001/1002/1003 pattern: switch set /"
  echo "          THE POINTS / failure signatures / falsification / regression guards). The build side:"
  echo "          scripts/build_1004.sh + scripts/park_1004.sh + the 1004 clause in build_entry.sh."
  exit 1
fi
# The SKELETON carries a sentinel; the team-lead removes it when the real block lands. A skeleton
# pre-registration (placeholder POINTS) must NOT be pressed.
if grep -q '^# 1004-SKELETON' "$SELF"; then
  echo "REFUSING: this is the 1004 SKELETON pre-registration - the (0)-(8) POINTS above are 1003's SHAPE,"
  echo "          not the frozen 1004 block. The team-lead replaces the block (removing the 1004-SKELETON"
  echo "          sentinel) before the press. This refusal is BY DESIGN."
  exit 1
fi

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the known arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-7669309b ]] && {
  echo "REFUSING: out/stage90 holds 1003's arm (7669309b) - the predecessor (the per-tick stall sampler)."
  echo "Build 1004 (scripts/build_1004.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-b04976fe ]] && {
  echo "REFUSING: out/stage90 holds 1002's arm (b04976fe) - already pressed 2026-10-13 (the r3 fix"
  echo "took; the frontier moved to the pid-1 user-space stall after 22 SVCs; adjudicated in"
  echo "records/1002-press-read.txt)."
  echo "Build 1004 (scripts/build_1004.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-6ab42af7 ]] && {
  echo "REFUSING: out/stage90 holds 1001's arm (6ab42af7) - already pressed 2026-10-13 (frontier"
  echo "advanced into pid-1 dyld; adjudicated in records/1001-press-read.txt)."
  echo "Build 1004 (scripts/build_1004.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-bbdf41bc ]] && {
  echo "REFUSING: out/stage90 holds 1000's arm (bbdf41bc) - already pressed 2026-10-13 (death at"
  echo "10.31 s; adjudicated in records/1000-press-read.txt)."
  echo "Build 1004 (scripts/build_1004.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-467d59e4 ]] && {
  echo "REFUSING: out/stage90 holds 999's arm (467d59e4) - already pressed 2026-10-13."
  echo "Build 1004 (scripts/build_1004.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-dee822b0 ]] && {
  echo "REFUSING: out/stage90 holds 998's arm (dee822b0) - already pressed 2026-10-12."
  echo "Build 1004 (scripts/build_1004.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-518b6a94 ]] && {
  echo "REFUSING: out/stage90 holds 997's arm (518b6a94) - already pressed 2026-10-12."
  echo "Build 1004 (scripts/build_1004.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-cea0c1c2 ]] && {
  echo "REFUSING: out/stage90 holds 996's arm (cea0c1c2) - already pressed 2026-10-12."
  echo "Build 1004 (scripts/build_1004.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 1004 (scripts/build_1004.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 1004 (scripts/build_1004.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 1004 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 1004 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the ladder, the probes, the resident gate, the geometry, the USVC switch (the fix), the submap skip AND 1003's stall + 1004's user_pc instrument =="
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
STAGE90_XNU_USVC the SVC frontier hook - 1002's r3-save fix lives INSIDE it (must stay on)
STAGE90_XNU_SUBMAP_PMAP_SKIP the is_submap guard in vm_map_enter's tail (1001's; kept)
STAGE90_XNU_STALL the per-tick stall sampler (1003's; kept verbatim in 1004)
STAGE90_XNU_USER_PC the per-tick pid-1 frame instrument (1004's; a pure measurement rung)
EOF
echo "   (record carries the 1003 verbatim switch set PLUS the new STAGE90_XNU_USER_PC; USVC, SUBMAP_PMAP_SKIP and STALL all on)"

echo "== 2-0. the 1004 pid-1 frame instrument in the image (read by value BEFORE the press) + the 1003 stall instrument + the 1002 r3-save fix as regressions =="
# (0) THE 1004 CHANGE IS IN THE IMAGE: the recorder symbol present AND the 4 `xnu_live_user_pc*` key
#     literals embedded.  1004-TODO: the symbol name `entry_note_user_pc` is PROVISIONAL.
_d4_sym=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -cE '^[0-9a-f]+ [Tt] entry_note_user_pc$' || true)
_d4_keys=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_user_pc' || true)
echo "   (0) entry_note_user_pc x$_d4_sym (want 1), xnu_live_user_pc* literals x$_d4_keys (want 4)"
[[ "$_d4_sym" == 1 && "$_d4_keys" == 4 ]] || {
  echo "REFUSING: the 1004 pid-1 frame instrument is NOT in the image (sym x$_d4_sym want 1, keys x$_d4_keys want 4) - rebuild"; exit 1; }
# (0b) THE 1003 STALL INSTRUMENT IS A REGRESSION.
_d3_sym=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' || true)
_d3_keys=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_stall_' || true)
echo "   (0b) entry_note_stall_sample x$_d3_sym (want 1), xnu_live_stall_* literals x$_d3_keys (want 7)"
[[ "$_d3_sym" == 1 && "$_d3_keys" == 7 ]] || {
  echo "REFUSING: the 1003 stall instrument is NOT in the image (sym x$_d3_sym want 1, keys x$_d3_keys want 7) - 1004's set is 1003's verbatim, so it must be present - rebuild"; exit 1; }
# (0c) THE 1002 r3-SAVE FIX IS A REGRESSION. The prologue of locore_fleh_swi must STILL be six-word.
_ls=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
echo "   locore_fleh_swi ($_ls..${_lsx:-end}): six-word window x$_w6 (want 1), four-word x$_w4 (want 0)"
grep -B1 -A2 'bl.*<entry_note_user_svc>' <<<"$_lb" | sed 's/^/       /'
[[ "$_w6" == 1 && "$_w4" == 0 ]] || {
  echo "REFUSING: the 1002 r3-save fix REGRESSED (six-word window x$_w6 want 1, four-word x$_w4 want 0) -"
  echo "the assembly layer was not rebuilt with STAGE90_XNU_USVC=1 exported, or the tree is not at the 1002 form."
  echo "Re-run tools/patch_d13_usvc.py, then tools/assemble_arm_layer.sh (USVC=1), then scripts/build_1004.sh."
  exit 1; }
echo "   (0c) THE 1002 r3-SAVE FIX HOLDS (regression) - the press may now test the 1004 instrument"

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
# 1004 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg of its own, so start the
# HOST reader FIRST with --wait; it attaches the moment the payload enumerates and captures LIVE -
# expected EMPTY again (the handed-off kernel never idles, so the ladder never fires; harmless).
# Primary channel: step 4 (/proc/last_kmsg via the flashed ROM).
USB_OUT="out/stage90/captures/usb-console-1004-$(date -u +%Y%m%d-%H%M%S).txt"
tools/usb_console_read.py --out "$USB_OUT" --wait 600 --seconds 0 &
USB_PID=$!
echo "   (USB console reader armed: pid $USB_PID -> $USB_OUT)"
trap 'kill "$USB_PID" 2>/dev/null || true' EXIT

scripts/preflight_boot_check.sh --allow-xnu-entry
# The runner exits 2 when a RESIDENT rung does not come back - that is a normal outcome here, not a
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
# The recovery is the same either way: the device must come back ONCE (on its own, or by the
# operator's power press) and the NEXT boot's /proc/last_kmsg holds the run as its previous.
# --read-only when Android is already up (do NOT reboot it); no flags when it sits in fastboot (the
# script reboots the installed ROM - NEVER `fastboot boot`).
for _ in $(seq 1 90); do
  if adb devices 2>/dev/null | grep -q 4a2fe00b || fastboot devices 2>/dev/null | grep -q 4a2fe00b; then break; fi
  sleep 5
done
if adb devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh --read-only
elif fastboot devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh
else
  echo "   (device not visible yet; if it is still dark, the operator power-press is the next step, then:"
  echo "    scripts/recover_last_kmsg.sh --read-only while Android is up; do not boot anything else first)"
fi
echo "== DONE =="