#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE, after waking it (hold Power ~10-15s).
#
# WHY: 970g was FALSIFIED. It restored the D13 boot path (fast path removed + boot table zeroed), and
# the press produced ZERO output - capture 970g-armed-d13-321e3332-20261010-last_kmsg.txt (294656 B,
# 3932 lines) carries NO xnu_live_* key and ends at 'jumping to XNU's _start'. So the boot path was NOT
# the cause.
#
# THE CAUSE (970h). D13 `mmu_reinitialize` ran a WHOLE-TLB invalidate (`locore.s:85`, `mcr p15,0,r4,
# c8,c7,0`) BEFORE the TTBR0 write (`:113`) and before the zero/map loops build the boot table. The
# payload hands off with the MMU ON and caches OFF (`sctlr_before=0x00c5487b`) and its L1 identity-maps
# `[0x80000000,0x81000000)` (`src/mmu.c:5506`), so at the jump VA `0x80a00000` (the boot table,
# `topOfKernelData`) a valid identity TLB entry is LIVE. 4570 relies on exactly that: its `_start` has
# NO pre-switch TLB maintenance (its only `c8,c7,0` is at `start.s:337`, AFTER the table is built), and
# its first post-switch store - `invalidate_tte`'s `str` to the SAME VA `0x80a00000` - does not fault,
# which is only possible if a live identity entry survived the switch (ARMv7 does not auto-invalidate
# the TLB on a TTBR write). D13's pre-switch flush DESTROYED that entry, so 970g's zero loop's first
# store (`str r3,[r5],#4` to VA `0x80a00000`) had to WALK the just-switched stale table, whose slot
# `0x80a` holds DRAM (no descriptor) -> translation fault. With `cpsid if` set (`locore.s:55`) and VBAR
# still the PAYLOAD's (D13 sets its own only at `:235`), the abort-vector fetch itself faults: a double
# fault, no print, watchdog reset. That is the whole D13-line silence.
#
# THE FIX (970h, D13 `osfmk/arm/locore.s` ONLY, applied by tools/stage_d13_boot_path_970h.sh): REMOVE
# the pre-switch whole-TLB invalidate at `:85`, exactly as 4570's `_start` has none. D13 already
# performs a safe whole-TLB invalidate AFTER the table is built (`:274`, in `mmu_initialized`), so the
# removed one is redundant. Verified by value in the LINKED image: `mmu_reinitialize` @0x80000008 runs
# DACR/I-cache/TTBCR with NO `cr8,cr7` before the TTBR0 write @0x80000048; `_970g_zero_tte`
# @0x80000060; the post-build flush @0x8000012c REMAINS.
#
# WHAT THIS PROVES / HOW TO READ IT (the runner prints xnu_live_* keys):
#   `xnu_live_console` present  => the boot path ran AND the zero loop did not fault. The D13 line is
#                                  OBSERVABLE for the first time. The first ~15 keys (the init block
#                                  and the first wrapped probes) then show how far `_start` ->
#                                  `arm_init` got. Keys AFTER `arm_vm_init` are NOT expected yet -
#                                  D13's `arm_vm_init.c:352-353` bzero`s a FRESH cpu_ttb where 4570
#                                  bcopy`s the boot table (971, a separate later rung).
#   STILL ZERO keys            => the fault is BELOW `_start` too; recorded, not papered over.
#
# THE ARM is `armed-d13-299ee994` (entry bin `299ee994`, payload rebuilt around it): 970g's exact
# switch set (the 970h change is a TREE edit, not a switch, so the config record is byte-identical to
# 970g's). The medium is 965c/968's real iOS 7.1.2 HFSX rootfs over the CARD COW; `HDD_WRITE=0`, so the
# BASE IS NEVER WRITTEN - fully reversible, no brick risk (a power cycle reverts every write). RESIDENT
# rung (`POST_END_TICKS=0`): budget for a black screen + a power-cycle capture, not a clean return
# (mi4-xnu-reboot-path-cannot-reset).
#
# Three steps: (1) confirm the device is back, (2) build/write the REAL card image to userdata's head,
# (3) press. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

EXPECT_ARM=armed-d13-299ee994

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2a. stage the REAL 896 MiB iOS 7.1.2 rootfs as the card image (the COW base) =="
tools/build_root_volumes.sh --real

echo "== 2b. write it to userdata's head (partition offset 0) =="
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b push out/stage90/xnu_card_hfs.img /data/local/tmp/xnu_card_hfs.img
adb -s 4a2fe00b shell "dd if=/data/local/tmp/xnu_card_hfs.img of=/dev/block/mmcblk0p25 bs=1M seek=0 conv=fsync"
echo "read-back (the card's first 8 MiB):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=1M count=8 2>/dev/null" | sha256sum
echo "expected (first 8 MiB of the built card):"
head -c 8388608 out/stage90/xnu_card_hfs.img | sha256sum

echo "== 3. the press (gate + runner, fastboot boot only) =="
scripts/preflight_boot_check.sh --allow-xnu-entry
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm="$EXPECT_ARM"
