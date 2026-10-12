#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE. 993 = 992's switch set UNCHANGED (no new config key) + three
# staged tree edits: `arm_vm_init` ZEROES the stolen high-vector L2 table, and `pmap_create_sharedpage`
# MEASURES (and fail-soft clears) slot 0xf4 before the alias enter.
#
# WHY 992 IS NOT THE ONE TO PRESS. 992's press (2026-10-12, arm armed-d13-fce6e8dd, commit d5facfc,
# evidence /tmp/cancro-last_kmsg.txt) REGRESSED. 992 and 991's logs are identical through line 4043;
# 992 then panics at 4044, inside vm_commpage_init -> pmap_create_sharedpage:
#   panic(cpu 0 caller 0x8046b6b0): Kernel data abort. (faulting address: 0xc1233b14)
#   r4=0xc0837000 r6=0x80000000 r7=0xffff4000 r9=0x80b813d0 r10=0x003fffcc r11=0x000ffd5a
#   pc=0x80028b90 lr=0x80049abc fsr=0x7 (translation fault, page, READ)
#
# THE ROOT CAUSE, read BY VALUE (991/992 logs + the disassembly):
#   pc=0x80028b90 is `ldr r0, [r0, #12]` in pmap_enter_options+0x29c - the `pv_h->pmap` load before
#   `panic("pmap_enter_options: null pv_list\n")`, taken because the alias slot held a nonzero "old
#   mapping". fp = old_pte>>12 = r11 = 0x000FFD5A (leftover PA 0xFFD5A000); pai = 0xFFD5A - 0x80000 =
#   0x7FD5A (523,610); pv_h = r4 + pai*20 = 0xC1233B08; [pv_h+12] = 0xC1233B14 = the fault address
#   exactly. The pv_head_table holds 123,904 entries - the walk left it. WHY the slot held garbage:
#   arm_vm_init steals vecpt_start = first_avail as the high-vector L2 table and writes ONLY slot 0xF0
#   (VECTORS_BASE 0xFFFF0000); topOfKernel 0x80a00000 => vecpt_start = 0x80B81000, and r9 = 0x80b813d0
#   = vecpt_start + 0xF4*4 = exactly pmap_pte(kernel_pmap, 0xFFFF4000). The 992 alias enter was the
#   FIRST EVER enter into that slot; its 255 neighbors were raw DRAM. Every other table in the boot path
#   is zeroed (l2_cache_to_range(zero=TRUE), arm_vm_init.c:250's own "used by the bootloader" comment;
#   pmap_grab_page :2769) - the stolen vector page was the sole exception.
#
# THE FIX (tools/stage_d13_commpage_alias.sh stages all three, gated on the SAME switch):
#   1. arm_vm_init.c: count+print the stolen table's nonzero slots, bzero the whole L2 page, count+print
#      again ("IOS7LAB vecl2:").
#   2. pmap.c's alias block: print slot 0xf4 BEFORE the enter ("L2 slot pre"), fail-soft clear if nonzero
#      ("stale slot cleared"), print "L2 slot post" after.
# VERIFIED BY VALUE: tools/check_commpage_alias.py's checks 6-7 assert both in the linked image and
# REFUSE a 992-shaped one (rehearsed against the frozen 992 elf: exit 1, exactly checks 1/6/7 fail).
#
# THE ARM is `armed-d13-<sha8 of out/stage90/xnu_arm_entry.bin>`. Every switch is 992's, verbatim
# (window 0x1e400000 = 484 MiB, RESIDENT=1, USB ladder, MEM_TOTAL/CARD_TOTAL=1, HFSX root from card,
# CARD_COW=1, HDD_WRITE=0, KEXTB_LOGIN=1, SP_ALIGN=1, VMHINT_GUARD=1, COMMPAGE_ALIAS=1). The ENTRY BIN
# MOVED (fce6e8dd -> the new sha8).
#
# WHAT THIS PROVES / HOW TO READ IT (pre-registered):
#   `IOS7LAB vecl2: N nonzero slot(s) ... before zeroing` (a small N; slot 0xf4's 0xFFD5A0xx neighbourhood
#   among the printed lines), then `... 0 nonzero slot(s) after zeroing`; then
#   `IOS7LAB commpage alias: L2 slot pre = 0x00000000` (the arm_vm_init zero ran AND reached this pmap),
#   the 992 markers `IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN` and
#   `IOS7LAB commpage cpus: ...`; then bsd_init -> launchd, and the 231x
#   `0xffff4090 ... Translation fault` burst MUST NOT recur.
#   FALSIFICATION: the same 231x 0xffff4090 burst recurs (then the alias sits in a pmap the user context
#   does not consult - 994 = the user-pmap/context side), or a NEW kernel abort/panic appears (its pc
#   names the next wall).
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible. RESIDENT rung
# (POST_END_TICKS=0): budget for a black screen + a power-cycle capture, not a clean return
# (mi4-xnu-reboot-path-cannot-reset). The arm carries the full USB ladder (EP1-IN), so start the host
# reader FIRST and it captures the console LIVE - resident or faulted, either way readable.
#
# Three steps: (1) confirm the device is back, (2) write the REAL card image to userdata's head,
# (3) press. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the spent 991 and
# regressed 992 arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-13355bf0 ]] && {
  echo "REFUSING: out/stage90 holds 991's arm (13355bf0). Build 993 (scripts/build_993.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 993 (scripts/build_993.sh) first."; exit 1; }

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
# 993 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg, so start the HOST reader
# FIRST with --wait, and it attaches the moment the payload enumerates and captures the console LIVE.
USB_OUT="out/stage90/captures/usb-console-993-$(date -u +%Y%m%d-%H%M%S).txt"
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