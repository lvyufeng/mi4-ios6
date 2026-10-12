#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE. 995 = 994's switch set VERBATIM + STAGE90_XNU_DYLD_WITNESS=1:
# the dyld halt-message witness in the POOL (`osfmk/arm/trap.c`: `ios7wit_dyld_message` + its call in
# `sleh_undef`'s `udf #0xdefe` path).
#
# WHY 994 IS NOT THE ONE TO PRESS. 994's press (2026-10-12, arm armed-d13-8b4c9a3e, commit a4a2532,
# evidence out/stage90/captures/cancro-last_kmsg-994-armed-d13-8b4c9a3e.txt) CONFIRMED the trap-10
# wire fix - `IOS7LAB vm_allocate64 size=00000000:00001000 flags=1 result=0 address=000e1000`
# (log:4101) - and then HALTED inside dyld at its REAL error, with the message unreadable:
#     :4103 onward, 235x: `usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x2beb90c4`
#
# THE NEW FRONTIER, read BY VALUE:
#   slide 0xB8000, so pc 0x2be010c4 = _dyld_fatal_error (fe de ff e7 at file 0x10c4, i.e.
#   `udf #0xdefe`), and the register block's r5 = 0x2bedc390 = a HEAP STRING in dyld's malloc pool -
#   the message dyld::halt was handed (throwf -> __simple_salloc succeeds -> vsprintf -> strdup ->
#   _main's catch (const char*) fatal pad -> halt(r5)) and NOTHING on CANCRO can print it: the tree's
#   own reader (the LEO witness, trap.c:71-126) is compiled out under
#   `#if defined(BOARD_CONFIG_QSD8250_LEO)`.
#
# THE FIX (tools/stage_d13_dyld_witness.sh stages it, gated on the SAME switch):
#   osfmk/arm/trap.c (ONE pool file) grows ios7wit_dyld_message, called in sleh_undef immediately
#   after the dyld_all_image_info_addr printf - the SAME path that prints A, inside the `udf #0xdefe`
#   branch (trap.c:877 returns early for every other undefined instruction). The reader takes the
#   message pointer from the word at task->all_image_info_addr + 0x20 (the field setAlImageInfosHalt,
#   dyld 0x2be08d04 = `str r0, [r3, #32]`, writes; dyld::halt strlcpy-s _error_string into it) with
#   copyin + copyinstr, printing `IOS7WITNESS dyld msg=... copyinstr=... len=...` then `dyld> <text>`
#   in 48-byte lines, two print slots at most (claimed BEFORE any I/O), every failure branch printing
#   its reason. Fail-soft: every user read is a copyin; no panic, allocation, sleep or lock.
# VERIFIED BY VALUE: tools/check_d13_dyld_witness.py --expect on asserts, off the linked image's own
# disassembly and bytes, that ios7wit_dyld_message exists, that sleh_undef's region calls it after a
# `bl printf`, that the witness's region calls copyin/copyinstr/printf and names the `#32` field
# offset, and that the image carries the IOS7WITNESS dyld msg= / dyld> %s / dyld> <copyinstr failed>
# literals; the FROZEN 994 image is passed as --control and must FAIL ON and PASS OFF.
#
# THE ARM is `armed-d13-<sha8 of out/stage90/xnu_arm_entry.bin>`. Every switch is 994's, verbatim, plus
# STAGE90_XNU_DYLD_WITNESS=1 (window 0x1e400000 = 484 MiB, RESIDENT=1, USB ladder, MEM_TOTAL/CARD_TOTAL=1,
# HFSX root from card, CARD_COW=1, HDD_WRITE=0, KEXTB_LOGIN=1, SP_ALIGN=1, VMHINT_GUARD=1,
# COMMPAGE_ALIAS=1, IOS7_TRAPS=1). The ENTRY BIN MOVED (8b4c9a3e -> the new sha8).
#
# WHAT THIS PROVES / HOW TO READ IT (pre-registered; dyldtrace's convergence folded in 2026-10-12):
#   On the FIRST TWO `udf #0xdefe` traps (two print slots only), between the register dump and the
#   next dump, expect the rev-2 witness shape:
#     `IOS7WITNESS dyld msg=0x... copyinstr=0 len=<n>` + `dyld> <text>` lines
#        (word at A + 0x20, the strlcpy'd _error_string copy setAlImageInfosHalt stored; A = the
#        already-printed dyld_all_image_info_addr);
#     `IOS7WITNESS r5 msg=0x... copyinstr=0 len=<n>` + `dyld5> <text>` lines
#        (r5 = the strdup'd pool string halt was handed - the same text via the other pointer);
#     a verdict line: `IOS7WITNESS r5 matches errorMessage` (both pointers agree) /
#        `DIFFERS from errorMessage` (LOUD - two errors exist; r5's is the one dyld died on) /
#        `unreadable, no comparison`;
#     one r8 classification line: `exception object (signature at r8-0x20)` /
#        `exception block base (signature at r8+0x30)` / `message interior len=<n>` / `stale ...`.
#   BUDGET RISK IS EXCLUDED BY VALUE: 994's log carries 235 undefined-instruction traps, ALL at the
#   SAME pc 0x2beb90c4 (= _dyld_fatal_error + that run's slide 0xB8000), with NO earlier udf anywhere
#   - so the witness's two slots hold launchd's real fatal trap, not an unrelated storm.
#   LENGTH WINDOW: the message length is pinned to L in [68,71] (dyldtrace: r8 = the exception object
#   = allocation block + 0x50, measured; strdup(r5) -> __cxa_allocate_exception allocates nothing
#   between them, so (r8 - 0x50) - r5 = align4(L+1) = 0x48). SELF-CHECK: the printed len= must satisfy
#   (r8 - 0x50) - r5 == align4(len+1) == 0x48. A len OUTSIDE [68,71] falsifies the length model that
#   ranked the candidates (docs/reference/d13-dyld-first-failure.md sec 10.4/10.8) - report it, do
#   not reinterpret.
#   THE TEXT should match a ranked candidate (sec 10.8):
#   rank-1 `malformed mach-o image: segment __TEXT does not span all load commands`
#     (sniffLoadCommands+0x1f4; literal 66 + "__TEXT" 6 = exactly L=70 - the mapped image's segment
#     geometry is self-inconsistent with its load-command region);
#   rank-2 `no suitable image found.  Did find:\n\t<path>` (dyld::load; the 15-byte `image not found`
#     is excluded by the window; a RUNTIME lookup miss - the cache closure was audited 0-missing, so
#     no cache entry is actually absent);
#   rank-3 `incompatible cpu-subtype: 0x%08X in %s` (isCompatibleMachO). Also seen by value:
#   `recursiveLoadLibraries`'s `Library not loaded: %s` (dyld 0x2be0a35e) and mach_error_string
#   (0x2be09220) - whichever matches names the next rung's fix shape.
#   FALSIFICATION: no `IOS7WITNESS` header at all => the witness was never called (the switch never
#   reached osfmk_arm_trap.o - check with tools/check_d13_dyld_witness.py - or the trap is not on the
#   reader's path); `DIFFERS from errorMessage` => something else wrote A + 0x20 or r5 is not halt's
#   argument - report it rather than trusting either string; `copyinstr=<nonzero>` on BOTH reads =>
#   A + 0x20 is not the message pointer in this dyld (fall back to a frame walk of the halted thread).
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

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the confirmed
# 994/993 and the regressed 992 arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 995 (scripts/build_995.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 995 (scripts/build_995.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 995 (scripts/build_995.sh) first."; exit 1; }

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
# 995 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg, so start the HOST reader
# FIRST with --wait, and it attaches the moment the payload enumerates and captures the console LIVE.
USB_OUT="out/stage90/captures/usb-console-995-$(date -u +%Y%m%d-%H%M%S).txt"
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