#!/usr/bin/env bash
# Park 999 and append its block to records/revert-set.txt.
#
# 999 = 998's switch set VERBATIM (NO new switch in this rung) + FIVE entry-layer edits + ONE payload
# edit + ONE pool edit. The ENTRY BIN moves (dee822b0 -> ?); the PAYLOAD bytes move (the watchdog
# constant is embedded); the pool moves (bsd/vm/vm_unix.c gains the three slide prints via the
# vmabi-recon lane's extended tools/patch_d13_shreg_probe.py - now 10 edits / 8 literals).
#   * entry cap 16384 -> 24576 (entry_stubs.c), the read dump THINS past ordinal 256 (ST_LIVE_RD +
#     rd_full_ in entry_storage.c), the exec-done trio becomes DIRECT/uncounted with a sig/size
#     read-back probe (entry_stubs.c), and the WDT is fed from the storage read door
#     (entry_storage.c; entry_wdt_pet is non-static in entry_trace.c; build_entry.sh passes the
#     EXISTING -DSTAGE90_XNU_RESIDENT to entry_storage.c-s two compile lines; the reference is
#     WEAK + null-guarded - the first build died on the pass-1 stub collision, see the record).
#   * the timer discriminator (AMENDMENT #2, added 2026-10-13): `entry_timer_edge_probe`
#     (pe_msm8974.c beside the raw tick counter) called from TWO staged ASSEMBLY hooks -
#     machine_routines_asm.s-s enable edge (after `cpsie i`) and cswitch.s-s `return_to_user` tail -
#     both under the EXISTING -DSTAGE90_XNU_RESIDENT, which tools/assemble_arm_layer.sh now appends
#     to ASFLAGS; two NEW tools (tools/patch_d13_timer_edge.py + tools/stage_d13_timer_edge.sh).
#     NO pool object is touched by it (the C helper is entry-side).
#   * payload: STAGE90_HW_WATCHDOG_TIMEOUT_S 25u -> 29u (stage90.h:4211) - the arithmetic ceiling.
#   * pool: the 999 slide-half probe ladder inside _shared_region_slide (PRINTF ONLY).
# A fresh checkout needs ALL TEN stage scripts: tools/stage_d13_kextb_login.sh (988's),
# tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's), tools/stage_d13_shreg_probe.sh (996's - 999 extended it,
# 10 edits), tools/stage_d13_shreg_wire.sh (997's), tools/stage_d13_shreg_geom.sh (998's) and
# tools/stage_d13_timer_edge.sh (999 amendment #2 - the two assembly hooks).
#
# WHY (998's press, 2026-10-12, arm armed-d13-dee822b0, commit 36bb341, evidence
# out/stage90/captures/cancro-last_kmsg-998-armed-d13-dee822b0.txt): G1 CONFIRMED - the geometry
# literal prints exactly ONCE (:4098) and `IOS7SHREG map_file kr=0` (:4109) is the FIRST time in the
# series the shared cache file maps; the whole 997 halt chain (libbsm / witness / udf / `dyld>`) is
# GONE. The reading is bounded by the INSTRUMENT and the POLICY, not the machine: the 16384 record
# cap was hit MID-DYLD (`xnu_live_capped=0x00004000` at :20502 - that tombstone IS the displaced
# `xnu_live_exec_done_seq`) and ZERO `xnu_live_wdt_*` records exist with no wdt/usb/power trace (the
# SoC watchdog reset the phone silently at ~28 s: the handed-off kernel never reached an idle site).
# 999 changes the instrument and the net-s length; it changes NO kernel behaviour.
#
# THE 999 PRESS- PRE-REGISTRATION (frozen once embedded; deviations reported, not reinterpreted) is
# written by the TEAM-LEAD into scripts/press_999.sh (the build side is the plumbing this rung parks;
# the pre-registration block follows the 998 pattern: switch set, the point, the failure signature(s),
# falsification, regression guards). Expected reading per the lead: `xnu_live_wdt_bark=0xE7FA9` and
# `hw_watchdog_bite_ticks_written=0x000FFFA0` / `hw_watchdog_timeout_s=0x0000001d` once each, the
# `xnu_live_wdt_map/_via/_desc` trio once, `xnu_live_wdt_pet_calls` >= 1 growing at powers of two and
# never above the running `xnu_live_storage_rd_ordinal`, and the record log living PAST 998-s wall
# (the 0x4000 tombstone) toward 24576; falsifier: `xnu_live_wdt_map=0` with `_desc` NOT a 1 MB block
# of PA 0xf9000000.
#
# VERIFIED BY VALUE: build_999.sh asserts the 24576 cap constant, the 70/8 read-body split (six
# locating cells never thin), the five `xnu_live_exec_done_probe_` keys in the linked image, the
# door-s `bl <entry_wdt_pet>` in `entry_storage_driver_read`-s own window, the three `IOS7SHREG
# slide_` literals in bsd_vm_vm_unix.o, and `mov r0, #29` at the payload arm site (998 measured
# `mov r0, #25`); the checkers rerun ON the 999 image with the FROZEN 998/996/994 images as negative
# controls (the probe checker-s ONE by-design frozen 20 B clause is the only allowed FAIL).
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
# PRESSED 998 (dee822b0) or the earlier pressed/confirmed arms under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-dee822b0 ]] && { echo "REFUSING: $LIVE holds 998's arm (dee822b0), the PRESSED one; build 999 first"; exit 1; }
[[ $ARM == armed-d13-518b6a94 ]] && { echo "REFUSING: $LIVE holds 997's arm (518b6a94); build 999 first"; exit 1; }
[[ $ARM == armed-d13-cea0c1c2 ]] && { echo "REFUSING: $LIVE holds 996's arm (cea0c1c2); build 999 first"; exit 1; }
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 999 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 999 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 999 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 999 first"; exit 1; }
PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  999: THE RECORD CHANNEL READS PAST 998-S CAP (16384 -> 24576 + THINNED READS), THE EXEC-DONE RECORDS BECOME CAP-EXEMPT, THE WDT IS FED AT THE STORAGE READ DOOR, THE OUTER NET MOVES 25s -> 29s, AND THE SLIDE-HALF PROBE LADDER IS IN THE POOL.\n' "$ARM"
  printf '# Switch set is 998-s VERBATIM (STAGE90_XNU_DYLD_WITNESS, STAGE90_XNU_SHREG_PROBE, SHREG_WIRE,\n'
  printf '# SHREG_GEOM, RESIDENT all stay 1 - NO new switch); the ENTRY BIN moved (dee822b0 -> %s); the\n' "${ARM#armed-d13-}"
  printf '# PAYLOAD bytes move (the watchdog constant is embedded in stage90.img); the pool moves\n'
  printf '# (bsd/vm/vm_unix.c gains the three slide prints via the vmabi-recon lane-s patcher, now 10\n'
  printf '# edits / 8 literals). build_entry.sh now ALSO passes the EXISTING -DSTAGE90_XNU_RESIDENT to\n'
  printf '# entry_storage.c-s two compile lines (the read-door pet-s gate; a new reader of an old switch).\n#\n'
  printf '# FIVE entry-side edits (the entry layer + the two staged asm hooks - NO kernel behaviour\n'
  printf '# change; the fifth was added AFTER this rung was first written - the amendment of 2026-10-13):\n'
  printf '# 1. ENTRY_LIVE_CAP 16384u -> 24576u (entry_stubs.c:2288). 998 hit 16384 MID-DYLD: the tombstone\n'
  printf '#    `xnu_live_capped=0x00004000` at :20502 IS the displaced `xnu_live_exec_done_seq` (the\n'
  printf '#    absence was the INSTRUMENT-s ceiling, not a missing return; the full counter identity is in\n'
  printf '#    docs/reference/d13-shreg-frontier.md section 10).\n'
  printf '# 2. The read dump THINS past ordinal 256: `st_read_single_block` keeps its six LOCATING cells\n'
  printf '#    (rd_calls / rd_ordinal / rd_lba / rd_op / rd_err / rd_complete - the ones whose ABSENCE is\n'
  printf '#    evidence) and its ~70 extended cells move to ST_LIVE_RD, silent when the read-s own rd_full_\n'
  printf '#    (st_read_count < 256) is clear: ~78 cells / ~4.2 KB -> 6 cells / ~280 B per read in the\n'
  printf '#    multi-hundred-read dyld era. The failure mode of the thinning can only be FEWER records.\n'
  printf '# 3. `entry_note_exec_returned`-s three records become DIRECT (cap-exempt, uncounted\n'
  printf '#    entry_write_kv) and stash a sig/size read-back of RAM_CONSOLE_BASE (g_exec_probe_*),\n'
  printf '#    published by the next any-kind direct writer as\n'
  printf '#    xnu_live_exec_done_probe_seq / _sig / _size / _records / _state - the probe that tells a\n'
  printf '#    torn append from a lost record the next time a done record is questioned.\n'
  printf '# 4. The WDT is fed from `entry_storage_driver_read`-s FIRST statement (argument = the monotone\n'
  printf '#    st_read_count): 998 logged ZERO xnu_live_wdt_* records - the handed-off kernel never idled,\n'
  printf '#    so the idle-site pet never ran. `entry_wdt_pet` is non-static for the second caller; the\n'
  printf '#    install/power-of-two/dog-tracking publishes are unchanged. **A FINDING was made here: the\n'
  printf '#    original four-edit WDT prescription was incomplete.** The door feed is the FIRST pre-pass-1\n'
  printf '#    -> post-pass-1 symbol reference this build has ever had: entry_storage.o links before the\n'
  printf '#    pass-1 undefined-set link (build_entry.sh:2308 vs :29126) while entry_trace.o joins only\n'
  printf '#    AFTER it (:29338), so the strong cross-TU reference was reported undefined in pass 1, the\n'
  printf '#    stub generator emitted an ordinary stand-in definition for the pet\n'
  printf '#    (xnu_arm_entry_realstubs.c), and the final link refused with `multiple definition of\n'
  printf '#    entry_wdt_pet` (measured verbatim on the first 999 build, EXIT=1; the documented stub\n'
  printf '#    family trap at build_entry.sh:29101-29112). RESOLUTION (team-lead ruling 2026-10-13): the\n'
  printf '#    reference is WEAK with a NULL GUARD (`if (entry_wdt_pet != 0)`) - no pass-1 undefined name,\n'
  printf '#    resolves to trace.o-s strong definition in the final link, and the guard cannot be taken in\n'
  printf '#    an accepted image (RESIDENT=1 with ENTRY_TRACE=0 is refused at build_entry.sh:1343; the 999\n'
  printf '#    clause refuses a linked image that defines no pet). build_entry.sh-s 999 clause now also\n'
  printf '#    asserts the guard shape (cmp rN,#0 + a conditional branch beside the single `bl`) so a\n'
  printf '#    future edit cannot drop the guard while keeping `weak` silently. THIRD stop of the same\n'
  printf '#    finding family (measured 2026-10-13 third run, EXIT=1): build_999.sh-s own by-value check\n'
  printf '#    grepped the storage object for a STRONG `U entry_wdt_pet` and rejected the (correct) weak\n'
  printf '#    object - the check predated the ruling; it now asserts exactly one `w entry_wdt_pet`, so a\n'
  printf '#    silent revert to the strong form stops the build at the check as well as at the link.\n'
  printf '# 5. THE TIMER DISCRIMINATOR (amendment #2). 997/998 both deliver `xnu_live_tick_irq` on the\n'
  printf '#    powers of two up to 0x40 (captures :7344 / :5463) and 0x80 NEVER appears, so deliveries\n'
  printf '#    stop at raw tick count 64 - and the captures cannot separate model A (a delivery-side\n'
  printf '#    failure: ISENABLER0 bit 20 CLEAR at 998 :3922 with no writer in a 4.78-7.30 s window) from\n'
  printf '#    model B (the handed-off kernel-s I=1, post-spin cpsr 0x20000193, was never cleared - the\n'
  printf '#    CPU mask, not the distributor, is the wall). So 999 publishes PRESENCE OF THE EDGE: a\n'
  printf '#    DIRECT (cap-exempt) record at (1) `ml_set_interrupts_enabled`-s enable path, immediately\n'
  printf '#    after `cpsie i` (machine_routines_asm.s:151-153; r1 = `mrs cpsr` read back), and (2)\n'
  printf '#    `thread_exception_return`-s `return_to_user` tail (cswitch.s:167-170; r1 = the CPSR the\n'
  printf '#    edge is about to install, [uss + 0x40]). Both sites sit behind an internal local label\n'
  printf '#    (`astloop` / `return_to_user`), which is why per-site readers must range to the next\n'
  printf '#    GLOBAL `T` and not the next T/t. Gates: the raw tick count has reached the frozen value 64\n'
  printf '#    (the same MSM8974_TICK_FROZEN_AT the capture evidence froze) and 4 events per site, then\n'
  printf '#    silent; readiness via entry_live_ready (interrupt-path rule). The six keys: site 1\n'
  printf '#    `xnu_live_edge_mlie_seq / _cpsr / _tick`, site 2 `xnu_live_edge_texr_seq / _cpsr / _tick`.\n'
  printf '#    READING for the pre-reg: NO edge record => model B; an edge with `_tick=64` and no\n'
  printf '#    tick_irq 0x80 within ~2 ms => model A; an edge followed by tick_irq=0x80 => the freeze\n'
  printf '#    ended at that edge. Staged by tools/stage_d13_timer_edge.sh; gated by the EXISTING\n'
  printf '#    STAGE90_XNU_RESIDENT (assemble_arm_layer.sh now appends -DSTAGE90_XNU_RESIDENT=$RESIDENT\n'
  printf '#    to its ASFLAGS - without the rebuild the linked image keeps hook-free asm objects).\n#\n'
  printf '# ONE payload edit: STAGE90_HW_WATCHDOG_TIMEOUT_S 25u -> 29u (stage90.h:4211) - the ARITHMETIC\n'
  printf '# CEILING of the 20-bit registers: 29s bark = 29 * 32765 = 950185 = 0xE7FA9, plus the fixed 3 s\n'
  printf '# BITE_EXTRA = 32 * 32765 = 1048480 = 0x000FFFA0 <= 0xFFFFF (30 s would truncate). 998-s 25 s\n'
  printf '# net was 0xC7FB5. The run must outlive the old ~28 s wall for the reading to reach the record\n'
  printf '# ceiling - the outer net protects the instrument, the instrument measures the machine.\n#\n'
  printf '# ONE pool edit: three PRINTF-ONLY lines inside `_shared_region_slide` (vm_unix.c:1513-1554),\n'
  printf '# under the SAME gate as the existing IOS7SHREG probes (STAGE90_XNU_SHREG_PROBE=1), each with\n'
  printf '# its own static counter < 4: `IOS7SHREG slide_enter slide=.. src=.. n=..`, `slide_init\n'
  printf '# kr=.. entry=.. n=..` (fires only when slide_init returned 0; `entry` is necessarily NULL -\n'
  printf '# a presence marker, not a value) and `slide_copyin kr=..`. The ladder measures the SAME slide\n'
  printf '# half 997 named (map_slide + slide_init) at the inner call sites where the outcome is decided.\n#\n'
  printf '# 998 (pressed 2026-10-12, arm armed-d13-dee822b0, commit 36bb341, evidence\n'
  printf '# out/stage90/captures/cancro-last_kmsg-998-armed-d13-dee822b0.txt) CONFIRMED G1: the geometry\n'
  printf '# literal `IOS7LAB shared region 0x2c000000..0x40000000; stack below 0x2be00000` prints exactly\n'
  printf '# ONCE (:4098) and `IOS7SHREG map_file kr=0` (:4109) is the FIRST time in the whole series the\n'
  printf '# shared cache file maps; the 997 halt chain (libbsm / witness / udf / `dyld>`) is GONE - all\n'
  printf '# zero. Regression guards held: check_np ret=12, VMHINT991 2x. Deviations read as deviations:\n'
  printf '# the IOS7LAB vm_allocate64 line is ABSENT - a failure-path artifact (994-s ruling), not a\n'
  printf '# regression; and the exec_done trio-s absence - root-caused to the cap, which is what 999-s\n'
  printf '# cap-exempt trio + probe make directly measurable. The two walls (cap + never-fed SoC watchdog,\n'
  printf '# ~28 s silent reset with 0 wdt records) are the INSTRUMENT and the POLICY, not the machine.\n#\n'
  printf '# The 999 PRESS- PRE-REGISTRATION (frozen once embedded; deviations reported, not\n'
  printf '# reinterpreted) is written by the TEAM-LEAD into scripts/press_999.sh. Expected reading per the\n'
  printf '# lead: `xnu_live_wdt_bark=0xE7FA9` once (29 * 32765), the `xnu_live_wdt_map/_via/_desc` trio\n'
  printf '# once, `xnu_live_wdt_pet_calls` >= 1 at powers of two and never above the running\n'
  printf '# `xnu_live_storage_rd_ordinal`, the runtime observables `hw_watchdog_timeout_s=0x0000001d` and\n'
  printf '# `hw_watchdog_bite_ticks_written=0x000FFFA0`, and the record log living PAST 998-s wall (the\n'
  printf '# 0x4000 tombstone) toward 24576; falsifier: `xnu_live_wdt_map=0` with `_desc` NOT a 1 MB block\n'
  printf '# of PA 0xf9000000.\n#\n'
  printf '# VERIFIED BY VALUE (build_999.sh): the 24576 cap constant, the 70/8 read-body split, the five\n'
  printf '# xnu_live_exec_done_probe_ keys in the linked image, the door-s null-guarded `bl <entry_wdt_pet>`\n'
  printf '# (cmp rN,#0 + conditional branch beside the call - the weak-reference guard) inside\n'
  printf '# `entry_storage_driver_read`-s own window, the three `IOS7SHREG slide_` literals in\n'
  printf '# bsd_vm_vm_unix.o, and `mov r0, #29` at the payload arm site (998 measured `mov r0, #25`); it\n'
  printf '# also reads the amendment: `entry_timer_edge_probe` as a T symbol, ONE `U` reference to it in\n'
  printf '# EACH of machine_routines_asm.o and cswitch.o (the ASFLAGS half of the switch), exactly ONE\n'
  printf '# `bl` inside each staged site-s own body (the next-GLOBAL-T range), and the six edge keys. The\n'
  printf '# SAME amendment properties are asserted at the LINK site by build_entry.sh-s 999 clause (plus\n'
  printf '# a SECOND integration finding, same species: build_entry.sh-s pre-existing 996/997 probe-checker\n'
  printf '# clause ran the checker on the BASE spelling, which on a 999 image reports the by-design slide\n'
  printf '# outsider clause BESIDE the 20 B straddle one (2 FAIL lines) - the checker-s own text says\n'
  printf '# re-run with --ladder - so the clause now invokes --ladder and the contract is unchanged\n'
  printf '# (exactly one clause FAIL, the straddle one; verified by value: rc=1, 1 FAIL).\n'
  printf '# entry_live_write-s `cmp ..., #24576`, the trio-s 3x entry_write_kv / 0x entry_live_write\n'
  printf '# branches, the pet-s global `T`, and the door-s single pet call). The 996/997/998 checkers\n'
  printf '# rerun ON the 999 image with the FROZEN 998/996/994 images as negative controls - the probe\n'
  printf '# checker-s ONE by-design frozen 20 B clause is the only allowed FAIL.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=999-past-the-cap;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3