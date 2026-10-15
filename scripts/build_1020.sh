#!/usr/bin/env bash
# Build 1020: KILL THE VFP FALSE POSITIVE -- a genuine `udf` must RAISE, not loop.
# 1018's switch set VERBATIM (carried out of the arm record) plus ONE new pool switch,
# STAGE90_XNU_UNDEF_RAISE=1.  Design: docs/1020-design.md.  Root cause: records/1019-rootcause.txt.
#
# WHY.  1019 root-caused the resident no-return (commit d8c350b): pid 1's `udf #254`
# (un-slid `pc=0x3823aa54` = libsystem_platform.dylib `__os_alloc_slow+0x108`) RE-EXECUTES FOREVER
# (8183x measured, invariant `pc`, no advance).  In `osfmk/arm/trap.c`'s `sleh_undef` user-mode arm the
# `if (instruction != 0xfedeffe7)` test is a STAND-IN for "is this a coprocessor/VFP instruction" (the
# code's own comment says so) and it takes an UNCONDITIONAL `return;` for EVERY other undefined
# instruction: VFP is enabled once and the handler returns with the PC still on the `udf`, so the same
# instruction faults again.  The port's OWN LEO machinery already assumes the two-trip shape
# (`ios7leo_dyld_matches` at `trap.c:84`, `ios7leo_dyld_repeat` at `:105`); this rung restores it.
#
# THE ARM (ONE line moved, ONE switch).  `STAGE90_XNU_UNDEF_RAISE` (default 0) moves `sleh_undef`'s
# user-mode `return;` INSIDE the `if (!thread->machine.vfp_enable)` block, so a genuine `udf` arriving
# with VFP ALREADY enabled FALLS THROUGH to the existing `printf` + `EXC_BAD_INSTRUCTION` + `doexception`
# instead of silently looping.  The gate is `defined()`-FIRST
# (`#if defined(STAGE90_XNU_UNDEF_RAISE) && STAGE90_XNU_UNDEF_RAISE`), so switch OFF compiles the file's
# pre-existing bytes exactly ([[mi4-off-option-two-spellings]]).  The tree text is staged by
# `tools/stage_d13_undef_raise.sh` -> `tools/patch_d13_undef_raise.py` (idempotent, marker-bounded,
# PROVES OFF byte-identity with real `clang -E -P` runs before writing).
#
# **THIS IS A POOL-REBUILD RUNG (the 1015b shape), NOT ENTRY-ONLY.**  `trap.c` is a KERNEL-POOL file
# (`out/xnu_kernel_obj_d13/osfmk_arm_trap.o`), and `build_entry.sh` only LINKS that object -- the pool's
# `.text` is INSIDE the `.bin` whose sha256 IS the arm, so the arm MOVES only if the POOL is rebuilt with
# the -D.  The linked image keeps Apple's bytes if the pool is stale -- a green entry build says NOTHING
# about this half.  Unlike 1015b's `pmap.c` edit, THIS edit introduces NO new symbol (it MOVES an existing
# `return`), so the pool-object proof is the STAGED-TREE `--check` (step 7) plus the LINKED-IMAGE by-value
# checker (step 10), not a relocation census.  A byte census here would be a guess dressed as a check.
#
# **THE TWO CARRIERS ([[mi4-one-value-two-definitions]]).**  `tools/build_xnu_arm_kernel.sh:872` gates its
# OWN `DEFINES+=(-DSTAGE90_XNU_UNDEF_RAISE=1)` on the ENV var `STAGE90_XNU_UNDEF_RAISE`, AND this script
# passes `-DSTAGE90_XNU_UNDEF_RAISE=1` through `XNU_KERNEL_EXTRA_DEFINES` (== `EXTRA`).  BOTH must carry
# it: the env half is what the gate reads, the `EXTRA` token is the pool channel.  The precondition after
# the export block asserts BOTH are live, with a FAIL message -- an absent effect whose channel was wrong
# is the 1010 lesson.
#
# ORDER OF THE POOL DRIVERS.  All twelve idempotent stage drivers run in build_1015b.sh's EXACT order,
# then `tools/stage_d13_undef_raise.sh` LAST (after globalfix).  NOTE: `stage_d13_dyld_witness.sh` ALSO
# edits `osfmk/arm/trap.c` -- the SAME pool file this rung edits -- but the two are INDEPENDENT
# marker-bounded edits (dyld's `IOS7WITNESS_*` markers vs this rung's `IOS7UNDEFRAISE_*` markers); that
# is exactly why `patch_d13_undef_raise.py` scans trap.c's conditional directives and REFUSES if the set
# of switches it evaluates ever grows beyond the ones it can see.
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 1020: a genuine \`udf\` must RAISE, not loop (sleh_undef's user-mode \`return;\` moved INSIDE the VFP-enable block) =="
echo "-- carries the previous arm's FULL switch set (read out of $REC, never a default) + ONE new pool switch STAGE90_XNU_UNDEF_RAISE=1 --"

# **CARRY THE PREVIOUS ARM'S FULL SWITCH SET** -- never a default
# ([[mi4-build-variant-comes-from-an-env-default]]).  Read every input key out of the arm's own record
# (the one file the press gate reads), skip the OUTPUT keys, override the one switch this rung sets.
[ -r "$REC" ] || { echo "FAIL: no arm record at $REC -- build 1018 first (this rung carries ITS set)"; exit 1; }

# ---- the ENV/EXPORTS set (the 1018 pattern) --------------------------------------------------------
EXPORTS=()
while IFS= read -r line; do
  line=${line%%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  k=${line%%=*}; v=${line#*=}
  [[ $v == '(unset)' ]] && continue
  case $k in
    STAGE90_XNU_ENTRY_SHA256|STAGE90_XNU_ENTRY_BYTES) continue ;;   # outputs, not inputs
    STAGE90_XNU_UNDEF_RAISE) continue ;;                             # this rung's override, below
  esac
  EXPORTS+=("$k=$v")
done < "$REC"
EXPORTS+=("STAGE90_XNU_UNDEF_RAISE=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")
echo "   carrying $((${#EXPORTS[@]} - 3)) switch(es) from $REC, overriding STAGE90_XNU_UNDEF_RAISE=1"

# ---- the -D set (EXTRA == XNU_KERNEL_EXTRA_DEFINES; the 1015b pattern) ------------------------------
# A -D token for EVERY carried `STAGE90_*` key whose value is 0/1 or a literal (integer/hex) -- the
# `STAGE90_XNU_HFS_ROOT_RW` tree-layout define is the ONE exception, because it is carried by the
# ENVIRONMENT variable of the same name (exported below), not by `-D` (build_1015b.sh's own note).
EXTRA=""
while IFS= read -r line; do
  line=${line%%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  k=${line%%=*}; v=${line#*=}
  [[ $v == '(unset)' ]] && continue
  case $k in
    STAGE90_XNU_ENTRY_SHA256|STAGE90_XNU_ENTRY_BYTES) continue ;;
    STAGE90_XNU_UNDEF_RAISE) continue ;;                             # re-added as the override below
    STAGE90_XNU_HFS_ROOT_RW) continue ;;                             # env-carried tree-layout define
  esac
  [[ $k == STAGE90_* ]] || continue
  case $v in
    0|1) ;;                             # a boolean switch
    0x*|[0-9]*) ;;                      # a literal (hex window / tick / probe counts)
    *) continue ;;                      # not a switch value we forward as -D
  esac
  EXTRA="$EXTRA -D$k=$v"
done < "$REC"
EXTRA="$EXTRA -DSTAGE90_XNU_UNDEF_RAISE=1"
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 1. Stage the tree edits -- all idempotent; build_1015b.sh's EXACT order, plus 1020's own driver LAST.
echo "-- stage the __LINKEDIT teardown guard (988) + SP-alignment (990) + memq_hint guard (991) + commpage alias + vector-table zeroing (992+993) + iOS 7 VM wire + -3 SWI (994) + dyld witness rev-3 (995/996) + 294/438 shared-region probe + 999 slide ladder + 32 B shared-region wire (997) + shared-region geometry + stack top (998) + timer-discriminator edge hooks (999 am2) + user-SVC frontier hook (1000) + submap pmap-enter skip (1001) + TLB-flush witness (1014) + GLOBAL-FIX arm (1015b) + the udf-raise fix (1020) --"
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
# 1020's ONE driver, LAST (after globalfix).  `trap.c` is the SAME pool file `stage_d13_dyld_witness.sh`
# touches, and both edits are INDEPENDENT marker-bounded regions (the dyld witness's `IOS7WITNESS_*`
# markers vs this rung's `IOS7UNDEFRAISE_*` markers) -- the patcher REFUSES on a displaced/duplicated
# anchor rather than half-applying, so either order COMPOSES, but only this one is TESTED.
tools/stage_d13_undef_raise.sh

# 2. Export the pool switches + the env-carried tree-layout define, BEFORE the pool/platform build.
echo "-- HFS (the port is on, hfs files join the pool) --"
export STAGE90_XNU_HFS=1
# the ASSEMBLY layer reads these (assemble_arm_layer.sh reads the environment, not the record), so they
# must be exported BEFORE assemble_arm_layer.sh runs.
export STAGE90_XNU_SP_ALIGN=1
export STAGE90_XNU_IOS7_TRAPS=1
export STAGE90_XNU_RESIDENT=1
export STAGE90_XNU_USVC=1
# the POOL reads these (build_xnu_arm_kernel.sh gates the DEFINE of the same name on the environment
# variable), so they must be exported BEFORE build_xnu_arm_kernel.sh runs ([[mi4-one-value-two-definitions]]).
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
# **1020's OWN switch.  It MUST be exported (not only in `EXTRA`): `build_xnu_arm_kernel.sh:872` gates the
# pool DEFINE on the environment variable, and `osfmk_arm_trap.o` (the object carrying the moved `return`)
# is compiled there.  Export it HERE, before the pool build below -- the SAME value reaches build_entry.sh
# through EXPORTS.**
export STAGE90_XNU_UNDEF_RAISE=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN STAGE90_XNU_SP_ALIGN=$STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD=$STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_SUBMAP_PMAP_SKIP=$STAGE90_XNU_SUBMAP_PMAP_SKIP STAGE90_XNU_COMMPAGE_ALIAS=$STAGE90_XNU_COMMPAGE_ALIAS STAGE90_XNU_IOS7_TRAPS=$STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_DYLD_WITNESS=$STAGE90_XNU_DYLD_WITNESS STAGE90_XNU_SHREG_PROBE=$STAGE90_XNU_SHREG_PROBE STAGE90_XNU_SHREG_WIRE=$STAGE90_XNU_SHREG_WIRE STAGE90_XNU_SHREG_GEOM=$STAGE90_XNU_SHREG_GEOM STAGE90_XNU_STALL=$STAGE90_XNU_STALL STAGE90_XNU_CADENCE=$STAGE90_XNU_CADENCE STAGE90_XNU_FLUSH_WITNESS=$STAGE90_XNU_FLUSH_WITNESS STAGE90_XNU_UNDEF_RAISE=$STAGE90_XNU_UNDEF_RAISE (pool DEFINES + asm ASFLAGS + EXTRA agree) --"
# The 905 B2 rw clear is a TREE-layout define carried by the ENVIRONMENT, not by `-D` (987's trap,
# [[mi4-one-value-two-definitions]]): the record's key and the tree's guard differ by one word.
export STAGE90_XNU_HFS_ROOT_RW=$(awk -F= '$1=="STAGE90_XNU_HFS_ROOT_RW"{print $2}' "$REC")
echo "-- STAGE90_XNU_HFS_ROOT_RW=$STAGE90_XNU_HFS_ROOT_RW (the pool's hfs_vfsops.o carries the rw clear) --"

# **THE TWO-CARRIER PRECONDITION, ASSERTED AT THE SITE THAT ESTABLISHES IT.**  Both carriers of
# STAGE90_XNU_UNDEF_RAISE must be live: the exported ENV var (`build_xnu_arm_kernel.sh:872`'s gate) AND
# the `EXTRA` token (the `XNU_KERNEL_EXTRA_DEFINES` pool channel).  If either is absent the pool object
# is Apple's bytes and the raise silently cannot happen ([[mi4-one-value-two-definitions]],
# [[mi4-a-claim-in-a-comment-is-not-a-check]]).
[[ -n ${STAGE90_XNU_UNDEF_RAISE:-} ]] || { echo "FAIL: STAGE90_XNU_UNDEF_RAISE is not exported - tools/build_xnu_arm_kernel.sh gates its own -DSTAGE90_XNU_UNDEF_RAISE=1 on that ENV variable, so WITHOUT it the pool object is the tree's ORIGINAL bytes and the raise silently cannot fire (the 1010 lesson: an absent effect whose channel was wrong)"; exit 1; }
[[ "$EXTRA" == *"-DSTAGE90_XNU_UNDEF_RAISE=1"* ]] || { echo "FAIL: -DSTAGE90_XNU_UNDEF_RAISE=1 is not in EXTRA ('$EXTRA') - the XNU_KERNEL_EXTRA_DEFINES pool path would not carry it, so the moved \`return\` would compile OUT of osfmk_arm_trap.o. Both carriers of the switch must be live ([[mi4-one-value-two-definitions]])"; exit 1; }

# 3. The asm layer (1020 touches NO asm file, but the layer is kept consistent -- it reads the exported
#    asm switches above).
echo "-- arm asm layer (994/999am2/1000: traps_lo.o + machine_routines_asm.o + cswitch.o; no 1020 asm edit) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# 4. The POOL build (the whole D13 kernel pool + platform block).  `trap.c` carries the moved `return`.
echo "-- the WHOLE D13 kernel pool + platform block (trap.c carries the udf-raise fix; osfmk_arm_trap.o is the object this rung moves) --"
XNU_TREE="$XNU_TREE" \
XNU_KERNEL_CONFIG=STAGE90_XNU \
XNU_MASTER_LOCAL="$PWD/tools/xnu_config/boot/STAGE90_XNU.local" \
XNU_KERNEL_EXTRA_DEFINES="$EXTRA" \
    ./tools/build_xnu_arm_kernel.sh

# ====================================================================================================
# 5. POOL-OBJECT ASSERTION (the 1015b "DEFINE MUST BE ASSERTED" pattern).
# ====================================================================================================
# The object must EXIST, and the STAGED TREE TEXT must be exactly this rung's (the patcher's own check).
# **NOTE, AND IT IS THE HONEST SCOPING**: unlike 1015b's `pmap.c` edit (which materializes `kernel_pmap`
# one extra time and so has an object-side fingerprint), THIS edit introduces NO new symbol -- it MOVES
# an existing `return` inside a block.  There is therefore NO stable object-side relocation/byte census
# to assert here, and inventing one would be a guess dressed as a check ([[mi4-measurement-defects]]).
# The by-value confidence comes from the STAGED-TREE check below PLUS the LINKED-IMAGE checker in step 8.
tr_obj=out/xnu_kernel_obj_d13/osfmk_arm_trap.o
[[ -f $tr_obj ]] || { echo "FAIL: $tr_obj does not exist - the pool build did not produce the object that carries the moved \`return\` in sleh_undef (trap.c is a KERNEL-POOL file; build_entry.sh only LINKS it)"; exit 1; }
# The staged-tree check.  The spec calls `tools/patch_d13_undef_raise.py "$XNU_TREE" --check`; the tool
# as authored exposes `--dry-run`/`--verify-off` and NOT (yet) `--check` (MEASURED: no `--check` token in
# the file).  Call `--check` when the tool grows it; otherwise require the staged MARKER in the tree AND
# the tool's own OFF-identity proof, so an UNSTAGED tree still fails this clause rather than sailing past.
if grep -q -- '--check' tools/patch_d13_undef_raise.py; then
  python3 tools/patch_d13_undef_raise.py "$XNU_TREE" --check >/dev/null || { echo "FAIL: tools/patch_d13_undef_raise.py --check did not PASS - the tree text of osfmk/arm/trap.c is NOT exactly this rung's staged chunk (re-run tools/stage_d13_undef_raise.sh). The patcher refuses on a displaced anchor rather than half-applying, so this is also what a WRONG DRIVER ORDER looks like"; exit 1; }
  echo "   POOL: tools/patch_d13_undef_raise.py --check PASSes (the staged trap.c is this rung's text)"
else
  grep -qF 'IOS7UNDEFRAISE_BEGIN' "$XNU_TREE/osfmk/arm/trap.c" || { echo "FAIL: the staged tree $XNU_TREE/osfmk/arm/trap.c carries NO \`IOS7UNDEFRAISE_BEGIN\` marker - tools/stage_d13_undef_raise.sh did not run (or the OFF proof left the tree reverted), so the compile below is NOT the rung's text and the raise is absent from osfmk_arm_trap.o. tools/patch_d13_undef_raise.py has no --check yet; this marker + the OFF-identity proof below stand in for it"; exit 1; }
  python3 tools/patch_d13_undef_raise.py "$XNU_TREE" --verify-off >/dev/null || { echo "FAIL: tools/patch_d13_undef_raise.py --verify-off did not PASS - the switch-OFF byte-identity proof of the staged chunk failed, so the ON/OFF shapes are not the ones this rung declares ([[mi4-off-option-two-spellings]])"; exit 1; }
  echo "   POOL: the staged tree carries the IOS7UNDEFRAISE_BEGIN marker and the OFF byte-identity proof PASSes (tools/patch_d13_undef_raise.py has no --check yet)"
fi
echo "   POOL: the object-side proof is the staged tree + the LINKED-image checker in the next steps (this edit adds NO new symbol, so there is no relocation census to read)"

# 6. The ENTRY link (build_entry.sh LINKS the pool objects, including the fresh osfmk_arm_trap.o; the
#    by-value clause at build_entry.sh:31344 refuses a link whose sleh_undef still returns before the raise).
echo "-- entry link (build_entry.sh reads the pool object it just baked; UNDEF_RAISE reaches it through EXPORTS and the record echo at :40251) --"
( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

# 7. The PAYLOAD rebuild (embeds the new entry blob; preflight REFUSES a payload carrying a different image).
echo "-- payload (embeds the new entry blob AND carries the 29s watchdog constant; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

# 8. The BY-VALUE checker on the LINKED image (written in parallel): with the switch ON, sleh_undef's
#    user arm must reach its `return` ONLY under the VFP-enable branch (walking the disassembly, never a
#    source grep -- [[mi4-linked-code-order-is-not-source-order]], [[mi4-a-claim-in-a-comment-is-not-a-check]]).
echo "-- by value: the LINKED image's sleh_undef must reach its user-mode \`return\` ONLY under the VFP-enable branch --"
python3 tools/check_d13_undef_raise.py out/stage90/xnu_arm_entry.elf --expect on || { echo "FAIL: tools/check_d13_undef_raise.py did not PASS on out/stage90/xnu_arm_entry.elf with --expect on - the linked image's sleh_undef DOES NOT carry the moved \`return\`. The pool object was rebuilt but a STALE one was LINKED, or the -D never reached the pool compile (build_entry.sh only LINKS out/xnu_kernel_obj_d13/osfmk_arm_trap.o, so a green entry build says nothing about this half)"; exit 1; }

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   (compare against the previous arm's sha; a MATCH would mean the switch reached nothing)"
echo "-- what moved (the record must carry the switch -- the press gate reads THIS line) --"
grep -E 'STAGE90_XNU_UNDEF_RAISE' "$REC"

echo "== 1020 build DONE. Not pressed. Park it, then press with scripts/press_1020.sh. =="