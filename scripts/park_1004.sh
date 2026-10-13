#!/usr/bin/env bash
# ====================================================================================================
# SKELETON - park_1004.sh.  MODELLED BYTE-STRUCTURALLY ON scripts/park_1003.sh.
#
# Park 1004 and append its block to records/revert-set.txt.
#
# 1004 = 1003's switch set VERBATIM (all checker/witness/USB-ladder switches, RESIDENT, USVC,
# SUBMAP_PMAP_SKIP and STALL all stay 1) + exactly ONE new switch STAGE90_XNU_USER_PC - a pure
# MEASUREMENT rung that generalises 1003's per-tick stall sampler into a pid-1 FRAME instrument
# (user pc + cpsr + frame word count), so SPIN-vs-ASLEEP can be read as a CHANGING user pc vs a
# KERNEL cpsr.
#
# 1003's change (the baseline this rung keeps): the stall instrument - TWO src/ files, NO tree edit
# and NO stage driver: the recorder `entry_note_stall_sample` in `src/entry/entry_stubs.c` and ONE
# gated call site in `src/platform/darwin13/pe_msm8974.c` under STAGE90_XNU_STALL.  It published
# `xnu_live_stall_*` on the tick's powers-of-two ladder.
#
# 1004 adds the generalised pid-1 frame instrument under STAGE90_XNU_USER_PC.  1004-TODO: the two
# source halves (the recorder + the ONE call site) are UNWRITTEN - candidate (D) is
# `src/entry/entry_trace.c` near the Idle_load_context wrapper (~l.3040-3080), candidate (B) is
# `src/platform/darwin13/pe_msm8974.c` in the tick handler gated on the thread being pid 1.
#
# A fresh checkout needs ALL TWELVE tree stage scripts (1004 adds NONE): tools/stage_d13_kextb_login.sh
# (988's), tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it),
# tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 am2), tools/stage_d13_usvc.sh (1000's - the traps_lo.s hook), and
# tools/stage_d13_submap_pmap_skip.sh (1001's - the vm_map.c is_submap guard).
#
# VERIFIED BY VALUE (build_1004.sh, run against the linked image): the USER_PC instrument in the
# linked image (the recorder symbol T + the 4 `xnu_live_user_pc*` key literals) + the source shape
# (1 gated call site), with the FROZEN 1003 elf (armed-d13-7669309b) as the NEGATIVE CONTROL (x0
# symbol, x0 literals).  The 1003 stall instrument and the 1002 r3-save fix are asserted as
# REGRESSIONS (both still present).  The 996/997/998/999/1000/1001 checkers rerun ON the 1004 image
# with the FROZEN images as negative controls (the probe checker's ONE by-design frozen 20 B clause is
# the only allowed FAIL).
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
[[ $ARM == armed-d13-7669309b ]] && { echo "REFUSING: $LIVE holds 1003's arm (7669309b), the parked predecessor; build 1004 first"; exit 1; }
[[ $ARM == armed-d13-b04976fe ]] && { echo "REFUSING: $LIVE holds 1002's arm (b04976fe), the PRESSED one (the r3 fix took, the frontier moved to the pid-1 stall, adjudicated in records/1002-press-read.txt); build 1004 first"; exit 1; }
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
# STAGE90_XNU_USER_PC=1.
# The 1003 stall switch, the 1002 r3-save fix (USVC hook ON) and 1001's submap skip BOTH/ALL stay on
# as regressions.
grep -q '^STAGE90_XNU_USER_PC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USER_PC=1 - 1004's pid-1 frame instrument was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_STALL=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_STALL=1 - 1003's stall sampler is part of 1004's set verbatim and must stay on"; exit 1; }
grep -q '^STAGE90_XNU_USVC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USVC=1 - 1002's r3-save fix lives INSIDE the USVC hook, so the hook must be on"
  exit 1; }
grep -q '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - 1001's fix must stay on, or the 1001 death would be re-litigated"
  exit 1; }

# BY VALUE, at the freeze point: three things.
# (1) THE 1004 CHANGE (the pid-1 frame instrument) must be in the linked image: the recorder symbol is
#     present AND the 4 `xnu_live_user_pc*` key literals are embedded.  1004-TODO: the symbol name
#     `entry_note_user_pc` is PROVISIONAL.
# (2) THE 1003 STALL INSTRUMENT is a REGRESSION: its recorder + 7 `xnu_live_stall_*` literals must
#     STILL be present (1004 keeps 1003's set verbatim).
# (3) THE 1002 r3-SAVE FIX is a REGRESSION: locore_fleh_swi must STILL carry the six-word save set.
_upc_sym=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | grep -cE '^[0-9a-f]+ [Tt] entry_note_user_pc$' || true)
[[ "$_upc_sym" == 1 ]] || {
  echo "REFUSING: entry_note_user_pc is not in the parked image (x$_upc_sym, want 1) - 1004's pid-1 frame instrument is gone (1004-TODO: confirm the symbol name)"; exit 1; }
_upc_keys=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_user_pc' || true)
[[ "$_upc_keys" == 4 ]] || {
  echo "REFUSING: the parked image does not carry the 4 xnu_live_user_pc* key literals (x$_upc_keys, want 4) - the recorder's stores are gone"; exit 1; }
echo "park_1004: by value - entry_note_user_pc x$_upc_sym, xnu_live_user_pc* literals x$_upc_keys (the pid-1 frame instrument is present)"

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

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1004: THE PER-TICK PID-1 FRAME INSTRUMENT (sampled interrupted pid-1 frame: a CHANGING\n# user pc + user cpsr = SPINNING, the pc names the function; a KERNEL cpsr = ASLEEP/blocked).\n' "$ARM"
  printf '# Switch set is 1003-s VERBATIM plus STAGE90_XNU_USER_PC=1 (STALL, DYLD_WITNESS, SHREG_PROBE,\n'
  printf '# SHREG_WIRE, SHREG_GEOM, RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1); the ENTRY BIN moved\n'
  printf '# (7669309b -> %s); the PAYLOAD moves (it embeds the entry blob; the 29 s watchdog constant is\n' "${ARM#armed-d13-}"
  printf '# 999-s, unchanged); the TREE does NOT move - 1004 has NO tree edit and NO stage driver (the\n'
  printf '# instrument lives in TWO src/ files, exactly like 1003: a recorder in src/entry/entry_stubs.c\n'
  printf '# and ONE gated call site at candidate (D) src/entry/entry_trace.c or (B) src/platform/darwin13/\n'
  printf '# pe_msm8974.c - 1004-TODO: UNWRITTEN, see scripts/build_1004.sh).\n#\n'
  printf '# THE FRONTIER, BY VALUE (records/1002-press-read.txt, docs/1003-dyld-stall-design-draft.md):\n'
  printf '# after the 1002 r3-save fix the boot entered pid-1 user mode, ran 22 SVCs, and went silent while\n'
  printf '# the kernel stayed alive ~23 min; cs_invalid_page(0x2f7f6000) is TOLERATED (retval 0), NOT the\n'
  printf '# frontier.  1003 sampled the interrupted frame to decide SPIN vs ASLEEP; 1004 GENERALISES that\n'
  printf '# sample into a per-tick pid-1 FRAME instrument so the pc can be read as CHANGING (a named\n'
  printf '# function) rather than a single fixed reading.\n#\n'
  printf '# THE 1004 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not interpreted) is\n'
  printf '# written by the TEAM-LEAD into scripts/press_1004.sh: THE READING is the xnu_live_user_pc samples -\n'
  printf '# a CHANGING user pc (cpsr low 5 bits = 0x10) = pid 1 is SPINNING and the pc names the function;\n'
  printf '# every sample a KERNEL cpsr = pid 1 is ASLEEP/blocked.  The frozen regression guards are\n'
  printf '# re-verified only under the survival gate.\n#\n'
  printf '# VERIFIED BY VALUE (build_1004.sh): the linked image carries the recorder (T) + the 4\n'
  printf '# xnu_live_user_pc* key literals + 1 gated call site, with the FROZEN 1003 elf (armed-d13-7669309b)\n'
  printf '# as the NEGATIVE CONTROL (x0 symbol, x0 literals); the 1003 stall instrument and the 1002\n'
  printf '# six-word save set asserted present (regressions).  The 996/997/998/999/1000/1001 checkers rerun\n'
  printf '# ON the 1004 image - the probe checker-s ONE by-design frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1004-pid1-frame-instrument;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3