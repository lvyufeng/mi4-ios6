#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE. 996 = 995's switch set VERBATIM + STAGE90_XNU_SHREG_PROBE=1:
# (a) the ios7wit_lines CHUNKER FIX in the dyld witness (all 103 bytes of the message now print),
# (b) NEW witness reads of the SAME dyld_all_image_infos struct (A+0x64 sharedCacheBaseAddress, A+0x50
#     slide, A+0x40 errorKind, A+0x44/0x48/0x4c char* fields), (c) a bounded kernel probe on the
#     shared-region syscalls 294/438 in bsd/vm/vm_unix.c.
#
# WHY. 995's press (arm armed-d13-4efcabae, commit 947a75d, log
# out/stage90/captures/cancro-last_kmsg-995-armed-d13-4efcabae.txt :4111-4138) read dyld's halt
# message by measurement: len=103, BOTH pointers (A+0x20 and r5) byte-identical, text beginning
#     `Library not loaded: /usr/lib/libbsm.0.dylib?  Re`
# ('?' = the witness's by-design \n->'?' at index 43; the fmt @0x1e0df has ONE xref in the whole
# dyld: the throw at 0x2be0a35e in ImageLoader::recursiveLoadLibraries). The message was composed at
# that site, so the referrer R = this->getPath() (13 chars, /sbin/launchd) and the literal is 51
# chars + P 23 chars => R+S = 29 => the REASON S is 16 chars. A chunker bug in ios7wit_lines
# (it zeroed the byte after each 48-byte chunk) hid everything past byte 47; fixed.
#
# THE REFRAMING: /usr/lib/libbsm.0.dylib is launchd's FIRST LC_LOAD_DYLIB (verified by value; second
# is libSystem.B.dylib). Neither has a FILE on the volume (usr/lib holds only dyld + two ios7lab
# dylibs), but libbsm IS in the shared cache (@0x37454000; cache 315,952,666 B on the volume, 696
# images). Minimal explanation: dyld's shared cache was NEVER MAPPED (mapSharedCache failed silently;
# no fd 2 on CANCRO) and every cached dylib degraded to a file lookup - the first dependency to miss
# halted dyld. dyld's shared-region entry points are BSD syscalls (294 shared_region_check_np,
# wrapper 0x2be05774; 438 shared_region_map_and_slide_np, wrapper 0x2be0579c); the D13 tree wires
# BOTH (init_sysent.c:440/649, bodies linked @0x8041fd84 / @0x804201c0) - so the table is complete
# and only the RUNTIME returns are unknown. 996 measures them.
#
# WHAT THIS PROVES / HOW TO READ IT (pre-registered, three INDEPENDENT readings; report each as
# measured, do not reinterpret any):
#
#   READING 1 - the full message (witness chunker fixed). Between the register dump and the next
#     dump, per firing: `dyld> ` lines now carry ALL 103 bytes (48 + 48 + 7) and `dyld5> ` the same.
#     Read R (at index 63, 13 bytes) and S (at index 87, 16 bytes) VERBATIM. Expect (from the
#     disassembly, dyldtrace in flight): R = `/sbin/launchd`; S = one of the dyld reason strings -
#     measured candidate list: `image not found` @0x1cc9d (15 - EXCLUDED by the 16-char arithmetic
#     unless R is 14), `no suitable image found.  Did find:` @0x1ccad (long - excluded),
#     `symbol not found` @0x1f168 (16 - the only exact fit, but surprising for a load failure; if it
#     is really this, the next rung re-targets BIND-time, not load-time). If R+S != 29 in the actual
#     text, report the discrepancy - the literal count is the thing that would be wrong.
#     FALSIFICATION: no `IOS7WITNESS` header at all => witness dead in this build (checker says ON;
#     check the arm's record); `DIFFERS from errorMessage` => two errors exist - report, don't pick.
#
#   READING 2 - dyld's OWN word for the shared cache (witness, same firing):
#     `IOS7WITNESS info cacheBase=0x........ slide=0x........ errKind=0x........`
#     cacheBase = word at A+0x64 (dyld_all_image_infos.sharedCacheBaseAddress, v0x0e layout; offsets
#     confirmed by value by dyldtrace off the pinned dyld). cacheBase != 0 => the cache WAS mapped
#     (at that address) and the "never mapped" hypothesis is FALSIFIED - the next rung re-targets
#     WHY libbsm was not found IN the cache (path mismatch / UUID / mapSharedCache's header checks).
#     cacheBase == 0 => the cache was never mapped - hypothesis HOLDS; read the IOS7SHREG lines for
#     WHERE. Also: `IOS7WITNESS info client="..." target="..." symbol="..."` may carry dyld's own
#     error fields (A+0x44/0x48/0x4c) - if non-empty they are a SECOND, independent source for R/S
#     ("target" should name libbsm if dyld-239 populates them on this path; empty/null = it does not).
#
#   READING 3 - the syscall returns (kernel probe, lines appear inline during the boot):
#     `IOS7SHREG check_np pid=<n> out=0x........` then `IOS7SHREG check_np ret=<n>`:
#       ret=0 means a shared region ALREADY existed (its start went to the out pointer) - impossible
#       on a fresh boot, so ret=0 at this point would itself be the finding; docs/reference/
#       d13-shreg-frontier.md sec 3 pins the expected first-call value to ENOMEM 12 (vm_map_exec
#       CREATES the region and leaves it empty). 22 = no region at all - a deeper defect. pid should be
#       the process that exec'd dyld = launchd (pid 1 on this rung's root... report the number).
#     `IOS7SHREG map_slide pid=<n> fd=<n> count=<n> mappings=0x........ slide=0x........
#      slide_start=0x........ size=0x........` then `IOS7SHREG map_slide ret=<n>`:
#       ret=0 => map+slide SUCCEEDED (then cacheBase must be != 0 - if both disagree, report both);
#       ret!=0 => THIS is the failing step; the D13 failure catalog (docs/reference/d13-shreg-
#       frontier.md, vmabi-recon) maps the value to a cause. NOTE the errno space is MIXED in this
#       function (top-level: EINVAL=22 chroot / KERN_FAILURE=5 too-many-mappings / copyin errno;
#       _shared_region_map folds KERN_* into errno: EFAULT=14, EPERM=1, ENOMEM=12, EINVAL=22) -
#       match the value to the SITE the probe prints, not to a global table.
#       THE PRE-REGISTERED PREDICTION (vmabi-recon sec 2/7, by value): check_np ret=12, then
#       map_slide count=3 mappings=<addr> slide=<n> ret=22 WITH map_file kr=4 (KERN_INVALID_ARGUMENT)
#       = the 32 B-wire misparse (this kernel's struct shared_file_mapping_np is 20 B, so m[0].size
#       reads 0 -> vm_map_enter_mem_object -> EINVAL, before any slide work). If map_slide instead
#       returns 12 with kr=3 (KERN_NO_SPACE), the wire is NOT the wall and sec 6's geometry is; sec 7
#       has the full decision tree. Report as measured either way.
#     FALSIFICATION: NO IOS7SHREG lines at all => the probe never reached the compiled object
#     (build_xnu_arm_kernel.sh only LINKS out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o - the .o must have
#     been RECOMPILED when the tree was staged; check the build log) or dyld never reached
#     mapSharedCache (then Reading 2's cacheBase decides); check the arm's record carries
#     STAGE90_XNU_SHREG_PROBE=1.
#
#   CROSS-CHECK (the whole point of three readings): message-says-not-found + cacheBase==0 +
#   map_slide ret!=0 = one coherent story (cache mapping failed at the returned errno); message +
#   cacheBase!=0 + ret==0 = the opposite (cache fine; lookup failed) - the next rung is chosen by
#   WHICH story the numbers actually tell, never by preference.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible. RESIDENT rung
# (POST_END_TICKS=0): budget for a black screen + a power-cycle capture (mi4-xnu-reboot-path-cannot-
# reset). The arm carries the full USB ladder (EP1-IN): start the host reader FIRST, it captures the
# console LIVE either way.
#
# Three steps: (1) confirm the device is back, (2) write the REAL card image to userdata's head,
# (3) press. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the confirmed
# 995/994/993 and the regressed 992 arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 996 (scripts/build_996.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 996 (scripts/build_996.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 996 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 996 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the USB ladder and BOTH probes (the readers only help if it does) =="
grep -q '^STAGE90_XNU_USB_STREAM=1' out/stage90/xnu_arm_entry-config.txt || {
  echo "REFUSING: this entry record has no STAGE90_XNU_USB_STREAM=1 - the host reader below would"
  echo "capture an empty file. Rebuild the arm with the ladder on."; exit 1; }
grep -q '^STAGE90_XNU_DYLD_WITNESS=1' out/stage90/xnu_arm_entry-config.txt || {
  echo "REFUSING: record has no STAGE90_XNU_DYLD_WITNESS=1 - Readings 1 and 2 would be absent."
  exit 1; }
grep -q '^STAGE90_XNU_SHREG_PROBE=1' out/stage90/xnu_arm_entry-config.txt || {
  echo "REFUSING: record has no STAGE90_XNU_SHREG_PROBE=1 - Reading 3 would be absent."; exit 1; }
echo "   (record carries USB_PROBE/DEV/ENUM/STREAM=1 + DYLD_WITNESS=1 + SHREG_PROBE=1)"

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
# 996 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg, so start the HOST reader
# FIRST with --wait, and it attaches the moment the payload enumerates and captures the console LIVE.
USB_OUT="out/stage90/captures/usb-console-996-$(date -u +%Y%m%d-%H%M%S).txt"
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