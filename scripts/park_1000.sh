#!/usr/bin/env bash
# Park 1000 and append its block to records/revert-set.txt.
#
# 1000 = 999's switch set VERBATIM (all checker/witness/USB-ladder switches and RESIDENT stay 1) + ONE
# NEW switch (STAGE90_XNU_USVC: the ASSEMBLY layer's ASFLAGS half via tools/assemble_arm_layer.sh +
# build_entry.sh's EXPORTS; NO pool object reads it - the same shape RESIDENT had at 999) + FOUR
# C-side edits in the entry image + ONE staged assembly hook + ONE already-committed move (the
# read-dump thinning ordinal 256 -> 64, commit 0504be5) + the saved-state table pair + TWO new tools.
# The ENTRY BIN moves (467d59e4 -> ?); the PAYLOAD moves (it embeds the entry blob; the 29 s watchdog
# constant is 999's, unchanged); the POOL does NOT move (no pool source edit this rung); the TREE
# moves by ONE staged edit (traps_lo.s gains the hook).
#
#   * (a) the tick RATE COLLAPSE is measured at the DELIVERY: `msm8974_tick_sample` (pe_msm8974.c,
#     noinline) is ONE call in `msm8974_handle_interrupt`'s timer branch BEFORE its EOI; it reads
#     CNTVCT and, only past one full period, publishes the delta `_d` and the raw pair `_tval`/`_ctl`
#     read AT the entry (first four events, DIRECT). `entry_tick_summary` publishes the aggregates
#     `_sum_count`/`_sum_anom`/`_sum_maxd`. A second CNTV writer and a long masked window are
#     DIFFERENT logs ([[mi4-one-value-two-definitions]]: the deadline lives in CNTV_TVAL, so the
#     helper takes its delta from CNTVCT - mrrc p15, 1 - not its physical twin).
#   * (b) the absence-is-evidence channels go DIRECT (999's cap bit again and swallowed the pet's own
#     evidence): the pet's feed records move under `xnu_live_wdt_pet_n`/`_calls`/`_countdown` (first
#     four feeds in full, then a powers-of-two ladder); the tick-anomaly records are direct from
#     birth; the summaries ride the pet's sparse gate AND the exec-done point
#     (`entry_note_exec_returned`) - the two moments the counted channel goes quiet.
#   * (c) the user-SVC FRONTIER after `cs_invalid_page`: `entry_note_user_svc` (entry_stubs.c) called
#     from ONE `bl _entry_note_user_svc` hooked into traps_lo.s's `fleh_swi` right after `cpsie i`
#     (staged by tools/patch_d13_usvc.py -> tools/stage_d13_usvc.sh), gated by STAGE90_XNU_USVC. The
#     first 64 SVCs publish all eight fields DIRECT (n/num/pid/thr/pc/lr/cpsr/r0); a powers-of-two
#     ladder follows - so the LAST `xnu_live_usvc_n` in a log IS the last syscall the user side
#     managed.
#   * the saved-state table gains the two words the C half reads (`STAGE90_SS_R0`, `STAGE90_SS_R12`
#     in entry_saved_state.h), with tools/check_saved_state_offsets.py deriving the offsets from the
#     assym sources (selftest included) and wired into `make check` (three lines: --selftest, the
#     default 4570 tree, and the D13 tree variant).
# A fresh checkout needs ALL ELEVEN stage scripts: tools/stage_d13_kextb_login.sh (988's),
# tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it,
# 10 edits), tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's),
# tools/stage_d13_timer_edge.sh (999 amendment #2) and tools/stage_d13_usvc.sh (1000's - the
# traps_lo.s hook; its patcher tools/patch_d13_usvc.py inserts the 13-line block idempotently).
#
# WHY (the 999 press read, 2026-10-13, arm armed-d13-467d59e4, adjudication in
# records/999-press-read.txt): the run reached USER MODE - the exec-done trio byte-closed at :28715
# and launchd's dyld mapped the 438 shared cache (`IOS7SHREG map_slide ret=0` :4113) - and then the
# COUNTED channel goes quiet after `cs_invalid_page` (:4114). The machine stayed ALIVE (31 min, no
# USB return at all, fed by the read door the whole time; 998, unfed, bit itself in ~28 s). Two walls
# bounded that reading: the record cap bit AGAIN (tombstone `xnu_live_capped=0x6000` @:28714) and the
# silence itself, which cannot say where pid 1 stopped. 1000 changes WHERE the evidence is recorded
# (direct channels the cap cannot swallow), adds ONE probe at the delivery, and opens ONE frontier
# reader; it changes NO kernel behaviour.
#
# THE 1000 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not reinterpreted) is
# written by the TEAM-LEAD into scripts/press_1000.sh. Expected reading per the lead: the first 64
# SVCs each with the eight `xnu_live_usvc_*` fields (pid expected 1 = launchd; num a plausible
# launchd syscall, ladder pairs `_n`/`_num` past 64); `xnu_live_tick_sum_count` climbing past 999's
# 122 if the machine lived on; <= 4 `xnu_live_tick_anom_*` events each carrying d/tval/ctl (healthy
# d ~ one 19,200-tick period + vector latency); the pet feed ladder `xnu_live_wdt_pet_n`/`_calls`/
# `_countdown` present REGARDLESS of the cap with `_calls` monotone and never above the last
# preceding `xnu_live_storage_rd_ordinal`. Falsifier: `xnu_live_usvc_n` ABSENT with the exec proven
# (done trio present, map_slide ret=0) => the hook did not reach the image or the state pointer is
# wrong - rebuild, do not judge.
#
# VERIFIED BY VALUE (build_1000.sh, run against the linked image): the thinning ordinal AT THE
# SOURCE (`rd_full_ = (st_read_count < 64u);` exactly once, no `< 256u` survivor, and the key-order
# tool whose --selftest the build runs PARSES the ordinal rather than restating it); `USVC_FULL_N 64u`;
# the saved-state checker's --selftest + both trees; the fresh record carries `STAGE90_XNU_USVC=1`;
# `entry_note_user_svc` is a T symbol; traps_lo.o has exactly one `U entry_note_user_svc`; the
# `locore_fleh_swi` window (reader ranged to the next GLOBAL T - the body has internal labels) has
# exactly ONE hook call, IMAGE-WIDE one, and the hook is the body's FIRST branch (before the -3
# timebase fast path); the C half's window: >= 8 `entry_write_kv` / 0 `entry_live_write`, `cmp #64`
# present, exactly 8 `xnu_live_usvc_*` keys; the FROZEN 999 elf as the negative control (0 usvc keys,
# 0 tick sum_/anom_ keys); `entry_tick_summary` a T symbol, the sample's ONE call site inside
# `msm8974_handle_interrupt`'s own window, each of the two summary call sites (`entry_note_exec_returned`,
# `entry_wdt_pet`) calls it exactly once, and the ten new direct keys once each (3 summary + 4
# anomaly + 3 pet); `mov r0, #29` at the payload's arm site (the 29 s net cannot silently fall back
# to 25). The 996/997/998 checkers rerun ON the 1000 image with the FROZEN 999/998/996 images as
# negative controls (the probe checker's ONE by-design frozen 20 B clause is the only allowed FAIL).
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
# PRESSED 999 (467d59e4) or the earlier pressed/confirmed arms under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-467d59e4 ]] && { echo "REFUSING: $LIVE holds 999's arm (467d59e4), the PRESSED one; build 1000 first"; exit 1; }
[[ $ARM == armed-d13-dee822b0 ]] && { echo "REFUSING: $LIVE holds 998's arm (dee822b0); build 1000 first"; exit 1; }
[[ $ARM == armed-d13-518b6a94 ]] && { echo "REFUSING: $LIVE holds 997's arm (518b6a94); build 1000 first"; exit 1; }
[[ $ARM == armed-d13-cea0c1c2 ]] && { echo "REFUSING: $LIVE holds 996's arm (cea0c1c2); build 1000 first"; exit 1; }
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 1000 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 1000 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 1000 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 1000 first"; exit 1; }

# The new switch's own guard: the park freezes the record AS THE ARM'S FINGERPRINT, and the press
# refuses an arm whose record does not name the switch the image was built with
# ([[mi4-one-value-two-definitions]] - the record's key and the layers' value must be one thing).
grep -q '^STAGE90_XNU_USVC=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_USVC=1 - the park would freeze an arm whose new switch the record does not name"
  exit 1; }

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1000: THE TICK RATE IS MEASURED AT THE DELIVERY (CNTVCT delta + the raw TVAL/CTL pair), THE ABSENCE-IS-EVIDENCE RECORDS GO DIRECT (pet feed + tick anomalies + summaries), AND THE USER-SVC FRONTIER IS OPEN BY VALUE (first 64 SVCs, 8 fields each).\n' "$ARM"
  printf '# Switch set is 999-s VERBATIM (DYLD_WITNESS, SHREG_PROBE, SHREG_WIRE, SHREG_GEOM, RESIDENT all\n'
  printf '# stay 1) plus ONE NEW switch, STAGE90_XNU_USVC (asm ASFLAGS + build_entry EXPORTS; NO pool\n'
  printf '# object reads it); the ENTRY BIN moved (467d59e4 -> %s); the PAYLOAD moves (it embeds the\n' "${ARM#armed-d13-}"
  printf '# entry blob; the 29 s watchdog constant is 999-s, unchanged); the POOL does NOT move (no pool\n'
  printf '# source edit this rung - 1000 adds no key to build_xnu_arm_kernel.sh-s EXTRA either); the\n'
  printf '# TREE moves by ONE staged edit (traps_lo.s gains the USVC hook). build_entry.sh carries the\n'
  printf '# switch end-to-end (new arm key parsed + guarded, ENTRY_ARM_KEYS read-back, record writer).\n#\n'
  printf '# THE THREE GIVENS the run answers (the 999 read-s own [134]-[141]), and what carries each:\n'
  printf '# (a) THE TICK RATE COLLAPSE - not a stop: 122 deliveries with 24 of them across the >= 2.4 s the\n'
  printf '#     rest of the boot took (the :5468 powers-of-two ladder vs the :28723 direct probes).\n'
  printf '#     `msm8974_tick_sample` (pe_msm8974.c, noinline) is ONE call in `msm8974_handle_interrupt`-s\n'
  printf '#     timer branch BEFORE its EOI; it reads CNTVCT (the counter the deadline is expressed in -\n'
  printf '#     mrrc p15, 1 - never its physical twin) and, only past one full period, publishes the raw\n'
  printf '#     pair `xnu_live_tick_anom_tval`/`_ctl` read AT the entry plus the delta `_d` (first four\n'
  printf '#     events; `_n` is the ordinal, incremented before anything is written). A SECOND CNTV writer\n'
  printf '#     (`_tval` a value the gap could not produce) and a long MASKED window (`_tval` consistent,\n'
  printf '#     `_ctl` IMASK/ENABLE) are DIFFERENT logs. `entry_tick_summary` publishes `_sum_count` /\n'
  printf '#     `_sum_anom` / `_sum_maxd` - the aggregates without the sampling rule.\n'
  printf '# (b) A RECORD WHOSE ABSENCE IS READ AS EVIDENCE MUST BE CAP-EXEMPT (999-s cap bit again at\n'
  printf '#     0x6000 and swallowed the pet-s own evidence). The pet-s feed records move to the DIRECT\n'
  printf '#     channel under `xnu_live_wdt_pet_n`/`_calls`/`_countdown` (first four feeds in full, then a\n'
  printf '#     powers-of-two ladder); the tick-anomaly records are direct from birth; the summaries ride\n'
  printf '#     the pet-s sparse gate AND the exec-done point (`entry_note_exec_returned`) - the two\n'
  printf '#     moments the counted channel goes quiet.\n'
  printf '# (c) THE USER-SVC FRONTIER after `cs_invalid_page`: `entry_note_user_svc` (entry_stubs.c) called\n'
  printf '#     from ONE `bl _entry_note_user_svc` hooked into traps_lo.s-s `fleh_swi` right after\n'
  printf '#     `cpsie i` (staged by tools/patch_d13_usvc.py -> tools/stage_d13_usvc.sh; the 13-line block\n'
  printf '#     pushes {r0,r1,r8,lr}, passes r8 = the saved-state frame, pops it back - a `bl` between\n'
  printf '#     `cpsie i` and the -3 timebase fast path, the body-s FIRST branch). Gated by\n'
  printf '#     STAGE90_XNU_USVC (ASFLAGS half via tools/assemble_arm_layer.sh). The first 64 SVCs publish\n'
  printf '#     all eight fields DIRECT (n/num/pid/thr/pc/lr/cpsr/r0, USVC_FULL_N 64u); a powers-of-two\n'
  printf '#     ladder follows - so the LAST `xnu_live_usvc_n` in a log IS the last syscall the user side\n'
  printf '#     managed before it stopped. `num` is `saved_state.r[12]` (STAGE90_SS_R12), the frame\n'
  printf '#     word the SWI convention puts the syscall number in.\n#\n'
  printf '# PLUS the ordinary move committed separately (0504be5): the read-dump thinning ordinal\n'
  printf '# 256 -> 64 (`rd_full_ = (st_read_count < 64u);` in entry_storage.c). 999 measured 218 reads in\n'
  printf '# the WHOLE boot, so 256 never fired and the full-width dump spent the cap before exec - the\n'
  printf '# thinning was dead code that boot. The key-order tool parses the ordinal from the source (its\n'
  printf '# --selftest runs in the build); a `|`-less restatement elsewhere is what the build refuses.\n#\n'
  printf '# AND the saved-state table gains the two words the C half reads (`STAGE90_SS_R0`,\n'
  printf '# `STAGE90_SS_R12` in entry_saved_state.h), with tools/check_saved_state_offsets.py deriving\n'
  printf '# the offsets from the assym SOURCES (selftest included; wired into `make check` with both the\n'
  printf '# default 4570 tree and the D13 tree) - a wrong offset would print a field of the WRONG frame\n'
  printf '# word, and the USVC records are exactly such fields.\n#\n'
  printf '# 999 (pressed 2026-10-13, arm armed-d13-467d59e4, evidence out/stage90/captures/ -\n'
  printf '# records/999-press-read.txt) reached USER MODE: the exec-done trio byte-closed at :28715,\n'
  printf '# `IOS7SHREG map_slide ret=0` at :4113 (the 438 mapped the shared cache), then the counted\n'
  printf '# channel goes quiet after `cs_invalid_page` (:4114). The machine stayed ALIVE (31 min, no USB\n'
  printf '# return at all, fed by the read door). TWO walls bounded that reading: the cap bit AGAIN\n'
  printf '# (`xnu_live_capped=0x6000` @:28714) and the silence itself. 1000 changes WHERE the evidence is\n'
  printf '# recorded, adds ONE probe at the delivery, and opens ONE frontier reader - NO kernel behaviour\n'
  printf '# change.\n#\n'
  printf '# The 1000 PRESS PRE-REGISTRATION (frozen once embedded; deviations reported, not reinterpreted)\n'
  printf '# is written by the TEAM-LEAD into scripts/press_1000.sh. Expected per the lead: the first 64\n'
  printf '# SVCs each with the eight usvc fields (pid expected 1 = launchd; a plausible launchd syscall;\n'
  printf '# ladder pairs `_n`/`_num` past 64); `xnu_live_tick_sum_count` climbing past 999-s 122 if the\n'
  printf '# machine lived on; <= 4 `xnu_live_tick_anom_*` events each carrying d/tval/ctl; the pet feed\n'
  printf '# ladder present REGARDLESS of the cap, `_calls` monotone and never above the last preceding\n'
  printf '# `xnu_live_storage_rd_ordinal`. Falsifier: `xnu_live_usvc_n` ABSENT with the exec proven\n'
  printf '# (done trio present, map_slide ret=0) => the hook did not reach the image or the state pointer\n'
  printf '# is wrong - rebuild, do not judge.\n#\n'
  printf '# VERIFIED BY VALUE (build_1000.sh): the thinning ordinal AT THE SOURCE (exactly one\n'
  printf '# `rd_full_ = (st_read_count < 64u);`, no `< 256u` survivor, key-order tool --selftest), the\n'
  printf '# `USVC_FULL_N 64u` define, the saved-state checker (--selftest + both trees), the fresh record\n'
  printf '# carrying `STAGE90_XNU_USVC=1`; in the LINKED IMAGE: `entry_note_user_svc` a T symbol,\n'
  printf '# traps_lo.o with exactly one `U entry_note_user_svc`, the `locore_fleh_swi` window (reader\n'
  printf '# ranged to the next GLOBAL T - the body has internal labels, so the C readers first-T/t rule\n'
  printf '# would stop early; same no-early-exit awk as 999-s edge reader) carrying exactly ONE hook call,\n'
  printf '# IMAGE-WIDE exactly one, the hook as the body-s FIRST branch, the C half-s window with >= 8\n'
  printf '# entry_write_kv / 0 entry_live_write / `cmp #64` / exactly 8 `xnu_live_usvc_*` keys; the FROZEN\n'
  printf '# 999 elf as the NEGATIVE CONTROL (0 usvc keys, 0 tick sum_/anom_ keys); `entry_tick_summary` a\n'
  printf '# T symbol, the sample-s ONE call site inside `msm8974_handle_interrupt`, each of the two summary\n'
  printf '# call sites (`entry_note_exec_returned`, `entry_wdt_pet`) calling it exactly once, the ten new\n'
  printf '# direct keys once each (3 summary + 4 anomaly + 3 pet); `mov r0, #29` at the payload-s arm site\n'
  printf '# (the 29 s net cannot silently fall back to 999-s predecessor). The 996/997/998 checkers rerun\n'
  printf '# ON the 1000 image with the FROZEN 999/998/996 images as negative controls - the probe\n'
  printf '# checker-s ONE by-design frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1000-tick-rate+cap-exempt+usvc;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3