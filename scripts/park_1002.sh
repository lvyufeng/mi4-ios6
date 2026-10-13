#!/usr/bin/env bash
# Park 1002 and append its block to records/revert-set.txt.
#
# 1002 = 1001's switch set VERBATIM (NO new switch: all checker/witness/USB-ladder switches,
# RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1) + exactly ONE code change: the 1000 USER-SVC hook's
# save set gains r3, `{r0,r1,r8,lr}` -> `{r0-r3,r8,lr}` (6 words, still 8-aligned, 990). Staged by
# tools/patch_d13_usvc.py, which MIGRATES a tree already at the 1000/1001 four-word form in place
# (and refuses a tree carrying both forms, or neither). The change is to an ASSEMBLY-layer object
# (`out/xnu_asm_obj_d13/traps_lo.o`) - re-run tools/assemble_arm_layer.sh with STAGE90_XNU_USVC=1
# EXPORTED, then relink. NO pool object changes. The ENTRY BIN moves (6ab42af7 -> ?) - the linked
# traps_lo.o bytes change, so the sha8 changes; the PAYLOAD moves (it embeds the entry blob; the 29 s
# watchdog constant is 999's, unchanged); the TREE moves by ONE staged edit (traps_lo.s's save set).
#
#   * THE DEFECT: `traps_lo.s`'s 1000 hook saved `{r0,r1,r8,lr}` around `bl _entry_note_user_svc`,
#     but that C helper CLOBBERS r3 (built image: `movw r3,#0xfff8`/`movt r3,#0x8068` on every path,
#     `mov r3,r0` on the n<=64 full path), and `fleh_swi`'s `swi_trap` dispatches the `0x80000000`
#     traps ON LIVE r3 (`cmp r3,#3; addls pc,pc,r3,lsl#2` - r3=3 = thread_set_cthread_trap, which
#     writes TPIDRURO). Saving r0/r1/r8/lr was the wrong set. The LEO sibling block two lines below
#     saves `{r0-r12,lr}` for exactly this reason.
#   * THE FIX: save r3 too - `{r0-r3,r8,lr}`. SIX words = 24 B, still 8-aligned at the `bl` (990).
# A fresh checkout needs ALL TWELVE stage scripts: tools/stage_d13_kextb_login.sh (988's),
# tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it,
# 10 edits), tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 amendment #2), tools/stage_d13_usvc.sh (1000's - the traps_lo.s
# hook; its patcher tools/patch_d13_usvc.py inserts the 13-line block idempotently AND carries the
# 1001->1002 in-place migration to the six-word save set) and tools/stage_d13_submap_pmap_skip.sh
# (1001's - the vm_map.c is_submap guard + the SUBMAPSKIP probe, in a POOL file).
#
# WHY (the 1001 press read, 2026-10-13, arm armed-d13-6ab42af7, adjudication in
# records/1001-press-read.txt §[6]): the 1001 run CONFIRMED its fix by value (SUBMAPSKIP x2 == 999's
# two objects; VMHINT991 inverted to 0) and the boot ADVANCED into pid-1 user mode - then faulted
# 230x, byte-identical, `launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0x00000004 ... (Translation
# fault on page)`, right after `IOS7SHREG check_np ret=12`. BY VALUE (slide 0xE7000): the fault is
# dyld's OWN errno store - `_cerror_nocancel` (vmaddr 0x2be1b8d4) reads TPIDRURO
# (`mrc p15,0,r1,c13,c0,3`), masks it, and `ldr r1,[r1,#4]` faults because the register reads 0. The
# 999 CONTRAST is decisive: SAME binary, SAME syscall, SAME ret=12 - and 999 prints `IOS7SHREG
# map_slide` on the NEXT line with ZERO `usermode data abort`. So the difference is not a structural
# kernel gap (a pure one would fault on 999 too): it is 1001's ONE kernel-code change - the USVC hook.
# 1002 fixes it by saving r3.
#
# THE 1002 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not reinterpreted) is
# written by the TEAM-LEAD into scripts/press_1002.sh. Expected per the lead: the USVC hook is
# TRANSPARENT on r3 (the six-word window in the image before any judgement); dyld proceeds PAST
# `check_np ret=12` into `IOS7SHREG map_slide` (the 999 reading) with NO recurrence of the 230x
# `usermode data abort at 0x4`; the SUBMAPSKIP probe >= 1 (unchanged); VMHINT991 stays 0; the
# survival gate (no panic + exec-done trio + records past the exec-return point); the pet's first
# feed once the boot passes ~14.5 s with the read door in use; the tick summaries continuing.
# Falsifiers: the 230x `usermode data abort at 0x4` RECURS (the r3 clobber was not the cause - the
# structural hardness item, our tree's missing TPIDRURO writers, moves to the front and becomes the
# 1003 target); death elsewhere => the frontier ADVANCED, record by value. The frozen regression
# guards (geometry exactly once, cache mapping x4, shared region x1, check_np ret=12 x1, map_file
# kr=0, map_slide ret=0) are re-verified only under the survival gate.
#
# VERIFIED BY VALUE (build_1002.sh, run against the linked image): the traps_lo.o save set is the
# SIX-word `{r0-r3,r8,lr}` and NOT the four-word `{r0,r1,r8,lr}` (both object and linked
# locore_fleh_swi); the SOURCE edit is the staged shape; the FROZEN 1001 elf as the NEGATIVE CONTROL
# (four-word x2, six-word x0 - a reader that cannot fail certifies nothing). The 996/997/998/999/1000
# checkers rerun ON the 1002 image with the FROZEN 1000/999 images as negative controls (the probe
# checker's ONE by-design frozen 20 B clause is the only allowed FAIL).
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
[[ $ARM == armed-d13-6ab42af7 ]] && { echo "REFUSING: $LIVE holds 1001's arm (6ab42af7), the PRESSED one (frontier advanced into pid-1 dyld, adjudicated in records/1001-press-read.txt); build 1002 first"; exit 1; }
[[ $ARM == armed-d13-bbdf41bc ]] && { echo "REFUSING: $LIVE holds 1000's arm (bbdf41bc), the PRESSED one (death at 10.31 s, adjudicated in records/1000-press-read.txt); build 1002 first"; exit 1; }
[[ $ARM == armed-d13-467d59e4 ]] && { echo "REFUSING: $LIVE holds 999's arm (467d59e4); build 1002 first"; exit 1; }
[[ $ARM == armed-d13-dee822b0 ]] && { echo "REFUSING: $LIVE holds 998's arm (dee822b0); build 1002 first"; exit 1; }
[[ $ARM == armed-d13-518b6a94 ]] && { echo "REFUSING: $LIVE holds 997's arm (518b6a94); build 1002 first"; exit 1; }
[[ $ARM == armed-d13-cea0c1c2 ]] && { echo "REFUSING: $LIVE holds 996's arm (cea0c1c2); build 1002 first"; exit 1; }
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 1002 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 1002 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 1002 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 1002 first"; exit 1; }

# 1002 adds NO new switch, so there is no new key to guard - but the 1000 USVC hook and the 1001
# submap skip BOTH stay on (the fix is INSIDE the USVC hook, and the SUBMAPSKIP fix must remain so
# the frontier the 1001 read advanced to is not re-litigated): the park refuses an arm whose record
# does not carry BOTH ([[mi4-one-value-two-definitions]] - the record's keys and the layers' values
# must be one thing).
grep -q '^STAGE90_XNU_USVC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USVC=1 - 1002's fix lives INSIDE the USVC hook, so the hook must be on"
  exit 1; }
grep -q '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - 1001's fix must stay on, or the 1001 death would be re-litigated"
  exit 1; }

# BY VALUE, at the freeze point: the linked image's locore_fleh_swi must carry the 1002 SIX-word save
# set {r0-r3,r8,lr} and NOT the 1000/1001 four-word one. This is the arm's whole reason to exist, and
# parking an image that still clobbers r3 would freeze the defect ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
_ls=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="locore_fleh_swi"{print "0x" $1; found=1} END{exit(found?0:1)}') || {
  echo "REFUSING: locore_fleh_swi is not in the parked image - the USVC hook's body is gone"; exit 1; }
_lsx=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ls#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv && $2=="T"{if(!d){print "0x" $1; d=1}}')
_lb=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ls" --stop-address="${_lsx:-0xffffffff}")
_w6=$(grep -cE 'push[[:space:]]+\{r0, r1, r2, r3, r8, lr\}' <<<"$_lb" || true)
_w4=$(grep -cE '(push|pop)[[:space:]]+\{r0, r1, r8, lr\}' <<<"$_lb" || true)
[[ "$_w6" == 1 && "$_w4" == 0 ]] ||
  { echo "REFUSING: the parked image's locore_fleh_swi does not carry the 1002 six-word save set (six=$_w6 want 1, four=$_w4 want 0) - the arm would freeze the r3 clobber the 1001 press read diagnosed"; exit 1; }
echo "park_1002: by value - locore_fleh_swi carries the SIX-word save set {r0-r3,r8,lr} x$_w6, four-word x$_w4 (the 1002 delta is in the frozen binary)"

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1002: THE USER-SVC HOOK SAVES r3 (`{r0,r1,r8,lr}` -> `{r0-r3,r8,lr}`), SO THE `0x80000000` TRAP DISPATCH IT FEEDS KEEPS ITS LIVE r3 AND dyld STOPPED READING TPIDRURO=0.\n' "$ARM"
  printf '# Switch set is 1001-s VERBATIM (NO new switch: DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE,\n'
  printf '# SHREG_GEOM, RESIDENT, USVC and SUBMAP_PMAP_SKIP all stay 1); the ENTRY BIN moved (6ab42af7 ->\n'
  printf '# %s); the PAYLOAD moves (it embeds the entry blob; the 29 s watchdog constant is 999-s,\n' "${ARM#armed-d13-}"
  printf '# unchanged); NO POOL object moves (the change is ASSEMBLY-layer only); the TREE moves by ONE\n'
  printf '# staged edit (traps_lo.s-s save set). tools/patch_d13_usvc.py MIGRATES a tree already at the\n'
  printf '# 1000/1001 four-word form in place (and refuses a tree carrying both forms, or neither); the\n'
  printf '# rebuild is TOOLS/ASSEMBLE_ARM_LAYER.SH with STAGE90_XNU_USVC=1 EXPORTED, then relink\n'
  printf '# (build_entry.sh only LINKS traps_lo.o).\n#\n'
  printf '# THE DEFECT, BY VALUE (records/1001-press-read.txt section [6]): traps_lo.s-s 1000 hook saved\n'
  printf '# `{r0,r1,r8,lr}` around `bl _entry_note_user_svc`, but that C helper CLOBBERS r3 (built image:\n'
  printf '# `movw r3,#0xfff8`/`movt r3,#0x8068` on every path, `mov r3,r0` on the n<=64 full path), and\n'
  printf '# `fleh_swi`-s `swi_trap` dispatches the `0x80000000` traps ON LIVE r3 (`cmp r3,#3; addls\n'
  printf '# pc,pc,r3,lsl#2` - r3=3 = thread_set_cthread_trap, which writes TPIDRURO). Saving r0/r1/r8/lr\n'
  printf '# was the wrong set. The LEO sibling block two lines below saves `{r0-r12,lr}` for exactly this\n'
  printf '# reason. FIX: save r3 too - `{r0-r3,r8,lr}`. SIX words = 24 B, still 8-aligned at the `bl`\n'
  printf '# (990).\n#\n'
  printf '# THE 1002 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not\n'
  printf '# reinterpreted) is written by the TEAM-LEAD into scripts/press_1002.sh. Expected per the lead:\n'
  printf '# the USVC hook is TRANSPARENT on r3 (the SIX-word window `push {r0,r1,r2,r3,r8,lr} / mov r0,r8\n'
  printf '# / bl <entry_note_user_svc> / pop {r0,r1,r2,r3,r8,lr}` must be read IN THE IMAGE before any\n'
  printf '# judgement - a four-word window means the assembly layer was not rebuilt); dyld proceeds PAST\n'
  printf '# `check_np ret=12` into `IOS7SHREG map_slide` (the 999 reading) with NO recurrence of the 230x\n'
  printf '# `launchd[1]: usermode data abort ... at 0x00000004 ... (Translation fault on page)`; the\n'
  printf '# SUBMAPSKIP probe >= 1 (unchanged from 1001); VMHINT991 stays 0; the SURVIVAL GATE (no panic +\n'
  printf '# the exec-done trio and its five probe keys + records past the exec-return point); the pet-s\n'
  printf '# FIRST feed once the boot passes ~14.5 s with the read door in use; the tick summaries\n'
  printf '# continuing. Falsifiers: the 230x `usermode data abort at 0x4` RECURS => the r3 clobber was NOT\n'
  printf '# the cause and the structural hardness item (our tree-s missing TPIDRURO writers -\n'
  printf '# `machine_load_context`-s dead write, no `machine_thread_set_tsd_base`/`tsd_base` plumbing, no\n'
  printf '# `start.s`/`machine_routines_asm.s`) moves to the front as the 1003 target - the fix is\n'
  printf '# SELF-TESTING; death elsewhere later => the frontier ADVANCED, record by value. The frozen\n'
  printf '# regression guards (geometry exactly once, cache mapping x4, shared region x1, check_np ret=12\n'
  printf '# x1, map_file kr=0, map_slide ret=0) are re-verified only under the survival gate.\n#\n'
  printf '# VERIFIED BY VALUE (build_1002.sh): traps_lo.o AND the linked locore_fleh_swi carry the\n'
  printf '# SIX-word `{r0-r3,r8,lr}` x1 and the four-word `{r0,r1,r8,lr}` x0; the SOURCE edit is the\n'
  printf '# staged shape (one six-word stmfd + one matching ldmfd, no four-word); the payload arm site\n'
  printf '# still passes 29; the FROZEN 1001 elf as the NEGATIVE CONTROL (its locore_fleh_swi carries the\n'
  printf '# four-word x2 and the six-word x0 - a reader that cannot fail certifies nothing). The\n'
  printf '# 996/997/998/999/1000 checkers rerun ON the 1002 image with the FROZEN 1000/999 images as\n'
  printf '# negative controls - the probe checker-s ONE by-design frozen 20 B clause is the only allowed\n'
  printf '# FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1002-usvc-hook-saves-r3;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3