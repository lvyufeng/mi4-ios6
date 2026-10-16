#!/usr/bin/env bash
# ====================================================================================================
# park_1022.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1021d.sh, WITH 1015b's POOL-MOVED
# DISCIPLINE (a pool rebuild rung: syscall_sw.c AND mach_kernelrpc.c are POOL files).
#
# Park 1022 (install the mach trap-15 row) and append its block to records/revert-set.txt.
#
# 1022 = 1021d's switch set VERBATIM (STAGE90_XNU_GENERAL_REGS=1, STAGE90_XNU_GLOBAL_FIX=1,
# STAGE90_XNU_UNDEF_RAISE=1, STAGE90_XNU_UNDEF_DIRECT=1, STAGE90_XNU_IOS7_TRAPS=1 and the whole
# inherited set stay) with **NO NEW SWITCH**: the row-15 and inner gates widen to read the switch that
# is ALREADY 1 in the record (records/1022-trap15-plumbing.txt §2, §8).  Design:
# docs/1022-mach-trap-15-design.md.  Adjudication of the wire dispute: records/1022-trap15-adjudication.txt.
#
# WHY.  The 1021d press read A-RETIRED: `-msoft-float` retired the kernel-mode NEON undef at
# `0x8047b0ac`, and the run now reaches and stays at pid 1's USER-MODE `udf` at `__os_alloc_slow+0x108`
# (`0x39e38a54 - slide 0x1bfe000 = 0x3823aa54`, records/1021d-press-read.txt).  That udf is the
# `mach_vm_map`-failure join.  Root cause (records/1021d-mmap-failure.txt, docs/1022-mach-trap-15-design.md):
# `mach_trap_table[15]` is `kern_invalid` on CANCRO because `IOS7_COMPAT_PLATFORM` is
# `BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO` (osfmk/arm/ios7_compat.h:5) and CANCRO/MSM8974 is
# neither.  The client shim (`0x381c6618`) GUARD PASSES and issues `svc #0x80` with r12 = -15
# (trap 15 = mach_vm_map, args in r0,r1,r2,r4,r8), and the kernel dispatches row 15 = `kern_invalid`
# => the map never happens => the udf.  The fix installs the row.
#
# **THE ARM (THREE widened/exchanged gates, ONE switch already on).**  (1) osfmk/kern/syscall_sw.c:126
# row-15 `#if IOS7_COMPAT_PLATFORM` and the extern block at :102 -> `|| (defined(STAGE90_XNU_IOS7_TRAPS)
# && STAGE90_XNU_IOS7_TRAPS)`; (2) osfmk/ipc/mach_kernelrpc.c:59 inner gate (the `ios7lab_mach_vm_map_trap`
# definition at :208 lives in it) -> same clause; (3) mach_kernelrpc.c:217 the runtime self-gate
# `if (!ios7_compat_vmabi_enabled())` -> `if (!ios7_traps_enabled())`.  Gate 3 is MANDATORY and is the
# rung's own defect cell (§7 T15-SENTINEL): CANCRO's `ios7_compat_vmabi_enabled()` is 0, and the
# client shim's `cmp r4,r0` against `MACH_SEND_INVALID_DEST = 0x10000003` treats the old gate's return as
# its own sentinel and branches to the MIG slow leg (`0x381c66ba`), skipping the map
# (records/1021d-mmap-failure.txt §B).
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
# `STAGE90_XNU_GLOBAL_FIX=1 role=1022-trap15` (1015b's GLOBAL_FIX is a POOL switch no writer in
# src/entry/ echoes).  A grep of that file is a PROCESS check; the by-value clauses below are what
# actually certify the arm ([[mi4-self-written-record-is-not-a-constraint]]).
#
# THE 1022 VERDICT CELLS (design §7; the press reads them off the capture):
#   G-CTL         - neither `xnu_live_undef_seq` nor any `IOS7LAB vm_map64` line        => WITHHOLD
#   T15-FIRES     - `IOS7LAB vm_map64 … result=0 address=<nonzero>` AND the boot proceeds past
#                   `__os_alloc_slow` (no udf at 0x3823aa54's slide-corrected pc)      => WALL MOVED
#   T15-REFUSED   - `IOS7LAB vm_map64 … result=4` at least once, no panic              => ROW USED,
#                   THE WIRE IS REFUSED (read the printed frame words)
#   T15-SENTINEL  - the run fails earlier/differently, `-15` never reaches the row       => REGRESSION -
#                   ROW INSTALLED, SELECTOR STILL 0 (the :217 gate did not land)
#   T15-PANICS    - a `swi_mach_error`-shaped panic                                     => STALE POOL
#   T15-UNEXERCISED - the new frame dump reads the SLOW shape (frame0 == 7)             => GUARD FAILED
#   T15-OTHER-CALL  - the frame dump reads a FAST shape (a userspace pointer)           => residue (ii)
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
# RECENT PRESSED arms under a new name (build 1022 first).  ee2dd4fb is the arm the 1021d press SPENT
# and is the arm 1022 must MOVE AWAY from; the rest are earlier rungs.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-ee2dd4fb ]] && { echo "REFUSING: $LIVE holds 1021d's PRESSED arm (ee2dd4fb) - the arm whose user-mode os_alloc udf (trap-15 wall) 1022 exists to retire. A park of the SAME arm would freeze a set that does not carry this rung's row; build 1022 first"; exit 1; }
[[ $ARM == armed-d13-b5093f00 ]] && { echo "REFUSING: $LIVE holds 1020b's PRESSED arm (b5093f00); build 1022 first"; exit 1; }
[[ $ARM == armed-d13-47f664c4 ]] && { echo "REFUSING: $LIVE holds 1020's DROPPED-GLOBAL-FIX arm (47f664c4); build 1022 first"; exit 1; }
[[ $ARM == armed-d13-c2168632 ]] && { echo "REFUSING: $LIVE holds 1018's arm (c2168632); build 1022 first"; exit 1; }
[[ $ARM == armed-d13-16fe8885 ]] && { echo "REFUSING: $LIVE holds 1015b's arm (16fe8885); build 1022 first"; exit 1; }
[[ $ARM == armed-d13-e51d14fa ]] && { echo "REFUSING: $LIVE holds 1015's arm (e51d14fa); build 1022 first"; exit 1; }
[[ $ARM == armed-d13-29ec9e48 ]] && { echo "REFUSING: $LIVE holds 1014's arm (29ec9e48); build 1022 first"; exit 1; }
[[ $ARM == armed-d13-9bf25f8f ]] && { echo "REFUSING: $LIVE holds 1013b's arm (9bf25f8f); build 1022 first"; exit 1; }
[[ $ARM == armed-d13-ffdc8ba1 ]] && { echo "REFUSING: $LIVE holds 1012's arm (ffdc8ba1); build 1022 first"; exit 1; }

# ====================================================================================================
# 1022'S OWN CHANGE, BY VALUE, FIRST - BEFORE EVERY INHERITED REGRESSION CLAUSE.
# **THE ORDER IS THE POINT.**  A park that lost 1022's row must fail HERE, on ITS OWN claim (the row-15
# fingerprint on the LINKED image), rather than further down where an inherited clause would stop it for
# a reason that names the wrong thing.
# ====================================================================================================
echo "== 1022's OWN change, by value, first =="

# (a) THE RECORD'S OWN KEY.  `STAGE90_XNU_IOS7_TRAPS=1` is BOTH the readability carrier AND the switch
#     the widened gates read (syscall_sw.c / mach_kernelrpc.c).  Absent it, the row does not compile and
#     the arm is not 1022's (records/1022-trap15-plumbing.txt §2).
grep -q '^STAGE90_XNU_IOS7_TRAPS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_IOS7_TRAPS=1 - 1022's row gates widen to read THIS switch and it is already 1 in this arm's set. Its absence means the pool was built without -DSTAGE90_XNU_IOS7_TRAPS=1, so row 15 stays kern_invalid and the os_alloc udf recurs. Build with scripts/build_1022.sh"; exit 1; }

# (b) THE BY-VALUE CLAUSE - THE PARK'S OWN CLAIM, ON THE LINKED IMAGE THE PRESS WILL BOOT.
#     `tools/check_1022_trap15.py <elf> --expect on` reads `mach_trap_table` BY VALUE and asserts row 15
#     is {arg_count=8; fn=&ios7lab_mach_vm_map_trap; name="ios7lab_mach_vm_map_trap"}, and that BOTH
#     symbols are PRESENT (an absent symbol is a refusal, never a pass).  `build_entry.sh` only LINKS the
#     pool objects, so this must be read on the LINKED image (a stale pool object hides row 15).
#
#     **THE CHECKER IS REQUIRED, AND ITS ABSENCE IS A REFUSAL, NOT A SKIP.**  A park that skipped this
#     would freeze an arm whose own claim nothing checks ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
[[ -f tools/check_1022_trap15.py ]] || {
  echo "REFUSING: tools/check_1022_trap15.py is ABSENT. It is this park's by-value claim on the LINKED image's row 15, and there is no sound fallback (a name grep cannot distinguish 'row installed' from 'handler present but row still kern_invalid'). An absent checker means the by-value half of this rung cannot be certified"; exit 1; }

# 1022-0. THE CHECKER'S OWN SELF-TEST.  A checker that cannot fail is not a check
#         ([[mi4-a-claim-in-a-comment-is-not-a-check]]).  Refuse the park unless it PASSes its selftest.
python3 -I tools/check_1022_trap15.py --selftest >/dev/null 2>&1 || {
  echo "REFUSING: tools/check_1022_trap15.py --selftest did NOT PASS - the checker cannot certify anything (an inert checker would bless any image). Fix the checker before parking the arm it is supposed to prove"; exit 1; }
echo "   BY VALUE(0): tools/check_1022_trap15.py --selftest PASSes (the checker is live)"

# 1022-1. THE RUNG'S OWN CLAIM, ON THE LINKED IMAGE.  Read on `$LIVE/xnu_arm_entry.elf` (the image the
#         press boots), because the change lives in POOL objects the entry build only LINKS.
[[ -s $LIVE/xnu_arm_entry.elf ]] || { echo "REFUSING: $LIVE/xnu_arm_entry.elf is absent - there is nothing to read row 15 out of"; exit 1; }
python3 -I tools/check_1022_trap15.py "$LIVE/xnu_arm_entry.elf" --expect on >/dev/null 2>&1 || {
  echo "REFUSING: the LINKED image's mach_trap_table[15] is NOT the ios7lab_mach_vm_map_trap row (arg_count=8) - either the widened gate did not reach osfmk_kern_syscall_sw.o, or a STALE pool object was linked (build_entry.sh only LINKS it). Row 15 must be kern_invalid's row for this to be the pre-rung arm. Re-run scripts/build_1022.sh"; exit 1; }
echo "   BY VALUE(1): the LINKED $LIVE/xnu_arm_entry.elf's mach_trap_table[15] IS the ios7lab_mach_vm_map_trap row (arg_count=8, handler + name present)"

# 1022-2. THE DISCRIMINATING CONTROL.  A clause that cannot FAIL is not a check
#         ([[mi4-a-claim-in-a-comment-is-not-a-check]]); so the SAME checker is run in the `--expect off`
#         direction on the FROZEN PRE-RUNG arm (1021d, ee2dd4fb), whose row 15 IS `kern_invalid`
#         (MEASURED this session: frozen ee2dd4fb's row 15 == row 0 == {argc=0, fn=0x8047f5a4=kern_invalid,
#         nameptr=0x805560b8}).  The pair (pre-rung -> off PASS, this arm -> on PASS) is what proves the
#         checker DISCRIMINATES rather than always saying the same thing.
_ctrl=$LIVE/frozen/armed-d13-ee2dd4fb/xnu_arm_entry.elf
[[ -s $_ctrl ]] || { echo "REFUSING: the frozen 1021d elf ($_ctrl) is absent - the NEGATIVE CONTROL for this rung's by-value clause cannot be taken, so the clause above cannot be shown to DISCRIMINATE. 1021d's park must exist before 1022's"; exit 1; }
python3 -I tools/check_1022_trap15.py "$_ctrl" --expect off >/dev/null 2>&1 || {
  echo "REFUSING: the frozen 1021d elf's row 15 does NOT read kern_invalid in the --expect off direction - the checker is either broken or pointed at the wrong row, so the 'on' PASS above certifies nothing. This is the control; a control that agrees with the test proves only that the checker says one thing"; exit 1; }
echo "   BY VALUE(2): NEGATIVE CONTROL - the frozen 1021d elf (ee2dd4fb) reads row 15 == kern_invalid (--expect off PASSes), so the 'on' above is a reading and not a constant"

# (c) THE ARM-EXTRA DECLARATION CARRIER.  1022's build WRITES this file at its tail
#     (`STAGE90_XNU_GLOBAL_FIX=1 role=1022-trap15`), so THIS park requires it verbatim and never appends.
#     1015b's GLOBAL_FIX is a POOL switch no writer in src/entry/ echoes, so the record cannot carry it;
#     this drop-in is its ONE declaration, and the role tag names 1022 so a stale 1021d line is
#     distinguishable by content.  **THE GREP IS THE PROCESS CHECK, NOT THE PROOF** - the pmap_flush_tlbs
#     fingerprint below is what certifies the fix is in the object ([[mi4-self-written-record-is-not-a-constraint]]).
_b_extra="$LIVE/xnu_arm_entry-arm-extra.txt"
[[ -f $_b_extra ]] || { echo "REFUSING: $_b_extra is absent - 1022's build writes the declaration carrier at its tail; an absent one means this arm's build did not finish"; exit 1; }
[[ $(wc -l < "$_b_extra") == 1 && $(cat "$_b_extra") == 'STAGE90_XNU_GLOBAL_FIX=1 role=1022-trap15' ]] || {
  echo "REFUSING: $_b_extra is not EXACTLY the one 1022 line (it holds '$(cat "$_b_extra")' across $(wc -l < "$_b_extra") line(s)). 1015b's GLOBAL_FIX is a POOL switch no writer in src/entry/ echoes, so this drop-in is its ONE declaration, and 1022's build writes it as exactly this line. A surviving 1021d line (role=1021d-general-regs) would be a declaration OF A DIFFERENT BUILD ([[mi4-self-written-record-is-not-a-constraint]]). Re-run scripts/build_1022.sh"; exit 1; }
echo "   DECLARATION: $_b_extra carries exactly '$(cat "$_b_extra")' (written by THIS build; the proof is the fingerprint below)"

# ---- 1022'S INHERITED GLOBAL-FIX CLAUSE: IT IS IN THE FROZEN POOL OBJECT, BY VALUE ----
# 1020's pool rebuild DROPPED 1015b (records/1020-build-defect.txt): the storm returned.  1022 rebuilds
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
  echo "REFUSING: pmap_flush_tlbs materializes kernel_pmap x$_gf_kp (want 3), arm_tlb_flushID x$_gf_full / arm_tlb_flushID_ASID x$_gf_asid (want 1/1) - GLOBAL-FIX is NOT in the frozen pool object, so the retry storm 1015b killed is BACK and the run would never reach pid 1's os_alloc udf this rung exists to retire. -DSTAGE90_XNU_GLOBAL_FIX=1 has NO record carrier (records/1020-build-defect.txt). Re-run scripts/build_1022.sh."; exit 1; }
echo "   POOL(GLOBAL-FIX): by value - pmap_flush_tlbs carries the 3rd kernel_pmap materialization ($_gf_kp sites) and the declared 1/1 flush arms, so 1015b's GLOBAL-FIX IS in the frozen pool object"

# ---- 1022'S INHERITED NEON-FREE CLAUSE: 1021d's -msoft-float must still hold (this build re-OBJECTs
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
  echo "REFUSING: $_pool holds $_got_objs object(s) but $_src_rec names $_want_objs source(s) - the census would count the WRONG set (a partial or stale directory passes any NEON-free test vacuously). Re-run scripts/build_1022.sh"; exit 1; }
python3 -I tools/check_d13_no_neon.py --pool "$_pool" --expect none --min-objects "$_want_objs" >/dev/null 2>&1 || {
  echo "REFUSING: tools/check_d13_no_neon.py --pool $_pool --expect none did NOT pass - the pool the linked image was built from still contains FP/NEON instructions. 1022 rebuilds mach_kernelrpc.c and syscall_sw.c, so the -msoft-float flag must still reach the pool or the kernel-mode NEON undef at 0x8047b0ac returns. Re-run scripts/build_1022.sh"; exit 1; }
python3 -I tools/check_d13_no_neon.py "$LIVE/xnu_arm_entry.elf" --symbol mach_msg_send --expect none >/dev/null 2>&1 || {
  echo "REFUSING: the LINKED image's mach_msg_send still contains FP/NEON instructions - 1021d's -msoft-float flag did not reach the object the linker saw, or a STALE pool object was linked. Re-run scripts/build_1022.sh"; exit 1; }
echo "   POOL(NEON-FREE): by value - $_got_objs objects NEON-free (== $_want_objs source(s)), and the LINKED mach_msg_send is NEON-free (1021d's -msoft-float holds)"

# ====================================================================================================
# THE INHERITED REGRESSION CLAUSES - 1021d's / 1020b's / 1018's, kept VERBATIM IN MEANING (placed AFTER
# 1022's own change, so a park that lost 1022's row has already stopped above).
# ====================================================================================================
echo "== inherited regressions =="
# 1021d's general-regs switch stays on (the kernel pool must stay NEON-free).
grep -q '^STAGE90_XNU_GENERAL_REGS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_GENERAL_REGS=1 - 1021d's -msoft-float flag switch is part of this arm's set verbatim and must stay on (without it the kernel-mode NEON undef at 0x8047b0ac returns and this rung would measure a different frontier than the os_alloc udf it exists to retire)"; exit 1; }
# 1020's raise switch stays on: 1022 must not regress the fix that killed the udf loop.
grep -q '^STAGE90_XNU_UNDEF_RAISE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_UNDEF_RAISE=1 - 1020's raise fix is part of this arm's set verbatim and must stay on (without it pid 1's genuine udf loops forever again, 8183x measured)"; exit 1; }
# 1018's direct-channel switch stays on (its witness survives the cap) - the recorder that names the udf.
grep -q '^STAGE90_XNU_UNDEF_DIRECT=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_UNDEF_DIRECT=1 - 1018's direct channel is part of this arm's set verbatim and must stay on (without it the udf witness rides the CAPPED channel and can be silenced - the 1017/1018 instrument defect)"; exit 1; }
# 1020's by-value clause, re-run on the LINKED image: `sleh_undef`'s user arm must still reach the
# EXC-printf raise on the VFP-already-enabled path.
[[ -f tools/check_d13_undef_raise.py ]] || { echo "REFUSING: tools/check_d13_undef_raise.py is ABSENT - it is the inherited 1020 claim's by-value check on the linked image (the build writes it, so an absent one means the build tree is incomplete)"; exit 1; }
python3 -I tools/check_d13_undef_raise.py "$LIVE/xnu_arm_entry.elf" --expect on >/dev/null 2>&1 || {
  echo "REFUSING: tools/check_d13_undef_raise.py --expect on did NOT pass on the linked image - 1020's moved \`return\` is NOT in this image's sleh_undef, so the udf loop is back and this rung would measure a different frontier. Re-run scripts/build_1022.sh"; exit 1; }
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
python3 -I tools/check_1022_trap15.py "$PARK/xnu_arm_entry.elf" --expect on >/dev/null 2>&1 || {
  echo "REFUSING: the FROZEN $PARK/xnu_arm_entry.elf does not carry the claim the live one did - the copy is not the certified bytes. This park's directory is incomplete; remove $PARK and re-run"; exit 1; }
echo "   BY VALUE(6): the FROZEN $PARK/xnu_arm_entry.elf's row 15 IS the ios7lab_mach_vm_map_trap row (the parked bytes are the certified bytes)"

{
  printf '# %s  -  1022: INSTALL THE mach trap-15 ROW (mach_vm_map) so pid 1'"'"'s os_alloc stops faulting.\n' "$ARM"
  printf '# (The 1021d frontier was pid 1'"'"'s USER-MODE udf at __os_alloc_slow+0x108, the mach_vm_map-failure\n'
  printf '#  join: mach_trap_table[15] is kern_invalid on CANCRO because IOS7_COMPAT_PLATFORM is\n'
  printf '#  BOARD_CONFIG_ARMPBA8||BOARD_CONFIG_QSD8250_LEO (osfmk/arm/ios7_compat.h:5) and CANCRO/MSM8974\n'
  printf '#  is neither. records/1021d-press-read.txt, docs/1022-mach-trap-15-design.md.)\n'
  printf '# Switch set is 1021d-s VERBATIM (STAGE90_XNU_GENERAL_REGS=1, STAGE90_XNU_UNDEF_RAISE=1,\n'
  printf '# STAGE90_XNU_UNDEF_DIRECT=1, STAGE90_XNU_GLOBAL_FIX=1 and the whole inherited set stay) with\n'
  printf '# **NO NEW SWITCH**: the row-15 and inner gates widen to read STAGE90_XNU_IOS7_TRAPS, which is\n'
  printf '# ALREADY 1 in this arm-s record (records/1022-trap15-plumbing.txt sec.2/sec.8).\n'
  printf '# THE ARM: THREE gates - (1) syscall_sw.c:126 row-15 + :102 extern -> `|| (defined(\n'
  printf '# STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS)`; (2) mach_kernelrpc.c:59 inner gate (the\n'
  printf '# ios7lab_mach_vm_map_trap definition at :208); (3) mach_kernelrpc.c:217 runtime self-gate\n'
  printf '# `if(!ios7_compat_vmabi_enabled())` -> `if(!ios7_traps_enabled())`. Gate 3 is MANDATORY: CANCRO-s\n'
  printf '# ios7_compat_vmabi_enabled() is 0, and the client shim-s `cmp r4,r0` against\n'
  printf '# MACH_SEND_INVALID_DEST=0x10000003 treats the old gate-s return as its own sentinel and branches\n'
  printf '# to the MIG slow leg, skipping the map (records/1021d-mmap-failure.txt sec.B).\n'
  printf '# THE POOL MOVES: syscall_sw.c and mach_kernelrpc.c are POOL objects, so the pool-s .text (and the\n'
  printf '# entry .bin whose sha8 IS the arm) MOVES (ee2dd4fb -> %s). build_entry.sh only LINKS the pool\n'
  printf '# objects, so a green entry build says nothing about this half.\n#\n' "${ARM#armed-d13-}"
  printf '# THE BY-VALUE PROOF OF 1022-S CHANGE, asserted FIRST and re-run on the FROZEN bytes.\n'
  printf '# tools/check_1022_trap15.py reads mach_trap_table BY VALUE:\n'
  printf '#   (0) --selftest PASSes (the checker can fail);\n'
  printf '#   (1) --expect on PASSes on the LINKED out/stage90/xnu_arm_entry.elf: row 15 is\n'
  printf '#       {arg_count=8; fn=&ios7lab_mach_vm_map_trap; name="ios7lab_mach_vm_map_trap"}, both\n'
  printf '#       symbols PRESENT (an absent symbol is a refusal, never a pass);\n'
  printf '#   (2) NEGATIVE CONTROL - the SAME checker --expect off PASSes on the frozen PRE-RUNG arm\n'
  printf '#       1021d (armed-d13-ee2dd4fb), whose row 15 IS kern_invalid (MEASURED: row 15 == row 0 ==\n'
  printf '#       {argc=0, fn=0x8047f5a4=kern_invalid}), so the on direction is a reading and not a\n'
  printf '#       constant ([[mi4-a-claim-in-a-comment-is-not-a-check]]).\n#\n'
  printf '# THE CARRIERS ([[mi4-one-value-two-definitions]]). The record carries STAGE90_XNU_IOS7_TRAPS=1\n'
  printf '# (the switch the widened gates read AND the readability carrier). The declaration carrier\n'
  printf '# out/stage90/xnu_arm_entry-arm-extra.txt is written by THIS build-s tail as\n'
  printf '# STAGE90_XNU_GLOBAL_FIX=1 role=1022-trap15 (1015b-s GLOBAL_FIX is a POOL switch no writer in\n'
  printf '# src/entry/ echoes, so the record cannot carry it); the grep of it is the process check and the\n'
  printf '# pmap_flush_tlbs fingerprint is the proof.\n#\n'
  printf '# THE INHERITED REGRESSIONS held (asserted by the park): STAGE90_XNU_GENERAL_REGS=1 (1021d-s\n'
  printf '# -msoft-float), STAGE90_XNU_UNDEF_RAISE=1 and STAGE90_XNU_UNDEF_DIRECT=1 in the record, the\n'
  printf '# pmap_flush_tlbs kernel_pmap==3 fingerprint (1015b-s kernel fix), the whole-pool NEON-free census\n'
  printf '# + the linked mach_msg_send NEON-free read (1021d), check_d13_undef_raise.py --expect on on the\n'
  printf '# linked image (1020-s raise intact), and by value __wrap_sleh_undef branching to entry_write_kv\n'
  printf '# (>=6) and NOT entry_live_write (0).\n#\n'
  printf '# THE 1022 VERDICT CELLS (docs/1022-mach-trap-15-design.md sec.7):\n'
  printf '#   G-CTL        - neither xnu_live_undef_seq nor any IOS7LAB vm_map64 line anywhere =>\n'
  printf '#                  INSTRUMENT_UNSOUND, WITHHOLD.\n'
  printf '#   T15-FIRES    - IOS7LAB vm_map64 ... result=0 address=<nonzero> AND the boot proceeds past\n'
  printf '#                  __os_alloc_slow => ROW INSTALLED, USED, AND SUCCESSFUL; the wall moved.\n'
  printf '#   T15-REFUSED  - IOS7LAB vm_map64 ... result=4 (KERN_INVALID_ARGUMENT) at least once, no panic\n'
  printf '#                  => ROW USED; THE WIRE IS REFUSED (read the printed frame words).\n'
  printf '#   T15-SENTINEL - the run fails earlier/differently, -15 never reaches the row => REGRESSION, ROW\n'
  printf '#                  INSTALLED, SELECTOR STILL 0 (the :217 gate did not land).\n'
  printf '#   T15-PANICS   - a swi_mach_error-shaped panic => STALE POOL, not a behaviour reading.\n'
  printf '#   T15-UNEXERCISED - the new frame dump reads the SLOW shape (frame0 == 7) => GUARD FAILED.\n'
  printf '#   T15-OTHER-CALL  - the frame dump reads a FAST shape (a userspace pointer) => residue (ii).\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1022-trap15;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3