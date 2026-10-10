#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE, after waking it (hold Power ~10-15s).
#
# WHY: this is the FIRST D13 arm whose entry line is OBSERVABLE. The D13 entry->kernel line has never
# emitted a single key on hardware (970's 484 MiB and 968's 16 MiB arms both produced zero output), and
# 970e established the cause by construction: the D13 `__start` MMU fast path (locore.s:61 `beq
# mmu_initialized`, taken because the payload jumps in with M=1) skips the boot path's only TTBR0 write
# (locore.s:108), so the PAYLOAD's identity table - which maps the RAM console at mmu.c:5478 - is still
# TTBR0 when `entry_live_init` runs. `entry_live_map` SKIPS an occupied slot (entry_stubs.c:2122), every
# candidate reads occupied, and `entry_live_refuse(2u)` turns the whole live channel off - probes AND
# panics alike. That is why the D13 line is silent. 4570 boots because its `start.s:152` writes TTBR0
# UNCONDITIONALLY, so its console slot is free.
#
# THE FIX (970f, `src/entry/entry_stubs.c`, gated `#if STAGE90_ENTRY_D13`): `entry_live_init` now ADOPTS
# the pre-existing mapping when it is a section descriptor whose PA is the console's own (`0xde500000`)
# and AF is set - and STILL requires the signature check through it (`entry_stubs.c:2370`, the same proof
# a fresh install must pass). Nothing is written to the table; someone else's mapping is verified and
# used. A 4570 entry build compiles none of it, so the proven 4570 image is byte-identical.
#
# WHAT THIS PROVES / HOW TO READ IT (the runner prints xnu_live_* keys):
#   `xnu_live_adopted=1`   => the console came up THROUGH an inherited mapping - this *is* the cause, and
#                             the D13 line is now observable. Then the ordinary keys (`xnu_live_console`,
#                             `xnu_live_tmr_setup_*`, `xnu_live_pce_*`, storage/COW/SMEM keys) for the
#                             first time say how far __start -> arm_init -> ... actually got.
#   NO `xnu_live_adopted` and NO keys => adoption alone was not enough; the fault is genuinely BELOW
#                             `entry_live_init` (a distinct, now-separated question - do NOT paper over).
#   `xnu_live_refusals` > 0 with no `_console` => the console was refused for a DIFFERENT reason
#                             (`g_live_refuse` names which: 1 no table, 3 domain, 4 no signature,
#                             5 no uncached encoding).
#
# THE ARM is `armed-d13-2544428e` (entry bin `2544428e`, payload `1fd1c060`), 968's exact switch set with
# ONE source change (entry_stubs.c). The medium is 965c/968's real iOS 7.1.2 HFSX rootfs over the CARD
# COW; `HDD_WRITE=0`, so the BASE IS NEVER WRITTEN - fully reversible, no brick risk (a power cycle
# reverts every write). RESIDENT rung (`POST_END_TICKS=0`): budget for a black screen + a power-cycle
# capture, not a clean return (`mi4-xnu-reboot-path-cannot-reset`).
#
# Three steps: (1) confirm the device is back, (2) build/write the REAL card image to userdata's head,
# (3) press. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

EXPECT_ARM=armed-d13-2544428e

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