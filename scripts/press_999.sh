#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE. 999 = 998's switch set VERBATIM (DYLD_WITNESS, SHREG_PROBE,
# SHREG_WIRE, SHREG_GEOM, RESIDENT and the full USB ladder all stay 1) + NO new switch: the watchdog
# feed is gated by the EXISTING STAGE90_XNU_RESIDENT and the period is the constant
# STAGE90_HW_WATCHDOG_TIMEOUT_S 25u -> 29u.
#
# WHY (the two walls 998 measured, plus the three readings this rung opens).
#   1. The live-record channel capped at 16384 mid-dyld (998: `xnu_live_capped=0x00004000` at :20502).
#      999 raises the cap to 24576 and THINS the per-read dump past rd_ordinal > 256 (70 extended +
#      8 locating cells), so the post-exec record tail extends past 998's boundary - or the whole
#      boot fits.
#   2. The exec-done trio DIED AT THE CAP (it ran at counter >= 16384; printf had already printed).
#      999 makes the trio DIRECT (cap-exempt, entry_write_kv) + carries the channel state inside the
#      records (self-locating) + a stash probe published by the NEXT direct write of any kind.
#   3. The SoC watchdog reset the machine ~28 s in with ZERO idles ever (998: 0 wdt/usb/pcx keys).
#      999 feeds the watchdog AT THE STORAGE READ DOOR (entry_storage_driver_read first statement,
#      entry_wdt_pet(st_read_count), RESIDENT-gated; throttle = the WDT's own STS counter) and the
#      period moves 25 -> 29 s.
#   4. The 438 (`shared_region_map_and_slide_np`) single call NEVER RETURNED in 998 (one call, no
#      exit print - the hang is inside the slide half). 999 adds THREE probe prints inside
#      `_shared_region_slide` (shreg-hang report #2 verbatim, own static <4 each, SHREG_PROBE gate):
#      entry / step past slide_init / step past the copyin. The reading ladder in the pre-reg block
#      localizes the stop by which prints appear.
#   5. The timer delivery froze at tick 64 (both 997 and 998) and the captures do NOT cleanly
#      separate A (delivery hardware) from B (I=1 never cleared). 999 adds asm edge probes at the two
#      unmask points (mlie enable edge; thread_exception_return tail), first-4-per-site, gated on the
#      raw tick >= 64, all DIRECT (cap-exempt) - absence stays evidence.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible. RESIDENT rung
# (POST_END_TICKS=0): budget for a black screen + a power-cycle capture (mi4-xnu-reboot-path-cannot-
# reset). The arm carries the full USB ladder (EP1-IN): start the host reader FIRST - its capture is
# expected EMPTY again (the handed-off kernel never idles, so the ladder never fires); the PRIMARY
# channel is /proc/last_kmsg, read by scripts/recover_last_kmsg.sh after the silent reset (step 4
# does it - a plain reboot of the FLASHED ROM is the carrier, NEVER `fastboot boot`).
#
# Steps: (1) confirm the device is back, (2) write the REAL card image to userdata's head, (3) press,
# (4) recover the log. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot,
# NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")

# ====================================================================================================
# 999 PRESS PRE-REGISTRATION (frozen; deviations reported, not reinterpreted)
# ====================================================================================================
# SWITCH SET. 998's verbatim (STAGE90_XNU_DYLD_WITNESS=1, STAGE90_XNU_SHREG_PROBE=1,
# STAGE90_XNU_SHREG_WIRE=1, STAGE90_XNU_SHREG_GEOM=1, STAGE90_XNU_RESIDENT=1, USB ladder
# PROBE/DEV/ENUM/STREAM=1); CARD_COW=1, HDD_WRITE=0; RESIDENT rung (POST_END_TICKS=0). NO new
# switch: the WDT feed is gated by the EXISTING RESIDENT (now also passed to entry_storage.c's
# compile lines) and the period is the constant STAGE90_HW_WATCHDOG_TIMEOUT_S 25u -> 29u.
#
# THE POINTS.
# (1) THE CAP MOVED 16384 -> 24576 (+ read-dump thinning after rd_ordinal > 256). 998's wall =
#     `xnu_live_capped=0x00004000` at :20502 (mid-dyld). PASS = the post-exec record tail extends
#     past 998's boundary (998's tail ended ~:20501, last drv_lba 0x00422963). A NEW
#     `xnu_live_capped=0x6000` = wall moved but still hit; NOTHING past it may be read as a stop.
#     No capped key at all = the whole boot fit.
# (2) EXEC_DONE - root-caused 2026-10-13 by value: total 16,426 = 16,384 counted + 42 direct (the
#     one extra line is the space-prefixed ostext_at at :4110; identity 15,260+1,124=16,384 exact).
#     The trio died at the CAP (ran at
#     counter >= 16384; the tombstone :20502 = the displaced done_seq). 999 makes the trio
#     direct/cap-exempt (entry_stubs.c:5012-5017) + carries g_live_records/g_live_state inside the
#     records (self-locating) + a stash probe published by the NEXT direct write of ANY kind.
#     KEYS: xnu_live_exec_done_seq / _who / _current + xnu_live_exec_done_probe_seq / _sig / _size /
#     _records / _state (size = cursor INCLUDING the trio). PASS = the done records appear even if
#     the cap is hit; their carried counter localizes the channel at return; the probe localizes the
#     trio in the stream. Absence AGAIN => the exempt-path edit did not reach the image => rebuild,
#     do not judge.
# (3) WDT (29 s policy). Pet at the storage read door: first statement of entry_storage_driver_read's
#     body (`entry_wdt_pet(st_read_count);`; decl `__attribute__((weak))` + null guard - the first
#     pre-pass-1 -> post-pass-1 reference in this build, ruled 2026-10-13; the null path is
#     unreachable in any accepted build). Throttle = the WDT's own STS counter (no ISR / no
#     mach-time dependency); argument is the monotone st_read_count, never 0.
#     KEYS (all published by entry_wdt_pet): xnu_live_wdt_map, _slot_before, _desc, _base, _bark,
#     _via (once, first pet), _sts, _countdown, _pets (powers of two of the argument), _pet_calls
#     (per store); payload-side hw_watchdog_timeout_s, hw_watchdog_bite_ticks_written.
#     OBSERVABLES (frozen): hw_watchdog_timeout_s=0x0000001d; hw_watchdog_bite_ticks_written=
#     0x000FFFA0; xnu_live_wdt_bark=0x000E7FA9 (950185 = 29*32765; 25 s was 0xC7FB5). These are
#     RUNTIME arithmetic (hw_watchdog.c:223) - the build proves the 29 s net via `mov r0, #29` at
#     the stage90_main.o arm site (998 measured `mov r0, #25`; 999 verified: exactly one, at the
#     bl stage90_hw_watchdog_arm). 998 had ZERO xnu_live_wdt_* keys; 999 must have them, and the
#     log must outlive the old wall (records toward 24576; run past ~28-32 s).
#     ATTRIBUTION: every xnu_live_wdt_pet_calls value is a power of two AND <= the last preceding
#     xnu_live_storage_rd_ordinal. FALSIFICATION: xnu_live_wdt_map=0 with xnu_live_wdt_desc not a
#     1 MB block of PA 0xf9000000 -> install refused, run dies at the deadline.
# (4) SLIDE-HALF LADDER (report #2 verbatim; §11 audit by value, commits 6d4e67f / 51d5930).
#     998 has exactly 4 IOS7SHREG lines (:4102-03 check_np, :4104 entry, :4109 map_file kr=0);
#     the elf carries FIVE IOS7SHREG strings (the three exits share one `map_slide ret=%d`, so no
#     binary position identifies which fired). The console channel CANNOT drop: 128 KB os-block
#     (define :2510, fill loop :2738-2739), ostext_total ends 0x17ee=6,126 B, 0 ostext_limited,
#     :4110 is space fill. dyld disasm: mapSharedCache -> check_np(12) -> openSharedCacheFile ->
#     8192 B header read -> 4 mappings -> ONE 438. 999 probes: (a) `IOS7SHREG slide_enter
#     slide=0x%x src=0x%08x n=0x%x` at :1523 (before the :1524 call); (b) `IOS7SHREG slide_init
#     kr=%d entry=%p n=0x%x` at :1528 (fires only when slide_init SUCCEEDED; entry is still NULL
#     there = presence marker only); (c) `IOS7SHREG slide_copyin kr=%d` between :1536 and :1537
#     (both outcomes print). READING: none => stop in the map tail (atime :1479 RULED OUT by §11:
#     hfs_update hfs_vnops.c:1352 is the ONLY catalog-write call and the fast path :1236-1239
#     jumps over it; put/drop/deallocate :1493-1503 no-I/O). (a) only => stop inside slide_init
#     (obj lock vm_shared_region.c:1458 / kmem_alloc :1502). +(b) => inside the copyin or its page
#     delivery. +(c) kr=0 => stop in sanity :1541 (its SUCCESS prints NOTHING - `Succesfully init`
#     absent, DEBUG=0 - so the absent `map_slide ret=` at :1654 is the locator); (c) kr!=0 => real
#     EFAULT (map_slide ret=14 must follow). No deadline anywhere; frozen timer enters only via an
#     empty page pool (137 pages don't reach it - vm_resident.c:4574).
#     RECORD PREDICTION (cap 24576 + thinning): 8192-B header read (0x422AE8+, 16 sectors) becomes
#     visible; if the copyin runs, dr_lba enters [0x4a3838, 0x4a3c80]; capped-key last dr_lba still
#     launchd/header-range => stop before the copyin. Text-side loss REFUTED by byte-match (mapping
#     lines == rootfs.hfs header fields; +slide reproduces VAs; 0 sectors in cache span, end
#     0x4B956F by value => cap fired BEFORE the header read AND before 438: record tail is pre-438).
#     Exit :1654 printed => the slide stage completed and the hang moved into dyld's load/link path.
#     Counts by multiset (§11): rd 129 = 119 door + 10 direct; drv 120 calls, the 120th head-only
#     (cut after dr_timeout :20501). Audit of this ladder LANDED (§11, commit 6d4e67f): probes (a)-(c)
#     confirmed, one falsifier each; (b) is a presence marker by necessity (`slide_info_entry` NULL
#     there; non-NULL guaranteed after success, vm_shared_region.c:1502-1504).
# (5) TIMER (the captures do NOT cleanly separate A from B - tape favors B; one measured A fact
#     survives: ISENABLER0 bit 20 CLEAR at 998 :3922 with no writer in a 4.78-7.30 s window).
#     999 adds the discriminator: asm hooks at (1) `ml_set_interrupts_enabled`'s enable edge
#     (machine_routines_asm.s: `enable_interrupts:` :151, `cpsie i` :152 - hook between :152 and
#     :153; reads back mrs cpsr) and (2) `thread_exception_return` tail (cswitch.s: `return_to_user:`
#     :167, `LoadThreadRegister(r12)` :169 - hook after it; records the CPSR the edge is about to
#     install, [uss+0x40] - a post-edge CPSR is unobservable there, the tail ends `movs pc, lr`).
#     First-4-per-site, DIRECT, gated on entry_live_ready() AND the raw tick >= 64 (frozen: the
#     tick_irq ladder 1,2,4,8,16,32,64 ends 0x40 in BOTH captures - 997 :7344 / 998 :5463; 0x80
#     never appears). <=8 events total (<=24 kv lines). KEYS: xnu_live_edge_mlie_seq / _cpsr / _tick
#     and xnu_live_edge_texr_seq / _cpsr / _tick.
#     READING: no edge record => B (no unmask ever ran; the handoff's I=1 never cleared); edge
#     record with tick=64 and no tick_irq>=128 within ~2 ms => A (delivery hardware); edge record
#     then tick_irq=0x80 => the freeze ENDED at that edge. Also corrected: tmr_enter_over does NOT
#     stop (0x10 :17334, 0x20 :17426, 0x40 :19100 - ARMINGS, not deliveries; the freeze is a
#     [64,128)-arming ~60 ms window, not a point). Q5: no post-freeze record requires a timer expiry
#     (all sleep deadlines 0; the five block_returns :19371 = wakeups).
#
# CONSTANTS/GUARDS (changed only where noted): `check_np ret=12` still; VMHINT991 exactly 2x at
# exec; four `IOS7LAB cache mapping` lines stay; geom line exactly once (vm_shared_region.c:635);
# `IOS7SHREG map_file kr=0` still (G1 held, single call); `IOS7LAB vm_allocate64` absent = EXPECTED
# (997 proved it is the failure path's buffer alloc); USB/`pcx` keys 0 = EXPECTED (kernel never
# idles). The checker contract (verified on the built elf 2026-10-13): the 999 image `--expect on
# --ladder --tree` FAILs EXACTLY the one frozen straddle clause (the 20 B wire vs the 32 B parse);
# the frozen 996 elf stays baseline PASS.
#
# REGRESSION (do not judge): zero mapping lines = wire regression; zero geom line = the define did
# not reach the record; `map_file kr!=0` = geometry regression, stop and re-model.
#
# FAILURE SIGNATURES, distinguishable: (a) any NEW panic = stop, report verbatim; (b) log ends with
# neither a capped key nor a bark = a REAL stall past 998's wall - the new last-record line IS the
# frontier finding; (c) bark present with no later records = machine alive until ~29 s then the SoC
# bit - the last pre-bark record localizes the stop; (d) done keys present AND a new capped key =
# the channel is still lossy at 24576, the thinning did not buy enough - read (1) anyway.
if ! grep -q '^# 999 PRESS PRE-REGISTRATION' "$SELF"; then
  echo "REFUSING: the 999 pre-registration block is NOT embedded in this script yet."
  echo "          The team-lead writes it (the 997/998 pattern: switch set / THE POINTS /"
  echo "          failure signatures / falsification / regression guards). The build side:"
  echo "          scripts/build_999.sh + scripts/park_999.sh + the 999 clause in build_entry.sh."
  exit 1
fi

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the pressed
# 998/997/996/995/994 and the confirmed 993 and the regressed 992 arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-dee822b0 ]] && {
  echo "REFUSING: out/stage90 holds 998's arm (dee822b0) - already pressed 2026-10-12."
  echo "Build 999 (scripts/build_999.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-518b6a94 ]] && {
  echo "REFUSING: out/stage90 holds 997's arm (518b6a94) - already pressed 2026-10-12."
  echo "Build 999 (scripts/build_999.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-cea0c1c2 ]] && {
  echo "REFUSING: out/stage90 holds 996's arm (cea0c1c2) - already pressed 2026-10-12."
  echo "Build 999 (scripts/build_999.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 999 (scripts/build_999.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 999 (scripts/build_999.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 999 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 999 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the ladder, the probes, the resident gate and the geometry =="
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
STAGE90_XNU_SHREG_GEOM the geometry/stack half (kept from 998; G1 held)
STAGE90_XNU_RESIDENT the WDT door feed's gate AND the pet in the 999 clause
EOF
echo "   (record carries the 998 verbatim switch set + RESIDENT=1; no new switch in 999)"

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
# 999 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg of its own, so start the
# HOST reader FIRST with --wait; it attaches the moment the payload enumerates and captures LIVE -
# expected EMPTY again (the handed-off kernel never idles, so the ladder never fires; harmless).
# Primary channel: step 4 (-proc-last_kmsg via the flashed ROM).
USB_OUT="out/stage90/captures/usb-console-999-$(date -u +%Y%m%d-%H%M%S).txt"
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
# The silent SoC-watchdog reset (~29 s in) reboots the machine on its own; that boot's own log then
# holds the RUN as its previous. Wait for the device to be visible, then read: --read-only when
# Android is already up (do NOT reboot it - a further boot would overwrite the single-slot record),
# no flags when it sits in fastboot (the script reboots the installed ROM - NEVER `fastboot boot`).
for _ in $(seq 1 90); do
  if adb devices 2>/dev/null | grep -q 4a2fe00b || fastboot devices 2>/dev/null | grep -q 4a2fe00b; then break; fi
  sleep 5
done
if adb devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh --read-only
elif fastboot devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh
else
  echo "   (device not visible yet - when it is, run: scripts/recover_last_kmsg.sh"
  echo "    [--read-only if Android is already up]; do not boot anything else first)"
fi
echo "== DONE =="