#!/usr/bin/env bash
# Park 1003 and append its block to records/revert-set.txt.
#
# 1003 = 1002's switch set VERBATIM (NO new switch beyond STAGE90_XNU_<TOPIC>: all checker/witness/
# USB-ladder switches, RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1) + exactly ONE code change on
# the CODE-SIGNING page-validation wall. The rung answers the frontier the 1002 press read reached
# (2026-10-13, arm armed-d13-b04976fe):
#
#     CODE SIGNING: cs_invalid_page(0x2f7f6000): p=1[launchd] clearing CS_VALID
#
# - the SAME wall 999 stopped on. This is NOT a rung-inside-a-hook bug (1002's shape); it is the
# code-signing page-validation path. The exact root cause and edit are being determined by the
# parallel forensics agent (forensics-1003).
#
#   * 1002's headline (the baseline this rung builds on): the USVC hook's save set gained r3
#     (`{r0,r1,r8,lr}` -> `{r0-r3,r8,lr}`, six words, still 8-aligned, 990), staged by
#     tools/patch_d13_usvc.py, an ASSEMBLY-layer change to out/xnu_asm_obj_d13/traps_lo.o. It was
#     CONFIRMED BY VALUE: the 230x dyld `usermode data abort at 0x4` is GONE and the frontier MOVED
#     to the code-signing wall (records/1002-press-read.txt).
#   * 1003's change (TODO(1003): set from forensics-1003): <the exact root cause - WHY launchd's page
#     0x2f7f6000 is judged invalid / CS_VALID cleared - and the precise edit>. Switch:
#     STAGE90_XNU_<TOPIC> (placeholder). Staged by tools/stage_d13_<topic>.sh. If the change is in a
#     POOL file it rides BOTH EXTRA and the env export (1001's SUBMAP_PMAP_SKIP shape); if it is
#     ASSEMBLY-layer it rides the ASFLAGS half (1000/1002's USVC shape).
#
# A fresh checkout needs ALL THIRTEEN stage scripts: tools/stage_d13_kextb_login.sh (988's),
# tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it,
# 10 edits), tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 amendment #2), tools/stage_d13_usvc.sh (1000's - the traps_lo.s
# hook; its patcher tools/patch_d13_usvc.py inserts the 13-line block idempotently AND carries the
# 1001->1002 in-place migration to the six-word save set), tools/stage_d13_submap_pmap_skip.sh
# (1001's - the vm_map.c is_submap guard + the SUBMAPSKIP probe, in a POOL file), and
# tools/stage_d13_<topic>.sh (1003's - TODO(1003): set from forensics-1003).
#
# WHY (the 1002 press read, 2026-10-13, arm armed-d13-b04976fe, records/1002-press-read.txt): the
# 1002 run CONFIRMED its fix by value (the USVC hook's six-word window present, the 230x dyld abort at
# 0x4 GONE) and the boot ADVANCED to a NEW/OLD wall: `CODE SIGNING: cs_invalid_page(0x2f7f6000):
# p=1[launchd] clearing CS_VALID` - the code-signing page-validation path, the same wall 999 stopped
# on. 1003 is the rung that answers it.
#
# THE 1003 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not reinterpreted) is
# written by the TEAM-LEAD into scripts/press_1003.sh. TODO(1003): set from forensics-1003 - the
# expected reading after the code-signing fix (the boot SURVIVES past `cs_invalid_page` for launchd /
# the wall MOVES again), and the falsifiers. The frozen regression guards (geometry exactly once,
# cache mapping x4, shared region x1, check_np ret=12 x1, map_file kr=0, map_slide ret=0) are
# re-verified only under the survival gate.
#
# VERIFIED BY VALUE (build_1003.sh, run against the linked image): TODO(1003): set from
# forensics-1003 - the 1003 change by value in the object + linked image + source, with the FROZEN
# 1002 elf (armed-d13-b04976fe) as the NEGATIVE CONTROL. The 1002 r3-save fix is asserted as a
# REGRESSION (six-word window still present, four-word absent). The 996/997/998/999/1000/1001
# checkers rerun ON the 1003 image with the FROZEN images as negative controls (the probe checker's
# ONE by-design frozen 20 B clause is the only allowed FAIL).
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
# PRESSED arms under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-b04976fe ]] && { echo "REFUSING: $LIVE holds 1002's arm (b04976fe), the PRESSED one (the r3 fix took, the frontier moved to the code-signing wall cs_invalid_page, adjudicated in records/1002-press-read.txt); build 1003 first"; exit 1; }
[[ $ARM == armed-d13-6ab42af7 ]] && { echo "REFUSING: $LIVE holds 1001's arm (6ab42af7), the PRESSED one (frontier advanced into pid-1 dyld, adjudicated in records/1001-press-read.txt); build 1003 first"; exit 1; }
[[ $ARM == armed-d13-bbdf41bc ]] && { echo "REFUSING: $LIVE holds 1000's arm (bbdf41bc), the PRESSED one (death at 10.31 s, adjudicated in records/1000-press-read.txt); build 1003 first"; exit 1; }
[[ $ARM == armed-d13-467d59e4 ]] && { echo "REFUSING: $LIVE holds 999's arm (467d59e4); build 1003 first"; exit 1; }
[[ $ARM == armed-d13-dee822b0 ]] && { echo "REFUSING: $LIVE holds 998's arm (dee822b0); build 1003 first"; exit 1; }
[[ $ARM == armed-d13-518b6a94 ]] && { echo "REFUSING: $LIVE holds 997's arm (518b6a94); build 1003 first"; exit 1; }
[[ $ARM == armed-d13-cea0c1c2 ]] && { echo "REFUSING: $LIVE holds 996's arm (cea0c1c2); build 1003 first"; exit 1; }
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 1003 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 1003 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 1003 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 1003 first"; exit 1; }

# 1003's NEW key MUST be in the record ([[mi4-one-value-two-definitions]] - the record's keys and the
# layers' values must be one thing): the park refuses an arm whose record does not carry
# STAGE90_XNU_<TOPIC>=1. TODO(1003): set from forensics-1003 - replace the placeholder token.
# The 1002 r3-save fix (USVC hook ON) and 1001's submap skip BOTH stay on as regressions.
grep -q '^STAGE90_XNU_<TOPIC>=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_<TOPIC>=1 - 1003's new switch was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_USVC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USVC=1 - 1002's r3-save fix lives INSIDE the USVC hook, so the hook must be on"
  exit 1; }
grep -q '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - 1001's fix must stay on, or the 1001 death would be re-litigated"
  exit 1; }

# BY VALUE, at the freeze point: two things.
# (1) THE 1003 CHANGE must be in the linked image. TODO(1003): set from forensics-1003 - assert the new
#     change by value (a literal/symbol/call site) exactly as the delta clause in build_1003.sh does.
#     A claim in a comment is not a check ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
# (2) THE 1002 r3-SAVE FIX is a REGRESSION: the linked image's locore_fleh_swi must STILL carry the
#     six-word save set {r0-r3,r8,lr} and NOT the four-word one - parking an image that clobbers r3
#     again would reintroduce the 1001 defect.
# TODO(1003): set from forensics-1003 - insert the 1003 change's by-value guard here (e.g.
#     _d3=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | grep -c '^[0-9a-f]* [Tt] <new_symbol>$' || true)
#     [[ "$_d3" == "1" ]] || { echo "REFUSING: ..."; exit 1; }
_ls=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the parked image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
[[ "$_w6" == 1 && "$_w4" == 0 ]] ||
  { echo "REFUSING: the parked image's locore_fleh_swi does not carry the 1002 six-word save set (six=$_w6 want 1, four=$_w4 want 0) - a revert would reintroduce the r3 clobber the 1001 press read diagnosed"; exit 1; }
echo "park_1003: by value - locore_fleh_swi still carries the 1002 SIX-word save set {r0-r3,r8,lr} x$_w6, four-word x$_w4 (regression held)"

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1003: THE CODE-SIGNING PAGE-VALIDATION FIX (cs_invalid_page / clearing CS_VALID for launchd at 0x2f7f6000). TODO(1003): set from forensics-1003 - the confirmed root cause and the one-line result.\n' "$ARM"
  printf '# Switch set is 1002-s VERBATIM plus STAGE90_XNU_<TOPIC>=1 (DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE,\n'
  printf '# SHREG_GEOM, RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1); the ENTRY BIN moved (b04976fe ->\n'
  printf '# %s); the PAYLOAD moves (it embeds the entry blob; the 29 s watchdog constant is 999-s,\n' "${ARM#armed-d13-}"
  printf '# unchanged); the TREE moves by 1003-s ONE staged edit (TODO(1003): set from forensics-1003 -\n'
  printf '# the file(s) tools/stage_d13_<topic>.sh edits and the layer it must be rebuilt in).\n#\n'
  printf '# THE ROOT CAUSE, BY VALUE (records/1002-press-read.txt; TODO(1003): set from forensics-1003):\n'
  printf '# <the kernel-side reason cs_invalid_page fires for launchd 0x2f7f6000 and CS_VALID is cleared>.\n#\n'
  printf '# THE 1003 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not\n'
  printf '# reinterpreted) is written by the TEAM-LEAD into scripts/press_1003.sh. TODO(1003): set from\n'
  printf '# forensics-1003 - the expected reading after the fix (the boot SURVIVES past `cs_invalid_page`\n'
  printf '# for launchd, or the wall MOVES again) and the falsifiers. The frozen regression guards\n'
  printf '# (geometry exactly once, cache mapping x4, shared region x1, check_np ret=12 x1, map_file kr=0,\n'
  printf '# map_slide ret=0) are re-verified only under the survival gate. The 1002 r3-save fix is a\n'
  printf '# REGRESSION: the six-word window must STILL be present in locore_fleh_swi.\n#\n'
  printf '# VERIFIED BY VALUE (build_1003.sh): TODO(1003): set from forensics-1003 - the 1003 change by\n'
  printf '# value (object + linked image + source) with the FROZEN 1002 elf (armed-d13-b04976fe) as the\n'
  printf '# NEGATIVE CONTROL; the 1002 six-word save set asserted present (regression). The 996/997/998/\n'
  printf '# 999/1000/1001 checkers rerun ON the 1003 image with the FROZEN images as negative controls -\n'
  printf '# the probe checker-s ONE by-design frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1003-cs-invalid-page-fix;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3