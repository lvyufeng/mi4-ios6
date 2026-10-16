#!/usr/bin/env bash
# Build 1022: 1021d PLUS THE mach_trap_table[15] ROW -- install `ios7lab_mach_vm_map_trap` as the
# handler the trap-15 `svc` dispatches to.  It adds NO new switch: it reuses STAGE90_XNU_IOS7_TRAPS,
# which is already the record key, already in the pool DEFINES, already in ASFLAGS and already in the
# entry EXPORTS (records/1022-trap15-plumbing.txt section 2, verified by value).
#
# WHY.  records/1021d-mmap-failure.txt proved the port defect by value: dyld's `_mach_vm_map` shim
# (`0x381c6618`) passes its guard, the fast-trap stub `0x381c5960` (`mvn ip,#14; svc #0x80`, so
# `ip == -15`) issues the syscall, and `swi_mach` finds `mach_trap_table[15] == mach_trap_table[0] ==
# kern_invalid` and calls `swi_mach_error` -> panic.  The row exists in exactly one place and is gated
# shut by `IOS7_COMPAT_PLATFORM` (`osfmk/kern/syscall_sw.c:126-130`), and the body it needs already
# exists (`osfmk/ipc/mach_kernelrpc.c:208-243`), gated shut by the same macro at `:59`.
#
# THE FIX SHAPE IS (B), THE NARROW SWITCH (docs/1022-mach-trap-15-design.md section 2), NOT (A).
# Widening `IOS7_COMPAT_PLATFORM` itself (`osfmk/arm/ios7_compat.h:5`) reaches 24 bare `#if` sites,
# one of which is `osfmk/mach/arm/vm_param.h:63` (`VM_MAX_ADDRESS` HALVES, 0x80000000 -> 0x40000000)
# and ~15 runtime `ios7_compat_vmabi_enabled()` call sites.  This rung widens exactly TWO gates in
# syscall_sw.c (the extern block + row 15) and ONE in mach_kernelrpc.c (the inner lab-adapter block),
# and swaps the map trap's runtime selector -- all through `tools/patch_d13_mach_trap15.py`.
#
# **THE RUNTIME SELECTOR IS LOAD-BEARING, NOT OPTIONAL (docs/1022 section 0.2).**
# `ios7_compat_vmabi_enabled()` is a compile-time 0 on CANCRO (`ios7_compat.h:11-21`), so a row
# installed WITHOUT the selector returns `MACH_SEND_INVALID_DEST == 0x10000003`, which is the shim's
# OWN sentinel (`osfmk/mach/message.h:692`): the shim's `cmp r4,r0; bne` would treat that as "the fast
# trap ran" and branch AWAY from `bl 0x381c7f74`, the MIG demux that works today.  So the map trap's
# early-out reads `ios7_traps_enabled()` (the selector traps -10/-12/-14 already read) when the switch
# is on, and the tree's own `ios7_compat_vmabi_enabled()` line otherwise.
#
# **THE BY-VALUE CLAUSE (docs/1022 section 3), AND WHY IT READS THE LINKED IMAGE.**  A mach trap's
# dispatch word is data, and `build_entry.sh` only LINKS the pool objects -- so the linked image is the
# only place a stale pool cannot hide (records/1020-build-defect.txt is the precedent defect).
# `tools/check_1022_trap15.py <elf> --expect on` reads `mach_trap_table` and `ios7lab_mach_vm_map_trap`
# off the section table and asserts row 15 == {arg_count 8, fn == &handler, name == the handler}.
# `--expect off` is the mirror (row 15 == entry 0's own {fn,name}), and the CURRENT image is its
# negative control: it reads `{fn=0x8047f5a4, name='kern_invalid'}` == entry 0, and `--expect on` on it
# REFUSES because the handler symbol is absent.
#
# **THE CARRIERS.**  The switch is already everywhere (plumbing record section 2); what this rung adds
# is the two pool-side `-D`s this build must set explicitly:
#   1. `export STAGE90_XNU_IOS7_TRAPS=1` BEFORE the pool build -- `tools/build_xnu_arm_kernel.sh:902`
#      reads that ENV variable into `DEFINES` (this is what actually compiles the two pool objects);
#   2. `EXTRA="$EXTRA -DSTAGE90_XNU_IOS7_TRAPS=1"` (the record→EXTRA carry loop does this already) AND
#      `-DSTAGE90_XNU_GLOBAL_FIX=1` (1015b's fix has NO record carrier -- records/1020-build-defect.txt
#      -- so a build that derives EXTRA from the record alone compiles the retry storm BACK IN).
#   3. `tools/check_d13_ios7_vmwire.py` MUST still apply cleanly, and its `DEFERRED_TRAPS` must read
#      `(24, 25, 41, 42)` -- 1022 opens row 15, so the 994 checker's deferral tuple drops 15 (and keeps
#      the other four, which are still unopened).  The build REFUSES if 15 is still in the tuple.  Rung
#      1023 drops 24 from the tuple in turn, so this rung's grep accepts EITHER spelling.
#      records/1023-patcher-composition.txt (the same "a total is not a property" lesson as the patch
#      assertion fixed there).
#   4. `out/stage90/xnu_arm_entry-arm-extra.txt`, written LAST by this script as EXACTLY one line:
#      `STAGE90_XNU_GLOBAL_FIX=1 role=1022-trap15` (grep -qx-able by the park/press scripts).
set -euo pipefail
cd "$(dirname "$0")/.."

# ====================================================================================================
# ARGUMENTS.  `--resume-after-pool-build <log>` skips the (expensive) pool rebuild and reads the
# BEFORE reading out of the first run's log by value -- the build_1021d.sh pattern.  The pool on disk
# is NOT read for a "before" census here (unlike 1021d): this rung's pool change is a source edit, not
# a flag, so the honest before-reading is the source state, which this script checks directly.
# ====================================================================================================
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
ELF=out/stage90/xnu_arm_entry.elf
CHECK=tools/check_1022_trap15.py
PATCH=tools/patch_d13_mach_trap15.py

echo "== 1022: install mach_trap_table[15] = ios7lab_mach_vm_map_trap (shape (B): the narrow switch) =="
echo "-- carries the previous arm's FULL switch set (read out of $REC, never a default); adds NO new switch --"

# ====================================================================================================
# 0. PRECONDITIONS: the by-value tool must exist AND pass its own selftest (a checker whose selftest
#    cannot fail is the m1015b lesson).  The staging patch must apply cleanly or already be staged.
# ====================================================================================================
echo "-- step 0: the by-value tool and the staging patch are the two things this rung is read with --"
[[ -f $CHECK ]] || { echo "REFUSING: $CHECK does not exist. The trap-15 row's substance is DATA in the linked image, so reading row 15's {arg_count, fn, name} off the section table is the only reading of whether the row installed, and there is no other check -- a build that skipped this would be a record over a row that is still kern_invalid ([[mi4-a-claim-in-comment-is-not-a-check]]). CLI: <elf> --expect on|off | --selftest"; exit 1; }
python3 "$CHECK" --selftest >/dev/null || { echo "REFUSING: $CHECK --selftest did NOT pass. A checker whose own selftest fails cannot be the reading of this rung (m1015b). Do not build 1022 until the tool's selftest is green"; exit 1; }
echo "   step 0: $CHECK --selftest PASSes"

[[ -f $PATCH ]] || { echo "REFUSING: $PATCH does not exist - the row and its body are gated shut on CANCRO and this script stages them through that patcher"; exit 1; }

# ====================================================================================================
# 1. CARRY THE PREVIOUS ARM'S FULL SWITCH SET -- never a default
#    ([[mi4-build-variant-comes-from-an-env-default]]), read out of the arm's own record.
# ====================================================================================================
[ -r "$REC" ] || { echo "FAIL: no arm record at $REC -- build 1021d first (this rung carries ITS set)"; exit 1; }
grep -q '^STAGE90_XNU_IOS7_TRAPS=1$' "$REC" || { echo "FAIL: $REC does not carry STAGE90_XNU_IOS7_TRAPS=1 -- this rung reuses that switch (NO new one); without it the row is not gated open"; exit 1; }
grep -q '^STAGE90_XNU_GENERAL_REGS=1$' "$REC" || { echo "FAIL: $REC does not carry STAGE90_XNU_GENERAL_REGS=1 -- this is not 1021d's arm, so 1022 would not be 1021d + the trap-15 row (1021d's -msoft-float must be preserved: dropping it brings the kernel-mode NEON undef back)"; exit 1; }
grep -q '^STAGE90_XNU_UNDEF_RAISE=1$' "$REC" || { echo "FAIL: $REC does not carry STAGE90_XNU_UNDEF_RAISE=1 -- this is not the 1020b-derived arm"; exit 1; }

EXPORTS=()
while IFS= read -r line; do
  line=${line%%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  k=${line%%=*}; v=${line#*=}
  [[ $v == '(unset)' ]] && continue
  case $k in
    STAGE90_XNU_ENTRY_SHA256|STAGE90_XNU_ENTRY_BYTES) continue ;;   # outputs, not inputs
  esac
  EXPORTS+=("$k=$v")
done < "$REC"
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")
echo "   carrying $(( ${#EXPORTS[@]} - 2 )) switch(es) from $REC (unmodified -- this rung adds no switch)"

# ---- the -D set (EXTRA == XNU_KERNEL_EXTRA_DEFINES; the 1015b pattern) ------------------------------
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
# **1015b's GLOBAL-FIX TOKEN, WHICH HAS NO RECORD CARRIER.**  `STAGE90_XNU_GLOBAL_FIX` is a POOL switch
# whose ONLY channel is this EXTRA token; a build that derives EXTRA from the record alone compiles the
# fix OUT and the retry storm returns (records/1020-build-defect.txt -- the 1020 defect 1020b repaired).
EXTRA="$EXTRA -DSTAGE90_XNU_GLOBAL_FIX=1"
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# ====================================================================================================
# 2. STAGE THE TREE EDITS.  The first three are this rung's; the vmwire one must still apply cleanly
#    (it owns the > 50 VM-wire adapters the trap-15 body sits beside and ios7_traps_enabled()).
# ====================================================================================================
echo "-- stage the tree edits --"
# **CHECK ORDER IS LOAD-BEARING.**  `patch_d13_ios7_vmwire.py`'s inner-guard close anchor is the
# trap-15 map trap's `done:` tail; this rung's patcher edits the SAME region (the selector) -- the two
# anchors do not overlap, and this assertion names that dependency rather than assuming it.
python3 tools/patch_d13_ios7_vmwire.py "$XNU_TREE" >/dev/null || { echo "FAIL: tools/patch_d13_ios7_vmwire.py refused or failed. This rung edits the SAME mach_kernelrpc.c region (the map trap's prologue/selector) and depends on vmwire's inner-guard close anchor being intact -- run the vmwire patcher first, or the tree is not in the state this rung was written against"; exit 1; }
grep -qF 'ios7_traps_enabled' "$XNU_TREE/osfmk/arm/ios7_compat.h" || { echo "FAIL: $XNU_TREE/osfmk/arm/ios7_compat.h carries no ios7_traps_enabled() -- vmwire's patcher did not stage its selector, and the trap-15 map trap's #if arm calls it"; exit 1; }
echo "   (vmwire's selector is staged: ios7_traps_enabled() is in ios7_compat.h)"

# **THE 994 CHECKER'S DEFERRAL TUPLE MUST DROP 15 -- or the entry build REFUSES by design.**
# tools/check_d13_ios7_vmwire.py check 6 asserts (in BOTH modes) that traps 15/24/25/41/42 all read
# kern_invalid; opening row 15 makes that fail.  1022 removes 15 and KEEPS the other four (they are
# still unopened), so this is a refusal, not a silent edit.
grep -qE '^DEFERRED_TRAPS = \(24, 25, 41, 42\)|^DEFERRED_TRAPS = \(25, 41, 42\)' tools/check_d13_ios7_vmwire.py || {
  echo "FAIL: tools/check_d13_ios7_vmwire.py's DEFERRED_TRAPS None of \`(24, 25, 41, 42)\` (1022's own"
  echo "      state) or \`(25, 41, 42)\` (1023's, which opens row 24) is in tools/check_d13_ios7_vmwire.py."
  echo "      1022 opens trap 15 (syscall_sw.c:126) and check 6 asserts the tuple's rows still read"
  echo "      kern_invalid in BOTH modes. 15 must be gone either way; 24 may be gone if 1023 has run."
  echo "      records/1022-trap15-plumbing.txt section 2, records/1023-patcher-composition.txt."
  exit 1; }
echo "   (check_d13_ios7_vmwire.py DEFERRED_TRAPS == (24, 25, 41, 42) or (25, 41, 42): trap 15 dropped, 24 dropped only if 1023 has run)"

# The staging patch itself: plan first (must refuse nothing on THIS tree), then apply; a second apply
# must be a no-op.  The patch's own OFF byte-identity proof runs inside --apply, BEFORE it writes.
echo "-- $PATCH --"
python3 "$PATCH" "$XNU_TREE" || { echo "FAIL: $PATCH (plan mode) refused - the tree is not in the state this rung was written against (a displaced anchor, a partial edit, or a hand-edited region). It writes nothing on a refusal"; exit 1; }
python3 "$PATCH" "$XNU_TREE" --apply || { echo "FAIL: $PATCH --apply refused. The OFF byte-identity proof (real preprocessor, both files) runs BEFORE the write, so a refusal here means an OFF arm does not reproduce HEAD byte for byte"; exit 1; }
python3 "$PATCH" "$XNU_TREE" --apply >/dev/null || { echo "FAIL: a second --apply of $PATCH did not succeed"; exit 1; }
python3 "$PATCH" "$XNU_TREE" --verify-off >/dev/null || { echo "FAIL: $PATCH --verify-off did not PASS on the staged tree - the ON/OFF shapes are not the ones this rung declares ([[mi4-off-option-two-spellings]])"; exit 1; }
grep -qF 'IOS7LAB trap15 frame' "$XNU_TREE/osfmk/ipc/mach_kernelrpc.c" || { echo "FAIL: the staged mach_kernelrpc.c carries NO \`IOS7LAB trap15 frame\` dump - the dispute-resolver (records/1022-trap15-adjudication.txt) is absent, so the press could not decide the trap-15 wire question"; exit 1; }
echo "   POOL: the staged tree carries the trap-15 dump and the OFF byte-identity proof PASSes"

# ====================================================================================================
# 3. Export the pool switches + the env-carried tree-layout define, BEFORE the pool/platform build.
# ====================================================================================================
echo "-- HFS (the port is on, hfs files join the pool) --"
export STAGE90_XNU_HFS=1
export STAGE90_XNU_SP_ALIGN=1
# **THIS RUNG'S CHANNEL 1: the env variable tools/build_xnu_arm_kernel.sh:902 reads into DEFINES.**
# Without the export the two pool objects are recompiled with the OLD `#if` and the row stays
# kern_invalid -- and NOTHING in the entry build notices, because build_entry.sh only LINKS the pool.
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
# **1021d'S FLAG SWITCH, CARRIED UNCHANGED.**  Its channel is the FP flag list
# (tools/build_xnu_arm_kernel.sh's FP_FLAGS), NOT a `-D`; dropping it brings the kernel-mode NEON undef
# back (records/1021-fpu-rootcause.txt).  This rung is 1021d + the row, so it is re-exported.
export STAGE90_XNU_GENERAL_REGS=1
echo "-- STAGE90_XNU_IOS7_TRAPS=$STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_GENERAL_REGS=$STAGE90_XNU_GENERAL_REGS STAGE90_XNU_UNDEF_RAISE=$STAGE90_XNU_UNDEF_RAISE STAGE90_XNU_SHREG_WIRE=$STAGE90_XNU_SHREG_WIRE (pool DEFINES + the FP flag list + asm ASFLAGS + EXTRA agree) --"
export STAGE90_XNU_HFS_ROOT_RW=$(awk -F= '$1=="STAGE90_XNU_HFS_ROOT_RW"{print $2}' "$REC")
echo "-- STAGE90_XNU_HFS_ROOT_RW=$STAGE90_XNU_HFS_ROOT_RW (the pool's hfs_vfsops.o carries the rw clear) --"

# **THE TWO-CHANNEL PRECONDITION, ASSERTED AT THE SITE THAT ESTABLISHES IT** (build_1020b.sh:193 style).
# `STAGE90_XNU_IOS7_TRAPS` has TWO carriers for the pool (the env variable read by the script, and the
# `-D` in EXTRA), and BOTH must be live or the two spellings can diverge
# ([[mi4-one-value-two-definitions]]).
[[ -n ${STAGE90_XNU_IOS7_TRAPS:-} ]] || { echo "FAIL: STAGE90_XNU_IOS7_TRAPS is not exported - tools/build_xnu_arm_kernel.sh:902 reads that ENV variable into DEFINES, so without it the two pool objects are recompiled with the OLD #if and the row stays kern_invalid while the record says the switch is 1 (the 1010 lesson: an absent effect whose channel was wrong)"; exit 1; }
[[ "$EXTRA" == *"-DSTAGE90_XNU_IOS7_TRAPS=1"* ]] || { echo "FAIL: -DSTAGE90_XNU_IOS7_TRAPS=1 is NOT in EXTRA ('$EXTRA') - the two pool objects (osfmk_kern_syscall_sw.o, osfmk_ipc_mach_kernelrpc.o) are compiled from BOTH channels, and a build that sets only one is the records/1020-build-defect.txt shape"; exit 1; }
[[ "$EXTRA" == *"-DSTAGE90_XNU_GLOBAL_FIX=1"* ]] || { echo "FAIL: -DSTAGE90_XNU_GLOBAL_FIX=1 is NOT in EXTRA ('$EXTRA') - 1015b's fix has NO record carrier (records/1020-build-defect.txt); without this token the retry storm returns and the run stops on the storm, not on anything 1022 is about"; exit 1; }
echo "   the switch's two pool channels (env -> DEFINES, EXTRA -> -D) are BOTH live, and GLOBAL_FIX is in EXTRA"

# 4. The asm layer.  1022 touches NO asm file (traps_lo.s's swi_mach addresses the table generically,
#    so installing a row is a DATA change) -- the layer is kept consistent and its OFF is byte-identical
#    by construction.
echo "-- arm asm layer (1022 touches NO .s file: the trap table is addressed generically) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# 5. The POOL build -- the change is in two POOL .c files, so the pool MUST be rebuilt or the linked
#    image keeps the old row (build_entry.sh only LINKS the objects; records/1020-build-defect.txt).
echo "-- the D13 kernel pool + platform block, recompiled (the trap-15 row and its body live in POOL files) --"
if [[ -n $RESUME_LOG ]]; then
  echo "   RESUME: SKIPPING the pool rebuild -- the first run rebuilt the pool before dying later."
  echo "   Clauses 5a/5b below read the objects on disk LIVE."
else
  XNU_TREE="$XNU_TREE" \
  XNU_KERNEL_CONFIG=STAGE90_XNU \
  XNU_MASTER_LOCAL="$PWD/tools/xnu_config/boot/STAGE90_XNU.local" \
  XNU_KERNEL_EXTRA_DEFINES="$EXTRA" \
      ./tools/build_xnu_arm_kernel.sh
fi

# ====================================================================================================
# 5a. THE OBJECT-SIDE ASSERTIONS (docs/1022 section 3.5).  Two stale-pool guards, one per object.
# ====================================================================================================
SSW_OBJ=$POOL/osfmk_kern_syscall_sw.o
RPC_OBJ=$POOL/osfmk_ipc_mach_kernelrpc.o
echo "-- POOL(5a): the two objects the row and its body live in --"
[[ -f $SSW_OBJ ]] || { echo "FAIL: $SSW_OBJ is absent - the pool build did not produce syscall_sw.o (the table the row lives in)"; exit 1; }
[[ -f $RPC_OBJ ]] || { echo "FAIL: $RPC_OBJ is absent - the pool build did not produce mach_kernelrpc.o (the row's body)"; exit 1; }

# (a) mach_kernelrpc.o must DEFINE ios7lab_mach_vm_map_trap as a `T`.  Today it does not -- the lab
#     bodies are gated out -- so this symbol's appearance IS this rung's own fingerprint.
arm-none-eabi-nm "$RPC_OBJ" | awk '$3=="ios7lab_mach_vm_map_trap" && $2=="T"{f=1} END{exit !f}' || {
  echo "FAIL: $RPC_OBJ does not DEFINE ios7lab_mach_vm_map_trap as T -- the trap-15 body was not compiled. Both gates must open (mach_kernelrpc.c:59 inner + :39 file-wide) AND -DSTAGE90_XNU_IOS7_TRAPS=1 must reach this compile; a stale object from before the patch looks exactly like this (records/1020-build-defect.txt)"; exit 1; }
echo "   POOL(5a): $RPC_OBJ defines T ios7lab_mach_vm_map_trap (the body compiled in)"

# (b) syscall_sw.o must carry a relocation to ios7lab_mach_vm_map_trap AT THE ROW-15 FN SLOT.
#     **The offset is RESOLVED BY ARITHMETIC from the object's own table, never typed**: the object's
#     relocations are relative to `.rodata`'s base, and this build reads that base and asserts the
#     offset equals 15*12+4 ([[mi4-measurement-defects]] -- the 0xB8 quoted in 1021b section 3.5 is for
#     the PRE-rung object).
python3 - "$SSW_OBJ" <<'PY'
import re, subprocess, sys
obj = sys.argv[1]
h = subprocess.run(["arm-none-eabi-objdump", "-h", obj], capture_output=True, text=True).stdout
base = None
for line in h.splitlines():
    m = re.match(r"\s*\d+\s+(\.rodata)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)", line)
    if m:
        base = int(m.group(4), 16)      # VMA of .rodata (relocatable, so 0)
r = subprocess.run(["arm-none-eabi-objdump", "-r", obj], capture_output=True, text=True).stdout
sec, hits = None, []
for line in r.splitlines():
    m = re.match(r"RELOCATION RECORDS FOR \[(\S+)\]", line)
    if m:
        sec = m.group(1)
        continue
    m = re.match(r"([0-9a-f]+)\s+(\S+)\s+(\S+)", line.strip())
    if m and sec == ".rodata" and "ios7lab_mach_vm_map_trap" in m.group(3):
        hits.append((int(m.group(1), 16), m.group(3)))
want = 15 * 12 + 4
if base is None:
    print(f"FAIL: {obj} has no .rodata section - cannot resolve the row-15 offset by arithmetic"); sys.exit(1)
if not hits:
    print(f"FAIL: {obj}'s .rodata carries NO relocation to ios7lab_mach_vm_map_trap - the row-15 fn word still points at kern_invalid; the syscall_sw.c:126 gate did not open for this compile"); sys.exit(1)
offs = [o - base for o, _ in hits]
if want not in offs:
    print(f"FAIL: {obj}'s .rodata references ios7lab_mach_vm_map_trap at offset(s) {[hex(o) for o in offs]} (base {base:#x}), none at 15*12+4 == {hex(want)} - the reference is not in the row-15 fn slot"); sys.exit(1)
print(f"   POOL(5a): {obj}'s .rodata relocates ios7lab_mach_vm_map_trap at offset {hex(want)} == 15*12+4 (resolved from the object's own .rodata base {base:#x}, not typed)")
PY

# (c) the staged tree carries the row's widened gate and the deferred four keep their bare gate.
[[ $(grep -c 'IOS7_COMPAT_PLATFORM || (defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS)' "$XNU_TREE/osfmk/kern/syscall_sw.c") == 2 ]] || { echo "FAIL: $XNU_TREE/osfmk/kern/syscall_sw.c does not carry EXACTLY 2 widened gates (the extern block + row 15)"; exit 1; }
[[ $(grep -c '^#if IOS7_COMPAT_PLATFORM$' "$XNU_TREE/osfmk/kern/syscall_sw.c") == 3 ]] || { echo "FAIL: $XNU_TREE/osfmk/kern/syscall_sw.c does not carry EXACTLY 3 bare gates (rows 24/25/41/42) - the deferred rows must stay closed"; exit 1; }
echo "   POOL(5c): syscall_sw.c has 2 widened gates (extern + row 15) and 3 bare gates (rows 24/25/41/42)"

# ====================================================================================================
# 5b. THE GLOBAL-FIX BY-VALUE CLAUSE -- KEPT FROM build_1021d.sh:365-373 (this is a POOL-rebuild rung).
# ====================================================================================================
gf_obj=$POOL/osfmk_arm_pmap.o
[[ -f $gf_obj ]] || { echo "FAIL: $gf_obj is absent - the 1022 pool build did not produce the pmap object 1015b's fix lives in"; exit 1; }
_gf_kp=$(arm-none-eabi-objdump -r "$gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'MOVW_ABS_NC.*kernel_pmap' || true)
_gf_full=$(arm-none-eabi-objdump -r "$gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'R_ARM_CALL.*arm_tlb_flushID$' || true)
_gf_asid=$(arm-none-eabi-objdump -r "$gf_obj" 2>/dev/null | awk '$1>="00000418" && $1<="00000640"' | grep -c 'R_ARM_CALL.*arm_tlb_flushID_ASID$' || true)
echo "   POOL(5b): kernel_pmap materializations in pmap_flush_tlbs x$_gf_kp (want 3); arm_tlb_flushID x$_gf_full, arm_tlb_flushID_ASID x$_gf_asid (want 1/1)"
[[ "$_gf_kp" == "3" ]] || { echo "FAIL: pmap_flush_tlbs materializes kernel_pmap at $_gf_kp site(s), want EXACTLY 3 -- 2 is OFF/Apple, i.e. STAGE90_XNU_GLOBAL_FIX is NOT in this pool object, so the retry storm 1015b killed is BACK and the run would stop on the storm, not on anything 1022 is about (records/1020-build-defect.txt). -DSTAGE90_XNU_GLOBAL_FIX=1 rides XNU_KERNEL_EXTRA_DEFINES ONLY"; exit 1; }
[[ "$_gf_full" == "1" && "$_gf_asid" == "1" ]] || { echo "FAIL: the flush arms are not the declared shape (full=$_gf_full asid=$_gf_asid, want 1/1)"; exit 1; }
echo "   POOL(5b): pmap_flush_tlbs carries the 3rd kernel_pmap materialization (1015b's fix is IN this object)"

# Also re-verify the 1020 raise and 1021d flags are still in the staged tree / objects.
python3 tools/patch_d13_undef_raise.py "$XNU_TREE" --verify-off >/dev/null || { echo "FAIL: tools/patch_d13_undef_raise.py --verify-off did not PASS - the udf raise this whole ladder rests on is not in the staged tree"; exit 1; }
[[ -f $POOL/osfmk_arm_trap.o ]] || { echo "FAIL: $POOL/osfmk_arm_trap.o is absent - the pool build did not produce 1020's object"; exit 1; }
echo "   POOL(5b): the 1020 udf-raise and 1021d's flag arm are both still staged"

# ====================================================================================================
# 6. The ENTRY link.  build_entry.sh LINKS the freshly built pool objects and runs its OWN 1022 clause.
# ====================================================================================================
echo "-- entry link (build_entry.sh reads the pool objects it just baked) --"
( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

# ====================================================================================================
# 6b. THE BY-VALUE CLAUSE, CALLED AGAIN HERE, **BEFORE THE PAYLOAD IS EMBEDDED** -- so a refusal does
#     not leave a blob on disk that is the ON arm's.  Read off the LINKED image, not the `.o`.
# ====================================================================================================
echo "-- by value (6b): the LINKED image's mach_trap_table[15] must read --expect on, BEFORE the payload --"
python3 "$CHECK" "$ELF" --expect on || { echo "FAIL: $CHECK $ELF --expect on did NOT pass (see the FAIL lines above) - the linked row 15 is not the trap-15 handler. The pool objects may have been rebuilt (step 5a proves the symbol and the relocation), but the ENTRY image was linked from something else or from a STALE object: build_entry.sh only LINKS $POOL/osfmk_kern_syscall_sw.o (records/1020-build-defect.txt)"; exit 1; }
echo "   6b PASS: mach_trap_table[15] == &ios7lab_mach_vm_map_trap in the linked image (arg_count 8, name word intact)"

# 7. The PAYLOAD rebuild (embeds the new entry blob; preflight REFUSES a payload carrying a different
#    image).
echo "-- payload (embeds the new entry blob; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   (compare against 1021d's ee2dd4fb; a MATCH would mean the row reached nothing)"
echo "-- what the record carries (the press gate reads the RECORD; this rung adds no key) --"
grep -E 'STAGE90_XNU_IOS7_TRAPS|STAGE90_XNU_GENERAL_REGS|STAGE90_XNU_UNDEF_RAISE' "$REC"

# **THE DECLARATION CARRIER, WRITTEN LAST, FROM THE ARM THIS BUILD COMPILED -- EXACTLY ONE LINE.**
# 1015b's GLOBAL_FIX is a POOL switch no writer in `src/entry/` echoes, so this drop-in is its ONE
# declaration; its content is a declaration OF A DIFFERENT BUILD the moment the arm moves
# ([[mi4-self-written-record-is-not-a-constraint]]), so the 1022 line REPLACES 1021d's -- a surviving
# 1021d line would be a press reading a previous run's claim.
_b_extra=out/stage90/xnu_arm_entry-arm-extra.txt
printf 'STAGE90_XNU_GLOBAL_FIX=1 role=1022-trap15\n' > "$_b_extra"
echo "-- 1022 wrote the declaration carrier $_b_extra:"
sed 's/^/     /' "$_b_extra"

echo "== 1022 build DONE (1021d + mach_trap_table[15] = ios7lab_mach_vm_map_trap; object side at 5a, linked image at 6b). Not pressed."
echo "   Park it, then press -- but see docs/1022-mach-trap-15-design.md section 5.1: do NOT press 1022 until 1021d's capture exists. =="