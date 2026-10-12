#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE. 990 = 989's switch set + STAGE90_XNU_SP_ALIGN=1: the D13
# tree hands C 8-byte-aligned stacks at its two measured gates.
#
# WHY 989 IS NOT THE ONE TO PRESS. 989's press (2026-10-12, arm armed-d13-2e0eebf5, commit 1bc3fdb) is
# a DECISIVE SUCCESS in the pre-registered sense - the abort MOVED. 988's dt-census guard HELD
# (xnu_live_dtref_ok=0; the probe walked only the tree root), the HFSX mount stayed up, and the boot
# ran the WHOLE mount + bsd_init: `Added memory device md0/rmd0` -> `BSD root: md0` ->
# `bsd_init: done` -> `attempting to start init of /sbin/launchd` -> NEW first abort:
#   Fatal exception: sleh_abort: alignment fault
#   pc=0x8021b6a8 = parse_machfile+0x60c, f461279f `vld1.32 {d18}, [r1 :64]`, r1=sp+168=0xcfdbbb24,
#   sp=0xcfdbba7c.  168 is 8-aligned, so the qualified NEON pair load faults iff sp is 4 (mod 8).
# The block is widen_segment_command() inlined (mach_loader.c:238: scp->vmaddr = scp32->vmaddr; ...).
#
# ROOT CAUSE (990). Two D13-tree gates hand C a 4-mod-8 stack; 4570 has neither:
#   1. astloop (osfmk/arm/machine_routines_asm.s:133) saves 9 words (`stmfd sp!,{r0-r7,lr}` = 36 = 4
#      mod 8) immediately before `blx _ast_taken` - the OPERATIVE gate: bsdinit's thread first ran C
#      in ast_taken, entered from thread_return_join (no push of its own).
#   2. osfmk/arm/pcb.c: iss = STACK_IKS(stack) = stack_top - 68 (68 = sizeof(arm_saved_state_t) = 4
#      mod 8), and Call_continuation (cswitch.s:46) loads the continuation's sp straight from it -
#      4570's thread_kernel_state was 16-aligned (80): one value, two definitions.
# The exec chain that died sits under gate 1: ast_taken -> bsd_ast -> bsdinit_task -> load_init_program
# -> execve -> __mac_execve -> exec_activate_image -> exec_mach_imgact -> load_machfile ->
# parse_machfile (its own 304-byte entry frame preserves the residue).
#
# THE FIX (tools/stage_d13_sp_align.sh + the switch): `& ~7u` on the saved-state block AND the frame
# below it in BOTH pcb.c functions (top 4 stack bytes unused), and plain `sub sp, sp, #4` /
# `add sp, sp, #4` around astloop's `blx _ast_taken`. VERIFIED BY VALUE: tools/check_sp_alignment.py
# PASSES the linked image and REFUSES 989's (astloop's cumulative delta -36 = 4 mod 8). Switch OFF
# compiles the tree's original text byte for byte (#else carries it).
#
# THE ARM is `armed-d13-<sha8 of out/stage90/xnu_arm_entry.bin>`.  Every switch is 989's (window
# 0x1e400000 = 484 MiB, RESIDENT=1, USB_PROBE/DEV/ENUM/STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1,
# HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD/CARD_COW=1, HDD_WRITE=0, SMEM_PROBE=1, IDLE_NO_SLEEP=0)
# plus STAGE90_XNU_SP_ALIGN=1.  The ENTRY BIN MOVED (2e0eebf5 -> the new sha8).
#
# WHAT THIS PROVES / HOW TO READ IT (the discriminating experiment):
#   The boot moves PAST the pid-1 exec (no vld1.32 {d18} alignment fault; launchd's own C runs, or a
#   new stop further in) => the 4-mod-8 C stack was 989's frontier and the two gates were the whole of
#   it.
#   FALSIFICATION: the SAME fault recurs (a vld1.32-class abort at a 4-mod-8 sp) => the walker's model
#   missed a producer of the residue - check the 990 clause actually ran (it should have refused such
#   an image) and find the third 4-mod-8 source.
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

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the stale 989 arm.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-2e0eebf5 ]] && {
  echo "REFUSING: out/stage90 holds 989's arm (2e0eebf5). Build 990 (scripts/build_990.sh) first."; exit 1; }

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
# 990 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg, so start the HOST reader
# FIRST with --wait, and it attaches the moment the payload enumerates and captures the console LIVE.
USB_OUT="out/stage90/captures/usb-console-990-$(date -u +%Y%m%d-%H%M%S).txt"
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