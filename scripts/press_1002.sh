#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE. 1002 = 1001's switch set VERBATIM (all checker/witness/
# USB-ladder switches, RESIDENT, USVC and SUBMAP_PMAP_SKIP stay 1) with NO new switch, and exactly ONE
# code change: the USER-SVC hook's save set gains r3 (`{r0,r1,r8,lr}` -> `{r0-r3,r8,lr}`, 6 words,
# still 8-aligned). 1002 is the FIX rung for the 1001 press frontier.
#
# WHY (1001's press read, 2026-10-13, arm armed-d13-6ab42af7, records/1001-press-read.txt section [6]).
#   1. The 1001 FIX TOOK BY VALUE: `SUBMAPSKIP x2` (obj = 999's two VMHINT991 objects 0xc0005b48 /
#      0xc0005ab0), `VMHINT991` inverted to 0, and the boot ADVANCED past the fixed call into pid-1
#      user mode (the exec-done trio, 7528 records past it). No panic; the device self-recovered via
#      the never-fed ~29 s SoC watchdog.
#   2. THE NEW FRONTIER - 230x byte-identical `launchd[1]: usermode data abort, EXC_BAD_ACCESS at
#      0x00000004 ... (Translation fault on page)` right after `IOS7SHREG check_np ret=12`. BY VALUE
#      (slide 0xE7000): the fault is dyld's OWN errno store - `_cerror_nocancel` (vmaddr 0x2be1b8d4 =
#      pc 0x2bf028d4) reads TPIDRURO (`mrc p15,0,r1,c13,c0,3`), masks it, and `ldr r1,[r1,#4]` faults
#      because the register reads 0. The 999 CONTRAST is decisive: SAME binary, SAME syscall, SAME
#      ret=12 - and 999 prints `IOS7SHREG map_slide` on the NEXT line with ZERO `usermode data abort`.
#      So the difference is not a structural kernel gap (a pure one would fault on 999 too): it is
#      1001's ONE kernel-code change - the USVC hook.
#   3. ROOT CAUSE: `traps_lo.s`'s 1000 hook saved `{r0,r1,r8,lr}` around `bl _entry_note_user_svc`,
#      but that C helper CLOBBERS r3, and `fleh_swi`'s `swi_trap` dispatches the `0x80000000` traps ON
#      LIVE r3 (`cmp r3,#3; addls pc,pc,r3,lsl#2` - r3=3 = thread_set_cthread_trap, which writes
#      TPIDRURO). Saving r0/r1/r8/lr was the wrong set; the LEO sibling block two lines below saves
#      `{r0-r12,lr}` for exactly this reason.
# 1002: (a) the hook saves r3 too - `{r0-r3,r8,lr}`, SIX words = 24 B, still 8-aligned at the bl (990);
# (b) EVERY 1001 instrument stays on (the fix is INSIDE the USVC hook, so the hook must be on; the 1001
# submap skip must stay on so the 1001 death is not re-litigated); (c) the rebuild is the ASSEMBLY
# layer - tools/assemble_arm_layer.sh with STAGE90_XNU_USVC=1 EXPORTED, then relink.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible. RESIDENT rung
# (POST_END_TICKS=0): 1001 fed the read door and the frontier moved; 1002 is expected to do the same,
# and the fix is expected to move the frontier PAST dyld's `_cerror` (self-testing). Budget the same
# long survival and a power-press; if it dies anywhere the never-fed SoC watchdog may return the phone
# by itself. The arm carries the full USB ladder (EP1-IN): start the host reader FIRST - its capture is
# expected EMPTY again (the handed-off kernel never idles, so the ladder never fires); the PRIMARY
# channel is /proc/last_kmsg, read by scripts/recover_last_kmsg.sh after the device comes back (step 4
# - a plain boot of the FLASHED ROM is the carrier, NEVER `fastboot boot`).
#
# Steps: (1) confirm the device is back, (2) write the REAL card image to userdata's head, (3) press,
# (4) recover the log. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot,
# NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")

# ====================================================================================================
# 1002 PRESS PRE-REGISTRATION (frozen; deviations reported, not reinterpreted)
# ====================================================================================================
# SWITCH SET. 1001's verbatim (STAGE90_XNU_DYLD_WITNESS=1, STAGE90_XNU_SHREG_PROBE=1,
# STAGE90_XNU_SHREG_WIRE=1, STAGE90_XNU_SHREG_GEOM=1, STAGE90_XNU_RESIDENT=1, STAGE90_XNU_USVC=1,
# STAGE90_XNU_SUBMAP_PMAP_SKIP=1, USB ladder PROBE/DEV/ENUM/STREAM=1); CARD_COW=1, HDD_WRITE=0;
# RESIDENT rung (POST_END_TICKS=0). NO new switch - the change is INSIDE the USVC hook. USVC=1 and
# SUBMAP_PMAP_SKIP=1 must BOTH be in the record (grepped below).
#
# THE POINTS.
# (0) THE HOOK IS TRANSPARENT ON r3 - READ IT IN THE IMAGE BEFORE ANY JUDGEMENT. The prologue of
#     locore_fleh_swi (traps_lo.o's fleh_swi under the link's rename) must be the SIX-word window:
#     `push {r0, r1, r2, r3, r8, lr}` / `mov r0, r8` / `bl <entry_note_user_svc>` /
#     `pop {r0, r1, r2, r3, r8, lr}`. A FOUR-word window `{r0,r1,r8,lr}` here means the assembly layer
#     was NOT rebuilt with STAGE90_XNU_USVC=1 exported, and NOTHING below may be read as a test of the
#     fix - rebuild, do not re-adjudicate. (build_1002.sh's delta clause and scripts/park_1002.sh both
#     assert this by value; the frozen 1001 elf carries the four-word form as the negative control.)
# (1) dyld PROCEEDS PAST `check_np ret=12` - THE 230x ABORT DOES NOT RECUR. EXPECTED (the 999 reading,
#     the fix's target): right after `IOS7SHREG check_np ret=12` the NEXT line is `IOS7SHREG map_slide
#     pid=1 fd=0 ... slide=<n>` (999 @:4104), and the count of `launchd[1]: usermode data abort,
#     EXC_BAD_ACCESS at 0x00000004` in the whole capture is **0** (1001 had 230). If dyld runs on, it
#     reaches `IOS7LAB cache mapping` x4 / `IOS7SHREG map_file kr=0` / `IOS7SHREG map_slide ret=0` and
#     then 999's `cs_invalid_page` frontier - a MOVED frontier (GOOD). FALSIFIER: the 230x abort at
#     0x4 RECURS => the r3 clobber was NOT the cause; the structural hardness item (our tree's missing
#     TPIDRURO writers - `machine_load_context`'s dead write, no `machine_thread_set_tsd_base`/
#     `tsd_base` plumbing, no `start.s`/`machine_routines_asm.s`) moves to the FRONT and becomes the
#     1003 target. The fix is SELF-TESTING: this one reading settles it.
# (2) SUBMAPSKIP - UNCHANGED FROM 1001 (the fix must stay on). TEXT channel: `SUBMAPSKIP: obj=<ptr>
#     start=<ptr> end=<ptr> size=<ptr> lr=<ptr>`, BOUNDED (max 8). EXPECTED: >= 1; most likely EXACTLY
#     2, obj = 999's two VMHINT991 objects 0xc0005b48 / 0xc0005ab0. ABSENCE while the (4) gate holds =
#     the skip did not reach the image (rebuild).
# (3) VMHINT991 - UNCHANGED: **0 WHILE THE BOOT SURVIVES**. Any count > 0 while surviving = the skip
#     did not reach the image, OR another caller path still arrives at the lookup with a non-object -
#     adjudicate by lr/obj (a READING).
# (4) THE SURVIVAL GATE (the 1000 adjudicator's lesson: guard absence counts ONLY when this gate
#     holds; tools/check_1001_capture.py carries the gate). The gate holds iff ALL of: (i) the text
#     channel carries NO panic stanza; (ii) the exec-done trio `xnu_live_exec_done_seq` / `_who` /
#     `_current` and the five `xnu_live_exec_done_probe_*` keys are present; (iii) records from AFTER
#     the exec-return point exist. Only under this gate may any absence elsewhere be a regression.
# (5) THE USER-SVC FRONTIER continues. KEYS: `xnu_live_usvc_n` / `_num` / `_pid` / `_thr` / `_pc` /
#     `_lr` / `_cpsr` / `_r0` (8 keys; the first 64 SVCs in FULL - USVC_FULL_N 64u - then a powers-of-
#     two ladder). The LAST `xnu_live_usvc_n` is the last syscall the user side managed. `_pid` = 1;
#     `_pc` a user VA (never 0x8000xxxx); `_cpsr` a USER-mode CPSR (T set). With the r3 fix, MORE user
#     SVCs are expected than 1001's six (dyld no longer dies at `_cerror`).
# (6) THE PET. The feed gate is `count >= bark/2` = 14.5 s. If 1002 lives past ~14.5 s with the read
#     door in use (`xnu_live_storage_rd_ordinal` climbing), `xnu_live_wdt_pet_n` (the FIRST feed) MUST
#     appear. ZERO feeds past 14.5 s with the door demonstrably used = the door stopped or the gate
#     moved: a READING.
# (7) THE CAP AND THE TICK (frozen; neither may be read as new). Cap stays 24576; the read-dump
#     thinning ordinal stays 64. A `xnu_live_capped=0x6000` tombstone is NOT a regression: the direct
#     channel carries the reading past it (1001's capture carried it - three HARD FAILs that were the
#     text channel's saturation + the upstream frontier, adjudicated in records/1001-press-read.txt
#     section [7]). The tick summaries continue; the 2.58 s MASKED-WINDOW question stays OPEN.
# (8) WHAT WOULD MAKE THIS RUNG WRONG (falsifiers, frozen): (a) the (0) window is FOUR words => the
#     assembly layer was not rebuilt (the fix is not in the image) - REBUILD; (b) the 230x `usermode
#     data abort at 0x4` recurs past `check_np ret=12` => the r3 clobber was NOT the cause (the
#     structural item moves to the front - see (1)); (c) death at the SAME submap call
#     (hw_lock_lock / vm_map_pmap_enter / vm_map_enter) with SUBMAPSKIP ABSENT => the 1001 skip did not
#     reach the image - REBUILD, do not re-adjudicate; (d) death ELSEWHERE, later => the frontier
#     ADVANCED - record by value (last direct keys, then the text channel's last stanza). The FROZEN
#     REGRESSION GUARDS - the geometry line exactly once, `IOS7LAB cache mapping` x4, the shared region
#     x1, `check_np ret=12` x1, `IOS7SHREG map_file kr=0`, `IOS7SHREG map_slide ret=0` - are
#     re-verified only under the survival gate of (4). The 1001 capture (records/1001-press-read.txt)
#     is the comparison baseline by value.
#
# FAILURE SIGNATURES, distinguishable: (a) any NEW panic = stop, report verbatim; (b) the text channel
# ends in SUBMAPSKIP itself or in vm_map_enter = the skip branch is not transparent - read the skipped-
# block shape IN THE IMAGE before any judgement; (c) the log ends with a capped key and no direct key
# after it = the direct channel edit did not reach the image - rebuild; (d) the log ends in a direct
# record = the machine did stop there, and that record's moment IS the frontier finding this rung was
# built to produce - read the last direct keys (usvc last, then pet, then anom) as the frontier triple;
# (e) the (0) window is four words = the fix is not in the image - rebuild, nothing else is a test.
if ! grep -q '^# 1002 PRESS PRE-REGISTRATION' "$SELF"; then
  echo "REFUSING: the 1002 pre-registration block is NOT embedded in this script yet."
  echo "          The team-lead writes it (the 997/998/999/1000/1001 pattern: switch set / THE POINTS /"
  echo "          failure signatures / falsification / regression guards). The build side:"
  echo "          scripts/build_1002.sh + scripts/park_1002.sh + the 1002 clause in build_entry.sh."
  exit 1
fi

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the pressed
# 1001/1000/999/998/997/996/995/994 and the confirmed 993 and the regressed 992 arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-6ab42af7 ]] && {
  echo "REFUSING: out/stage90 holds 1001's arm (6ab42af7) - already pressed 2026-10-13 (frontier"
  echo "advanced into pid-1 dyld; adjudicated in records/1001-press-read.txt)."
  echo "Build 1002 (scripts/build_1002.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-bbdf41bc ]] && {
  echo "REFUSING: out/stage90 holds 1000's arm (bbdf41bc) - already pressed 2026-10-13 (death at"
  echo "10.31 s; adjudicated in records/1000-press-read.txt)."
  echo "Build 1002 (scripts/build_1002.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-467d59e4 ]] && {
  echo "REFUSING: out/stage90 holds 999's arm (467d59e4) - already pressed 2026-10-13."
  echo "Build 1002 (scripts/build_1002.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-dee822b0 ]] && {
  echo "REFUSING: out/stage90 holds 998's arm (dee822b0) - already pressed 2026-10-12."
  echo "Build 1002 (scripts/build_1002.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-518b6a94 ]] && {
  echo "REFUSING: out/stage90 holds 997's arm (518b6a94) - already pressed 2026-10-12."
  echo "Build 1002 (scripts/build_1002.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-cea0c1c2 ]] && {
  echo "REFUSING: out/stage90 holds 996's arm (cea0c1c2) - already pressed 2026-10-12."
  echo "Build 1002 (scripts/build_1002.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 1002 (scripts/build_1002.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 1002 (scripts/build_1002.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 1002 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 1002 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the ladder, the probes, the resident gate, the geometry, the USVC switch (the fix) AND the submap skip =="
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
STAGE90_XNU_USVC the SVC frontier hook - 1002's fix lives INSIDE it (must be on)
STAGE90_XNU_SUBMAP_PMAP_SKIP the is_submap guard in vm_map_enter's tail (1001's; kept so the 1001 death is not re-litigated)
EOF
echo "   (record carries the 1001 verbatim switch set - NO new switch; USVC and SUBMAP_PMAP_SKIP both on)"

echo "== 2-0. the 1002 FIX in the image: the hook's SIX-word window (read by value BEFORE the press) =="
_ls=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
echo "   locore_fleh_swi ($_ls..${_lsx:-end}): six-word window x$_w6 (want 1), four-word x$_w4 (want 0)"
grep -B1 -A2 'bl.*<entry_note_user_svc>' <<<"$_lb" | sed 's/^/       /'
[[ "$_w6" == 1 && "$_w4" == 0 ]] || {
  echo "REFUSING: the 1002 fix is NOT in the image (six-word window x$_w6 want 1, four-word x$_w4 want 0) -"
  echo "the assembly layer was not rebuilt with STAGE90_XNU_USVC=1 exported, or the tree is not at the 1002 form."
  echo "Re-run tools/patch_d13_usvc.py, then tools/assemble_arm_layer.sh (USVC=1), then scripts/build_1002.sh."
  exit 1; }
echo "   (0) THE HOOK IS TRANSPARENT ON r3 - the press may now test the fix"

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
# 1002 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg of its own, so start the
# HOST reader FIRST with --wait; it attaches the moment the payload enumerates and captures LIVE -
# expected EMPTY again (the handed-off kernel never idles, so the ladder never fires; harmless).
# Primary channel: step 4 (-proc-last_kmsg via the flashed ROM).
USB_OUT="out/stage90/captures/usb-console-1002-$(date -u +%Y%m%d-%H%M%S).txt"
tools/usb_console_read.py --out "$USB_OUT" --wait 600 --seconds 0 &
USB_PID=$!
echo "   (USB console reader armed: pid $USB_PID -> $USB_OUT)"
trap 'kill "$USB_PID" 2>/dev/null || true' EXIT

scripts/preflight_boot_check.sh --allow-xnu-entry
# The runner exits 2 when a RESIDENT rung does not come back - that is a normal outcome here (1001 fed
# the read door and the frontier moved; the fix is expected to do the same), not a failure, so do not
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
# 1001 fed the door and never returned on its own (a power-press returned it); if 1002 dies anywhere the
# never-fed SoC watchdog may return the phone. The recovery is the same either way: the device must come
# back ONCE (on its own, or by the operator's power press) and the NEXT boot's /proc/last_kmsg holds the
# run as its previous. --read-only when Android is already up (do NOT reboot it - a further boot would
# overwrite the single-slot record); no flags when it sits in fastboot (the script reboots the installed
# ROM - NEVER `fastboot boot`).
for _ in $(seq 1 90); do
  if adb devices 2>/dev/null | grep -q 4a2fe00b || fastboot devices 2>/dev/null | grep -q 4a2fe00b; then break; fi
  sleep 5
done
if adb devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh --read-only
elif fastboot devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh
else
  echo "   (device not visible yet - 1001 lived fed and needed a power-press; if it is still dark, the"
  echo "    operator power-press is the next step, then: scripts/recover_last_kmsg.sh --read-only"
  echo "    while Android is up; do not boot anything else first)"
fi
echo "== DONE =="