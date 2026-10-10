#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE, after waking it (hold Power ~10-15s).
#
# WHY: this is the FIRST D13 arm whose entry line has a chance to be OBSERVABLE. The D13 entry->kernel
# line has never emitted a single key on hardware (970's 484 MiB and 968's 16 MiB arms both produced zero
# output), and the cause is now pinned: D13 `__start` takes its MMU fast path (`locore.s:61 beq
# mmu_initialized`, taken because the payload jumps in with SCTLR.M=1), so the boot path below it NEVER
# RUNS. That has two consequences, both fixed by 970g:
#
#   (1) TTBR0 is never rebuilt, so it stays the PAYLOAD's table - `stage90_l1_table` at PA 0x6c4000, BELOW
#       0x80000000 (the real 970 capture reads mmu_ttbr0_after=0x006c4000). `entry_live_init` refuses a
#       table outside the kernel window (`entry_stubs.c:2308`, why=1), and that refusal is SILENT (its own
#       record travels through the console it just refused).
#   (2) the boot translation table at `topOfKernelData` is never BUILT and never ZEROED. 4570's `start.s`
#       zeroes it (`invalidate_tte:` 0 over 10240 entries = 0xa000 bytes) before mapping; D13's boot path
#       omits that loop, so every slot the `map:` loop does not reach - INCLUDING the console's own L1 slot
#       (0xde500000 >> 20 = index 0xde5, far above memSize) - keeps stale DRAM. `entry_section_install`
#       refuses an occupied slot, so the console is refused EVEN WITH a HIGH table. The working 4570
#       capture reads `xnu_live_slot_before=0x00000000`: the slot was zero before the install.
#
# THE FIX (970g, D13 `osfmk/arm/locore.s` ONLY, applied by tools/stage_d13_boot_path.sh):
#   edit 1 - REMOVE the fast path, so `__start` always runs the boot path;
#   edit 2 - ZERO the boot table after the TTBR0 write, exactly as 4570's start.s does.
# Verified by value in the linked image: `__start` @0x80000000 falls into `mmu_reinitialize` @0x80000008
# (no beq), TTBR0 write @0x8000004c, `_970g_zero_tte` @0x80000064 runs the 10240-word loop, then the PC
# section map and `map:` @0x800000b4 - 4570's exact order.
#
# WHAT THIS PROVES / HOW TO READ IT (the runner prints xnu_live_* keys):
#   `xnu_live_console` present  => the boot path ran, the table is HIGH, the console slot was free. The
#                                  D13 line is OBSERVABLE for the first time. The first ~15 keys (the init
#                                  block and the first wrapped probes) then show how far `_start` ->
#                                  `arm_init` got. Keys AFTER `arm_vm_init` are NOT expected yet - D13's
#                                  `arm_vm_init.c:352-353` bzero`s a FRESH cpu_ttb where 4570 bcopy`s the
#                                  boot table (a separate, later defect).
#   STILL ZERO keys            => the boot path was NOT the cause. Recorded, not papered over.
#   `xnu_live_l1` still 0x6c4000 / `xnu_live_refusals>0` with no `_console` => TTBR0 did not move; the
#                                  fast-path removal did not take (should be impossible - verified in the ELF).
#
# THE ARM is `armed-d13-321e3332` (entry bin `321e3332`, payload `a42d7386`): 968/970f's exact switch set
# (the 970g change is a TREE edit, not a switch, so the config record is byte-identical to 970f's). The
# medium is 965c/968's real iOS 7.1.2 HFSX rootfs over the CARD COW; `HDD_WRITE=0`, so the BASE IS NEVER
# WRITTEN - fully reversible, no brick risk (a power cycle reverts every write). RESIDENT rung
# (`POST_END_TICKS=0`): budget for a black screen + a power-cycle capture, not a clean return
# (mi4-xnu-reboot-path-cannot-reset).
#
# Three steps: (1) confirm the device is back, (2) build/write the REAL card image to userdata's head,
# (3) press. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

EXPECT_ARM=armed-d13-321e3332

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