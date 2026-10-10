#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE, after waking it (hold Power ~10-15s).
#
# WHERE THIS SITS.  The 970h press (2026-10-10) BOOTED the D13 XNU entry line for the FIRST TIME:
# capture 970h-armed-d13-299ee994-20261010-last_kmsg.txt (426731 B, 3981 lines) carries 32 xnu_live_*
# keys and the whole arm_vm_init console, and it stops DEAD on the last line
#   arm_vm_init: switching translation-tables now...
# with no abort and no panic - exactly the 971 frontier.  The pre-switch TLB flush (970h) is the
# reason the boot path ran at all; this arm (971) is the next, confirmed stop.
#
# THE CAUSE (doc 971).  D13 osfmk/arm/arm_vm_init.c built the system table WRONG:
#     cpu_ttb = gTopOfKernel + L1_SIZE;
#     bzero((void*)phys_to_virt(cpu_ttb), L1_SIZE);     <-- a FRESH table, nothing copied
# 4570 COPIES the boot table into the system table instead:
#     boot_ttep = args->topOfKernelData;                 <-- the boot table at 0x80a00000
#     cpu_ttep  = boot_ttep + ARM_PGBYTES * 4;           <-- the SAME address D13 bzeroes
#     bcopy(boot_tte, cpu_tte, ARM_PGBYTES * 4);
# The entry's own source states the dependency by name (src/entry/entry_stubs.c:2210-2214): the
# console's descriptors survive set_mmu_ttb(cpu_ttb) BECAUSE arm_vm_init copies the boot table into
# the table the MMU walks afterwards.  D13 does no such copy, and D13's managed map is
# [MANAGED_BASE 0xC0000000, 0xC0000000 + gMemSize), which does NOT contain the console VA 0xde500000
# (RAM_CONSOLE_BASE) - so the moment the table switched, the console's sections were gone and every
# record written after arm_vm_init was dropped.  That is the 970h log ending at exactly that line.
#
# THE FIX (971, D13 osfmk/arm/arm_vm_init.c ONLY, applied by tools/stage_d13_vm_init.sh): replace the
# bzero with 4570's bcopy.  ONE statement.  D13 needs NO V==P clear loop after it (it maps the
# identity region FRESH at l2_cache_to_range/l2_map_linear_range, and gPhysBase == gVirtBase here).
# Verified by value in the LINKED image: arm_vm_init @0x8001e928 now calls bcopy @0x80015d20
# (0x8001e9d4); the 299ee994 elf called bzero there.
#
# WHAT THIS PROVES / HOW TO READ IT:
#   keys AFTER arm_vm_init present  => the console survived the table switch.  The SMC/USB/storage
#                                      probes and, if the boot is clean, the Darwin banner appear for
#                                      the first time - the frontier moves past arm_vm_init.
#   STILL STOPS at arm_vm_init       => the boot table is not the only place the console's
#                                      descriptors live; the model is wrong one rung out.  Recorded,
#                                      not papered over.
#
# THE ARM is `armed-d13-89bcc6e3` (entry bin 89bcc6e3, payload rebuilt around it; the payload's own
# switch record 6c2b6038 is UNCHANGED - the 971 change is a kernel-tree edit, so no arm switch moved).
# Entry image 6331476 bytes; the press sends stage90-qcdt.img 89e86c77... (9351168 bytes).
# The medium is 965c/968's real iOS 7.1.2 HFSX rootfs over the CARD COW; HDD_WRITE=0, so the BASE IS
# NEVER WRITTEN - fully reversible, no brick risk (a power cycle reverts every write).  RESIDENT rung
# (POST_END_TICKS=0): budget for a black screen + a power-cycle capture, not a clean return
# (mi4-xnu-reboot-path-cannot-reset).
#
# Three steps: (1) confirm the device is back, (2) build/write the REAL card image to userdata's head,
# (3) press.  Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

EXPECT_ARM=armed-d13-89bcc6e3

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2a. stage the REAL 896 MiB iOS 7.1.2 rootfs as the card image (the COW base) =="
tools/build_root_volumes.sh --real

echo "== 2b. write it to userdata's head (partition offset 0) =="
# **STREAMED, not pushed.**  The 970h attempt pushed the 896 MiB image to /data/local/tmp and hit
# "No space left on device": /data on this device is a 512 MB tmpfs, not the userdata partition.
# p25 (userdata, 13610499584 bytes) is directly writable as root and unmounted, so dd the image in
# over adb stdin.  This is the 965c/968 COW base write and it is the one that worked.
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b shell "dd of=/dev/block/mmcblk0p25 bs=1M conv=fsync" < out/stage90/xnu_card_hfs.img
echo "read-back (the card's first 8 MiB):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=1M count=8 2>/dev/null" | sha256sum
echo "expected (first 8 MiB of the built card):"
head -c 8388608 out/stage90/xnu_card_hfs.img | sha256sum

echo "== 3. the press (gate + runner, fastboot boot only) =="
scripts/preflight_boot_check.sh --allow-xnu-entry
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm="$EXPECT_ARM"