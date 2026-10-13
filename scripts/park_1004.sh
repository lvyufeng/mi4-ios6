#!/usr/bin/env bash
# ====================================================================================================
# park_1004.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1003.sh.
#
# Park 1004 and append its block to records/revert-set.txt.
#
# 1004 = 1003's switch set VERBATIM (all checker/witness/USB-ladder switches, RESIDENT, USVC,
# SUBMAP_PMAP_SKIP and STALL all stay 1) + exactly ONE new switch STAGE90_XNU_ABORT_TRACE - a pure
# MEASUREMENT rung that WIDENS the sleh (second-level abort) detail caps so that EVERY abort of a run
# publishes its full tuple, and publishes the faulting frame's USER pc (`xnu_live_sleh_upc`) beside it.
#
# WHAT 1004 IS: 1003 sampled the interrupted frame and found pid 1's thread (0xc0bfa000) in the
# kernel's data-abort RETURN path, taking repeated aborts on dyld pages and returning from each - but
# the sleh caps published only the FIRST 8 of 40 aborts (`xnu_live_sleh_seen`=40, `_storm`=9), and
# those 8 name DIFFERENT `far` values.  The hanging iteration is in the TAIL the cap hid.  1004 raises
# the caps (under the switch) so the WHOLE sequence is published and the `far` walk can be classified
# as a LOOP (same page re-faulting) or a WALK (dyld paging forward - merely slow).
#
# 1004's change is SOURCE-ONLY and inside ONE existing instrumentation block: THREE guards on the caps
# in `src/entry/entry_stubs.c` (SLEH_LIVE_MAX / SLEH_LIVE_FRAMES / SLEH_LIVE_SEEN) plus ONE gated
# `entry_live_write("xnu_live_sleh_upc", pc)` inside `entry_note_sleh`'s FRAMES band.  NO tree edit and
# NO stage driver.  When the switch is 0 the image is byte-identical to today (the guards are `#if
# defined(...) && ...`, not a widened constant - [[mi4-off-option-two-spellings]]).
#
# A fresh checkout needs ALL TWELVE tree stage scripts (1004 adds NONE): tools/stage_d13_kextb_login.sh
# (988's), tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it),
# tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 am2), tools/stage_d13_usvc.sh (1000's - the traps_lo.s hook), and
# tools/stage_d13_submap_pmap_skip.sh (1001's - the vm_map.c is_submap guard).
#
# VERIFIED BY VALUE (build_1004.sh, run against the linked image): the abort trace in the linked image
# (`xnu_live_sleh_upc` literal x1, `entry_note_sleh`'s #512 comparisons) + the source shape (the two
# `defined()`-first guards, the 512u caps x3) with the FROZEN 1003 elf (armed-d13-7669309b) as the
# NEGATIVE CONTROL (xnu_live_sleh_upc x0, #512 x0).  The 1003 stall instrument and the 1002 r3-save fix
# are asserted as REGRESSIONS (both still present).  The 996/997/998/999/1000/1001 checkers rerun ON
# the 1004 image with the FROZEN images as negative controls (the probe checker's ONE by-design frozen
# 20 B clause is the only allowed FAIL).
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible. fastboot boot only, NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

LIVE=out/stage90
REC=records/revert-set.txt
MEMBERS=(xnu_arm_entry.bin xnu_arm_entry.elf xnu_arm_entry.h xnu_arm_entry-config.txt \
         xnu_arm_entry-sources.txt stage90.bin stage90.elf stage90.img stage90-qcdt.img \
         stage90_fixture.macho stage90-build-config.txt SHA256SUMS.txt)

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse to re-park the
# PRESSED/known arms under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-7669309b ]] && { echo "REFUSING: $LIVE holds 1003's arm (7669309b), the PRESSED predecessor (pid 1 is in the kernel's data-abort return path, adjudicated in records/1003-press-read.txt); build 1004 first"; exit 1; }
[[ $ARM == armed-d13-b04976fe ]] && { echo "REFUSING: $LIVE holds 1002's arm (b04976fe), the PRESSED one (the r3 fix took, the frontier moved to the pid-1 stall); build 1004 first"; exit 1; }
[[ $ARM == armed-d13-6ab42af7 ]] && { echo "REFUSING: $LIVE holds 1001's arm (6ab42af7), the PRESSED one (frontier advanced into pid-1 dyld, adjudicated in records/1001-press-read.txt); build 1004 first"; exit 1; }
[[ $ARM == armed-d13-bbdf41bc ]] && { echo "REFUSING: $LIVE holds 1000's arm (bbdf41bc), the PRESSED one (death at 10.31 s, adjudicated in records/1000-press-read.txt); build 1004 first"; exit 1; }
[[ $ARM == armed-d13-467d59e4 ]] && { echo "REFUSING: $LIVE holds 999's arm (467d59e4); build 1004 first"; exit 1; }
[[ $ARM == armed-d13-dee822b0 ]] && { echo "REFUSING: $LIVE holds 998's arm (dee822b0); build 1004 first"; exit 1; }
[[ $ARM == armed-d13-518b6a94 ]] && { echo "REFUSING: $LIVE holds 997's arm (518b6a94); build 1004 first"; exit 1; }
[[ $ARM == armed-d13-cea0c1c2 ]] && { echo "REFUSING: $LIVE holds 996's arm (cea0c1c2); build 1004 first"; exit 1; }
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 1004 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 1004 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 1004 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 1004 first"; exit 1; }

# 1004's NEW key MUST be in the record ([[mi4-one-value-two-definitions]] - the record's keys and the
# layers' values must be one thing): the park refuses an arm whose record does not carry
# STAGE90_XNU_ABORT_TRACE=1.
# The 1003 stall switch, the 1002 r3-save fix (USVC hook ON) and 1001's submap skip BOTH/ALL stay on
# as regressions.
grep -q '^STAGE90_XNU_ABORT_TRACE=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_ABORT_TRACE=1 - 1004's abort trace was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_STALL=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_STALL=1 - 1003's stall sampler is part of 1004's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_USVC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USVC=1 - 1002's r3-save fix lives INSIDE the USVC hook, so the hook must be on"
  exit 1; }
grep -q '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - 1001's fix must stay on, or the 1001 death would be re-litigated"
  exit 1; }

# BY VALUE, at the freeze point: four things.
# (1) THE 1004 CHANGE (the abort trace) must be in the linked image: the `xnu_live_sleh_upc` key
#     literal is embedded AND `entry_note_sleh`'s body compares against the RAISED caps (512) rather
#     than the legacy 8/16/64.
# (2) THE 1003 STALL INSTRUMENT is a REGRESSION: its recorder + 7 `xnu_live_stall_*` literals must
#     STILL be present (1004 keeps 1003's set verbatim).
# (3) THE 1002 r3-SAVE FIX is a REGRESSION: locore_fleh_swi must STILL carry the six-word save set.
# (4) The FROZEN 1003 elf (out/stage90/frozen/armed-d13-7669309b/) as the NEGATIVE CONTROL: it must
#     carry xnu_live_sleh_upc x0 and #512 x0 (a reader that cannot fail certifies nothing).
_upc_keys=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c '^xnu_live_sleh_upc$' || true)
[[ "$_upc_keys" == 1 ]] || {
  echo "REFUSING: the parked image does not carry the xnu_live_sleh_upc key literal (x$_upc_keys, want 1) - the 1004 store is gone"; exit 1; }
_sleh=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="entry_note_sleh"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: entry_note_sleh is not in the parked image - the sleh instrument's body is gone"; exit 1; }
_slehx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_sleh#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_slehb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_sleh" --stop-address="${_slehx:-0xffffffff}")
_cap512=$(grep -cE 'cmp[[:space:]]+r[0-9]+, #(512|0x200)\b|movw[[:space:]]+r[0-9]+, #(512|0x200)\b' <<<"$_slehb" || true)
_capold=$(grep -cE 'cmp[[:space:]]+r[0-9]+, #(8|16|64)\b' <<<"$_slehb" || true)
[[ "$_cap512" -ge 3 && "$_capold" == 0 ]] || {
  echo "REFUSING: the parked image's entry_note_sleh does not carry the raised caps (512=$_cap512 want >=3, legacy 8/16/64=$_capold want 0) - the STAGE90_XNU_ABORT_TRACE caps did not reach the image"; exit 1; }
echo "park_1004: by value - xnu_live_sleh_upc literal x$_upc_keys, entry_note_sleh raised caps #512 x$_cap512, legacy caps x$_capold (the abort trace is present)"

_stall_sym=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' || true)
[[ "$_stall_sym" == 1 ]] || {
  echo "REFUSING: entry_note_stall_sample is not in the parked image (x$_stall_sym, want 1) - 1003's stall instrument was dropped"; exit 1; }
_stall_keys=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_stall_' || true)
[[ "$_stall_keys" == 7 ]] || {
  echo "REFUSING: the parked image does not carry the 7 xnu_live_stall_* key literals (x$_stall_keys, want 7) - 1003's instrument regressed"; exit 1; }
echo "park_1004: by value - entry_note_stall_sample x$_stall_sym, xnu_live_stall_* literals x$_stall_keys (1003's instrument kept)"

_ls=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the parked image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
[[ "$_w6" == 1 && "$_w4" == 0 ]] ||
  { echo "REFUSING: the parked image's locore_fleh_swi does not carry the 1002 six-word save set (six=$_w6 want 1, four=$_w4 want 0) - a revert would reintroduce the r3 clobber"; exit 1; }
echo "park_1004: by value - locore_fleh_swi still carries the 1002 SIX-word save set {r0-r3,r8,lr} x$_w6, four-word x$_w4 (regression held)"

# THE FROZEN 1003 elf as the NEGATIVE CONTROL for the new reader.
_ctrl=out/stage90/frozen/armed-d13-7669309b/xnu_arm_entry.elf
if [[ -f "$_ctrl" ]]; then
  _c_upc=$(strings "$_ctrl" | grep -c '^xnu_live_sleh_upc$' || true)
  _c_sleh=$(arm-none-eabi-nm "$_ctrl" | awk '$3=="entry_note_sleh"{print "0x" $1; found=1} END{exit(found?0:1)}') || true
  _c_slehx=$(arm-none-eabi-nm -n "$_ctrl" | awk -v s="${_c_sleh#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
  _c_b=$(arm-none-eabi-objdump -d "$_ctrl" --start-address="${_c_sleh}" --stop-address="${_c_slehx:-0xffffffff}" 2>/dev/null || true)
  _c_512=$(grep -cE 'cmp[[:space:]]+r[0-9]+, #(512|0x200)\b|movw[[:space:]]+r[0-9]+, #(512|0x200)\b' <<<"$_c_b" || true)
  [[ "$_c_upc" == 0 && "$_c_512" == 0 ]] || {
    echo "REFUSING: the frozen 1003 elf negatively-controls nothing (xnu_live_sleh_upc=$_c_upc want 0, #512=$_c_512 want 0) - the control image is not the pre-1004 one"; exit 1; }
  echo "park_1004: by value - NEGATIVE CONTROL (frozen 1003 elf): xnu_live_sleh_upc x$_c_upc, #512 x$_c_512 (the reader can fail)"
else
  echo "park_1004: WARNING - the frozen 1003 elf is absent; the negative control was NOT checked this park"
fi

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1004: THE ABORT TRACE (raised sleh detail caps + the frame-s USER pc xnu_live_sleh_upc),\n# so EVERY abort of a run is published, not just the first 8; classifies the far walk LOOP vs WALK.\n' "$ARM"
  printf '# Switch set is 1003-s VERBATIM plus STAGE90_XNU_ABORT_TRACE=1 (STALL, DYLD_WITNESS, SHREG_PROBE,\n'
  printf '# SHREG_WIRE, SHREG_GEOM, RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1); the ENTRY BIN moved\n'
  printf '# (7669309b -> %s); the PAYLOAD moves (it embeds the entry blob; the 29 s watchdog constant is\n' "${ARM#armed-d13-}"
  printf '# 999-s, unchanged); the TREE does NOT move - 1004 has NO tree edit and NO stage driver (the\n'
  printf '# change is THREE guards on the sleh caps + ONE gated store in src/entry/entry_stubs.c).\n#\n'
  printf '# THE FRONTIER, BY VALUE (records/1003-press-read.txt): pid 1-s thread 0xc0bfa000 is in the\n'
  printf '# kernel-s data-abort RETURN path (pc=0x8001b1fd, sp=0xcfdbbf00, lr=sleh_abort+0x468); the run\n'
  printf '# reached 40 abort entries (xnu_live_sleh_seen=40, _storm=9) but the caps published only the\n'
  printf '# FIRST 8, and those 8 name DIFFERENT far values (dyld walking forward).  The hanging iteration\n'
  printf '# is in the TAIL the cap hid; 1004 raises the caps so the whole sequence is published.\n#\n'
  printf '# THE 1004 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not interpreted) is\n'
  printf '# written by the TEAM-LEAD into scripts/press_1004.sh: THE READING is the full ordered sleh\n'
  printf '# sequence and its far walk - consecutive far values that REPEAT (a small window) = LOOP (the\n'
  printf '# same page re-faulting); strictly ADVANCING = WALK (dyld paging the cache forward - slow, not\n'
  printf '# stuck).  The LAST abort-s far/upc is the frontier.  The frozen regression guards are re-verified\n'
  printf '# only under the survival gate.\n#\n'
  printf '# VERIFIED BY VALUE (build_1004.sh): the linked image carries xnu_live_sleh_upc x1 and\n'
  printf '# entry_note_sleh-s raised caps (#512), with the FROZEN 1003 elf (armed-d13-7669309b) as the\n'
  printf '# NEGATIVE CONTROL (xnu_live_sleh_upc x0, #512 x0); the 1003 stall instrument and the 1002\n'
  printf '# six-word save set asserted present (regressions).  The 996/997/998/999/1000/1001 checkers rerun\n'
  printf '# ON the 1004 image - the probe checker-s ONE by-design frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1004-abort-trace;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3