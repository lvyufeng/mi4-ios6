#!/usr/bin/env bash
# Park 1001 and append its block to records/revert-set.txt.
#
# 1001 = 1000's switch set VERBATIM (all checker/witness/USB-ladder switches, RESIDENT and USVC all
# stay 1) + ONE NEW switch (STAGE90_XNU_SUBMAP_PMAP_SKIP: the POOL define rides
# tools/build_xnu_arm_kernel.sh's DEFINES array - the SAME array as VMHINT_GUARD, because vm_map.c is
# a kernel-pool file - and build_entry.sh's EXPORTS records it in the entry image) + ONE staged POOL
# edit (osfmk/vm/vm_map.c: the is_submap guard in vm_map_enter's pmap-enter tail) + the bounded
# SUBMAPSKIP probe in the skip branch + ONE new stage tool (tools/stage_d13_submap_pmap_skip.sh,
# defined()-first like every 991+ kernel edit). The ENTRY BIN moves (bbdf41bc -> ?) - the new key
# rides EXPORTS, so the entry image re-records its arm keys; the PAYLOAD moves (it embeds the entry
# blob; the 29 s watchdog constant is 999's, unchanged); the POOL MOVES (osfmk_vm_vm_map.o carries
# the fix itself - unlike 1000, whose pool did not move); the TREE moves by ONE staged edit (vm_map.c
# gains the guard and the probe).
#
#   * THE FIX: vm_map_enter's pmap-enter tail (vm_map.c:2149-2161) calls `vm_map_pmap_enter` with the
#     `object` parameter - which on a SUBMAP enter IS the submap itself (:2063 `submap = (vm_map_t)
#     object;`), a vm_map, not a vm_object. The block's guard is `object != VM_OBJECT_NULL &&
#     vm_map_pmap_enter_enable && !anywhere && !needs_copy && size < 128K` - with NO `is_submap`
#     test, so the helper locks the element's +8 word as if it were a struct vm_object's lock and
#     reads its +0x18 (memq_hint) as if it were a vm_object's queue. The SAME function guards the
#     SAME parameter 30 lines later (:2192 `if (!is_submap && object != VM_OBJECT_NULL &&
#     object->named && ...)` for the pager block); the fix is that idiom: `if (!is_submap) {
#     override_nx...; vm_map_pmap_enter(...); } else { probe; }`, keeping `pmap_empty = FALSE`,
#     behind STAGE90_XNU_SUBMAP_PMAP_SKIP. All four 2050-era trees in this workspace carry the
#     unguarded call verbatim (pristine apple-xnu-rel-2050 :2143, xnu-2050.18.24 :2143, xnu-upstream
#     :2143, hd2-darwin13 :2149).
#   * THE PROBE (the VMHINT991 pattern; text channel; BOUNDED, max 8 hits): `SUBMAPSKIP: obj=<ptr>
#     start=<ptr> end=<ptr> size=<ptr> lr=<ptr>` once per skip - `obj` is the submap (on a submap
#     enter `object` IS the submap), `lr` names the callers by measurement, `start`/`end`/`size` range
#     them. The expected hits are launchd's two submap enters - the same two addresses 999's guard
#     recorded (0xc0005b48 / 0xc0005ab0) - and 1000 died AT the lock of the first.
# A fresh checkout needs ALL TWELVE stage scripts: tools/stage_d13_kextb_login.sh (988's),
# tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it,
# 10 edits), tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 amendment #2), tools/stage_d13_usvc.sh (1000's - the traps_lo.s
# hook; its patcher tools/patch_d13_usvc.py inserts the 13-line block idempotently) and
# tools/stage_d13_submap_pmap_skip.sh (1001's - the vm_map.c is_submap guard + the SUBMAPSKIP probe,
# in a POOL file this time).
#
# WHY (the 1000 press read, 2026-10-13, arm armed-d13-bbdf41bc, adjudication in
# records/1000-press-read.txt): the run DIED at XNU time 10.31 s - `panic(cpu 0 caller 0x80498058):
# hw_lock_lock(): PANIC: Lock 0xc0005b50 = 0xffb5be0b`, immediately after `attempting to start init
# of /sbin/launchd`. The locked word - 8 = 0xc0005b48 = the "object" 999's VMHINT991 guard had
# already recorded (obj=0xc0005b48 / 0xc0005ab0, w04=0xbaddecaf = zalloc's free marker => both are
# FREED vm_map-zone elements passed as vm_objects). 990 died on the same two non-objects one field
# over (+0x18, vm_page_lookup); 999's benign DRAM words let it live; 1000's +8 word held 0xffb5be0b
# (bit0 set) and hw_lock_lock panicked. The death point sits BEFORE every one of 1000's instruments -
# the adjudicator's five anomalies are ALL death artifacts (its own §[5]), ZERO regressions. 1001
# removes the common cause: the call that locks a vm_map as if it were a vm_object.
#
# THE 1001 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not reinterpreted) is
# written by the TEAM-LEAD into scripts/press_1001.sh. Expected per the lead: the SUBMAPSKIP probe
# >= 1 (most likely exactly 2, obj = 999's two VMHINT991 objects 0xc0005b48 / 0xc0005ab0); VMHINT991
# **0 WHILE THE BOOT SURVIVES** (INVERTED from 999's 2 - the call is skipped before the lookup; any
# count > 0 while surviving = the skip did not reach the image, or another caller path still arrives
# at the lookup with a non-object); the survival gate (no panic + exec-done trio and probe + records
# past the exec-return point) - guard absence counts ONLY under it, the 1000 adjudicator's lesson,
# carried by tools/check_1001_capture.py; the USVC first records (8 field keys, first 64 in full -
# the frontier 1000 armed and never reached; `cs_invalid_page` may recur, which would mean the dyld
# frontier MOVED rather than stopped); the pet's first feed once the boot passes ~14.5 s with the
# read door in use (1000's zero feeds was the 10.31 s death - the gate is bark/2 - not a door
# failure); the tick summaries continuing (the 2.58 s masked-window question stays OPEN). Falsifiers:
# death at the SAME call (hw_lock_lock / vm_map_pmap_enter / vm_map_enter) with SUBMAPSKIP ABSENT =>
# the skip did not reach the image - REBUILD, do not re-adjudicate; death elsewhere later => the
# frontier ADVANCED, record by value. The frozen regression guards (geometry exactly once, cache
# mapping x4, shared region x1, check_np ret=12 x1, map_file kr=0, map_slide ret=0) are re-verified
# only under the survival gate.
#
# VERIFIED BY VALUE (build_1001.sh, run against the linked image): the staged vm_map.c edit BY SOURCE
# (`!is_submap` inside the existing pmap-enter block, `pmap_empty = FALSE` kept, the SUBMAPSKIP
# literal and its <= 8 bound in the same guarded region, defined()-first like every 991+ kernel edit)
# and BY OBJECT (`osfmk_vm_vm_map.o` carries the SUBMAPSKIP literal); the fresh record carries
# `STAGE90_XNU_SUBMAP_PMAP_SKIP=1`; the FROZEN 1000 elf as the NEGATIVE CONTROL (0 SUBMAPSKIP
# literals - a reader that cannot fail certifies nothing). The 996/997/998/999/1000 checkers rerun ON
# the 1001 image with the FROZEN 1000/999 images as negative controls (the probe checker's ONE
# by-design frozen 20 B clause is the only allowed FAIL).
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
# PRESSED 1000 (bbdf41bc) or the earlier pressed/confirmed arms under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-bbdf41bc ]] && { echo "REFUSING: $LIVE holds 1000's arm (bbdf41bc), the PRESSED one (death at 10.31 s, adjudicated in records/1000-press-read.txt); build 1001 first"; exit 1; }
[[ $ARM == armed-d13-467d59e4 ]] && { echo "REFUSING: $LIVE holds 999's arm (467d59e4); build 1001 first"; exit 1; }
[[ $ARM == armed-d13-dee822b0 ]] && { echo "REFUSING: $LIVE holds 998's arm (dee822b0); build 1001 first"; exit 1; }
[[ $ARM == armed-d13-518b6a94 ]] && { echo "REFUSING: $LIVE holds 997's arm (518b6a94); build 1001 first"; exit 1; }
[[ $ARM == armed-d13-cea0c1c2 ]] && { echo "REFUSING: $LIVE holds 996's arm (cea0c1c2); build 1001 first"; exit 1; }
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 1001 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 1001 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 1001 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 1001 first"; exit 1; }

# The new switch's own guard: the park freezes the record AS THE ARM'S FINGERPRINT, and the press
# refuses an arm whose record does not name the switch the image was built with
# ([[mi4-one-value-two-definitions]] - the record's key and the layers' value must be one thing).
grep -q '^STAGE90_XNU_SUBMAP_PMAP_SKIP=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_SUBMAP_PMAP_SKIP=1 - the park would freeze an arm whose new switch the record does not name"
  exit 1; }

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1001: THE SUBMAP-AS-OBJECT CALL IS SKIPPED (vm_map_enter-s pmap-enter tail, `!is_submap`), THE SKIP BRANCH REPORTS ITSELF (SUBMAPSKIP, bounded, <= 8), AND THE SURVIVING BOOT OPENS THE USVC/PET READINGS 1000 ARMED.\n' "$ARM"
  printf '# Switch set is 1000-s VERBATIM (DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE, SHREG_GEOM, RESIDENT\n'
  printf '# and USVC all stay 1) plus ONE NEW switch, STAGE90_XNU_SUBMAP_PMAP_SKIP (the POOL define\n'
  printf '# rides build_xnu_arm_kernel.sh-s DEFINES array - the SAME array as VMHINT_GUARD, vm_map.c\n'
  printf '# being a kernel-pool file - and build_entry.sh-s EXPORTS records it in the entry image); the\n'
  printf '# ENTRY BIN moved (bbdf41bc -> %s); the PAYLOAD moves (it embeds the entry blob; the 29 s\n' "${ARM#armed-d13-}"
  printf '# watchdog constant is 999-s, unchanged); the POOL MOVES this time (osfmk_vm_vm_map.o carries\n'
  printf '# the fix - 1000-s pool did not move); the TREE moves by ONE staged edit (vm_map.c gains the\n'
  printf '# guard and the probe). build_entry.sh carries the switch end-to-end (new arm key parsed +\n'
  printf '# guarded, ENTRY_ARM_KEYS read-back, record writer) - the VMHINT_GUARD shape, not the USVC one\n'
  printf '# (USVC had an ASFLAGS half; this one is pool-side).\n#\n'
  printf '# THE FIX, BY VALUE (records/1000-press-read.txt - "Root cause"): vm_map_enter-s pmap-enter\n'
  printf '# tail (vm_map.c:2149-2161) calls `vm_map_pmap_enter` with the `object` parameter - which on a\n'
  printf '# SUBMAP enter IS the submap itself (:2063 `submap = (vm_map_t) object;`), a vm_map, not a\n'
  printf '# vm_object. The block-s guard is `object != VM_OBJECT_NULL && vm_map_pmap_enter_enable &&\n'
  printf '# !anywhere && !needs_copy && size < 128K` - with NO `is_submap` test - so the helper locks the\n'
  printf '# element-s +8 word as if it were a struct vm_object-s lock and reads its +0x18 (memq_hint) as\n'
  printf '# if it were a vm_object-s queue. The SAME function guards the SAME parameter 30 lines later\n'
  printf '# (:2192 pager block: `if (!is_submap && object != VM_OBJECT_NULL && object->named && ...)`);\n'
  printf '# the fix is that idiom: `if (!is_submap) { override_nx...; vm_map_pmap_enter(...); } else {\n'
  printf '# probe; }`, keeping `pmap_empty = FALSE`, behind STAGE90_XNU_SUBMAP_PMAP_SKIP (defined()-first\n'
  printf '# like every 991+ kernel edit; tools/stage_d13_submap_pmap_skip.sh). All four 2050-era trees in\n'
  printf '# this workspace carry the unguarded call verbatim (pristine apple-xnu-rel-2050 :2143,\n'
  printf '# xnu-2050.18.24 :2143, xnu-upstream :2143, hd2-darwin13 :2149).\n#\n'
  printf '# THE PROBE (the VMHINT991 pattern; text channel; BOUNDED, max 8 hits): `SUBMAPSKIP: obj=<ptr>\n'
  printf '# start=<ptr> end=<ptr> size=<ptr> lr=<ptr>` once per skip - `obj` is the submap, `lr` names\n'
  printf '# the callers by measurement and `start`/`end`/`size` range them. The reading it makes possible\n'
  printf '# is positional: SUBMAPSKIP present + VMHINT991 0 + survival = the call was skipped before the\n'
  printf '# lookup AND the boot lived; SUBMAPSKIP absent + death at the same call = the skip did not\n'
  printf '# reach the image.\n#\n'
  printf '# 1000 (pressed 2026-10-13, arm armed-d13-bbdf41bc, evidence out/stage90/captures/,\n'
  printf '# records/1000-press-read.txt) DIED at XNU time 10.31 s: `panic(cpu 0 caller 0x80498058):\n'
  printf '# hw_lock_lock(): PANIC: Lock 0xc0005b50 = 0xffb5be0b`, immediately after `attempting to\n'
  printf '# start init of /sbin/launchd`. The locked word - 8 = 0xc0005b48 = the "object" 999-s\n'
  printf '# VMHINT991 guard had already recorded (obj=0xc0005b48 / 0xc0005ab0, w04=0xbaddecaf =\n'
  printf '# zalloc-s free marker => both are FREED vm_map-zone elements passed as vm_objects). 990 died\n'
  printf '# on the same two non-objects one field over (+0x18, vm_page_lookup); 999-s benign DRAM words\n'
  printf '# let it live; 1000-s +8 word held 0xffb5be0b (bit0 set) and hw_lock_lock panicked. Three\n'
  printf '# fields of the same two non-objects, one common cause: the call. The death point sits BEFORE\n'
  printf '# every one of 1000-s instruments - the adjudicator-s five anomalies are ALL death artifacts\n'
  printf '# (its own [5]), ZERO regressions (the tick geometry - 16 = 4x4 anomaly fields + 12 = 3x4\n'
  printf '# summaries - is the instrument that proves the image WAS 1000-s).\n#\n'
  printf '# The 1001 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not\n'
  printf '# reinterpreted) is written by the TEAM-LEAD into scripts/press_1001.sh. Expected per the lead:\n'
  printf '# the SUBMAPSKIP probe >= 1 (most likely EXACTLY 2, with obj equal to 999-s two VMHINT991\n'
  printf '# objects 0xc0005b48 / 0xc0005ab0 - report by value; absence while survived = the probe did not\n'
  printf '# reach the image); VMHINT991 **0 WHILE THE BOOT SURVIVES** (INVERTED from 999-s 2 - the call\n'
  printf '# is skipped before the lookup; any count > 0 while surviving = the skip did not reach the\n'
  printf '# image OR another caller path still arrives at the lookup with a non-object - a reading); the\n'
  printf '# SURVIVAL GATE (no panic + the exec-done trio and its five probe keys + records past the\n'
  printf '# exec-return point) - guard absence counts ONLY under it, the 1000 adjudicator-s lesson,\n'
  printf '# carried by tools/check_1001_capture.py; the USVC first records (8 field keys, first 64 in\n'
  printf '# full - the frontier 1000 armed and never reached; `cs_invalid_page` may recur, which would\n'
  printf '# mean the dyld frontier MOVED rather than stopped); the pet-s FIRST feed (`xnu_live_wdt_pet_n`)\n'
  printf '# once the boot passes ~14.5 s with the read door in use (1000-s zero feeds was the 10.31 s\n'
  printf '# death - the gate is bark/2 = 14.5 s - not a door failure; zero feeds past 14.5 s with the\n'
  printf '# door used = the door stopped or the gate moved, a READING); the tick summaries continuing\n'
  printf '# (the 2.58 s masked-window question stays OPEN). Falsifiers: death at the SAME call\n'
  printf '# (hw_lock_lock / vm_map_pmap_enter / vm_map_enter) with SUBMAPSKIP ABSENT => the skip did not\n'
  printf '# reach the image - REBUILD, do not re-adjudicate; death elsewhere later => the frontier\n'
  printf '# ADVANCED, record by value. The frozen regression guards (geometry exactly once, cache mapping\n'
  printf '# x4, shared region x1, check_np ret=12 x1, map_file kr=0, map_slide ret=0) are re-verified\n'
  printf '# only under the survival gate.\n#\n'
  printf '# VERIFIED BY VALUE (build_1001.sh): the staged vm_map.c edit BY SOURCE (`!is_submap` inside\n'
  printf '# the existing pmap-enter block, `pmap_empty = FALSE` kept, the SUBMAPSKIP literal and its\n'
  printf '# <= 8 bound in the same guarded region, defined()-first) and BY OBJECT (`osfmk_vm_vm_map.o`\n'
  printf '# carries the SUBMAPSKIP literal); the fresh record carries `STAGE90_XNU_SUBMAP_PMAP_SKIP=1`;\n'
  printf '# the FROZEN 1000 elf as the NEGATIVE CONTROL (0 SUBMAPSKIP literals - a reader that cannot\n'
  printf '# fail certifies nothing). The 996/997/998/999/1000 checkers rerun ON the 1001 image with the\n'
  printf '# FROZEN 1000/999 images as negative controls - the probe checker-s ONE by-design frozen 20 B\n'
  printf '# clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1001-submap-pmap-skip+probe;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3