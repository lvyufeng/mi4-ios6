#!/usr/bin/env bash
# ====================================================================================================
# park_1021d.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1020b.sh, WITH 1015b's POOL-MOVED
# DISCIPLINE.
#
# Park 1021d (STAGE90_XNU_GENERAL_REGS) and append its block to records/revert-set.txt.
#
# 1021d = 1020b's switch set VERBATIM (STAGE90_XNU_GLOBAL_FIX=1, STAGE90_XNU_UNDEF_RAISE=1,
# STAGE90_XNU_UNDEF_DIRECT=1 and the whole inherited set stay) + exactly ONE new switch:
# STAGE90_XNU_GENERAL_REGS=1.  Design: docs/1021c-general-regs-design.md.  Root cause:
# records/1021-fpu-rootcause.txt (commit 063288e).
#
# WHY.  The 1020b press read R-RAISES-ONCE: the 1019 `udf` loop is DEAD, and the frontier MOVED to a
# KERNEL-MODE NEON undefined instruction at `0x8047b0ac` inside `mach_msg_send` (`vld1.32 {d16-d17},
# [r0]!`).  1021 root-caused it (records/1021-fpu-rootcause.txt): the kernel starts with FPEXC.EN ON,
# and `save_vfp_context` (osfmk/arm/pcb.c:298-307) clears it on the NEXT context switch while kernel
# switch-in has NO restore, so the kernel's OWN compiler-emitted NEON faults the first time it runs.
# The pool is built `-mfpu=neon-vfpv4 -mfloat-abi=softfp` -- hard-float codegen with a soft-float
# calling convention, the one combination that lets clang emit NEON into the kernel.
#
# **THE TASK'S PREMISE WAS FALSIFIED** (docs/1021c-general-regs-design.md §0): `-mgeneral-regs-only`
# is an AARCH64-only clang feature; on armv7 the driver forwards it and `cc1` DROPS it (measured: the
# same TU compiles byte-identically with and without it).  A build that used it would produce a pool
# byte-identical to today's, read as ON, and re-measure `0x8047b0ac` -- the most-repeated failure class
# (`[[mi4-off-option-two-spellings]]`).  **The measured fix is `-msoft-float`**, which is exactly what
# Apple's own ARM kernel flags make (`external/xnu-hd2-darwin13/xnu/makedefs/MakeInc.def:377-379`).
#
# THE ARM (ONE flag-list change, ONE switch).  `STAGE90_XNU_GENERAL_REGS` (default 0) makes
# `tools/build_xnu_arm_kernel.sh` APPEND `-msoft-float` to its `CC_ARGS`/`CXX_ARGS` under `-eq 1`, and
# it is that append -- not a `#if` -- that removes the FP register file from the allocator.  Switch OFF
# appends nothing, so the arrays are exactly today's (`[[mi4-off-option-two-spellings]]`).
#
# **THIS IS A POOL-REBUILD RUNG (the 1015b shape), NOT ENTRY-ONLY.**  The flag reaches every pool
# object's `.text`, and the pool's `.text` is INSIDE the `.bin` whose sha256 IS the arm.  `build_entry.sh`
# only LINKS the pool objects, so a green entry build says NOTHING about this half: a stale pool object
# keeps Apple's NEON bytes.  This is why the park's OWN change is asserted BY VALUE on the FROZEN
# artifacts -- the pool census and the linked `mach_msg_send` body -- before every inherited clause.
#
# **THE TWO CARRIERS, AND WHY THE RECORD IS NOT ONE OF THEM FOR THE SUBSTANCE
# ([[mi4-one-value-two-definitions]]).**  Unlike 1020's `-D` switch, this switch's SUBSTANCE is the
# compiler FLAG, not a `-D`: the record carries `STAGE90_XNU_GENERAL_REGS=1` so the arm is READABLE,
# but a record that says "NEON-free" over a pool full of `vld1` is exactly the 1020 defect
# (records/1020-build-defect.txt).  The proof the flag reached the compile is a BY-VALUE property of
# the object -- `tools/check_d13_no_neon.py` (design §5).  The declaration carrier
# `out/stage90/xnu_arm_entry-arm-extra.txt` is written by the BUILD (1021d's build writes
# `STAGE90_XNU_GLOBAL_FIX=1 role=1021d-general-regs` at its tail, exactly as 1020b wrote the
# `role=1020b-globalfix` line): a grep of that file is a process check, and the by-value clauses below
# are what actually certify the fix ([[mi4-self-written-record-is-not-a-constraint]]).
#
# THE 1021d VERDICT CELLS (design §4.4; the press reads them off the capture):
#   A RETIRED        - no kernel-mode undef at `0x8047b0ac`; the run proceeds past `mach_msg_send` (the
#                      frontier moves, or the run ends at `entry_epilogue`)             => THE FIX STANDS
#   B STILL-THERE    - the same undef at `0x8047b0ac` (same `lr=0x8047b0b0`)             => NOT REBUILT
#                      (read the by-value cell FIRST -- it should have refused this press)
#   C NEW-FAULT      - a DIFFERENT undef pc, or a link refusal on `__aeabi_f*`/`__aeabi_d*` => soft-float
#                      added a libgcc reference the link does not resolve (name it, add `$LIBGCC`)
#   D EARLIER-STOP   - the run stops somewhere earlier (e.g. `arm_init`'s VFP self-test) => the flag
#                      broke something that legitimately needs the FPU (read design §3.2)
#   E NO-CHANGE      - byte-for-byte the 1020b frontier => the switch did not reach the compile; the
#                      by-value clause should have caught this, so if it passed, the clause is wrong
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible.  fastboot boot only, NEVER flash.
# ====================================================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

LIVE=out/stage90
REC=records/revert-set.txt
# xnu_arm_entry-arm-extra.txt is 1015b's declaration carrier and is FROZEN with the arm it describes
# (see the header).  park_1015b.sh's MEMBERS list, PLUS this file, verbatim.
MEMBERS=(xnu_arm_entry.bin xnu_arm_entry.elf xnu_arm_entry.h xnu_arm_entry-config.txt \
         xnu_arm_entry-arm-extra.txt xnu_arm_entry-sources.txt stage90.bin stage90.elf stage90.img \
         stage90-qcdt.img stage90_fixture.macho stage90-build-config.txt SHA256SUMS.txt)

# The arm name IS the entry bin's sha8 (the project's rule).  Derive it, and refuse to re-park the
# RECENT PRESSED arms under a new name (build 1021d first).  b5093f00 is the arm the 1020b press SPENT
# and is the arm 1021d must MOVE AWAY from; the rest are earlier rungs.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-b5093f00 ]] && { echo "REFUSING: $LIVE holds 1020b's PRESSED arm (b5093f00) - the arm whose kernel-mode NEON undef at 0x8047b0ac 1021d exists to retire. A park of the SAME arm would freeze a set that does not carry this rung's flag switch; build 1021d first"; exit 1; }
[[ $ARM == armed-d13-47f664c4 ]] && { echo "REFUSING: $LIVE holds 1020's DROPPED-GLOBAL-FIX arm (47f664c4); build 1021d first"; exit 1; }
[[ $ARM == armed-d13-c2168632 ]] && { echo "REFUSING: $LIVE holds 1018's arm (c2168632); build 1021d first"; exit 1; }
[[ $ARM == armed-d13-16fe8885 ]] && { echo "REFUSING: $LIVE holds 1015b's arm (16fe8885); build 1021d first"; exit 1; }
[[ $ARM == armed-d13-e51d14fa ]] && { echo "REFUSING: $LIVE holds 1015's arm (e51d14fa); build 1021d first"; exit 1; }
[[ $ARM == armed-d13-29ec9e48 ]] && { echo "REFUSING: $LIVE holds 1014's arm (29ec9e48); build 1021d first"; exit 1; }
[[ $ARM == armed-d13-9bf25f8f ]] && { echo "REFUSING: $LIVE holds 1013b's arm (9bf25f8f); build 1021d first"; exit 1; }
[[ $ARM == armed-d13-ffdc8ba1 ]] && { echo "REFUSING: $LIVE holds 1012's arm (ffdc8ba1); build 1021d first"; exit 1; }

# ====================================================================================================
# 1021d'S OWN CHANGE, BY VALUE, FIRST - BEFORE EVERY INHERITED REGRESSION CLAUSE.
# **THE ORDER IS THE POINT.**  A park that lost 1021d's switch must fail HERE, on ITS OWN claim, rather
# than further down where an inherited (1020b/1018/1015b) clause would stop it for a reason that names
# the wrong thing (the 1015b lesson: its first draft asserted 1015's pool-UNMOVED shape as its own).
# ====================================================================================================
echo "== 1021d's OWN change, by value, first =="

# (a) THE RECORD'S OWN KEY.  The record carries the RESOLVED `STAGE90_XNU_GENERAL_REGS=1` line (written
#     by `src/entry/build_entry.sh`'s record echo, and named in `scripts/preflight_boot_check.sh`'s
#     ENTRY_CFG_KEYS so the set-difference gate does not refuse the arm).  This is the READABILITY
#     carrier, NOT the substance: it says the switch was ON, not that the flag reached the pool.  The
#     substance is asserted by value in (b) below ([[mi4-one-value-two-definitions]]).
grep -q '^STAGE90_XNU_GENERAL_REGS=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_GENERAL_REGS=1 - 1021d's flag switch was not recorded this run (the press gate reads THIS line), so a park here would freeze the switch set without the rung. Build with scripts/build_1021d.sh"; exit 1; }

# (b) THE BY-VALUE CLAUSE - THE PARK'S OWN CLAIM, ON THE ARTIFACTS THE LINKER SAW.  This switch's
#     substance is a FLAG, so the only proof it took is a property of the OBJECTS: the pool must be
#     NEON-free (design §5.2) and the linked `mach_msg_send` body -- the exact function 1020b stopped in
#     at `0x8047b0ac` -- must contain no FP/NEON instruction (design §5.1).
#
#     **THE CHECKER IS REQUIRED, AND ITS ABSENCE IS A REFUSAL, NOT A SKIP.**  A park that skipped this
#     would freeze an arm whose own claim nothing checks -- the copy-paste shape of
#     [[mi4-a-claim-in-a-comment-is-not-a-check]].  There is no sound object-side fallback: the census
#     IS the claim.
[[ -f tools/check_d13_no_neon.py ]] || {
  echo "REFUSING: tools/check_d13_no_neon.py is ABSENT. It is this park's by-value claim on the frozen pool and on the linked mach_msg_send body, and there is no sound fallback census. An absent checker means the by-value half of this rung cannot be certified - a park that skipped it would freeze an arm whose 'NEON-free' claim nothing ever checked ([[mi4-a-claim-in-a-comment-is-not-a-check]])"; exit 1; }

# 1021d-0. THE CHECKER'S OWN SELF-TEST.  A checker that cannot fail is not a check
#         ([[mi4-a-claim-in-a-comment-is-not-a-check]]).  Refuse the park unless it PASSes its own
#         selftest, so an inert or mis-anchored checker cannot bless this arm.
python3 -I tools/check_d13_no_neon.py --selftest >/dev/null 2>&1 || {
  echo "REFUSING: tools/check_d13_no_neon.py --selftest did NOT PASS - the checker cannot certify anything (an inert checker would bless any pool). Fix the checker before parking the arm it is supposed to prove"; exit 1; }
echo "   BY VALUE(0): tools/check_d13_no_neon.py --selftest PASSes (the checker is live)"

# 1021d-1. THE POOL CENSUS (design §5.2 - the ON arm's REAL assertion).  A flag that suppressed NEON in
#          `mach_msg.o` but not in `ip_dummynet.o` would leave `vpush {d8-d9}` waiting, so the whole
#          pool must be NEON-free.  **AND IT MUST NOT BE A VACUOUS PASS OVER AN EMPTY OR STALE DIR**
#          (defect class m1015b; design §5.3): the object count on disk must equal the count of source
#          files the pool recorded in `out/xnu_kernel_src_d13.sha256` -- two independent files agreeing
#          is what makes a stale directory a refusal and not a green line.
_pool=out/xnu_kernel_obj_d13
[[ -d $_pool ]] || { echo "REFUSING: $_pool is absent - the pool the linked image was built from is not on disk, so the whole-pool census would read nothing and pass vacuously"; exit 1; }
_src_rec=out/xnu_kernel_src_d13.sha256
[[ -s $_src_rec ]] || { echo "REFUSING: $_src_rec is absent - the pool's own source-content record (written by tools/build_xnu_arm_kernel.sh after the compile loop) is the independent count this clause compares against. Its absence means the pool build did not complete"; exit 1; }
_want_objs=$(grep -c '^[0-9a-f]\{64\} ' "$_src_rec" || true)
_got_objs=$(ls "$_pool"/*.o 2>/dev/null | wc -l)
[[ $_want_objs -gt 0 && $_got_objs -eq $_want_objs ]] || {
  echo "REFUSING: $_pool holds $_got_objs object(s) but $_src_rec names $_want_objs source(s) the pool was built from - the census below would count the WRONG set (a partial or stale directory passes any NEON-free test vacuously). Re-run scripts/build_1021d.sh so the pool on disk is the pool the record describes"; exit 1; }
python3 -I tools/check_d13_no_neon.py --pool "$_pool" --expect none --min-objects "$_want_objs" >/dev/null 2>&1 || {
  echo "REFUSING: tools/check_d13_no_neon.py --pool $_pool --expect none did NOT pass - the pool the linked image was built from still contains FP/NEON instructions (OFF/Apple: 273 of 705 objects have >=1). -msoft-float is a FLAG on CC_ARGS/CXX_ARGS, so it is dropped silently by a pool that a stale build produced ('the 1020 record that said NEON-free over a pool full of vld1'). Re-run scripts/build_1021d.sh"; exit 1; }
echo "   BY VALUE(1): the WHOLE pool is NEON-free - $_got_objs objects censused in $_pool (== the $_want_objs source(s) in $_src_rec), 0 FP/NEON instructions"

# 1021d-2. THE LINKED SYMBOL (design §5.1).  `mach_msg_send` carried the frontier: the 1020b press
#          stopped at `0x8047b0ac`, inside this symbol, and the frozen b5093f00 elf carries EXACTLY 4 FP
#          instructions there (offsets 0xb8/0xbc/0xd4/0xd8).  The window is the symbol's OWN extent, so a
#          neighbour cannot satisfy the check on its behalf.  Read on the LINKED image, because
#          `build_entry.sh` only LINKS the pool object - a stale object is invisible to a green entry
#          build (the 1020 defect exactly).
[[ -s $LIVE/xnu_arm_entry.elf ]] || { echo "REFUSING: $LIVE/xnu_arm_entry.elf is absent - there is nothing to read the linked symbol out of"; exit 1; }
python3 -I tools/check_d13_no_neon.py "$LIVE/xnu_arm_entry.elf" --symbol mach_msg_send --expect none >/dev/null 2>&1 || {
  echo "REFUSING: the LINKED image's mach_msg_send still contains FP/NEON instructions - the pool flag did not reach the object the linker saw, or a STALE out/xnu_kernel_obj_d13/osfmk_ipc_mach_msg.o was linked (build_entry.sh only LINKS it, so a green entry build says nothing about this half). Re-run scripts/build_1021d.sh"; exit 1; }
echo "   BY VALUE(2): the LINKED $LIVE/xnu_arm_entry.elf's mach_msg_send is NEON-free (the 1020b frontier instruction is gone)"

# 1021d-3. THE DISCRIMINATING CONTROL.  A clause that cannot FAIL is not a check
#          ([[mi4-a-claim-in-a-comment-is-not-a-check]]); so the SAME checker is run in the `--expect
#          any` direction on the FROZEN PRE-RUNG arm (1020b, b5093f00), whose `mach_msg_send` DOES carry
#          the 4 FP instructions.  The pair (pre-rung -> any PASS, this arm -> none PASS) is what proves
#          the checker discriminates rather than always saying the same thing.
_ctrl=$LIVE/frozen/armed-d13-b5093f00/xnu_arm_entry.elf
[[ -s $_ctrl ]] || { echo "REFUSING: the frozen 1020b elf ($_ctrl) is absent - the NEGATIVE CONTROL for this rung's by-value clause cannot be taken, so the clause above cannot be shown to DISCRIMINATE (it might pass on any image). Park/press chain order: 1020b's park must exist before 1021d's"; exit 1; }
python3 -I tools/check_d13_no_neon.py "$_ctrl" --symbol mach_msg_send --expect any >/dev/null 2>&1 || {
  echo "REFUSING: the frozen 1020b elf's mach_msg_send does NOT report FP instructions in the --expect any direction - the checker is either broken or pointed at the wrong extent, so the 'none' PASS above certifies nothing. This is the control; a control that agrees with the test proves only that the checker says one thing"; exit 1; }
echo "   BY VALUE(3): NEGATIVE CONTROL - the frozen 1020b elf (b5093f00) REPORTS FP in mach_msg_send (--expect any PASSes), so the 'none' above is a reading and not a constant"

# (c) THE ARM-EXTRA DECLARATION CARRIER.  1021d's build WRITES this file at its tail
#     (`STAGE90_XNU_GLOBAL_FIX=1 role=1021d-general-regs`), so THIS park requires it verbatim and never
#     appends.  1015b's GLOBAL_FIX is a POOL switch no writer in src/entry/ echoes, so the record cannot
#     carry it; this drop-in is its ONE declaration, and the role tag names 1021d so a stale 1020b line
#     is distinguishable by content.  **AND THE GREP IS THE PROCESS CHECK, NOT THE PROOF**: the file can
#     be stale in a way the codegen read cannot, so the fingerprint clause below is what certifies the
#     fix is in the object ([[mi4-self-written-record-is-not-a-constraint]]).
_b_extra="$LIVE/xnu_arm_entry-arm-extra.txt"
[[ -f $_b_extra ]] || { echo "REFUSING: $_b_extra is absent - 1021d's build writes the declaration carrier at its tail; an absent one means this arm's build did not finish"; exit 1; }
[[ $(wc -l < "$_b_extra") == 1 && $(cat "$_b_extra") == 'STAGE90_XNU_GLOBAL_FIX=1 role=1021d-general-regs' ]] || {
  echo "REFUSING: $_b_extra is not EXACTLY the one 1021d line (it holds '$(cat "$_b_extra")' across $(wc -l < "$_b_extra") line(s)). 1015b's GLOBAL_FIX is a POOL switch no writer in src/entry/ echoes, so this drop-in is its ONE declaration, and 1021d's build writes it as exactly this line. A surviving 1020b line (role=1020b-globalfix) would be a declaration OF A DIFFERENT BUILD, which the press would read as proof of this one ([[mi4-self-written-record-is-not-a-constraint]]); two lines mean a stale declaration was not truncated. Re-run scripts/build_1021d.sh"; exit 1; }
echo "   DECLARATION: $_b_extra carries exactly '$(cat "$_b_extra")' (written by THIS build; the proof is the fingerprint below)"

# ---- 1021d'S INHERITED GLOBAL-FIX CLAUSE: IT IS IN THE FROZEN POOL OBJECT, BY VALUE ----
# 1020's pool rebuild DROPPED 1015b (records/1020-build-defect.txt): the storm returned and the udf was
# never reached.  1021d rebuilds the pool AGAIN, so the same defect is available.  The fix's fingerprint
# is the count of `kernel_pmap` MOVW materializations inside `pmap_flush_tlbs` (OFF/Apple = 2, ON = 3).
# A grep of the arm-extra drop-in would be a check on a record no codegen writes
# ([[mi4-self-written-record-is-not-a-constraint]]); this reads the object the linker saw.
_gf_obj=out/xnu_kernel_obj_d13/osfmk_arm_pmap.o
[[ -f "$_gf_obj" ]] || { echo "REFUSING: $_gf_obj is absent - 1015b's GLOBAL-FIX lives in pmap_flush_tlbs, and a park that cannot read it cannot certify the storm stays gone"; exit 1; }
_gf_kp=$(arm-none-eabi-objdump -r "$_gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'MOVW_ABS_NC.*kernel_pmap' || true)
_gf_full=$(arm-none-eabi-objdump -r "$_gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'R_ARM_CALL.*arm_tlb_flushID$' || true)
_gf_asid=$(arm-none-eabi-objdump -r "$_gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'R_ARM_CALL.*arm_tlb_flushID_ASID$' || true)
[[ "$_gf_kp" == "3" && "$_gf_full" == "1" && "$_gf_asid" == "1" ]] || {
  echo "REFUSING: pmap_flush_tlbs materializes kernel_pmap x$_gf_kp (want 3), arm_tlb_flushID x$_gf_full / arm_tlb_flushID_ASID x$_gf_asid (want 1/1) - GLOBAL-FIX is NOT in the frozen pool object, so the retry storm 1015b killed is BACK and the kernel-mode undef 1021d exists to retire would never be reached. -DSTAGE90_XNU_GLOBAL_FIX=1 has NO record carrier (records/1020-build-defect.txt). Re-run scripts/build_1021d.sh."; exit 1; }
echo "   POOL(GLOBAL-FIX): by value - pmap_flush_tlbs carries the 3rd kernel_pmap materialization ($_gf_kp sites) and the declared 1/1 flush arms, so 1015b's GLOBAL-FIX IS in the frozen pool object"

# ====================================================================================================
# THE INHERITED REGRESSION CLAUSES - 1020b's and 1018's, kept VERBATIM IN MEANING (placed AFTER 1021d's
# own change, so a park that lost 1021d's switch has already stopped above).
# ====================================================================================================
echo "== inherited regressions =="
# 1020's raise switch stays on: 1021d must not regress the fix that killed the udf loop.
grep -q '^STAGE90_XNU_UNDEF_RAISE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_UNDEF_RAISE=1 - 1020's raise fix is part of this arm's set verbatim and must stay on (without it pid 1's genuine udf loops forever again, 8183x measured)"; exit 1; }
# 1018's direct-channel switch stays on (its witness survives the cap) - the recorder that names the udf.
grep -q '^STAGE90_XNU_UNDEF_DIRECT=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_UNDEF_DIRECT=1 - 1018's direct channel is part of this arm's set verbatim and must stay on (without it the udf witness rides the CAPPED channel and can be silenced)"; exit 1; }
# 1020's by-value clause, re-run on the FROZEN linked image: `sleh_undef`'s user arm must still reach
# the EXC-printf raise on the VFP-already-enabled path.
[[ -f tools/check_d13_undef_raise.py ]] || { echo "REFUSING: tools/check_d13_undef_raise.py is ABSENT - it is the inherited 1020 claim's by-value check on the linked image (the build writes it, so an absent one means the build tree is incomplete)"; exit 1; }
python3 -I tools/check_d13_undef_raise.py "$LIVE/xnu_arm_entry.elf" --expect on >/dev/null 2>&1 || {
  echo "REFUSING: tools/check_d13_undef_raise.py --expect on did NOT pass on the frozen linked image - 1020's moved \`return\` is NOT in this image's sleh_undef, so the udf loop is back and 1021d would be measuring a different frontier than the kernel-mode NEON undef it exists to retire. Re-run scripts/build_1021d.sh"; exit 1; }
echo "   BY VALUE(4): tools/check_d13_undef_raise.py --expect on PASSes (1020's raise is intact in the frozen linked image)"
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
  echo "REFUSING: the parked image's __wrap_sleh_undef ($_ua..$_ub) branches to entry_write_kv $_kv time(s) and entry_live_write $_lw time(s) - with the switch on it must be >=6 direct and 0 counted (the six keys off the capped channel). A park that kept the counted branch freezes an arm whose witness the cap can still silence."; exit 1; }
echo "   BY VALUE(5): __wrap_sleh_undef ($_ua..$_ub) branches to entry_write_kv x$_kv, entry_live_write x$_lw (1018's witness is on the DIRECT channel)"

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

# THE FROZEN RE-READ.  The checks above ran on the LIVE artifacts (so a bad arm never creates a park
# directory); this re-runs the rung's own claim on the PARKED bytes, so what is frozen is what was
# certified - a later build cannot silently change what was pressed.
python3 -I tools/check_d13_no_neon.py "$PARK/xnu_arm_entry.elf" --symbol mach_msg_send --expect none >/dev/null 2>&1 || {
  echo "REFUSING: the FROZEN $PARK/xnu_arm_entry.elf does not carry the claim the live one did - the copy is not the certified bytes. This park's directory is incomplete; remove $PARK and re-run"; exit 1; }
echo "   BY VALUE(6): the FROZEN $PARK/xnu_arm_entry.elf's mach_msg_send is NEON-free (the parked bytes are the certified bytes)"

{
  printf '# %s  -  1021d: REMOVE COMPILER-EMITTED FP/NEON FROM THE KERNEL POOL (-msoft-float).\n' "$ARM"
  printf '# (the 1020b frontier was a KERNEL-MODE NEON undefined instruction at 0x8047b0ac in\n'
  printf '#  mach_msg_send: the kernel starts with FPEXC.EN ON, save_vfp_context (osfmk/arm/pcb.c:298-307)\n'
  printf '#  clears it on the next context switch, kernel switch-in never restores it, and the kernel-s\n'
  printf '#  OWN compiler-emitted NEON then faults. records/1021-fpu-rootcause.txt.)\n'
  printf '# Switch set is 1020b-s VERBATIM (STAGE90_XNU_UNDEF_RAISE=1, STAGE90_XNU_UNDEF_DIRECT=1,\n'
  printf '# STAGE90_XNU_GLOBAL_FIX=1 and the whole inherited set stay) plus exactly ONE new switch:\n'
  printf '# STAGE90_XNU_GENERAL_REGS=1.\n'
  printf '# **THE FIX IS A FLAG, NOT A -D, AND NOT -mgeneral-regs-only.** -mgeneral-regs-only is\n'
  printf '# AARCH64-ONLY in clang and is SILENTLY DROPPED for armv7 (measured: byte-identical codegen\n'
  printf '# with and without it), so a build that used it would read as ON and re-measure 0x8047b0ac.\n'
  printf '# The measured fix is -msoft-float, the exact spelling Apple-s own ARM kernel CFLAGS_ARM uses\n'
  printf '# (external/xnu-hd2-darwin13/xnu/makedefs/MakeInc.def:377-379). It is the -mfloat-abi=softfp\n'
  printf '# half of the pre-rung flags that lets clang allocate FP registers; -msoft-float removes the\n'
  printf '# FP register file from the allocator, so no vld1/vst1/vldr/vstr/vpush/vpop is emitted.\n'
  printf '# OFF appends NOTHING to CC_ARGS/CXX_ARGS, so the arrays are exactly the pre-rung ones\n'
  printf '# ([[mi4-off-option-two-spellings]]).\n'
  printf '# THE POOL MOVES: every pool object-s .text (the flag is whole-pool), and the pool-s .text is\n'
  printf '# INSIDE the entry .bin whose sha8 IS the arm, so the arm MOVES (b5093f00 -> %s) and the\n'
  printf '# PAYLOAD moves. build_entry.sh only LINKS the pool objects, so a green entry build says\n'
  printf '# nothing about this half.\n#\n' "${ARM#armed-d13-}"
  printf '# THE BY-VALUE PROOF OF 1021d-S CHANGE, asserted FIRST and re-run on the FROZEN bytes.\n'
  printf '# tools/check_d13_no_neon.py decodes the actual instructions:\n'
  printf '#   (0) --selftest PASSes (the checker can fail);\n'
  printf '#   (1) --pool out/xnu_kernel_obj_d13 --expect none PASSes over the WHOLE pool (0 FP/NEON\n'
  printf '#       instructions), AND the object count on disk equals the source count in\n'
  printf '#       out/xnu_kernel_src_d13.sha256, so a stale/partial directory is a refusal and not a\n'
  printf '#       vacuous pass (design 5.3, defect class m1015b);\n'
  printf '#   (2) --symbol mach_msg_send --expect none PASSes on the LINKED out/stage90/xnu_arm_entry.elf\n'
  printf '#       (the symbol 1020b stopped in at 0x8047b0ac), and again on the FROZEN copy;\n'
  printf '#   (3) NEGATIVE CONTROL - the frozen pre-rung 1020b elf (armed-d13-b5093f00) REPORTS FP in\n'
  printf '#       mach_msg_send (--expect any PASSes), so the none direction is a reading and not a\n'
  printf '#       constant ([[mi4-a-claim-in-a-comment-is-not-a-check]]).\n#\n'
  printf '# THE CARRIERS ([[mi4-one-value-two-definitions]]). The record carries the RESOLVED\n'
  printf '# STAGE90_XNU_GENERAL_REGS=1 line (the readability carrier; a record that says NEON-free over\n'
  printf '# a pool full of vld1 is exactly the 1020 defect). The SUBSTANCE is the -msoft-float append to\n'
  printf '# tools/build_xnu_arm_kernel.sh-s CC_ARGS/CXX_ARGS, proved only by the object census above.\n'
  printf '# The declaration carrier out/stage90/xnu_arm_entry-arm-extra.txt is written by THIS build-s\n'
  printf '# tail as STAGE90_XNU_GLOBAL_FIX=1 role=1021d-general-regs (1015b-s GLOBAL_FIX is a POOL\n'
  printf '# switch no writer in src/entry/ echoes, so the record cannot carry it); the grep of it is the\n'
  printf '# process check and the pmap_flush_tlbs fingerprint is the proof.\n#\n'
  printf '# THE INHERITED REGRESSIONS held (asserted by the park): STAGE90_XNU_UNDEF_RAISE=1 in the\n'
  printf '# record (1020-s raise), STAGE90_XNU_UNDEF_DIRECT=1 in the record (1018-s cap-exempt witness),\n'
  printf '# the pmap_flush_tlbs kernel_pmap==3 fingerprint (1015b-s kernel fix), check_d13_undef_raise.py\n'
  printf '# --expect on on the linked image (1020-s raise intact), and by value __wrap_sleh_undef\n'
  printf '# branching to entry_write_kv (>=6) and NOT entry_live_write (0).\n#\n'
  printf '# THE 1021d VERDICT CELLS (docs/1021c-general-regs-design.md 4.4):\n'
  printf '#   A RETIRED     - no kernel-mode undef at 0x8047b0ac; the run proceeds past mach_msg_send\n'
  printf '#                   (the frontier moves, or the run ends at entry_epilogue)  => THE FIX STANDS.\n'
  printf '#   B STILL-THERE - the same undef at 0x8047b0ac (same lr=0x8047b0b0) => NOT REBUILT; read the\n'
  printf '#                   by-value cell FIRST (it should have refused this press).\n'
  printf '#   C NEW-FAULT   - a DIFFERENT undef pc, or a link refusal on __aeabi_f*/__aeabi_d* =>\n'
  printf '#                   soft-float added a libgcc reference the link does not resolve.\n'
  printf '#   D EARLIER-STOP- the run stops earlier (e.g. arm_init-s VFP self-test) => the flag broke\n'
  printf '#                   something that legitimately needs the FPU (read design 3.2).\n'
  printf '#   E NO-CHANGE   - byte-for-byte the 1020b frontier => the switch did not reach the compile;\n'
  printf '#                   if the by-value clause passed, the clause is wrong.\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1021d-general-regs;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3