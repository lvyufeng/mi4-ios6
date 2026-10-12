#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE.  988 = 987's switch set REBUILT with THE __LINKEDIT
# KEXT-BOOTSTRAP TEARDOWN COMPILED OUT, so the embedded entry image agrees with the source that fixes
# the panic the boot hit right after `bsd_init()` returned.
#
# WHY 987 IS NOT THE ONE TO PRESS.  987's press (2026-10-12, arm armed-d13-2df6333a, commit cb2dd66) is
# a DECISIVE SUCCESS for the HFS redirect: the boot ran the WHOLE HFS mount (HFSX vol header read at
# LBA 0x400002: w0=0x05005848 'HX' + 0x100 -> HFSX 0x4858 v5) and EVERY bsd_init line to
# `bsd_init: done` (55 bsd_init: lines). cal_enter/exit=1, iokitinit_exit=1 -- 984's wall retired. It
# then panicked at the FIRST statement after bsd_init returns:
#   osfmk/kern/startup.c:408  OSKextRemoveKextBootstrap()
#
# ROOT CAUSE (988).  `OSKext::removeKextBootstrap()` (`libkern/c++/OSKext.cpp:746`) does, inside a LIVE
# `#if CONFIG_KXLD` block (CONFIG_KXLD=1):
#     seg_to_remove = (kernel_segment_command_t *)getsegbyname("__LINKEDIT");   // :843
#     seg_data = (void *) seg_to_remove->vmaddr;                                // :859
# Our kernel is a synthetic Mach-O (`_mh_execute_header`=0x804e54e4) whose header declares ncmds=3
# with segments only __TEXT/__DATA/__PRELINK_TEXT -- there is NO __LINKEDIT, so getsegbyname returns
# NULL and :859 dereferences it. BY VALUE: pc=0x800b2c90 = removeKextBootstrap+0x198,
# `e590501c ldr r5,[r0,#28]`, r0=0, far=0x1c, fsr=0x5. The sibling calls at :805 (__KLD) and :838
# (__symtab) ARE guarded and do not fault; :843/:859 is the unguarded one.
#
# THE FIX (tools/stage_d13_kextb_login.sh + build_xnu_arm_kernel.sh define): a new switch
# `STAGE90_XNU_KEXTB_LOGIN` wraps the whole `#if CONFIG_KXLD ... #else ... #endif` block in
# `#ifndef ... #endif`, so with `-DSTAGE90_XNU_KEXTB_LOGIN=1` the teardown is compiled out on D13 and
# the switch-off text is byte-identical to Apple (4570's object does not move). VERIFIED BY VALUE: the
# linked `xnu_arm_entry.elf`'s removeKextBootstrap no longer carries the faulting dereference.
#
# THE ARM is `armed-d13-<sha8 of out/stage90/xnu_arm_entry.bin>`.  Every switch is 987's (window
# 0x1e400000 = 484 MiB, RESIDENT=1, USB_PROBE/DEV/ENUM/STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1,
# HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD/CARD_COW=1, HDD_WRITE=0, SMEM_PROBE=1, IDLE_NO_SLEEP=0).
# The ENTRY BIN MOVED (2df6333a -> the new sha8).
#
# WHAT THIS PROVES / HOW TO READ IT (the discriminating experiment):
#   The boot moves PAST `OSKextRemoveKextBootstrap` (a new stop, or further: serial_keyboard_init,
#   vm_page_init_local_q, thread_bind, then `vm_pageout()` and the idle loop) => the __LINKEDIT deref
#   was 987's frontier and the guard took.
#   FALSIFICATION: the SAME fault (pc=0x800b2c90-class, r0=0) recurs => it did not take (stale object
#   / the guard did not reach the compile) or the deref is not the (only) cause.
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

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the stale 987 arm.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-2df6333a ]] && {
  echo "REFUSING: out/stage90 holds 987's arm (2df6333a). Build 988 (scripts/build_988.sh) first."; exit 1; }

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
# **STREAMED, not pushed.** /data on this device is a 512 MB tmpfs, not the userdata partition, so the
# image is dd'd in over adb stdin to the unmounted userdata block device.
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b shell "dd of=/dev/block/mmcblk0p25 bs=1M conv=fsync" < out/stage90/xnu_card_hfs.img
echo "read-back (the card's first 8 MiB):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=1M count=8 2>/dev/null" | sha256sum
echo "expected (first 8 MiB of the built card):"
head -c 8388608 out/stage90/xnu_card_hfs.img | sha256sum

echo "== 3. the press (gate + runner, fastboot boot only) =="
# 988 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg, so start the HOST reader
# FIRST with --wait, and it attaches the moment the payload enumerates and captures the console LIVE.
USB_OUT="out/stage90/captures/usb-console-988-$(date -u +%Y%m%d-%H%M%S).txt"
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