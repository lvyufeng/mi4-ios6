#!/usr/bin/env bash
# Build 1021d: 1020b PLUS THE FLAG CHANGE THAT STOPS THE COMPILER EMITTING FP/NEON INTO THE KERNEL
# POOL.  One new switch, STAGE90_XNU_GENERAL_REGS=1, and it is a COMPILER FLAG LIST, not a `-D`.
#
# WHY.  records/1021-fpu-rootcause.txt (proven): 1020b's `udf` raise lets pid 1 die -- and then the
# kernel itself takes an UNDEFINED-INSTRUCTION exception in KERNEL mode at `0x8047b0ac`
# (`vld1.32 {d16-d17},[r0]!`, `lr=0x8047b0b0`, inside `mach_msg_send`).  The mechanism is a
# save-without-restore of `FPEXC.EN`: the kernel boots with FPEXC.EN ON (early-boot NEON works), the
# first context switch into a thread whose `vfp_enable` is TRUE runs `save_vfp_context`
# (`osfmk/arm/pcb.c:298-306`), which calls `vfp_enable_exception(FALSE)` and clears it -- and the
# switch-IN path never restores it (`vfp_context_load` runs only on the user-mode `udf` lazy-enable
# path in `trap.c`).  So the next KERNEL-mode NEON instruction of any kind is undefined, and
# `fleh_undef` is fatal in kernel mode.  **1020b EXPOSED this latent defect; it did not create it.**
#
# THE FIX IS APPLE'S OWN FLAG, AND THE DESIGN MEASURED WHICH ONE.  Apple's ARM kernel is built
# `-msoft-float` (`external/xnu-hd2-darwin13/xnu/makedefs/MakeInc.def:377-379`, `CFLAGS_ARM`), and
# this project's pool is built `-mfpu=neon-vfpv4 -mfloat-abi=softfp` -- hard-float CODEGEN with a
# soft-float CALLING CONVENTION, the one combination that lets the vectorizer emit NEON into the
# kernel.  `docs/1021c-general-regs-design.md` MEASURED that `-mfloat-abi=softfp` is the responsible
# half (17 FP instructions on a kernel-shaped TU, 0 without it) and that `-mgeneral-regs-only` is
# **silently dropped by clang 14 on armv7** (identical `-S` output, an "argument unused" warning) --
# so `-mgeneral-regs-only` would have produced a pool byte-identical to today's under a record that
# read "NEON off", this project's most-repeated failure class.
#
# THE EDIT (tools/build_xnu_arm_kernel.sh, the two flag arrays CC_ARGS/CXX_ARGS, NOT in place): each
# array now expands `${FP_FLAGS[@]}` at the FP-flags position, where `FP_FLAGS` is
#   STAGE90_XNU_GENERAL_REGS=0  ->  (-mfpu=neon-vfpv4 -mfloat-abi=softfp)     == today's argv, word for word
#   STAGE90_XNU_GENERAL_REGS=1  ->  (-msoft-float)      == Apple's CFLAGS_ARM spelling, both FP flags dropped
# and the script REFUSES any other value.  **OFF IS BYTE-IDENTICAL BY MECHANISM, NOT BY ASSERTION**:
# with the switch at 0 the resolved word list is the pre-rung one (`--print-flags` prints it, and
# step 0 below reads it), and ON uses exactly the word list `docs/1021c` section 1.1 measured at 0 FP
# instructions, so the OFF build cannot differ from the pre-rung pool by a second, unmeasured flag
# spelling.  `-mfpu=neon-vfpv4` is KEPT beside `-msoft-float` (not dropped) because removing
# `-mfloat-abi` changes what `proc_reg.h`'s cp10 moves lower to: MEASURED on this host, the plain
# `-msoft-float` turns `mrc p10,7,%0,c8,c0,0` into a real FP-register instruction
# (`vmrs r0, fpexc`), i.e. it leaves 6.19 uses of the FP register file in exactly the one C family
# this pool has (18 `mrc` + 15 `mcr` sites today).  **MEASURED FALSE**: `-mfpu` does not change that lowering
# (it is a property of the assembler's reading of `mrc p10`, identical under every flag set), so `-mfpu` is
# inert and is DROPPED -- docs/1021c section 2.1.  `-mcpu=cortex-a15 -marm` are untouched.
#
# **WHY THIS RUNG MUST REBUILD THE POOL, AND WHY A GREEN ENTRY BUILD SAYS NOTHING.**  The pool's
# `.text` is INSIDE the `.bin` whose sha256 IS the arm, so the arm MOVES only if the POOL is
# recompiled -- `src/entry/build_entry.sh` merely LINKS `out/xnu_kernel_obj_d13/*.o` and
# `out/xnu_platform_obj_d13/*.o`.  A stale pool links Apple's NEON and every check downstream of the
# link agrees with it (`records/1020-build-defect.txt` is the precedent defect).  **THE COVERAGE IS
# ALL 282 FP OBJECTS THE IMAGE CONTAINS** (resolved by value, docs/1021c section 3.5, whose open item
# this build closes): `out/xnu_kernel_obj_d13/` is 705 objects (280 with FP) and
# `out/xnu_platform_obj_d13/` is 10 (2 with FP: `stage90_root_media.o`, `stage90_crypto_functions.o`),
# and BOTH are built by `tools/build_xnu_arm_kernel.sh` through the SAME two arrays.  So ONE edit to
# the two arrays covers every FP object in the image.  **`tools/build_xnu_arm_layer.sh` is NOT
# edited**: `out/xnu_arm_obj_d13/` is in NO link list (`build_entry.sh` says so explicitly), and its
# only two FP objects (`loose_ends.o`, `pmap_asid.o`) have same-named twins in the kernel pool that
# ARE linked.  **`scripts/xnu_arm_assemble.sh` is NOT edited**: `vfp.o` is the VFP save/restore AND
# the CPACR grant ITSELF, it must keep its flags, and the pool builder skips `*.s` (so a C/C++ flag
# cannot reach it by construction).
#
# **THE BY-VALUE CLAUSE (docs/1021c section 5).**  The switch's substance is a flag, so the only
# proof it reached the compile is a property of the OBJECTS: FP-instruction counts, read by
# `tools/check_d13_no_neon.py` (authored in parallel; if it is MISSING, the clause FAILS LOUDLY
# rather than skipping).  Three readings:
#   (1) the LINKED image's `mach_msg_send` -- the function whose `vld1.32` at 0x8047b0ac is 1020b's
#       kernel-mode undef, 4 FP instructions in `osfmk_ipc_mach_msg.o` today.  Read off
#       `out/stage90/xnu_arm_entry.elf`, not the `.o`.
#   (2) the WHOLE POOL DIRECTORY with `--min-objects <the manifest's own count>`, so a flag that
#       reached `mach_msg.o` and not `ip_dummynet.o` (whose `vpush {d8-d9}` faults identically) is a
#       refusal, and an ON arm over an EMPTY or STALE directory FAILS instead of passing vacuously
#       (docs/1021c section 5.3, the m1015b census defect).
#   (3) `--expect any` when the switch is OFF, which is the direction that proves the OFF arm still
#       has its FP -- a switch that silently reached the compile while the record said 0 would turn
#       OFF into ON.
# The clause lives in `src/entry/build_entry.sh` at the 1021d link site (beside 1020's), so a bare
# entry relink cannot skip it; this script calls the same tool again BEFORE the payload is embedded
# (step 6b), so the build refuses before it writes an artifact.
#
# **THE CARRIERS (docs/1021c section 4.2), all of them.**  The switch is POOL-side and FLAG-shaped:
#   1. env resolve + 0/1 refusal in `tools/build_xnu_arm_kernel.sh` (beside its switch array pattern);
#   2. the flag channel: `${FP_FLAGS[@]}` in BOTH `CC_ARGS` and `CXX_ARGS`.  **There is no `-D`** --
#      `XNU_KERNEL_EXTRA_DEFINES` carries `-D` only and is NOT this switch's channel;
#   3. `src/entry/build_entry.sh` resolve + 0/1 refusal (mirrors `UNDEF_RAISE` at :1648);
#   4. `ENTRY_ARM_KEYS` (the tail, so the arm-change refusal sees it);
#   5. the record resolver `case`;
#   6. the record echo, writing the RESOLVED `$GENERAL_REGS` (never `${STAGE90_XNU_GENERAL_REGS}`);
#   7. `scripts/preflight_boot_check.sh` `ENTRY_CFG_KEYS`;
#   8. the by-value clause (above);
#   9. **no `.s` carrier** -- the flag cannot reach assembly;
#  10. `out/stage90/xnu_arm_entry-arm-extra.txt`, written by THIS script, LAST, from the arm it just
#      built.  **1020b's `STAGE90_XNU_GLOBAL_FIX=1` line is re-emitted VERBATIM beside it**, because
#      1015b's fix is still in the pool, `scripts/park_1020b.sh` / `scripts/press_1020b.sh` grep that
#      file for that EXACT line (`grep -qx`), and a line surviving from a previous run is a
#      declaration of a DIFFERENT build ([[mi4-self-written-record-is-not-a-constraint]]).
set -euo pipefail
cd "$(dirname "$0")/.."

# ====================================================================================================
# ARGUMENTS.  `--resume-after-pool-build <log>` exists because the FIRST run of this script rebuilt
# BOTH pool directories FP-free (the expensive, ~8-minute half: `out/xnu_kernel_obj_d13` 705 objects
# and `out/xnu_platform_obj_d13` 10 objects) and then died at the step-5c platform clause on a FALSE
# failure -- the clause passed the kernel pool's `--min-objects 705` floor to a 10-object directory
# (fixed above by reading the floor from the seam's `POOL_PLATFORM_EXPECT`).  So the pool on disk IS
# this rung's own output.  A bare re-run cannot proceed: step 0's BEFORE census requires the pool to
# still carry FP (`--expect any`), and re-running the pool build would only recompile the same
# objects.  This mode therefore SKIPS the pool rebuild and reads the BEFORE census out of the first
# run's log **by value** (`step 0 (BEFORE): ... reads --expect any`) rather than re-taking it against
# a directory that this rung already converted -- which is the only honest reading of "before" once
# the transformation has already happened ([[mi4-measurement-defects]],
# [[mi4-out-stage90-is-shared-live-state]]).  Everything the arm's substance rests on is STILL read
# live against the objects on disk: 5a (trap.o + staged trap.c), 5b (the GLOBAL-FIX relocation clause),
# 5c (`--expect none` over BOTH directories), 5d (libgcc residue), 6b (the LINKED image's
# `mach_msg_send`) -- none of those is a "before" reading, so none is skipped.
RESUME_LOG=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --resume-after-pool-build) RESUME_LOG=$2; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt
POOL=out/xnu_kernel_obj_d13
PLATFORM_POOL=out/xnu_platform_obj_d13
MANIFEST=out/xnu_arm_manifest_d13.txt
NEON=out/stage90/xnu_arm_entry.elf
TOOL=tools/check_d13_no_neon.py

echo "== 1021d: stop the compiler emitting FP/NEON into the kernel pool (Apple's own -msoft-float) =="
echo "-- carries the previous arm's FULL switch set (read out of $REC, never a default) + ONE new switch STAGE90_XNU_GENERAL_REGS=1 --"

# ====================================================================================================
# 0. PRECONDITIONS: the by-value tool must exist AND pass its own selftest, and the resolved flag list
#    must read as this rung declares it.  A missing/skipping check is the defect class
#    [[mi4-a-claim-in-a-comment-is-not-a-check]]; a tool that exists but cannot fail is m1015b.
# ====================================================================================================
echo "-- step 0: the by-value tool and the flag seam are the two things this rung is read with --"
[[ -f $TOOL ]] || { echo "REFUSING: $TOOL does not exist. STAGE90_XNU_GENERAL_REGS's substance is a COMPILER FLAG LIST, so the pool's FP-instruction count is the only reading of whether it took, and there is no other check -- a build that skipped this would be a record that says 'NEON-free' over a pool full of vld1 (docs/1021c 4.2, [[mi4-a-claim-in-a-comment-is-not-a-check]]). CLI: <elf> --symbol <name> --expect none|any | --pool <obj-dir> --expect none|any [--min-objects N] | --selftest"; exit 1; }
python3 "$TOOL" --selftest >/dev/null || { echo "REFUSING: $TOOL --selftest did NOT pass. A checker whose own selftest fails cannot be the reading of this rung -- the m1015b lesson is a census that passed while the substance was absent because the check's spelling could not fail. Do not build 1021d until the tool's selftest is green"; exit 1; }
echo "   step 0: $TOOL --selftest PASSes"

# The flag seam. `--print-flags` prints the two arrays the compile loops expand, so OFF-identity is
# read from the resolved word list and not from this file's text.
PRINTED=$(STAGE90_XNU_GENERAL_REGS=1 XNU_TREE="$XNU_TREE" bash tools/build_xnu_arm_kernel.sh --print-flags) || {
  echo "REFUSING: tools/build_xnu_arm_kernel.sh --print-flags did not run -- the flag seam this rung's OFF-identity claim rests on is absent"; exit 1; }
printf '%s\n' "$PRINTED" | sed 's/^/   /'
ON_CC=$(printf '%s\n' "$PRINTED" | awk -F= '$1=="CC_ARGS"{sub(/^CC_ARGS=/,"");print}')
ON_CXX=$(printf '%s\n' "$PRINTED" | awk -F= '$1=="CXX_ARGS"{sub(/^CXX_ARGS=/,"");print}')
ON_MIN=$(printf '%s\n' "$PRINTED" | awk -F= '$1=="POOL_OBJ_EXPECT"{print $2}')
# The SECOND directory's floor, from the same seam. `out/xnu_platform_obj_d13` holds ten objects
# (`stage90_root_media.o`, `stage90_crypto_functions.o`, the platform expert, the pthread table, ...),
# which is what this image actually links -- NOT the kernel pool's 705. The first version of the
# step-5c platform clause below reused `$MIN_OBJ` (the pool floor) as its anti-vacuous-pass floor and
# therefore FAILED a directory that had legitimately been rebuilt FP-free. That is this project's
# `[[mi4-measurement-defects]]` shape in the checker's clothes: a correct reading rejected because the
# FLOOR was read from the wrong directory. The floor is the platform directory's own pre-build object
# count, printed by `--print-flags` as `POOL_PLATFORM_EXPECT`, read here and never typed.
ON_PLAT=$(printf '%s\n' "$PRINTED" | awk -F= '$1=="POOL_PLATFORM_EXPECT"{print $2}')
[[ -n $ON_PLAT && $ON_PLAT -gt 0 ]] || {
  echo "REFUSING: the flag seam printed POOL_PLATFORM_EXPECT='$ON_PLAT', so $PLATFORM_POOL had no .o files BEFORE this build and the step-5c platform census would pass over an empty directory -- the m1015b vacuous-pass shape (docs/1021c 5.3)"; exit 1; }
echo "   step 0: the platform census floor is $ON_PLAT object(s), read from the flag seam's POOL_PLATFORM_EXPECT (not the pool's own floor, which is read from the manifest below)"
case " $ON_CC " in *" -msoft-float "*) ;; *) echo "REFUSING: with STAGE90_XNU_GENERAL_REGS=1 the resolved CC_ARGS does not carry '-msoft-float' (got: $ON_CC) -- the FP flags did not reach the array this build compiles with, so the pool would be built with the FP register file available under a record that says it is not"; exit 1 ;; esac
# The ON list must also NOT still carry the hard-float pair: leaving `-mfpu=neon-vfpv4` beside the
# switch that says the FPU is unusable is the two-spellings shape (docs/1021c 2.1), and it is inert.
case " $ON_CC " in *"-mfpu=neon-vfpv4"*) echo "REFUSING: WITH STAGE90_XNU_GENERAL_REGS=1 the resolved CC_ARGS STILL carries -mfpu=neon-vfpv4 (got: $ON_CC) -- the ON list must be '-msoft-float' ALONE (docs/1021c 2.1): -mfpu is inert with -msoft-float and only reads as 'NEON is on' beside the switch that says it is off"; exit 1 ;; esac
case " $ON_CXX " in *" -msoft-float "*) ;; *) echo "REFUSING: with STAGE90_XNU_GENERAL_REGS=1 the resolved CXX_ARGS does not carry '-msoft-float' (got: $ON_CXX) -- the C++ half of the pool (83 files) would keep its NEON"; exit 1 ;; esac
case " $ON_CXX " in *"-mfpu=neon-vfpv4"*) echo "REFUSING: with STAGE90_XNU_GENERAL_REGS=1 the resolved CXX_ARGS STILL carries -mfpu=neon-vfpv4 (got: $ON_CXX) -- the ON list must be '-msoft-float' ALONE"; exit 1 ;; esac
# And the OFF half, read from the same seam rather than assumed: with the switch at 0 the word list
# must be EXACTLY the pre-rung one.
OFF_CC=$(XNU_TREE="$XNU_TREE" bash tools/build_xnu_arm_kernel.sh --print-flags | awk -F= '$1=="CC_ARGS"{sub(/^CC_ARGS=/,"");print}')
case " $OFF_CC " in
  *" -mfpu=neon-vfpv4 -mfloat-abi=softfp "*) ;;
  *) echo "REFUSING: with STAGE90_XNU_GENERAL_REGS=0 the resolved CC_ARGS is not the pre-rung flag list (got: $OFF_CC) -- OFF must be the exact pre-1021d argv, or the OFF arm is silently the ON arm and every 'the switch did nothing' reading is a lie ([[mi4-off-option-two-spellings]])"; exit 1 ;;
esac
case " $OFF_CC " in *"-msoft-float"*) echo "REFUSING: STAGE90_XNU_GENERAL_REGS=0 but the resolved CC_ARGS carries -msoft-float (got: $OFF_CC)"; exit 1 ;; esac
echo "   step 0: OFF resolves to '-mfpu=neon-vfpv4 -mfloat-abi=softfp' and ON to '-msoft-float' in BOTH arrays -- OFF is the pre-rung argv, word for word"

# The pool census floor: the manifest's own count of C/C++ sources that exist on disk, MEASURED at
# 705 for D13 (710 manifest C/C++ lines, 5 of them non-source outputs). Read, not typed.
XNU=$XNU_TREE
MIN_OBJ=$(grep -E '\.(c|cpp)$' "$MANIFEST" | while read -r p; do [[ -f $p ]] && printf '.\n'; done | wc -l)
[[ $MIN_OBJ -gt 0 ]] || { echo "REFUSING: $MANIFEST names no existing C/C++ source, so the pool census below would have no floor and an ON arm over an EMPTY $POOL would pass vacuously (docs/1021c 5.3)"; exit 1; }
echo "   step 0: the census floor is $MIN_OBJ object(s), read from $MANIFEST (not typed)"

# The PRE census: the pool on disk is the previous arm's (1020b). It MUST have FP -- if it does not,
# the 'before' reading is missing and the 'after' reading cannot be attributed to this rung.
PRE_NOTE=""
if [[ -n $RESUME_LOG ]]; then
  # The pool on disk is THIS rung's own output (the first run reached the pool build and died
  # after it), so `--expect any` cannot be re-taken.  Read the BEFORE census out of the first run's
  # log, by value, and refuse if that log does not carry it -- a resume over a log without the
  # reading would be exactly the vacuous comparison docs/1021c 5.3 refuses.
  [[ -f $RESUME_LOG ]] || { echo "REFUSING: --resume-after-pool-build '$RESUME_LOG' is not a file"; exit 1; }
  _pre_line=$(grep -m1 '^   step 0 (BEFORE): ' "$RESUME_LOG" || true)
  [[ -n $_pre_line ]] || { echo "REFUSING: $RESUME_LOG carries no 'step 0 (BEFORE):' line, so the BEFORE census cannot be read from it and a resume would compare the after-reading against nothing (docs/1021c 5.3)"; exit 1; }
  case "$_pre_line" in
    *"reads --expect any"*) ;;
    *) echo "REFUSING: $RESUME_LOG's BEFORE line does not read '--expect any' ($_pre_line) -- the pool the first run built over did not itself carry FP, so the after-reading cannot be attributed to this rung's flag"; exit 1 ;;
  esac
  PRE_NOTE="(from $RESUME_LOG) ${_pre_line#'   step 0 (BEFORE): '}"
  echo "   step 0 (BEFORE): read out of $RESUME_LOG, NOT re-taken on disk -- the pool on disk is this rung's OWN output:"
  echo "     $PRE_NOTE"
  echo "   (the after-census at 5c is a LIVE reading of the objects on disk; only this before-reading is from the log)"
elif [[ -d $POOL ]] && [[ -n $(ls "$POOL"/*.o 2>/dev/null) ]]; then
  PRE_NOTE=$(python3 "$TOOL" --pool "$POOL" --expect any --min-objects "$MIN_OBJ" 2>&1) || {
    echo "$PRE_NOTE" >&2
    echo "REFUSING: the pool on disk ($POOL, the previous arm's) does not read --expect any over $MIN_OBJ object(s) -- it is EMPTY, PARTIAL, or already FP/NEON-free. The 'after' census below is only a reading of THIS rung's flag change if the 'before' pool had FP in it; a stale or partial directory would make the comparison say nothing (docs/1021c 5.3)"; exit 1; }
  echo "   step 0 (BEFORE): $POOL reads --expect any over $MIN_OBJ object(s) -- $PRE_NOTE"
else
  echo "REFUSING: $POOL has no .o files -- there is no previous arm's pool to read the 'before' census off, and an ON arm over an empty directory is exactly the vacuous pass docs/1021c 5.3 refuses. Build 1020b first (this rung carries ITS switch set)"; exit 1
fi

# ====================================================================================================
# 1. CARRY THE PREVIOUS ARM'S FULL SWITCH SET -- never a default
#    ([[mi4-build-variant-comes-from-an-env-default]]), read out of the arm's own record.
# ====================================================================================================
[ -r "$REC" ] || { echo "FAIL: no arm record at $REC -- build 1020b first (this rung carries ITS set)"; exit 1; }
grep -q '^STAGE90_XNU_UNDEF_RAISE=1$' "$REC" || { echo "FAIL: $REC does not carry STAGE90_XNU_UNDEF_RAISE=1 -- this is not 1020b's arm, so 1021d would not be 1020b + one switch"; exit 1; }

EXPORTS=()
while IFS= read -r line; do
  line=${line%%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  k=${line%%=*}; v=${line#*=}
  [[ $v == '(unset)' ]] && continue
  case $k in
    STAGE90_XNU_ENTRY_SHA256|STAGE90_XNU_ENTRY_BYTES) continue ;;   # outputs, not inputs
    STAGE90_XNU_GENERAL_REGS) continue ;;                           # this rung's override, below
  esac
  EXPORTS+=("$k=$v")
done < "$REC"
EXPORTS+=("STAGE90_XNU_GENERAL_REGS=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")
echo "   carrying $((${#EXPORTS[@]} - 3)) switch(es) from $REC, overriding STAGE90_XNU_GENERAL_REGS=1"

# ---- the -D set (EXTRA == XNU_KERNEL_EXTRA_DEFINES; the 1015b pattern) ------------------------------
# A -D token for every carried `STAGE90_*` key whose value is 0/1 or a literal.  **STAGE90_XNU_GENERAL_REGS
# IS DELIBERATELY ABSENT**: its channel is the COMPILER FLAG LIST (the environment variable read by
# tools/build_xnu_arm_kernel.sh's FP_FLAGS resolve), not a `-D` -- docs/1021c 4.2.  A `-D` would be a
# define that no source reads, i.e. a second spelling of a switch whose substance is elsewhere.
EXTRA=""
while IFS= read -r line; do
  line=${line%%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  k=${line%%=*}; v=${line#*=}
  [[ $v == '(unset)' ]] && continue
  case $k in
    STAGE90_XNU_ENTRY_SHA256|STAGE90_XNU_ENTRY_BYTES) continue ;;
    STAGE90_XNU_HFS_ROOT_RW) continue ;;                             # env-carried tree-layout define
    STAGE90_XNU_GENERAL_REGS) continue ;;                            # FLAG-carried switch, not a -D
  esac
  [[ $k == STAGE90_* ]] || continue
  case $v in
    0|1) ;;
    0x*|[0-9]*) ;;
    *) continue ;;
  esac
  EXTRA="$EXTRA -D$k=$v"
done < "$REC"
# **1015b's GLOBAL-FIX TOKEN, WHICH HAS NO RECORD CARRIER -- 1020b's ONE REASON TO EXIST, CARRIED ON.**
# (records/1020-build-defect.txt.)  `STAGE90_XNU_GLOBAL_FIX` is a POOL switch that 1015b deliberately
# kept out of the record and out of EXPORTS; its ONLY channel is this EXTRA token.  A build that
# derives its EXTRA from the record alone compiles the fix OUT, the single-address retry storm returns,
# and pid 1 never reaches the frontier -- which is precisely what happened to 1020 and what 1020b
# repaired.  1021d is 1020b + ONE switch, so the token is re-emitted, and step 5b proves it by value.
EXTRA="$EXTRA -DSTAGE90_XNU_GLOBAL_FIX=1"
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 2. Stage the tree edits -- exactly build_1020b.sh's order.  **1021d edits NO tree file**: its
#    substance is a flag list, so there is no new `tools/stage_d13_*` driver and no new marker.  The
#    whole list is re-run so the pool is compiled from the SAME staged tree 1020b measured against.
echo "-- stage the tree edits (1021d adds NO driver: its substance is a compiler flag, not a tree edit) --"
tools/stage_d13_kextb_login.sh
tools/stage_d13_sp_align.sh
tools/stage_d13_vmhint_guard.sh
tools/stage_d13_commpage_alias.sh
tools/stage_d13_ios7_traps.sh
tools/stage_d13_dyld_witness.sh
tools/stage_d13_shreg_probe.sh
tools/stage_d13_shreg_wire.sh
tools/stage_d13_shreg_geom.sh
tools/stage_d13_timer_edge.sh
tools/stage_d13_usvc.sh
tools/stage_d13_submap_pmap_skip.sh
tools/stage_d13_flushwitness.sh
tools/stage_d13_globalfix.sh
tools/stage_d13_undef_raise.sh

# 3. Export the pool switches + the env-carried tree-layout define, BEFORE the pool/platform build.
echo "-- HFS (the port is on, hfs files join the pool) --"
export STAGE90_XNU_HFS=1
export STAGE90_XNU_SP_ALIGN=1
export STAGE90_XNU_IOS7_TRAPS=1
export STAGE90_XNU_RESIDENT=1
export STAGE90_XNU_USVC=1
export STAGE90_XNU_VMHINT_GUARD=1
export STAGE90_XNU_SUBMAP_PMAP_SKIP=1
export STAGE90_XNU_COMMPAGE_ALIAS=1
export STAGE90_XNU_DYLD_WITNESS=1
export STAGE90_XNU_SHREG_PROBE=1
export STAGE90_XNU_SHREG_WIRE=1
export STAGE90_XNU_SHREG_GEOM=1
export STAGE90_XNU_STALL=1
export STAGE90_XNU_CADENCE=1
export STAGE90_XNU_FLUSH_WITNESS=1
export STAGE90_XNU_KEXTB_LOGIN=1
export STAGE90_XNU_UNDEF_RAISE=1
# **1021d'S OWN SWITCH.  IT MUST BE EXPORTED, not only passed to build_entry.sh.**  Its substance is
# the flag list `tools/build_xnu_arm_kernel.sh` resolves from THIS environment variable into
# `FP_FLAGS`, which both `CC_ARGS` and `CXX_ARGS` expand -- so without the export the pool is
# recompiled with the OLD flags, the objects are rebuilt byte-identical, and NOTHING else in the
# build notices, because every artifact-derived check agrees with whatever the objects are.  That is
# the 1010 lesson (an absent effect whose channel was wrong).  The SAME value reaches build_entry.sh
# through EXPORTS, so the record and the flag list cannot be two spellings
# ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_GENERAL_REGS=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN STAGE90_XNU_SP_ALIGN=$STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD=$STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_SUBMAP_PMAP_SKIP=$STAGE90_XNU_SUBMAP_PMAP_SKIP STAGE90_XNU_COMMPAGE_ALIAS=$STAGE90_XNU_COMMPAGE_ALIAS STAGE90_XNU_IOS7_TRAPS=$STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_DYLD_WITNESS=$STAGE90_XNU_DYLD_WITNESS STAGE90_XNU_SHREG_PROBE=$STAGE90_XNU_SHREG_PROBE STAGE90_XNU_SHREG_WIRE=$STAGE90_XNU_SHREG_WIRE STAGE90_XNU_SHREG_GEOM=$STAGE90_XNU_SHREG_GEOM STAGE90_XNU_STALL=$STAGE90_XNU_STALL STAGE90_XNU_CADENCE=$STAGE90_XNU_CADENCE STAGE90_XNU_FLUSH_WITNESS=$STAGE90_XNU_FLUSH_WITNESS STAGE90_XNU_UNDEF_RAISE=$STAGE90_XNU_UNDEF_RAISE STAGE90_XNU_GENERAL_REGS=$STAGE90_XNU_GENERAL_REGS (pool DEFINES + the FP flag list + asm ASFLAGS + EXTRA agree) --"
export STAGE90_XNU_HFS_ROOT_RW=$(awk -F= '$1=="STAGE90_XNU_HFS_ROOT_RW"{print $2}' "$REC")
echo "-- STAGE90_XNU_HFS_ROOT_RW=$STAGE90_XNU_HFS_ROOT_RW (the pool's hfs_vfsops.o carries the rw clear) --"

# **THE TWO-CHANNEL PRECONDITION, ASSERTED AT THE SITE THAT ESTABLISHES IT.**  Unlike UNDEF_RAISE,
# this switch has ONE channel (the environment variable) and it is the FLAG LIST, not a `-D`.  Both
# halves are asserted: the environment variable is live AND the resolved arrays carry it -- the
# second is what a future edit that moved the append behind a branch would break, silently.
[[ -n ${STAGE90_XNU_GENERAL_REGS:-} ]] || { echo "FAIL: STAGE90_XNU_GENERAL_REGS is not exported - tools/build_xnu_arm_kernel.sh resolves its FP_FLAGS from that ENV variable, so without it the pool is recompiled with the OLD flags and the objects are byte-identical while the record says NEON-free (the 1010 lesson: an absent effect whose channel was wrong)"; exit 1; }
[[ "$EXTRA" == *"-DSTAGE90_XNU_GENERAL_REGS"* ]] && { echo "FAIL: -DSTAGE90_XNU_GENERAL_REGS is in EXTRA ('$EXTRA') - this switch's channel is the COMPILER FLAG LIST, not a -D (docs/1021c 4.2). A -D here is a second spelling of a switch whose substance is elsewhere, and no source reads it"; exit 1; }
echo "   the switch's channel is the flag list (env -> FP_FLAGS -> CC_ARGS/CXX_ARGS), and there is deliberately NO -D for it"

# 4. The asm layer (1021d touches NO asm file; the layer is kept consistent -- it reads the exported
#    asm switches above, and the C/C++ flag change CANNOT reach it, since the pool builder skips *.s).
echo "-- arm asm layer (994/999am2/1000: traps_lo.o + machine_routines_asm.o + cswitch.o; vfp.o is NOT touched) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# 5. The POOL build -- THE WHOLE POINT OF THIS RUNG: every C and C++ file the image links is
#    recompiled through the two arrays that now carry `-msoft-float`.
echo "-- the WHOLE D13 kernel pool + platform block, recompiled through CC_ARGS/CXX_ARGS with the FP flag list this rung changes --"
if [[ -n $RESUME_LOG ]]; then
  echo "   RESUME: SKIPPING the pool rebuild -- the first run rebuilt BOTH directories FP-free (its log"
  echo "   carries the after-census: 705/0/0 and 10/0/0) before dying at the step-5c platform clause."
  echo "   The objects on disk ARE this rung's output; clauses 5a/5b/5c/5d below read them LIVE."
else
  XNU_TREE="$XNU_TREE" \
  XNU_KERNEL_CONFIG=STAGE90_XNU \
  XNU_MASTER_LOCAL="$PWD/tools/xnu_config/boot/STAGE90_XNU.local" \
  XNU_KERNEL_EXTRA_DEFINES="$EXTRA" \
      ./tools/build_xnu_arm_kernel.sh
fi

# ====================================================================================================
# 5a. POOL-OBJECT EXISTENCE (the two objects 1020b's clauses read, so a truncated pool is caught).
# ====================================================================================================
tr_obj=$POOL/osfmk_arm_trap.o
[[ -f $tr_obj ]] || { echo "FAIL: $tr_obj does not exist - the pool build did not produce the object that carries 1020's moved \`return\` in sleh_undef"; exit 1; }
python3 tools/patch_d13_undef_raise.py "$XNU_TREE" --verify-off >/dev/null || { echo "FAIL: tools/patch_d13_undef_raise.py --verify-off did not PASS - the staged tree's ON/OFF shapes are not the ones this arm declares ([[mi4-off-option-two-spellings]])"; exit 1; }
grep -qF 'IOS7UNDEFRAISE_BEGIN' "$XNU_TREE/osfmk/arm/trap.c" || { echo "FAIL: the staged tree $XNU_TREE/osfmk/arm/trap.c carries NO IOS7UNDEFRAISE_BEGIN marker - the 1020 driver did not run, so this arm is NOT 1020b + one switch and the udf raise the whole ladder rests on is absent"; exit 1; }
echo "   POOL(5a): osfmk_arm_trap.o exists and the staged trap.c is 1020's text (IOS7UNDEFRAISE_BEGIN + --verify-off PASS)"

# ====================================================================================================
# 5b. **THE GLOBAL-FIX BY-VALUE CLAUSE -- KEPT VERBATIM FROM build_1020b.sh:245-253.**
# ====================================================================================================
# The 1020 pool rebuild DROPPED 1015b (records/1020-build-defect.txt): the retry storm returned and
# the udf was never reached.  `STAGE90_XNU_GLOBAL_FIX` has no record carrier, so a build that derives
# its `EXTRA` from the record alone compiles the fix OUT -- and NOTHING in the entry build notices,
# because `build_entry.sh` only LINKS the pool object.  **1021d is a POOL-REBUILD RUNG TOO**, so this
# clause is exactly as load-bearing here as it was in 1020b -- and 1021d re-emits the token above.
gf_obj=$POOL/osfmk_arm_pmap.o
[[ -f $gf_obj ]] || { echo "FAIL: $gf_obj is absent - the 1021d pool build did not produce the pmap object 1015b's fix lives in"; exit 1; }
_gf_kp=$(arm-none-eabi-objdump -r "$gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'MOVW_ABS_NC.*kernel_pmap' || true)
_gf_full=$(arm-none-eabi-objdump -r "$gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'R_ARM_CALL.*arm_tlb_flushID$' || true)
_gf_asid=$(arm-none-eabi-objdump -r "$gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'R_ARM_CALL.*arm_tlb_flushID_ASID$' || true)
echo "   POOL(5b): kernel_pmap materializations in pmap_flush_tlbs x$_gf_kp (want 3); arm_tlb_flushID x$_gf_full, arm_tlb_flushID_ASID x$_gf_asid (want 1/1)"
[[ "$_gf_kp" == "3" ]] || { echo "FAIL: pmap_flush_tlbs materializes kernel_pmap at $_gf_kp site(s), want EXACTLY 3 - 2 is OFF/Apple, i.e. STAGE90_XNU_GLOBAL_FIX is NOT in this pool object, so the retry storm 1015b killed is BACK and the run would stop on the storm, not on anything 1021d is about (records/1020-build-defect.txt). -DSTAGE90_XNU_GLOBAL_FIX=1 rides XNU_KERNEL_EXTRA_DEFINES ONLY (no DEFINES+= clause, no record carrier); build_entry.sh only LINKS this object, so the entry build is green either way"; exit 1; }
[[ "$_gf_full" == "1" && "$_gf_asid" == "1" ]] || { echo "FAIL: the flush arms are not the declared shape (full=$_gf_full asid=$_gf_asid, want 1/1) - 1015b keeps Apple's ASID arm in the text (unreachable for non-kernel pmaps) and tail-merges the full-flush sites into ONE bl"; exit 1; }
echo "   POOL(5b): pmap_flush_tlbs carries the 3rd kernel_pmap materialization (1015b's inserted 'else if (pmap != kernel_pmap)'), so the GLOBAL-FIX is IN this object"

# ====================================================================================================
# 5c. **THE 1021d CENSUS, BY VALUE, BOTH DIRECTIONS -- THIS RUNG'S OWN CLAUSE.**
# ====================================================================================================
# Before vs after, over the WHOLE pool directory AND the platform directory, with the manifest's own
# object count as the floor.  `--expect none` on the linked image's `mach_msg_send` is the same
# reading `build_entry.sh`'s 1021d clause makes; it is repeated here so the build refuses BEFORE the
# payload embeds the blob (step 6b below).
echo "-- POOL(5c): the FP/NEON census, AFTER the rebuild (BEFORE was: $PRE_NOTE) --"
POST_POOL=$(python3 "$TOOL" --pool "$POOL" --expect none --min-objects "$MIN_OBJ" 2>&1) || {
  echo "$POST_POOL" >&2
  echo "FAIL: $POOL does not read --expect none over $MIN_OBJ object(s) after the rebuild. STAGE90_XNU_GENERAL_REGS=1 is exported and the arrays carry '-msoft-float' (step 0 read the seam), so a non-zero count here means the pool was NOT recompiled this run, or was recompiled through a DIFFERENT flag list than the one step 0 printed. BEFORE this rebuild the same directory read: $PRE_NOTE"; exit 1; }
echo "   POOL(5c): $POOL reads --expect none -- $POST_POOL"
if [[ -n $(ls "$PLATFORM_POOL"/*.o 2>/dev/null) ]]; then
  POST_PLAT=$(python3 "$TOOL" --pool "$PLATFORM_POOL" --expect none --min-objects "$ON_PLAT" 2>&1) || {
    echo "$POST_PLAT" >&2
    echo "FAIL: $PLATFORM_POOL does not read --expect none over $ON_PLAT object(s) after the rebuild. These objects are built by tools/build_xnu_arm_kernel.sh through the SAME CC_ARGS/CXX_ARGS and ARE linked into the image ('stage90_root_media.o' and 'stage90_crypto_functions.o' carry FP today, MEASURED) - a flag that reached the pool and not this directory leaves FP in the image the payload boots. The floor is this directory's OWN pre-build count ($ON_PLAT, from the seam's POOL_PLATFORM_EXPECT), not the pool's $MIN_OBJ: a floor read from a different directory rejects a correct rebuild (measured, this rung's first run)"; exit 1; }
  echo "   POOL(5c): $PLATFORM_POOL reads --expect none over $ON_PLAT -- $POST_PLAT"
fi

# **5d. NEWLY-UNDEFINED `__aeabi_*` (design section 3.3, pre-registered cell C).**  Soft-float can
# make a TU that does REAL FP arithmetic call the libgcc soft-float helpers.  docs/1021c section 3.2
# measures that the pool has no FP TYPES, so none is expected -- but "expected" is not "checked", and
# a new `__aeabi_f*`/`__aeabi_d*` reference the link cannot resolve is a link error deep inside a
# 10000-line link command.  So: REPORT them, and verify `$LIBGCC` can in fact supply each one.  This
# is a report-plus-resolution-check, NOT a refusal on sight: a new `__aeabi_*` is a legitimate
# consequence of the flag, and the fix (already in place) is libgcc's `--start-group`.
echo "-- POOL(5d): soft-float libgcc residue (docs/1021c 3.2/3.3; today's pool references NONE) --"
_libgcc=$(arm-none-eabi-gcc -print-libgcc-file-name)
_aeabi_new=""
for o in "$POOL"/*.o "$PLATFORM_POOL"/*.o; do
  [[ -f $o ]] || continue
  _u=$(arm-none-eabi-nm --undefined-only "$o" 2>/dev/null | awk '{print $2}' | grep -E '^__aeabi_(f|d)[a-z0-9]*$' || true)
  [[ -n $_u ]] && _aeabi_new="$_aeabi_new$_u"$'\n'
done
_aeabi_new=$(printf '%s' "$_aeabi_new" | grep -c . || true)
echo "   POOL(5d): $_aeabi_new soft-float libgcc reference(s) across the pool + platform objects"
if [[ "$_aeabi_new" != "0" ]]; then
  _unresolved=""
  for s in $(arm-none-eabi-nm --undefined-only "$POOL"/*.o "$PLATFORM_POOL"/*.o 2>/dev/null | awk '{print $2}' | grep -E '^__aeabi_(f|d)[a-z0-9]*$' | sort -u); do
    arm-none-eabi-nm --defined-only "$_libgcc" 2>/dev/null | awk '{print $3}' | grep -qx "$s" || _unresolved="$_unresolved $s"
  done
  if [[ -n $_unresolved ]]; then
    echo "FAIL: soft-float introduced libgcc reference(s) that \$LIBGCC ($_libgcc) does NOT supply:$_unresolved -- the entry link would fail on them. This is docs/1021c 4.4's pre-registered cell C: the flag is right and the RUNTIME is short. Add the missing member (or the archive that carries it) to src/entry/build_entry.sh's LIBGCC group (:2483) and re-run"; exit 1
  fi
  echo "   POOL(5d): every one of them IS defined in $_libgcc, which the entry link already pulls through \`--start-group\` (build_entry.sh:2483) - no new runtime needed"
else
  echo "   POOL(5d): none, as docs/1021c section 3.2 predicted (the pool has no FP types) - the check ran, it did not skip"
fi

# 6. The ENTRY link.  build_entry.sh LINKS the freshly built pool objects and runs its OWN 1021d
#    by-value clause (the link site, beside 1020's).
echo "-- entry link (build_entry.sh reads the pool objects it just baked; GENERAL_REGS reaches it through EXPORTS and the record echo) --"
( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

# ====================================================================================================
# 6b. THE SAME CLAUSE, CALLED AGAIN HERE, **BEFORE THE PAYLOAD IS EMBEDDED**.
# ====================================================================================================
# build_entry.sh's clause runs during its link and would catch this, but the payload step below is
# what writes the artifact the press reads, and a refusal AFTER it would leave a blob on disk that is
# the ON arm's.  So the linked image is read here too -- same tool, same switch, same direction --
# and the build stops before anything is embedded if the reading is wrong.  (docs/1021c 5.1: the
# LINKED image, not the `.o`, because this build only LINKS the pool object -- records/1020-build-defect.txt.)
echo "-- by value (6b): the LINKED image's mach_msg_send must read --expect none, BEFORE the payload embeds it --"
python3 "$TOOL" "$NEON" --symbol mach_msg_send --expect none || { echo "FAIL: $TOOL $NEON --symbol mach_msg_send --expect none did NOT pass - the linked image's mach_msg_send still carries FP/NEON. The pool objects were rebuilt NEON-free (step 5c proves it) but the ENTRY image was linked from something else, or from a STALE object: build_entry.sh only LINKS $POOL/osfmk_ipc_mach_msg.o, so a green entry build says nothing about this half (records/1020-build-defect.txt)"; exit 1; }
echo "   6b PASS: the linked image's mach_msg_send carries 0 FP/NEON instructions - the function whose vld1.32 at 0x8047b0ac was 1020b's kernel-mode undef"

# 7. The PAYLOAD rebuild (embeds the new entry blob; preflight REFUSES a payload carrying a different
#    image).
echo "-- payload (embeds the new entry blob AND carries the 29s watchdog constant; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   (compare against 1020b's sha; a MATCH would mean the flag reached nothing)"
echo "-- what moved (the record must carry the switch -- the press gate reads THIS line) --"
grep -E 'STAGE90_XNU_GENERAL_REGS|STAGE90_XNU_UNDEF_RAISE' "$REC"

# **THE DECLARATION CARRIER, WRITTEN LAST, FROM THE ARM THIS BUILD COMPILED -- EXACTLY ONE LINE.**
# 1015b's GLOBAL_FIX is a POOL switch no writer in `src/entry/` echoes, so this drop-in is its ONE
# declaration; 1020b wrote it as `STAGE90_XNU_GLOBAL_FIX=1 role=1020b-globalfix`, and this build
# RE-EMITS THE SAME SWITCH under ITS OWN role tag.  The tag is not cosmetic: a line surviving from a
# previous run is a declaration OF A DIFFERENT BUILD that the press would read as proof of this one
# ([[mi4-self-written-record-is-not-a-constraint]]), so `scripts/park_1021d.sh` and
# `scripts/press_1021d.sh` (authored in parallel with this rung) require this file to hold EXACTLY
# the one 1021d line and REFUSE a file that still holds 1020b's -- which is why a TWO-line carrier
# here would be a build that no press on this rung could accept.  **1020b's own park/press are NOT
# re-runnable against this arm anyway** (their record key `STAGE90_XNU_UNDEF_RAISE=1` still matches,
# but the arm has moved): their carrier check is superseded by 1021d's, and the process check is a
# grep of a file a BUILD writes -- the PROOF that 1015b's fix is in the linked image is the BY-VALUE
# clause at step 5b (:$_gf_kp kernel_pmap materializations in `pmap_flush_tlbs`, read off the object
# the linker sees).
_b_extra=out/stage90/xnu_arm_entry-arm-extra.txt
printf 'STAGE90_XNU_GLOBAL_FIX=1 role=1021d-general-regs\n' > "$_b_extra"
echo "-- 1021d wrote the declaration carrier $_b_extra:"
sed 's/^/     /' "$_b_extra"

echo "== 1021d build DONE (1020b + Apple's -msoft-float across CC_ARGS/CXX_ARGS; pool census read --expect none at 5c, linked image at 6b). Not pressed."
echo "   Park it, then press. =="