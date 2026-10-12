#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE.  987 = 986's switch set REBUILT with THE HFS MALLOC_ZONE
# REDIRECT GATED TO 4570, so the embedded entry image agrees with the source that fixes the stub 986's
# press hit.
#
# WHY 986 IS NOT THE ONE TO PRESS.  986's press (2026-10-12, arm armed-d13-2171f0d1, commit bcd4039) is
# a DECISIVE SUCCESS for the mutex fix: the boot leapt from 985's stop in IOKitInitializeTime (no
# cal_enter, no ubc_init) all the way past EVERY BSD-init line to `bsd_init: vfs_mountroot` ->
# `hfs_mountfs` -> `hfs_chash_getcnode`, and it READ A BLOCK OFF THE CARD (HFSX volume header at LBA
# 0x400002: w0=0x05005848 'HX' + 0x100 -> HFSX 0x4858 v5). It then stopped with a `stub_hit`:
# name_ptr=0x80561fec -> "__MALLOC", caller=0x801c90b4 -> `hfs_chash_getcnode+0x100`.
#
# ROOT CAUSE (987).  `src/shims/hfs/hfs_port_force.h` §3 rewrites the HFS `MALLOC_ZONE`/`FREE_ZONE`
# onto `__MALLOC`/`_FREE`.  That is a 4570-only necessity: 4570's `kern_malloc.c` cannot see
# `struct cnode` (no `<hfs/hfs_cnode.h>`), so its `kmzones[]` rows 75/76/77/91/92/95 are the literal
# `{0, KMZ_MALLOC}` and `__MALLOC_ZONE` PANICS at the first cnode allocation (experiment 900).  The
# Darwin-13 tree's `kern_malloc.c` DOES include `hfs_cnode.h` (line 100) and its rows are the real
# `{SOS(cnode),KMZ_CREATEZONE}` rows - so D13 serves every HFS type through its own zone path.  And the
# redirect is actively WRONG on D13: 4570 names the allocator `__MALLOC` (4-arg); D13 names it
# `_MALLOC` (3-arg, kern_malloc.c:551).  So `__MALLOC` names a symbol the D13 pool never defines, the
# linker turns it into a stub (`xnu_arm_entry_undef.txt:14`), and the boot dies at the mount's first
# allocation.
#
# THE FIX (tools/build_xnu_arm_kernel.sh define + hfs_port_force.h guard): gate §3 on
# `STAGE90_HFS_MALLOC_ZONE`, defined iff the tree's own `kern_malloc.c` does NOT see `hfs_cnode.h`
# (i.e. 4570).  With the guard off, the tree's own `MALLOC_ZONE`/`FREE_ZONE` (D13's `_MALLOC_ZONE`,
# which reaches the live zone rows) stand.  VERIFIED BY VALUE: no `bsd_hfs*.o` references `__MALLOC`;
# the linked `xnu_arm_entry.elf` has a real `_MALLOC_ZONE` at 0x80117008 and NO `__MALLOC` symbol.
#
# THE ARM is `armed-d13-2df6333a`.  Every switch is 986's (window 0x1e400000 = 484 MiB, RESIDENT=1,
# USB_PROBE/DEV/ENUM/STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1, HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD/
# CARD_COW=1, HDD_WRITE=0, SMEM_PROBE=1, IDLE_NO_SLEEP=0).  The ENTRY BIN MOVED (2171f0d1 -> 2df6333a).
#
# WHAT THIS PROVES / HOW TO READ IT (the discriminating experiment):
#   The boot moves PAST `hfs_chash_getcnode`'s first allocation (a new stop, or further: the HFS mount's
#   later stages, extents, `/sbin/launchd`) => the redirect was the frontier and the gating took.
#   FALSIFICATION: the same `stub_hit=__MALLOC` recurs => the mechanism is not the (or not the only)
#   cause.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible.  RESIDENT rung
# (POST_END_TICKS=0): budget for a black screen + a power-cycle capture, not a clean return
# (mi4-xnu-reboot-path-cannot-reset).  The arm carries the full USB ladder (EP1-IN), so start the host
# reader FIRST and it captures the console LIVE - resident or faulted, either way readable.
#
# Three steps: (1) confirm the device is back, (2) build/write the REAL card image to userdata's head,
# (3) press.  Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

EXPECT_ARM=armed-d13-2df6333a

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the USB ladder (the reader only helps if it does) =="
grep -q '^STAGE90_XNU_USB_STREAM=1' out/stage90/xnu_arm_entry-config.txt || {
  echo "REFUSING: this entry record has no STAGE90_XNU_USB_STREAM=1, so the arm streams nothing to"
  echo "EP1-IN and the host reader below would capture an empty file. Rebuild the arm with the ladder on."
  exit 1
}
echo "   (record carries USB_PROBE/DEV/ENUM/STREAM=1 - the console will stream to EP1-IN)"

echo "== 2a. stage the REAL 896 MiB iOS 7.1.2 rootfs as the card image (the COW base) =="
tools/build_root_volumes.sh --real

echo "== 2b. write it to userdata's head (partition offset 0) =="
# **STREAMED, not pushed.**  /data on this device is a 512 MB tmpfs, not the userdata partition, so the
# image is dd'd in over adb stdin to the unmounted userdata block device.
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b shell "dd of=/dev/block/mmcblk0p25 bs=1M conv=fsync" < out/stage90/xnu_card_hfs.img
echo "read-back (the card's first 8 MiB):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=1M count=8 2>/dev/null" | sha256sum
echo "expected (first 8 MiB of the built card):"
head -c 8388608 out/stage90/xnu_card_hfs.img | sha256sum

echo "== 3. the press (gate + runner, fastboot boot only) =="
# 987 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg, so start the HOST reader
# FIRST with --wait, and it attaches the moment the payload enumerates and captures the console LIVE.
USB_OUT="out/stage90/captures/usb-console-987-$(date -u +%Y%m%d-%H%M%S).txt"
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