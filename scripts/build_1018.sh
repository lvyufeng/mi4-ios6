#!/usr/bin/env bash
# Build 1018: THE CAP-EXEMPT UNDEF WITNESS -- the 1015b switch set VERBATIM plus ONE new switch,
# STAGE90_XNU_UNDEF_DIRECT=1.  Design: docs/1018-design.md.  Recon: records/1018-recon.txt (a7bafd7).
#
# WHY.  1017 (`docs/1017-shared-cache-naming.md`) named, statically and by value, the instruction pid
# 1's frame sits on: un-slid VA 0x3823aa54 = libsystem_platform.dylib `__os_alloc_slow`+0x108 = the
# Thumb `udf #254` (bytes fede), reached ONLY when `_mach_vm_map` returns non-zero.  1017 then wrote
# `# UNMEASURED` for the mechanism - does the undefined-instruction handler RETURN and re-trap, or does
# an outer retry re-enter the failure path?
#
# RECON FOUND WHY THE CAPTURE COULD NOT ANSWER IT, AND IT WAS THE INSTRUMENT, NOT THE DEVICE.  On the
# recovered 1015b capture (`out/stage90/captures/recovered-unknown-arm-20261015-163856-last_kmsg.txt`):
#   capture :29963   xnu_live_capped=0x00006000            (0x6000 = 24576 = ENTRY_LIVE_CAP, entry_stubs.c:4254)
#   capture :28988   xnu_live_sleh_upc=0x804975c0          (the LAST entry_live_write record)
#   capture :33977   xnu_live_stall_pc=0x3b04aa54          (the frontier -- ~4000 records LATER)
# `__wrap_sleh_undef` (`entry_trace.c:5537`) writes `entry_live_write` (the CAPPED channel); it fired
# ZERO times.  Not because no `udf` happened -- because by then its channel was exhausted and every
# record after `xnu_live_capped` was dropped.  `_stall_*`/`_usvc_*`/`_abt_*` ride `entry_write_kv`
# (the DIRECT channel) and reached the frontier; the two recorders that CARRY A FRAME did not.
#
# THE ARM (one switch, one key move).  With STAGE90_XNU_UNDEF_DIRECT=1, `__wrap_sleh_undef`'s six keys
# (`_seq`, `_user_seq`, `_pc`, `_lr`, `_spsr`, `_user`) route through `entry_write_kv` instead of
# `entry_live_write`, so the witness survives the cap.  `UNDEF_LIVE_MAX` widens 4u -> 64u on the direct
# path only, bounded by the console's own `0x00200000u - 12u` (entry_stubs.c:4019).  OFF is
# byte-identical (a `#if`/`#else` around the two macro definitions, never a widened constant).
#
# **THIS IS ENTRY-TOOLCHAIN ONLY.**  The recorder and its one call site are both in `entry_trace.c`,
# which `build_entry.sh` compiles in its `ENTRY_TRACE` block, so there is NO kernel-pool edit, NO
# `--wrap` change, NO `external/` source change, NO `tools/stage_d13_*` script.  The one structural
# difference from 1015b (which MOVES `osfmk/arm/pmap.c`, a pool object) is that nothing here moves the
# pool -- but the entry objects DO move (the six call sites change target), so the ARM HASH still moves
# and the by-value clause in `build_entry.sh` (:31238-31293) reads the property off the object this
# build compiled AND the linked image.
#
# The switch reaches `entry_trace.c` through `STUB_DEFINES_TRACE` (build_entry.sh:1558), NOT
# `STUB_DEFINES`: that is the array `entry_trace.c`'s own compile line reads, and putting the `-D` in
# the other one would leave the recorder on the capped channel while the build reported the switch on.
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 1018 step 1: rebuild the entry image with STAGE90_XNU_UNDEF_DIRECT=1 =="
echo "   (entry bin MOVES: the six undef keys change which writer they call)"

# **CARRY THE PREVIOUS ARM'S FULL SWITCH SET** -- never a default
# ([[mi4-build-variant-comes-from-an-env-default]]).  Read every input key out of the arm's own
# record (the one file the gate reads), skip the OUTPUT keys, and override the one switch this rung
# sets.  The nine-cost trap: a `STAGE90_*` left unset silently takes the tree default, which is a
# DIFFERENT arm than the record names.
[ -r "$REC" ] || { echo "FAIL: no arm record at $REC -- build 1015b first (this rung carries ITS set)"; exit 1; }
EXPORTS=()
while IFS= read -r line; do
  line=${line%%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  k=${line%%=*}; v=${line#*=}
  [[ $v == '(unset)' ]] && continue
  case $k in
    STAGE90_XNU_ENTRY_SHA256|STAGE90_XNU_ENTRY_BYTES) continue ;;   # outputs, not inputs
    STAGE90_XNU_UNDEF_DIRECT) continue ;;                            # this rung's override, below
  esac
  EXPORTS+=("$k=$v")
done < "$REC"
EXPORTS+=("STAGE90_XNU_UNDEF_DIRECT=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

echo "   carrying $((${#EXPORTS[@]} - 3)) switch(es) from $REC, overriding STAGE90_XNU_UNDEF_DIRECT=1"
( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "== step 2: the record must carry the switch (the gate reads THIS line) =="
grep -E "^STAGE90_XNU_UNDEF_DIRECT=" "$REC" || {
  echo "FAIL: the arm record does not carry STAGE90_XNU_UNDEF_DIRECT -- build_entry.sh's echo is missing and the gate would read an arm nobody built"; exit 1; }
grep -qxF 'STAGE90_XNU_UNDEF_DIRECT=1' "$REC" || {
  echo "FAIL: the record carries STAGE90_XNU_UNDEF_DIRECT but not =1 -- the record and the compile line disagree (mi4-one-value-two-definitions)"; exit 1; }

echo "== step 3: the by-value link clause (build_entry.sh :31238) must have passed =="
echo "   (it ran inside the build above; a mismatch is a layout_fail, so reaching here means the"
echo "    linked __wrap_sleh_undef branches to entry_write_kv >=6 and entry_live_write 0)"

echo "== step 4: what moved =="
grep -E "ENTRY_SHA256|ENTRY_BYTES|UNDEF_DIRECT" "$REC"
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   (compare against the previous arm's sha; a MATCH would mean the switch reached nothing)"

echo "== 1018 build DONE.  Not pressed.  Park it, then press with scripts/press_1018.sh. =="