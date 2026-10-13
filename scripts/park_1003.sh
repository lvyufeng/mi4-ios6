#!/usr/bin/env bash
# Park 1003 and append its block to records/revert-set.txt.
#
# 1003 = 1002's switch set VERBATIM (NO new switch beyond STAGE90_XNU_STALL: all checker/witness/
# USB-ladder switches, RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1) + exactly ONE new switch
# STAGE90_XNU_STALL - a pure MEASUREMENT rung (no kernel behaviour changes) that samples pid 1's user
# pc on the timer tick, to settle the frontier the 1002 press read reached (2026-10-13, arm
# armed-d13-b04976fe): a user-side STALL after 22 SVCs (last three num=-3 = `mach_absolute_time` at
# pc 0x2be97328, `lr` marching) while the kernel stays alive ~23 min. `cs_invalid_page(0x2f7f6000)` is
# a TOLERATED warning (clearing CS_VALID = retval 0), NOT the frontier - see
# docs/1003-dyld-stall-design-draft.md.
#
#   * 1002's headline (the baseline this rung builds on): the USVC hook's save set gained r3
#     (`{r0,r1,r8,lr}` -> `{r0-r3,r8,lr}`, six words, still 8-aligned, 990), staged by
#     tools/patch_d13_usvc.py, an ASSEMBLY-layer change to out/xnu_asm_obj_d13/traps_lo.o. It was
#     CONFIRMED BY VALUE: the 230x dyld `usermode data abort at 0x4` is GONE and the frontier MOVED
#     to the user-side stall (records/1002-press-read.txt).
#   * 1003's change: the stall instrument, TWO src/ files, NO tree edit and NO stage driver. The
#     recorder `entry_note_stall_sample` in `src/entry/entry_stubs.c` (compiled by build_entry.sh into
#     the payload) and ONE gated call site in `src/platform/darwin13/pe_msm8974.c` (compiled by the
#     platform block into $XNU_PLATFORM_OBJ_OUT/pe_msm8974.o, which the entry link carries, under
#     -DSTAGE90_XNU_STALL via XNU_KERNEL_EXTRA_DEFINES). It publishes `xnu_live_stall_*` on the tick's
#     powers-of-two ladder. Switch: STAGE90_XNU_STALL.
#
# A fresh checkout needs ALL TWELVE tree stage scripts (1003 adds NONE): tools/stage_d13_kextb_login.sh
# (988's), tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it,
# 10 edits), tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 amendment #2), tools/stage_d13_usvc.sh (1000's - the traps_lo.s
# hook; its patcher tools/patch_d13_usvc.py inserts the 13-line block idempotently AND carries the
# 1001->1002 in-place migration to the six-word save set), and tools/stage_d13_submap_pmap_skip.sh
# (1001's - the vm_map.c is_submap guard + the SUBMAPSKIP probe, in a POOL file).
#
# WHY (the 1002 press read, 2026-10-13, arm armed-d13-b04976fe, records/1002-press-read.txt): the 1002
# run CONFIRMED its fix by value (the USVC hook's six-word window present, the 230x dyld abort at 0x4
# GONE) and the boot ADVANCED into pid-1 user mode, ran 22 SVCs, then went silent. The frontier is
# that silence, and 1003 measures it.
#
# THE 1003 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not reinterpreted) is
# written by the TEAM-LEAD into scripts/press_1003.sh: the reading is the pid-1 pc SAMPLES - a user pc
# that advances/repeats = SPINNING; every sample a kernel pc = ASLEEP. The frozen regression guards
# (geometry exactly once, cache mapping x4, shared region x1, check_np ret=12 x1, map_file kr=0,
# map_slide ret=0) are re-verified only under the survival gate.
#
# VERIFIED BY VALUE (build_1003.sh, run against the linked image): the stall instrument in the object
# ($XNU_PLATFORM_OBJ_OUT/pe_msm8974.o references entry_note_stall_sample U) + the linked image (the
# symbol T + the 7 `xnu_live_stall_*` key literals) + the source shape (1 gated call site), with the
# FROZEN 1002 elf (armed-d13-b04976fe) as the NEGATIVE CONTROL (x0 symbol, x0 literals). The 1002
# r3-save fix is asserted as a REGRESSION (six-word window still present, four-word absent). The
# 996/997/998/999/1000/1001 checkers rerun ON the 1003 image with the FROZEN images as negative
# controls (the probe checker's ONE by-design frozen 20 B clause is the only allowed FAIL).
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
# STAGE90_XNU_STALL=1.
# The 1002 r3-save fix (USVC hook ON) and 1001's submap skip BOTH stay on as regressions.
grep -q '^STAGE90_XNU_STALL=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_STALL=1 - 1003's stall instrument was not recorded this run"; exit 1; }
grep -q '^STAGE90_XNU_USVC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USVC=1 - 1002's r3-save fix lives INSIDE the USVC hook, so the hook must be on"
  exit 1; }
grep -q '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - 1001's fix must stay on, or the 1001 death would be re-litigated"
  exit 1; }

# BY VALUE, at the freeze point: two things.
# (1) THE 1003 CHANGE (the stall instrument) must be in the linked image: the recorder symbol is
#     present AND the 7 `xnu_live_stall_*` key literals are embedded. A claim in a comment is not a
#     check ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
# (2) THE 1002 r3-SAVE FIX is a REGRESSION: the linked image's locore_fleh_swi must STILL carry the
#     six-word save set {r0-r3,r8,lr} and NOT the four-word one - parking an image that clobbers r3
#     again would reintroduce the 1001 defect.
_stall_sym=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | grep -cE '^[0-9a-f]+ [Tt] entry_note_stall_sample$' || true)
[[ "$_stall_sym" == 1 ]] || {
  echo "REFUSING: entry_note_stall_sample is not in the parked image (x$_stall_sym, want 1) - 1003's stall instrument is gone"; exit 1; }
_stall_keys=$(strings "$LIVE/xnu_arm_entry.elf" | grep -c 'xnu_live_stall_' || true)
[[ "$_stall_keys" == 7 ]] || {
  echo "REFUSING: the parked image does not carry the 7 xnu_live_stall_* key literals (x$_stall_keys, want 7) - the recorder's stores are gone"; exit 1; }
echo "park_1003: by value - entry_note_stall_sample x$_stall_sym, xnu_live_stall_* literals x$_stall_keys (the stall instrument is present)"

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
  printf '# %s  -  1003: THE PID-1 STALL INSTRUMENT (per-tick sample of the interrupted frame: a user pc\n# that advances/repeats = SPINNING; every sample a KERNEL pc = ASLEEP).\n' "$ARM"
  printf '# Switch set is 1002-s VERBATIM plus STAGE90_XNU_STALL=1 (DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE,\n'
  printf '# SHREG_GEOM, RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1); the ENTRY BIN moved (b04976fe ->\n'
  printf '# %s); the PAYLOAD moves (it embeds the entry blob; the 29 s watchdog constant is 999-s,\n' "${ARM#armed-d13-}"
  printf '# unchanged); the TREE does NOT move - 1003 has NO tree edit and NO stage driver (the instrument\n'
  printf '# lives in TWO src/ files: the recorder entry_note_stall_sample in src/entry/entry_stubs.c, and\n'
  printf '# ONE gated call site in src/platform/darwin13/pe_msm8974.c).\n#\n'
  printf '# THE FRONTIER, BY VALUE (records/1002-press-read.txt): the 1002 press CONFIRMED the r3-save fix\n'
  printf '# (the 230x dyld usermode data abort at 0x4 is GONE) and pushed the boot into pid-1 user mode:\n'
  printf '# 22 SVCs, the last three num=-3 = mach_absolute_time from the SAME pc 0x2be97328 with lr marching\n'
  printf '# 0x2be85a9f -> 0x2be85af1 -> 0x2be85b11, cpsr 0x60000010 (USR), then NO further syscall while the\n'
  printf '# kernel stays alive ~23 min. cs_invalid_page(0x2f7f6000) is a TOLERATED warning (clearing\n'
  printf '# CS_VALID = retval 0), NOT the frontier.\n#\n'
  printf '# THE 1003 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not interpreted) is\n'
  printf '# written by the TEAM-LEAD into scripts/press_1003.sh: THE READING is the xnu_live_stall_pc samples -\n'
  printf '# a USER pc (cpsr 0x10) that advances or repeats = the pid-1 program is SPINNING; every sample a\n'
  printf '# KERNEL pc (0x8000xxxx) = pid 1 is ASLEEP/blocked. The frozen regression guards (geometry exactly\n'
  printf '# once, cache mapping x4, shared region x1, check_np ret=12 x1, map_file kr=0, map_slide ret=0) are\n'
  printf '# re-verified only under the survival gate. The 1002 r3-save fix is a REGRESSION: the six-word\n'
  printf '# window must STILL be present in locore_fleh_swi.\n#\n'
  printf '# VERIFIED BY VALUE (build_1003.sh): the pe_msm8974.o references entry_note_stall_sample (U), the\n'
  printf '# linked image carries the symbol (T) + the 7 xnu_live_stall_* key literals + the source has 1\n'
  printf '# gated call site, with the FROZEN 1002 elf (armed-d13-b04976fe) as the NEGATIVE CONTROL (x0\n'
  printf '# symbol, x0 literals); the 1002 six-word save set asserted present (regression). The 996/997/998/\n'
  printf '# 999/1000/1001 checkers rerun ON the 1003 image with the FROZEN images as negative controls -\n'
  printf '# the probe checker-s ONE by-design frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1003-pid1-stall-instrument;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3