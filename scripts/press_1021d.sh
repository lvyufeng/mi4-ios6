#!/usr/bin/env bash
# ====================================================================================================
# press_1021d.sh.  MODELLED ON scripts/press_1020b.sh (the RESIDENT-rung press: the USB console reader
# + the /proc/last_kmsg recovery).
#
# Press 1021d: REMOVE COMPILER-EMITTED FP/NEON FROM THE KERNEL POOL (-msoft-float).
# Switch: STAGE90_XNU_GENERAL_REGS=1 (one flag append to tools/build_xnu_arm_kernel.sh's
# CC_ARGS/CXX_ARGS).  Design: docs/1021c-general-regs-design.md.  Root cause:
# records/1021-fpu-rootcause.txt (commit 063288e).
#
# THE ONE QUESTION THIS RUNG DECIDES.  The 1020b press read R-RAISES-ONCE: the 1019 `udf` loop is DEAD,
# and the frontier MOVED to a KERNEL-MODE NEON undefined instruction at `0x8047b0ac` inside
# `mach_msg_send` (`vld1.32 {d16-d17}, [r0]!`, `lr=0x8047b0b0`).  Root cause: the kernel starts with
# FPEXC.EN ON, `save_vfp_context` (osfmk/arm/pcb.c:298-307) clears it on the next context switch, kernel
# switch-in never restores it, and the kernel's OWN compiler-emitted NEON then faults.  The fix is to
# stop the compiler emitting it: `-msoft-float`, the flag Apple's own ARM kernel uses
# (MakeInc.def:377-379).  **Not `-mgeneral-regs-only`** - that is aarch64-only in clang and is silently
# dropped for armv7 (measured byte-identical codegen).  The question: does retiring the pool's FP retire
# the undef, and does the boot move past `mach_msg_send`?
#
# THE PRE-REGISTERED CELLS (design §4.4):
#   A RETIRED      - no kernel-mode undef at `0x8047b0ac`; the run proceeds past `mach_msg_send` (the
#                    frontier moves, or the run ends at `entry_epilogue`)          => THE FIX STANDS
#   B STILL-THERE  - the same undef at `0x8047b0ac` (same `lr=0x8047b0b0`)           => NOT REBUILT (read
#                    the by-value cell FIRST - it should have refused this press)
#   C NEW-FAULT    - a DIFFERENT undef pc, or a link refusal on `__aeabi_f*`/`__aeabi_d*` => soft-float
#                    added a libgcc reference the link does not resolve
#   D EARLIER-STOP - the run stops somewhere earlier (e.g. `arm_init`'s VFP self-test) => the flag broke
#                    something that legitimately needs the FPU (read design §3.2)
#   E NO-CHANGE    - byte-for-byte the 1020b frontier => the switch did not reach the compile; the
#                    by-value clause should have caught this - if it passed, the clause is wrong
#
# **THIS PRESS IS NOT TO READ A LOG.**  A `fastboot boot` spent to look at a log has no question behind
# it: this rung's question is the one above, and the arm must be the one the by-value gates below
# certify.  The recovery step reads `/proc/last_kmsg` only because the run's own log is the reading of
# the question ([[mi4-last-kmsg-reader-carrier]]: the carrier is the FLASHED ROM).
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible.  fastboot boot only, NEVER flash.
# ====================================================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

LIVE=out/stage90
REC=$LIVE/xnu_arm_entry-config.txt

echo "== 0. the record must be 1021d's arm =="
# 1021d's OWN switch (this rung's readability carrier - the substance is the flag, proved by value
# below; a record that says NEON-free over a pool full of vld1 is exactly the 1020 defect).
grep -q '^STAGE90_XNU_GENERAL_REGS=1$' "$REC" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_GENERAL_REGS=1 - this is not 1021d's arm (build it with scripts/build_1021d.sh first)"; exit 1; }
# The inherited switches this arm's set carries VERBATIM (1020b's set).
grep -q '^STAGE90_XNU_UNDEF_RAISE=1$' "$REC" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_UNDEF_RAISE=1 - 1020's raise fix must stay on, or pid 1's genuine udf loops forever again (8183x measured) and 1021d would measure a different frontier"; exit 1; }
grep -q '^STAGE90_XNU_UNDEF_DIRECT=1$' "$REC" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_UNDEF_DIRECT=1 - without the direct channel the udf witness can be silenced by the cap before the frontier (the 1017/1018 instrument defect)"; exit 1; }
# **GLOBAL_FIX IS NOT IN THE RECORD AND CANNOT BE** (press_1015b.sh (0g), MEASURED): it is a POOL switch
# (osfmk/arm/pmap.c:1357) that no writer in src/entry/ echoes, and preflight_boot_check.sh's
# ENTRY_CFG_KEYS is a set difference against the record, so naming it there would make the gate refuse
# every press.  Its ONE declaration is the `xnu_arm_entry-arm-extra.txt` drop-in, which **1021d's build
# writes itself**, and which the park freezes with the arm.
# **THE GREP BELOW IS THE PROCESS CHECK, NOT THE PROOF.** It reads a file the build wrote, so it can go
# stale in a way a codegen read cannot; the BY-VALUE clause that follows (the `pmap_flush_tlbs`
# fingerprint) is what actually certifies the fix is in the linked image
# ([[mi4-self-written-record-is-not-a-constraint]]).  BOTH are required: the grep catches a carrier that
# was not re-written this build, the fingerprint catches a stale pool object.
grep -qx 'STAGE90_XNU_GLOBAL_FIX=1 role=1021d-general-regs' "$LIVE/xnu_arm_entry-arm-extra.txt" || {
  echo "REFUSING: out/stage90/xnu_arm_entry-arm-extra.txt does not carry 'STAGE90_XNU_GLOBAL_FIX=1 role=1021d-general-regs' (it holds: '$(cat "$LIVE/xnu_arm_entry-arm-extra.txt" 2>/dev/null)') - the pool fix that keeps the retry storm gone is not declared by THIS build (build_1021d.sh writes this line at its tail), so the kernel-mode undef this rung's flag exists to retire would never be reached"; exit 1; }
EXPECT_ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
echo "   arm = $EXPECT_ARM"

# BY VALUE (0-POOL): the inherited GLOBAL-FIX (1015b) is in the pool object the linked image was built
# from - `pmap_flush_tlbs` materializes `kernel_pmap` EXACTLY 3 times (OFF/Apple = 2).  This is what the
# 1020 press lacked and what 1021d's pool rebuild could drop again: 1020 derived its EXTRA from the
# record, the token was lost, the storm returned, and pid 1 never reached the udf
# (records/1020-build-defect.txt).  Read off the FROZEN pool object (`osfmk_arm_pmap.o` is a POOL file),
# so a stale object is refused.
_gf_obj=out/xnu_kernel_obj_d13/osfmk_arm_pmap.o
[[ -f "$_gf_obj" ]] || { echo "REFUSING: $_gf_obj is absent - 1015b's GLOBAL-FIX cannot be certified"; exit 1; }
_gf_kp=$(arm-none-eabi-objdump -r "$_gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'MOVW_ABS_NC.*kernel_pmap' || true)
_gf_full=$(arm-none-eabi-objdump -r "$_gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'R_ARM_CALL.*arm_tlb_flushID$' || true)
_gf_asid=$(arm-none-eabi-objdump -r "$_gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'R_ARM_CALL.*arm_tlb_flushID_ASID$' || true)
[[ "$_gf_kp" == "3" && "$_gf_full" == "1" && "$_gf_asid" == "1" ]] || {
  echo "REFUSING: pmap_flush_tlbs materializes kernel_pmap x$_gf_kp (want 3), arm_tlb_flushID x$_gf_full / arm_tlb_flushID_ASID x$_gf_asid (want 1/1) - GLOBAL-FIX is NOT in the pool object, so the retry storm 1015b killed is BACK and the kernel-mode undef this rung's flag exists to retire would never be reached. -DSTAGE90_XNU_GLOBAL_FIX=1 has NO record carrier, so a record-derived EXTRA drops it (records/1020-build-defect.txt). Re-run scripts/build_1021d.sh."; exit 1; }
echo "   (0-POOL) by value: pmap_flush_tlbs carries the 3rd kernel_pmap materialization ($_gf_kp sites) and the 1/1 flush arms - 1015b's GLOBAL-FIX IS in the pool object this image was built from"

# BY VALUE (0a): THE RUNG'S OWN CLAIM.  The switch's substance is a FLAG, so the only proof it reached
# the compile is a property of the OBJECTS: the pool must be NEON-free, AND the linked `mach_msg_send`
# body - the symbol 1020b stopped in at `0x8047b0ac` - must contain no FP/NEON instruction.
#
# **THE CHECKER IS REQUIRED.**  Unlike 1020's `check_d13_undef_raise.py` (which `build_entry.sh` writes
# and which `src/entry/` ships), `tools/check_d13_no_neon.py` is this rung's own tool; its absence means
# the arm's own claim cannot be certified, and a press that skipped the clause would spend the device on
# an arm whose "NEON-free" claim nothing ever checked ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
[[ -f tools/check_d13_no_neon.py ]] || {
  echo "REFUSING: tools/check_d13_no_neon.py is ABSENT - it is this press's by-value claim on the frozen pool and on the linked mach_msg_send body, and there is no sound fallback census. A press that skipped it would spend the device on an arm whose 'NEON-free' claim nothing ever checked"; exit 1; }

# 0a-0. the checker's OWN self-test - a checker that cannot fail is not a check.
python3 -I tools/check_d13_no_neon.py --selftest >/dev/null 2>&1 || {
  echo "REFUSING: tools/check_d13_no_neon.py --selftest did NOT PASS - the checker cannot certify anything (an inert checker would bless any pool, and this press would spend the device on a claim that was never read)"; exit 1; }
echo "   (0a-0) by value: tools/check_d13_no_neon.py --selftest PASSes (the checker is live)"

# 0a-1. THE WHOLE-POOL CENSUS.  A switch that suppressed NEON in `mach_msg.o` but not in `ip_dummynet.o`
# would leave `vpush {d8-d9}` waiting.  **AND IT MUST NOT BE A VACUOUS PASS OVER AN EMPTY OR STALE DIR**
# (design §5.3, defect class m1015b): the object count on disk must equal the source count in the pool's
# own content record `out/xnu_kernel_src_d13.sha256`.
_pool=out/xnu_kernel_obj_d13
[[ -d $_pool ]] || { echo "REFUSING: $_pool is absent - the pool the linked image was built from is not on disk, so the census would read nothing and pass vacuously"; exit 1; }
_src_rec=out/xnu_kernel_src_d13.sha256
[[ -s $_src_rec ]] || { echo "REFUSING: $_src_rec is absent - it is the pool's own source-content record (written by tools/build_xnu_arm_kernel.sh after the compile loop) and the independent count this clause compares against. Its absence means the pool build did not complete"; exit 1; }
_want_objs=$(grep -c '^[0-9a-f]\{64\} ' "$_src_rec" || true)
_got_objs=$(ls "$_pool"/*.o 2>/dev/null | wc -l)
[[ $_want_objs -gt 0 && $_got_objs -eq $_want_objs ]] || {
  echo "REFUSING: $_pool holds $_got_objs object(s) but $_src_rec names $_want_objs source(s) the pool was built from - the census below would count the WRONG set (a partial or stale directory passes any NEON-free test vacuously). Re-run scripts/build_1021d.sh"; exit 1; }
python3 -I tools/check_d13_no_neon.py --pool "$_pool" --expect none --min-objects "$_want_objs" >/dev/null 2>&1 || {
  echo "REFUSING: tools/check_d13_no_neon.py --pool $_pool --expect none did NOT pass - the pool the linked image was built from still contains FP/NEON instructions (OFF/Apple: 273 of 705 objects have >=1). -msoft-float is a FLAG on CC_ARGS/CXX_ARGS, dropped silently by a stale pool, so the arm would read as ON over Apple's bytes. Re-run scripts/build_1021d.sh"; exit 1; }
echo "   (0a-1) by value: the WHOLE pool is NEON-free - $_got_objs objects censused in $_pool (== the $_want_objs source(s) in $_src_rec), 0 FP/NEON instructions"

# 0a-2. THE LINKED SYMBOL.  Read on the LINKED image, so the press refuses an image whose pool object is
# stale (`build_entry.sh` only LINKS `osfmk_ipc_mach_msg.o`).  The window is the symbol's OWN extent, so
# `mach_msg_trap`'s neighbours cannot satisfy the check on the ON arm's behalf.
python3 -I tools/check_d13_no_neon.py "$LIVE/xnu_arm_entry.elf" --symbol mach_msg_send --expect none >/dev/null 2>&1 || {
  echo "REFUSING: the LINKED image's mach_msg_send still contains FP/NEON instructions - the pool flag did not reach the object the linker saw, or a STALE out/xnu_kernel_obj_d13/osfmk_ipc_mach_msg.o was linked (build_entry.sh only LINKS it, so a green entry build says nothing about this half). Re-run scripts/build_1021d.sh"; exit 1; }
echo "   (0a-2) by value: the LINKED image's mach_msg_send is NEON-free (the instruction 1020b stopped at, 0x8047b0ac, is gone)"

# 0a-3. THE DISCRIMINATING CONTROL.  The SAME checker run in the `--expect any` direction on the FROZEN
# PRE-RUNG arm (1020b, b5093f00), whose `mach_msg_send` carries the 4 FP instructions 1020b stopped at.
# The pair (pre-rung -> any PASS, this arm -> none PASS) is what proves the checker discriminates rather
# than always saying the same thing ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
_ctrl=$LIVE/frozen/armed-d13-b5093f00/xnu_arm_entry.elf
[[ -s $_ctrl ]] || { echo "REFUSING: the frozen 1020b elf ($_ctrl) is absent - the NEGATIVE CONTROL for this press's by-value clause cannot be taken, so the clause above cannot be shown to DISCRIMINATE (it might pass on any image). 1020b's park must exist before 1021d's press"; exit 1; }
python3 -I tools/check_d13_no_neon.py "$_ctrl" --symbol mach_msg_send --expect any >/dev/null 2>&1 || {
  echo "REFUSING: the frozen 1020b elf's mach_msg_send does NOT report FP instructions in the --expect any direction - the checker is either broken or pointed at the wrong extent, so the 'none' PASS above certifies nothing. A control that agrees with the test proves only that the checker says one thing"; exit 1; }
echo "   (0a-3) by value: NEGATIVE CONTROL - the frozen 1020b elf (b5093f00) REPORTS FP in mach_msg_send (--expect any PASSes), so the 'none' above is a reading and not a constant"

# BY VALUE (0b): the inherited 1020 raise claim still holds on the LINKED image - 1021d must not regress
# it.  The SAME checker the 1020b build and press call (`build_1020b.sh` step 8 / `press_1020b.sh` 0a).
[[ -f tools/check_d13_undef_raise.py ]] || { echo "REFUSING: tools/check_d13_undef_raise.py is ABSENT - it is the inherited 1020 claim's by-value check on the linked image (the build writes it, so an absent one means the build tree is incomplete)"; exit 1; }
python3 -I tools/check_d13_undef_raise.py "$LIVE/xnu_arm_entry.elf" --expect on >/dev/null 2>&1 || {
  echo "REFUSING: tools/check_d13_undef_raise.py \"$LIVE/xnu_arm_entry.elf\" --expect on did NOT pass - the linked image's sleh_undef does not carry 1020's moved \`return\`, so the udf loop is back and this press would measure a different frontier than the kernel-mode NEON undef it exists to retire. Re-run scripts/build_1021d.sh"; exit 1; }
echo "   (0b) by value: tools/check_d13_undef_raise.py --expect on PASSes (1020's raise is intact in the linked image)"

echo "== 1. the real iOS 7.1.2 rootfs as the card image (the COW base) =="
tools/build_root_volumes.sh --real

echo "== 2. hardware gate =="
# THE HARDWARE SAFETY PROPERTIES ([[mi4-hardware-run-safety-gate]]), checked before anything is sent.
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2b. write the card image to userdata's head (partition offset 0) =="
# STREAMED, not pushed (/data is a 512 MB tmpfs, not the userdata partition).  This is userdata's HEAD,
# outside the boot chain; CARD_COW=1 / HDD_WRITE=0, so the base is never written - fully reversible.
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b shell "dd of=/dev/block/mmcblk0p25 bs=1M conv=fsync" < out/stage90/xnu_card_hfs.img
echo "read-back (the card's first 8 MiB):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=1M count=8 2>/dev/null" | sha256sum
echo "expected:"
head -c 8388608 out/stage90/xnu_card_hfs.img | sha256sum

echo "== 3. the press (gate + runner, fastboot boot only) =="
# 1021d is a RESIDENT rung: if it does not return it keeps NO /proc/last_kmsg of its own, so start the
# HOST reader FIRST; it attaches the moment the payload enumerates. Primary channel: step 4.
USB_OUT="out/stage90/captures/usb-console-1021d-$(date -u +%Y%m%d-%H%M%S).txt"
tools/usb_console_read.py --out "$USB_OUT" --wait 600 --seconds 0 &
USB_PID=$!
echo "   (USB console reader armed: pid $USB_PID -> $USB_OUT)"
trap 'kill "$USB_PID" 2>/dev/null || true' EXIT

scripts/preflight_boot_check.sh --allow-xnu-entry
# **THE COMMAND LINE, PRINTED BEFORE IT IS ISSUED.** The SINGLE device write this press makes is
# `fastboot boot` of the payload - never `fastboot flash`. The runner below issues exactly this, on the
# serial this gate has just confirmed alone (`run_and_capture.sh:84` sets `IMAGE`, `:119` the serial,
# `:3775` the call).
    echo "   fastboot boot -s 4a2fe00b out/stage90/stage90-qcdt.img   (fastboot boot, NEVER flash)"
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
echo "   adjudicate against docs/1021c-general-regs-design.md §4.4:"
echo "     A RETIRED      no kernel-mode undef at 0x8047b0ac; the run proceeds past mach_msg_send"
echo "                    (the frontier moves, or the run ends at entry_epilogue) => THE FIX STANDS"
echo "     B STILL-THERE  the same undef at 0x8047b0ac (same lr=0x8047b0b0) => NOT REBUILT; read the"
echo "                    by-value cell FIRST - it should have refused this press"
echo "     C NEW-FAULT    a DIFFERENT undef pc, or a link refusal on __aeabi_f*/__aeabi_d* =>"
echo "                    soft-float added a libgcc reference the link does not resolve"
echo "     D EARLIER-STOP the run stops earlier (e.g. arm_init's VFP self-test) => the flag broke"
echo "                    something that legitimately needs the FPU (read design §3.2)"
echo "     E NO-CHANGE    byte-for-byte the 1020b frontier => the switch did not reach the compile;"
echo "                    if the by-value clause passed, the clause is wrong"
echo "   Read the 1020b reading beside it: records/1020b-press-read.txt (R-RAISES-ONCE, the frontier"
echo "   this rung exists to retire).  And read the CAP as before: this arm carries UNDEF_DIRECT, so the"
echo "   xnu_live_undef_* keys ride the DIRECT channel."