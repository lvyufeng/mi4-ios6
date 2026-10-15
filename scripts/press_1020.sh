#!/usr/bin/env bash
# ====================================================================================================
# press_1020.sh.  MODELLED ON scripts/press_1018.sh (the RESIDENT-rung press: the USB console reader +
# the /proc/last_kmsg recovery).
#
# Press 1020: KILL THE VFP FALSE POSITIVE - a genuine `udf` must RAISE, not loop.
# Switch: STAGE90_XNU_UNDEF_RAISE=1 (pool: osfmk/arm/trap.c `sleh_undef`).  Design: docs/1020-design.md.
# Root cause: records/1019-rootcause.txt (commit d8c350b).
#
# THE ONE QUESTION THIS RUNG DECIDES.  1019 root-caused the resident no-return: pid 1's Thumb `udf #254`
# (un-slid VA 0x3823aa54 = libsystem_platform.dylib `__os_alloc_slow`+0x108) RE-EXECUTES FOREVER -
# 8183x measured, invariant pc, NO PC advance.  The cause is `sleh_undef`'s user-mode arm returning
# WITHOUT advancing PC for every undefined instruction except the hard-coded Apple-dyld magic
# `0xfedeffe7`.  1020 MOVES that `return;` INSIDE the `if (!thread->machine.vfp_enable)` block, so a
# genuine `udf` arriving with VFP already enabled FALLS THROUGH to the printf + `EXC_BAD_INSTRUCTION`.
# The question: does the loop DIE (the udf raises) and the boot move to a legible next frontier?
#
# THE PRE-REGISTERED CELLS (docs/1020-design.md §5; read by tools/check_1020_capture.py):
#   R-RAISES-ONCE  - `xnu_live_undef_seq` advances to 2 at ONE pc and STOPS + the `usermode undefined
#                    instruction` printf fires >=1x + (the process is gone or a panic follows) => FIXED.
#   R-RAISES-CLEAN - `_seq == 1` only (VFP was already on) + printf >=1x + no loop => FIXED.
#   R-STILL-LOOPS  - `_seq` climbs to > 100 at one invariant pc, printf 0x => NOT FIXED (the moved
#                    `return` did not take effect - check the `-D` reached the pool object; 1010 lesson).
#   R-VFP-CONTINUES- the boot advances PAST the udf to a DIFFERENT frontier (no undef loop, no panic) =>
#                    MOVED (the udf is passed; the next frontier is downstream).
#   R-CTL          - no `xnu_live_undef_*` keys at all AND no boot self-test `undef_handler_undef_handled`
#                    row => INSTRUMENT_UNSOUND, WITHHOLD.  R-CTL is MANDATORY FIRST: the control is the
#                    boot self-test row
#                    `stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled=0x1` AND a
#                    `xnu_live_undef_seq` row.
# Read it with: python3 -I tools/check_1020_capture.py <capture>
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible.  fastboot boot only, NEVER flash.
# ====================================================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

LIVE=out/stage90
REC=$LIVE/xnu_arm_entry-config.txt

echo "== 0. the record must be 1020's arm =="
grep -q '^STAGE90_XNU_UNDEF_RAISE=1$' "$REC" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_UNDEF_RAISE=1 - this is not 1020's arm (build it with scripts/build_1020.sh first)"; exit 1; }
# The DIRECT channel this arm carries, so `xnu_live_undef_*` survive the 24576u cap (1018's switch).
grep -q '^STAGE90_XNU_UNDEF_DIRECT=1$' "$REC" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_UNDEF_DIRECT=1 - without the direct channel the udf witness can be silenced by the cap before the frontier (the 1017/1018 instrument defect)"; exit 1; }
# **GLOBAL_FIX IS NOT IN THE RECORD AND CANNOT BE** (press_1015b.sh (0g), MEASURED): it is a POOL switch
# (osfmk/arm/pmap.c:1357) that no writer in src/entry/ echoes, and preflight_boot_check.sh's
# ENTRY_CFG_KEYS is a set difference against the record, so naming it there would make the gate refuse
# every press.  Its ONE declaration is the drop-in file park_1015b.sh wrote at the freeze point (1020's
# build does NOT touch it; this press requires exactly that one line).
grep -qx 'STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix' "$LIVE/xnu_arm_entry-arm-extra.txt" || {
  echo "REFUSING: out/stage90/xnu_arm_entry-arm-extra.txt does not carry 'STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix' - the pool fix that keeps the retry storm gone is not declared, so the udf this arm's raise exists to handle would never be reached"; exit 1; }
EXPECT_ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
echo "   arm = $EXPECT_ARM"

# BY VALUE (0a): the property the arm claims - with the switch ON, `sleh_undef`'s user arm reaches its
# `return` ONLY under the VFP-enable branch, i.e. the vfp_enable test's 'already enabled' successor
# reaches the EXC-printf raise - re-read here on the LINKED image, so the press refuses an image whose
# pool object is stale.  This is the SAME checker the build calls (build_1020.sh step 8).  It DISCRIMINATES
# - MEASURED: switch OFF -> FAIL, switch ON (arm 47f664c4) -> PASS - so it is a proof, not a formality.
# (There is NO linked-image back-edge census here: the compiler REORDERED the ON codegen so the raise is a
# FALL-THROUGH, so the park's first-draft 'backward branch' predicate read 0 on a CORRECT arm and would
# have REFUSED it.  See park_1020.sh block (b) and records/1020-build-defects.txt.)
[[ -f tools/check_d13_undef_raise.py ]] || { echo "REFUSING: tools/check_d13_undef_raise.py is ABSENT - it is the press's by-value claim on the linked image (the build writes it, so an absent one means the build tree is incomplete)"; exit 1; }
python3 -I tools/check_d13_undef_raise.py "$LIVE/xnu_arm_entry.elf" --expect on >/dev/null 2>&1 || {
  echo "REFUSING: tools/check_d13_undef_raise.py \"$LIVE/xnu_arm_entry.elf\" --expect on did NOT pass - the linked image's sleh_undef does not carry the moved \`return\`: the vfp_enable test's 'already enabled' successor does not reach the EXC-printf raise. The pool object was rebuilt but a STALE one was linked, or the -D never reached the pool compile (build_entry.sh only LINKS out/xnu_kernel_obj_d13/osfmk_arm_trap.o). Re-run scripts/build_1020.sh"; exit 1; }
echo "   (0a) by value: tools/check_d13_undef_raise.py --expect on PASSes (sleh_undef's raise IS reachable on the VFP-already-enabled path in the linked image)"

echo "== 1. the real iOS 7.1.2 rootfs as the card image (the COW base) =="
tools/build_root_volumes.sh --real

echo "== 2. hardware gate =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2b. write the card image to userdata's head (partition offset 0) =="
# STREAMED, not pushed (/data is a 512 MB tmpfs, not the userdata partition).
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b shell "dd of=/dev/block/mmcblk0p25 bs=1M conv=fsync" < out/stage90/xnu_card_hfs.img
echo "read-back (the card's first 8 MiB):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=1M count=8 2>/dev/null" | sha256sum
echo "expected:"
head -c 8388608 out/stage90/xnu_card_hfs.img | sha256sum

echo "== 3. the press (gate + runner, fastboot boot only) =="
# 1020 is a RESIDENT rung: if it does not return it keeps NO /proc/last_kmsg of its own, so start the
# HOST reader FIRST; it attaches the moment the payload enumerates. Primary channel: step 4.
USB_OUT="out/stage90/captures/usb-console-1020-$(date -u +%Y%m%d-%H%M%S).txt"
tools/usb_console_read.py --out "$USB_OUT" --wait 600 --seconds 0 &
USB_PID=$!
echo "   (USB console reader armed: pid $USB_PID -> $USB_OUT)"
trap 'kill "$USB_PID" 2>/dev/null || true' EXIT

scripts/preflight_boot_check.sh --allow-xnu-entry
set +e
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm="$EXPECT_ARM"
RUN_RC=$?
set -e
echo "   (runner exit $RUN_RC; a resident rung not returning shows as 2)"

kill "$USB_PID" 2>/dev/null || true
echo "== USB console capture: $USB_OUT =="
[ -s "$USB_OUT" ] && echo "   bytes read: $(wc -c < "$USB_OUT")" || echo "   (empty)"

echo "== 4. recovery: the run's own log via /proc/last_kmsg (carrier is the FLASHED ROM) =="
for _ in $(seq 1 90); do
  if adb devices 2>/dev/null | grep -q 4a2fe00b || fastboot devices 2>/dev/null | grep -q 4a2fe00b; then break; fi
  sleep 5
done
if adb devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh --read-only
elif fastboot devices 2>/dev/null | grep -q 4a2fe00b; then
  scripts/recover_last_kmsg.sh
else
  echo "   (device not visible yet; if still dark, the operator power-press is the next step, then:"
  echo "    scripts/recover_last_kmsg.sh --read-only while Android is up; do not boot anything else first)"
fi
echo "== DONE =="
echo "   adjudicate with: python3 -I tools/check_1020_capture.py <capture>"
echo "     R-RAISES-ONCE  the genuine udf RAISED (EXC_BAD_INSTRUCTION) after the lazy-VFP trip; loop DEAD"
echo "     R-RAISES-CLEAN VFP was already on: the first trip raised; loop DEAD"
echo "     R-STILL-LOOPS  _seq climbs to > 100 at one invariant pc, printf 0x => NOT FIXED (check the -D"
echo "                    reached osfmk_arm_trap.o - the 1010 lesson: an absent effect, wrong channel)"
echo "     R-VFP-CONTINUES the udf is PASSED; the frontier is downstream (the allocator's _mach_vm_map)"
echo "     R-CTL          WITHHOLD (the instrument did not run - read BOTH control halves FIRST)"
echo "   READ R-CTL BEFORE ANY CELL: withhold unless the boot self-test row"
echo "   stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled=0x1 AND a xnu_live_undef_seq row are"
echo "   both present.  And read the CAP: if xnu_live_capped precedes the last undef _seq, the witness was"
echo "   silenced by the channel (but this arm carries UNDEF_DIRECT, so the keys ride the DIRECT channel)."