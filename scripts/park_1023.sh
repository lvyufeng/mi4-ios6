#!/usr/bin/env bash
# ====================================================================================================
# park_1023.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1022.sh (itself the 1021d/1015b pattern),
# WITH 1015b's POOL-MOVED DISCIPLINE (a pool rebuild rung: syscall_sw.c AND mach_kernelrpc.c are POOL
# files).
#
# Park 1023 (install the mach trap-24 row, `mach_port_construct`) and append its block to
# records/revert-set.txt.
#
# 1023 = 1022's switch set VERBATIM (STAGE90_XNU_GENERAL_REGS=1, STAGE90_XNU_GLOBAL_FIX=1,
# STAGE90_XNU_UNDEF_RAISE=1, STAGE90_XNU_UNDEF_DIRECT=1, STAGE90_XNU_IOS7_TRAPS=1 and the whole
# inherited set stay) with **NO NEW SWITCH**: the row-24 gate widens to read the switch that is ALREADY
# 1 in the record.  Design: docs/1023-port-construct-trap24-design.md.
#
# WHY.  The 1022 press read T15-FIRES (records/1022-press-read.txt): the trap-15 row IS installed,
# `mach_vm_map` IS called 7 times and returns `result=0` every time, and the udf wall 1021d stopped on
# is GONE.  The frontier moved to libxpc's port-allocation wrapper, whose `udf #254` sits at file VA
# `0x3824e022`.  Root cause: libxpc's `__xpc_mach_port_allocate`-family (`0x3824dfc8`) does
# `blx 0x38260b60`, a dyld lazy-bind veneer whose slot at VA `0x39f45174` (the THIRD cache mapping,
# fileoff 0xdf46000) holds `0x381c66ed` -- the **trap-24 shim** at `0x381c66ec`.  Its `blx 0x381c59dc`
# targets the ARM fast-trap stub `mov r12,sp / push {r4,r5} / ldr r4,[r12] / mvn r12,#0x17 / svc #0x80`;
# the encoding is index = imm + 1 (anchor: trap 15's stub is `mvn r12,#14`), so the trap is **24**, not
# 16, and trap 24 is `mach_port_construct`.  The shim takes its slow leg ONLY when the return equals
# `0x10000003` (`MACH_SEND_INVALID_DEST`); the wrapper's `cbz r0 / cmp r0,#3 / cmpne r0,#6` chain falls
# through to `udf #254` for every other `kern_return_t`.  On CANCRO `mach_trap_table[24]` is
# `kern_invalid` (its gate at `syscall_sw.c:139` is `#if IOS7_COMPAT_PLATFORM`, 0 here) so the wrapper
# receives `KERN_INVALID_ARGUMENT` = 4.  The fix installs the row.
#
# **THE ARM (ONE widened gate + ONE exchanged selector, ONE switch already on).**  (1)
# osfmk/kern/syscall_sw.c:139 row-24 `#if IOS7_COMPAT_PLATFORM` -> `|| (defined(STAGE90_XNU_IOS7_TRAPS)
# && STAGE90_XNU_IOS7_TRAPS)`; the externs the row needs and the inner lab-adapter gate at
# mach_kernelrpc.c:59 were ALREADY widened by 1022.  (2) Inside `ios7lab_mach_port_construct_trap`
# (mach_kernelrpc.c:74) the runtime selector reads `ios7_traps_enabled()` instead of
# `ios7_compat_vmabi_enabled()`.  Edit 2 is MANDATORY and is the rung's own defect cell (§7
# T24-SENTINEL): CANCRO's `ios7_compat_vmabi_enabled()` is a compile-time 0, so an installed row would
# return the shim's OWN sentinel and branch it AWAY from the slow leg -- strictly worse than absent.
# (3) the body gains a switch-gated `IOS7LAB trap24 frame` dump, so the press DECIDES the user-side wire
# empirically (docs/1023 section 4 names the two inputs it must not assume).
#
# **THIS IS A POOL-REBUILD RUNG (the 1015b shape), NOT ENTRY-ONLY.**  syscall_sw.c and mach_kernelrpc.c
# are POOL objects; a stale pool object keeps row 15 = kern_invalid and the udf recurs.  `build_entry.sh`
# only LINKS the pool objects, so a green entry build says NOTHING about this half.  This is why the
# park's OWN change is asserted BY VALUE on the LINKED image (the row-15 fingerprint) and on the FROZEN
# PRE-RUNG arm (the negative control) before every inherited clause.
#
# **THE CARRIERS.**  The record carries `STAGE90_XNU_IOS7_TRAPS=1` (the readability carrier AND the
# switch the widened gates read; the plumbing record:79-100 shows it reaches the pool DEFINES at
# build_xnu_arm_kernel.sh:902, the asm layer, and the entry EXPORTS).  The declaration carrier
# out/stage90/xnu_arm_entry-arm-extra.txt is written by THIS build's tail as
# `STAGE90_XNU_GLOBAL_FIX=1 role=1023-trap24` (1015b's GLOBAL_FIX is a POOL switch no writer in
# src/entry/ echoes).  A grep of that file is a PROCESS check; the by-value clauses below are what
# actually certify the arm ([[mi4-self-written-record-is-not-a-constraint]]).
#
# THE 1023 VERDICT CELLS (docs/1023-port-construct-trap24-design.md section 7; the press reads them off
# the capture):
#   G-CTL          - neither `xnu_live_undef_seq` nor any `IOS7LAB trap24 frame` line      => WITHHOLD
#   T24-FIRES      - `IOS7LAB trap24 frame …` present AND pid 1 proceeds past libxpc's port allocation
#                    (no udf at 0x3824e022's slide-corrected pc)                          => WALL MOVED
#   T24-SENTINEL   - the run fails as before with NO frame line, or the frame line appears and the udf
#                    recurs at the same pc                                             => ROW INSTALLED,
#                    SELECTOR STILL 0 (edit 2 did not land)
#   T24-BADFRAME   - the frame line appears and the run faults INSIDE the handler (a bad `regs[1]`
#                    pointer: the `copyin` of 24 option bytes refuses, or a `regs[4]` write faults)
#                                                                                       => WIRE MISMATCH
#   T24-REFUSED    - the frame line appears, the body returns non-zero, and the boot proceeds
#                                                                                       => ROW USED,
#                    THE WIRE IS REFUSED (read the printed frame words)
#   T24-PANICS     - a `swi_mach_error`-shaped panic                                    => STALE POOL
#   T24-DEFERRAL-BROKEN - the press's own checker read of rows 25/41/42 fails            => COMPOSITION
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible.  fastboot boot only, NEVER flash.
# ====================================================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

LIVE=out/stage90
REC=records/revert-set.txt
MEMBERS=(xnu_arm_entry.bin xnu_arm_entry.elf xnu_arm_entry.h xnu_arm_entry-config.txt \
         xnu_arm_entry-arm-extra.txt xnu_arm_entry-sources.txt stage90.bin stage90.elf stage90.img \
         stage90-qcdt.img stage90_fixture.macho stage90-build-config.txt SHA256SUMS.txt)

# The arm name IS the entry bin's sha8 (the project's rule).  Derive it, and refuse to re-park the
# RECENT PRESSED arms under a new name (build 1023 first).  ee2dd4fb is the arm the 1021d press SPENT
# and is the arm 1022 must MOVE AWAY from; the rest are earlier rungs.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-b47131c8 ]] && { echo "REFUSING: $LIVE holds 1022's PRESSED arm (b47131c8) - the arm whose trap-15 row 1022 installed and spent. A park of the SAME arm would freeze a set that does not carry this rung's row; build 1023 first"; exit 1; }
[[ $ARM == armed-d13-ee2dd4fb ]] && { echo "REFUSING: $LIVE holds 1021d's PRESSED arm (ee2dd4fb); build 1023 first"; exit 1; }
[[ $ARM == armed-d13-b5093f00 ]] && { echo "REFUSING: $LIVE holds 1020b's PRESSED arm (b5093f00); build 1023 first"; exit 1; }
[[ $ARM == armed-d13-47f664c4 ]] && { echo "REFUSING: $LIVE holds 1020's DROPPED-GLOBAL-FIX arm (47f664c4); build 1023 first"; exit 1; }
[[ $ARM == armed-d13-c2168632 ]] && { echo "REFUSING: $LIVE holds 1018's arm (c2168632); build 1023 first"; exit 1; }
[[ $ARM == armed-d13-16fe8885 ]] && { echo "REFUSING: $LIVE holds 1015b's arm (16fe8885); build 1023 first"; exit 1; }
[[ $ARM == armed-d13-e51d14fa ]] && { echo "REFUSING: $LIVE holds 1015's arm (e51d14fa); build 1023 first"; exit 1; }
[[ $ARM == armed-d13-29ec9e48 ]] && { echo "REFUSING: $LIVE holds 1014's arm (29ec9e48); build 1023 first"; exit 1; }
[[ $ARM == armed-d13-9bf25f8f ]] && { echo "REFUSING: $LIVE holds 1013b's arm (9bf25f8f); build 1023 first"; exit 1; }
[[ $ARM == armed-d13-ffdc8ba1 ]] && { echo "REFUSING: $LIVE holds 1012's arm (ffdc8ba1); build 1023 first"; exit 1; }

# ====================================================================================================
# 1023'S OWN CHANGE, BY VALUE, FIRST - BEFORE EVERY INHERITED REGRESSION CLAUSE.
# **THE ORDER IS THE POINT.**  A park that lost 1023's row must fail HERE, on ITS OWN claim (the row-24
# fingerprint on the LINKED image), rather than further down where an inherited clause would stop it for
# a reason that names the wrong thing.
# ====================================================================================================
echo "== 1023's OWN change, by value, first =="

# (a) THE RECORD'S OWN KEY.  `STAGE90_XNU_IOS7_TRAPS=1` is BOTH the readability carrier AND the switch
#     the widened gates read (syscall_sw.c / mach_kernelrpc.c).  Absent it, the row does not compile and
#     the arm is not 1023's.
grep -q '^STAGE90_XNU_IOS7_TRAPS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_IOS7_TRAPS=1 - 1023's row-24 gate widens to read THIS switch and it is already 1 in this arm's set. Its absence means the pool was built without -DSTAGE90_XNU_IOS7_TRAPS=1, so row 24 stays kern_invalid and libxpc's port-allocation udf recurs. Build with scripts/build_1023.sh"; exit 1; }

# (b) THE BY-VALUE CLAUSE - THE PARK'S OWN CLAIM, ON THE LINKED IMAGE THE PRESS WILL BOOT.
#     `tools/check_1023_trap24.py <elf> --expect on` reads `mach_trap_table` BY VALUE and asserts row 24
#     is {arg_count=5; fn=&ios7lab_mach_port_construct_trap; name="ios7lab_mach_port_construct_trap"},
#     that BOTH symbols are PRESENT (an absent symbol is a refusal, never a pass), AND that rows 25/41/42
#     STILL read kern_invalid -- this rung's deferral.  `build_entry.sh` only LINKS the pool objects, so
#     this must be read on the LINKED image (a stale pool object hides row 24).
#
#     **THE CHECKER IS REQUIRED, AND ITS ABSENCE IS A REFUSAL, NOT A SKIP.**  A park that skipped this
#     would freeze an arm whose own claim nothing checks ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
[[ -f tools/check_1023_trap24.py ]] || {
  echo "REFUSING: tools/check_1023_trap24.py is ABSENT. It is this park's by-value claim on the LINKED image's row 24, and there is no sound fallback (a name grep cannot distinguish 'row installed' from 'handler present but row still kern_invalid'). An absent checker means the by-value half of this rung cannot be certified"; exit 1; }

# 1023-0. THE CHECKER'S OWN SELF-TEST.  A checker that cannot fail is not a check
#         ([[mi4-a-claim-in-comment-is-not-a-check]]).  Refuse the park unless it PASSes its selftest.
python3 -I tools/check_1023_trap24.py --selftest >/dev/null 2>&1 || {
  echo "REFUSING: tools/check_1023_trap24.py --selftest did NOT PASS - the checker cannot certify anything (an inert checker would bless any image). Fix the checker before parking the arm it is supposed to prove"; exit 1; }
echo "   BY VALUE(0): tools/check_1023_trap24.py --selftest PASSes (the checker is live)"

# 1023-1. THE RUNG'S OWN CLAIM, ON THE LINKED IMAGE.  Read on `$LIVE/xnu_arm_entry.elf` (the image the
#         press boots), because the change lives in POOL objects the entry build only LINKS.
[[ -s $LIVE/xnu_arm_entry.elf ]] || { echo "REFUSING: $LIVE/xnu_arm_entry.elf is absent - there is nothing to read row 24 out of"; exit 1; }
python3 -I tools/check_1023_trap24.py "$LIVE/xnu_arm_entry.elf" --expect on >/dev/null 2>&1 || {
  echo "REFUSING: the LINKED image's mach_trap_table[24] is NOT the ios7lab_mach_port_construct_trap row (arg_count=5, and rows 25/41/42 still kern_invalid) - either the widened gate did not reach osfmk_kern_syscall_sw.o, or a STALE pool object was linked (build_entry.sh only LINKS it). Re-run scripts/build_1023.sh"; exit 1; }
echo "   BY VALUE(1): the LINKED $LIVE/xnu_arm_entry.elf's mach_trap_table[24] IS the ios7lab_mach_port_construct_trap row (arg_count=5, handler + name present, rows 25/41/42 still kern_invalid)"

# 1023-2. THE DISCRIMINATING CONTROL.  A clause that cannot FAIL is not a check
#         ([[mi4-a-claim-in-comment-is-not-a-check]]); so the SAME checker is run in the `--expect off`
#         direction on the FROZEN PRE-RUNG arm (1022, b47131c8), whose row 24 IS `kern_invalid` (1022
#         opened row 15 ONLY).  The pair (pre-rung -> off PASS, this arm -> on PASS) is what proves the
#         checker DISCRIMINATES rather than always saying the same thing.
_ctrl=$LIVE/frozen/armed-d13-b47131c8/xnu_arm_entry.elf
[[ -s $_ctrl ]] || { echo "REFUSING: the frozen 1022 elf ($_ctrl) is absent - the NEGATIVE CONTROL for this rung's by-value clause cannot be taken, so the clause above cannot be shown to DISCRIMINATE. 1022's park must exist before 1023's"; exit 1; }
python3 -I tools/check_1023_trap24.py "$_ctrl" --expect off >/dev/null 2>&1 || {
  echo "REFUSING: the frozen 1022 elf's row 24 does NOT read kern_invalid in the --expect off direction - the checker is either broken or pointed at the wrong row, so the 'on' PASS above certifies nothing. This is the control; a control that agrees with the test proves only that the checker says one thing"; exit 1; }
echo "   BY VALUE(2): NEGATIVE CONTROL - the frozen 1022 elf (b47131c8) reads row 24 == kern_invalid (--expect off PASSes), so the 'on' above is a reading and not a constant"

# (c) THE ARM-EXTRA DECLARATION CARRIER.  1023's build WRITES this file at its tail
#     (`STAGE90_XNU_GLOBAL_FIX=1 role=1023-trap24`), so THIS park requires it verbatim and never appends.
#     1015b's GLOBAL_FIX is a POOL switch no writer in src/entry/ echoes, so the record cannot carry it;
#     this drop-in is its ONE declaration, and the role tag names 1023 so a stale 1022 line is
#     distinguishable by content.  **THE GREP IS THE PROCESS CHECK, NOT THE PROOF** - the pmap_flush_tlbs
#     fingerprint below is what certifies the fix is in the object ([[mi4-self-written-record-is-not-a-constraint]]).
_b_extra="$LIVE/xnu_arm_entry-arm-extra.txt"
[[ -f $_b_extra ]] || { echo "REFUSING: $_b_extra is absent - 1023's build writes the declaration carrier at its tail; an absent one means this arm's build did not finish"; exit 1; }
[[ $(wc -l < "$_b_extra") == 1 && $(cat "$_b_extra") == 'STAGE90_XNU_GLOBAL_FIX=1 role=1023-trap24' ]] || {
  echo "REFUSING: $_b_extra is not EXACTLY the one 1023 line (it holds '$(cat "$_b_extra")' across $(wc -l < "$_b_extra") line(s)). 1015b's GLOBAL_FIX is a POOL switch no writer in src/entry/ echoes, so this drop-in is its ONE declaration, and 1023's build writes it as exactly this line. A surviving 1022 line (role=1022-trap15) would be a declaration OF A DIFFERENT BUILD ([[mi4-self-written-record-is-not-a-constraint]]). Re-run scripts/build_1023.sh"; exit 1; }
echo "   DECLARATION: $_b_extra carries exactly '$(cat "$_b_extra")' (written by THIS build; the proof is the fingerprint below)"

# ---- 1022'S INHERITED GLOBAL-FIX CLAUSE: IT IS IN THE FROZEN POOL OBJECT, BY VALUE ----
# 1020's pool rebuild DROPPED 1015b (records/1020-build-defect.txt): the storm returned.  1023 rebuilds
# the pool AGAIN (syscall_sw.c + mach_kernelrpc.c), so the same defect is available.  The fix's
# fingerprint is the count of `kernel_pmap` MOVW materializations inside `pmap_flush_tlbs` (OFF/Apple = 2,
# ON = 3).  A grep of the arm-extra drop-in would be a check on a record no codegen writes; this reads
# the object the linker saw.
_gf_obj=out/xnu_kernel_obj_d13/osfmk_arm_pmap.o
[[ -f "$_gf_obj" ]] || { echo "REFUSING: $_gf_obj is absent - 1015b's GLOBAL-FIX lives in pmap_flush_tlbs, and a park that cannot read it cannot certify the storm stays gone"; exit 1; }
_gf_kp=$(arm-none-eabi-objdump -r "$_gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'MOVW_ABS_NC.*kernel_pmap' || true)
_gf_full=$(arm-none-eabi-objdump -r "$_gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'R_ARM_CALL.*arm_tlb_flushID$' || true)
_gf_asid=$(arm-none-eabi-objdump -r "$_gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'R_ARM_CALL.*arm_tlb_flushID_ASID$' || true)
[[ "$_gf_kp" == "3" && "$_gf_full" == "1" && "$_gf_asid" == "1" ]] || {
  echo "REFUSING: pmap_flush_tlbs materializes kernel_pmap x$_gf_kp (want 3), arm_tlb_flushID x$_gf_full / arm_tlb_flushID_ASID x$_gf_asid (want 1/1) - GLOBAL-FIX is NOT in the frozen pool object, so the retry storm 1015b killed is BACK and the run would never reach pid 1's os_alloc udf this rung exists to retire. -DSTAGE90_XNU_GLOBAL_FIX=1 has NO record carrier (records/1020-build-defect.txt). Re-run scripts/build_1023.sh."; exit 1; }
echo "   POOL(GLOBAL-FIX): by value - pmap_flush_tlbs carries the 3rd kernel_pmap materialization ($_gf_kp sites) and the declared 1/1 flush arms, so 1015b's GLOBAL-FIX IS in the frozen pool object"

# ---- 1021d'S INHERITED NEON-FREE CLAUSE: -msoft-float must still hold (this build re-OBJECTs
# mach_kernelrpc.c and syscall_sw.c, so the flag must still reach the pool).  The census is whole-pool
# with a source-count cross-check (a stale/partial dir is a refusal, not a vacuous pass, defect class m1015b).
[[ -f tools/check_d13_no_neon.py ]] || { echo "REFUSING: tools/check_d13_no_neon.py is ABSENT - it is the inherited 1021d claim's by-value check (the -msoft-float flag reached the pool). An absent one means the build tree is incomplete"; exit 1; }
python3 -I tools/check_d13_no_neon.py --selftest >/dev/null 2>&1 || { echo "REFUSING: tools/check_d13_no_neon.py --selftest did NOT PASS - the inherited NEON-free checker cannot certify anything"; exit 1; }
_pool=out/xnu_kernel_obj_d13
[[ -d $_pool ]] || { echo "REFUSING: $_pool is absent - the pool the linked image was built from is not on disk"; exit 1; }
_src_rec=out/xnu_kernel_src_d13.sha256
[[ -s $_src_rec ]] || { echo "REFUSING: $_src_rec is absent - the pool's own source-content record is the independent count this clause compares against. Its absence means the pool build did not complete"; exit 1; }
_want_objs=$(grep -c '^[0-9a-f]\{64\} ' "$_src_rec" || true)
_got_objs=$(ls "$_pool"/*.o 2>/dev/null | wc -l)
[[ $_want_objs -gt 0 && $_got_objs -eq $_want_objs ]] || {
  echo "REFUSING: $_pool holds $_got_objs object(s) but $_src_rec names $_want_objs source(s) - the census would count the WRONG set (a partial or stale directory passes any NEON-free test vacuously). Re-run scripts/build_1023.sh"; exit 1; }
python3 -I tools/check_d13_no_neon.py --pool "$_pool" --expect none --min-objects "$_want_objs" >/dev/null 2>&1 || {
  echo "REFUSING: tools/check_d13_no_neon.py --pool $_pool --expect none did NOT pass - the pool the linked image was built from still contains FP/NEON instructions. 1023 rebuilds mach_kernelrpc.c and syscall_sw.c, so the -msoft-float flag must still reach the pool or the kernel-mode NEON undef at 0x8047b0ac returns. Re-run scripts/build_1023.sh"; exit 1; }
python3 -I tools/check_d13_no_neon.py "$LIVE/xnu_arm_entry.elf" --symbol mach_msg_send --expect none >/dev/null 2>&1 || {
  echo "REFUSING: the LINKED image's mach_msg_send still contains FP/NEON instructions - 1021d's -msoft-float flag did not reach the object the linker saw, or a STALE pool object was linked. Re-run scripts/build_1023.sh"; exit 1; }
echo "   POOL(NEON-FREE): by value - $_got_objs objects NEON-free (== $_want_objs source(s)), and the LINKED mach_msg_send is NEON-free (1021d's -msoft-float holds)"

# ====================================================================================================
# THE INHERITED REGRESSION CLAUSES - 1022's / 1021d's / 1020b's / 1018's, kept VERBATIM IN MEANING
# (placed AFTER 1023's own change, so a park that lost 1023's row has already stopped above).
# ====================================================================================================
echo "== inherited regressions =="
# 1021d's general-regs switch stays on (the kernel pool must stay NEON-free).
grep -q '^STAGE90_XNU_GENERAL_REGS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_GENERAL_REGS=1 - 1021d's -msoft-float flag switch is part of this arm's set verbatim and must stay on (without it the kernel-mode NEON undef at 0x8047b0ac returns and this rung would measure a different frontier than libxpc's port-allocation udf it exists to retire)"; exit 1; }
# 1020's raise switch stays on: 1023 must not regress the fix that killed the udf loop.
grep -q '^STAGE90_XNU_UNDEF_RAISE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_UNDEF_RAISE=1 - 1020's raise fix is part of this arm's set verbatim and must stay on (without it pid 1's genuine udf loops forever again, 8183x measured)"; exit 1; }
# 1018's direct-channel switch stays on (its witness survives the cap) - the recorder that names the udf.
grep -q '^STAGE90_XNU_UNDEF_DIRECT=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_UNDEF_DIRECT=1 - 1018's direct channel is part of this arm's set verbatim and must stay on (without it the udf witness rides the CAPPED channel and can be silenced - the 1017/1018 instrument defect)"; exit 1; }
# 1020's by-value clause, re-run on the LINKED image: `sleh_undef`'s user arm must still reach the
# EXC-printf raise on the VFP-already-enabled path.
[[ -f tools/check_d13_undef_raise.py ]] || { echo "REFUSING: tools/check_d13_undef_raise.py is ABSENT - it is the inherited 1020 claim's by-value check on the linked image (the build writes it, so an absent one means the build tree is incomplete)"; exit 1; }
python3 -I tools/check_d13_undef_raise.py "$LIVE/xnu_arm_entry.elf" --expect on >/dev/null 2>&1 || {
  echo "REFUSING: tools/check_d13_undef_raise.py --expect on did NOT pass on the linked image - 1020's moved \`return\` is NOT in this image's sleh_undef, so the udf loop is back and this rung would measure a different frontier. Re-run scripts/build_1023.sh"; exit 1; }
echo "   BY VALUE(4): tools/check_d13_undef_raise.py --expect on PASSes (1020's raise is intact in the linked image)"
# 1018's by-value census: `__wrap_sleh_undef` in the LINKED image must branch to the DIRECT writer
# (entry_write_kv), NOT the capped one ([[mi4-linked-code-order-is-not-source-order]]).
_ua=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="__wrap_sleh_undef"{print "0x" $1; n++} END{exit(n==1?0:1)}') || {
  echo "REFUSING: __wrap_sleh_undef is not in the parked image (or is defined more than once) - the undef witness is gone"; exit 1; }
_ub=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ua#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv{if(!d){print "0x" $1; d=1}}') || true
[[ -n $_ub ]] || { echo "REFUSING: no symbol follows __wrap_sleh_undef ($_ua) - its range cannot be read"; exit 1; }
_ud=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ua" --stop-address="$_ub")
_kv=$(grep -c '<entry_write_kv>' <<<"$_ud" || true)
_lw=$(grep -c '<entry_live_write>' <<<"$_ud" || true)
[[ $_kv -ge 6 && $_lw -eq 0 ]] || {
  echo "REFUSING: the parked image's __wrap_sleh_undef ($_ua..$_ub) branches to entry_write_kv $_kv time(s) and entry_live_write $_lw time(s) - with the switch on it must be >=6 direct and 0 counted. A park that kept the counted branch freezes an arm whose witness the cap can still silence."; exit 1; }
echo "   BY VALUE(5): __wrap_sleh_undef ($_ua..$_ub) branches to entry_write_kv x$_kv, entry_live_write x$_lw (1018's witness is on the DIRECT channel)"

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

# THE FROZEN RE-READ.  The checks above ran on the LIVE artifacts (so a bad arm never creates a park
# directory); this re-runs the rung's own claim on the PARKED bytes, so what is frozen is what was
# certified - a later build cannot silently change what was pressed.
python3 -I tools/check_1023_trap24.py "$PARK/xnu_arm_entry.elf" --expect on >/dev/null 2>&1 || {
  echo "REFUSING: the FROZEN $PARK/xnu_arm_entry.elf does not carry the claim the live one did - the copy is not the certified bytes. This park's directory is incomplete; remove $PARK and re-run"; exit 1; }
echo "   BY VALUE(6): the FROZEN $PARK/xnu_arm_entry.elf's row 24 IS the ios7lab_mach_port_construct_trap row (the parked bytes are the certified bytes)"

{
  printf '# %s  -  1023: INSTALL THE mach trap-24 ROW (mach_port_construct) so libxpc-s port allocation stops faulting.\n' "$ARM"
  printf '# (The 1022 frontier was libxpc-s port-allocation wrapper: `udf #254` at file VA 0x3824e022,\n'
  printf '#  records/1022-press-read.txt PART 5. Its `blx 0x38260b60` is a dyld lazy-bind veneer whose\n'
  printf '#  slot at VA 0x39f45174 (THIRD cache mapping) holds 0x381c66ed = the trap-24 shim 0x381c66ec,\n'
  printf '#  whose `blx 0x381c59dc` targets the ARM fast-trap stub `mvn r12,#0x17; svc #0x80`. The\n'
  printf '#  encoding is index = imm+1 (trap 15-s stub is `mvn r12,#14`), so the trap is 24, NOT 16, and\n'
  printf '#  trap 24 is mach_port_construct. docs/1023-port-construct-trap24-design.md.)\n'
  printf '# Switch set is 1022-s VERBATIM (STAGE90_XNU_GENERAL_REGS=1, STAGE90_XNU_UNDEF_RAISE=1,\n'
  printf '# STAGE90_XNU_UNDEF_DIRECT=1, STAGE90_XNU_GLOBAL_FIX=1 and the whole inherited set stay) with\n'
  printf '# **NO NEW SWITCH**: the row-24 gate widens to read STAGE90_XNU_IOS7_TRAPS, ALREADY 1 here.\n'
  printf '# THE ARM: ONE widened gate + ONE exchanged selector - (1) syscall_sw.c:139 row-24 -> `|| (defined(\n'
  printf '# STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS)` (the externs and the inner lab-adapter gate\n'
  printf '# at mach_kernelrpc.c:59 were ALREADY widened by 1022); (2) inside ios7lab_mach_port_construct_trap\n'
  printf '# (mach_kernelrpc.c:74) the selector reads `ios7_traps_enabled()` instead of the compile-time-0\n'
  printf '# `ios7_compat_vmabi_enabled()`. Edit 2 is MANDATORY: without it an installed row returns\n'
  printf '# MACH_SEND_INVALID_DEST = 0x10000003 - the shim-s OWN sentinel - and branches it AWAY from the\n'
  printf '# slow leg it needs, strictly worse than absent. (3) the body gains a switch-gated\n'
  printf '# `IOS7LAB trap24 frame` dump so the press DECIDES the user-side wire empirically.\n'
  printf '# THE POOL MOVES: syscall_sw.c and mach_kernelrpc.c are POOL objects, so the pool-s .text (and the\n'
  printf '# entry .bin whose sha8 IS the arm) MOVES (b47131c8 -> %s). build_entry.sh only LINKS the pool\n'
  printf '# objects, so a green entry build says nothing about this half.\n#\n' "${ARM#armed-d13-}"
  printf '# THE BY-VALUE PROOF OF 1023-S CHANGE, asserted FIRST and re-run on the FROZEN bytes.\n'
  printf '# tools/check_1023_trap24.py reads mach_trap_table BY VALUE:\n'
  printf '#   (0) --selftest PASSes (the checker can fail);\n'
  printf '#   (1) --expect on PASSes on the LINKED out/stage90/xnu_arm_entry.elf: row 24 is\n'
  printf '#       {arg_count=5; fn=&ios7lab_mach_port_construct_trap; name="ios7lab_mach_port_construct_trap"},\n'
  printf '#       both symbols PRESENT (an absent symbol is a refusal, never a pass), AND rows 25/41/42\n'
  printf '#       STILL read kern_invalid (this rung-s deferral);\n'
  printf '#   (2) NEGATIVE CONTROL - the SAME checker --expect off PASSes on the frozen PRE-RUNG arm\n'
  printf '#       1022 (armed-d13-b47131c8), whose row 24 IS kern_invalid (1022 opened row 15 ONLY), so\n'
  printf '#       the on direction is a reading and not a constant\n'
  printf '#       ([[mi4-a-claim-in-a-comment-is-not-a-check]]).\n#\n'
  printf '# THE CARRIERS ([[mi4-one-value-two-definitions]]). The record carries STAGE90_XNU_IOS7_TRAPS=1\n'
  printf '# (the switch the widened gate reads AND the readability carrier). The declaration carrier\n'
  printf '# out/stage90/xnu_arm_entry-arm-extra.txt is written by THIS build-s tail as\n'
  printf '# STAGE90_XNU_GLOBAL_FIX=1 role=1023-trap24 (1015b-s GLOBAL_FIX is a POOL switch no writer in\n'
  printf '# src/entry/ echoes, so the record cannot carry it); the grep of it is the process check and the\n'
  printf '# pmap_flush_tlbs fingerprint is the proof.\n#\n'
  printf '# THE INHERITED REGRESSIONS held (asserted by the park): STAGE90_XNU_GENERAL_REGS=1 (1021d-s\n'
  printf '# -msoft-float), STAGE90_XNU_UNDEF_RAISE=1 and STAGE90_XNU_UNDEF_DIRECT=1 in the record, the\n'
  printf '# pmap_flush_tlbs kernel_pmap==3 fingerprint (1015b-s kernel fix), the whole-pool NEON-free census\n'
  printf '# + the linked mach_msg_send NEON-free read (1021d), check_d13_undef_raise.py --expect on on the\n'
  printf '# linked image (1020-s raise intact), and by value __wrap_sleh_undef branching to entry_write_kv\n'
  printf '# (>=6) and NOT entry_live_write (0). Also: 1022-s row 15 is NOT displaced - build_1023.sh 5a\n'
  printf '# asserts syscall_sw.o still relocates ios7lab_mach_vm_map_trap at 15*12+4.\n#\n'
  printf '# THE 1023 VERDICT CELLS (docs/1023-port-construct-trap24-design.md sec.7):\n'
  printf '#   G-CTL          - neither xnu_live_undef_seq nor any IOS7LAB trap24 frame line => WITHHOLD.\n'
  printf '#   T24-FIRES      - IOS7LAB trap24 frame present AND pid 1 proceeds past libxpc-s port\n'
  printf '#                    allocation (no udf at 0x3824e022-s slide-corrected pc) => WALL MOVED.\n'
  printf '#   T24-REFUSED    - the frame line appears, the body returns non-zero, and the boot proceeds\n'
  printf '#                    => ROW USED; THE WIRE IS REFUSED (read the printed frame words).\n'
  printf '#   T24-SENTINEL   - the run fails as before with NO frame line, or the frame line appears and\n'
  printf '#                    the udf recurs at the same pc => ROW INSTALLED, SELECTOR STILL 0.\n'
  printf '#   T24-BADFRAME   - the frame line appears and the run faults INSIDE the handler (a bad regs[1]\n'
  printf '#                    pointer: the copyin of 24 option bytes refuses) => WIRE MISMATCH.\n'
  printf '#   T24-PANICS     - a swi_mach_error-shaped panic => STALE POOL, not a behaviour reading.\n'
  printf '#   T24-DEFERRAL-BROKEN - the checker read of rows 25/41/42 fails => COMPOSITION.\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1023-trap24;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3