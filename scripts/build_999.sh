#!/usr/bin/env bash
# Build 999: 998's switch set VERBATIM (NO new switch in this rung) - the rung is an INSTRUMENT +
# WATCHDOG-POLICY rung built on the confirmed 998 arm, plus the slide-half probe ladder in the pool.
#
# FOUR ENTRY-layer edits (all my lane; build_entry.sh itself now passes the EXISTING switch
# `STAGE90_XNU_RESIDENT` to `entry_storage.c`'s two compile lines, so a new reader of an old switch is
# the only "switch" change and the record's own RESIDENT=1 line already carries it):
#   1. `ENTRY_LIVE_CAP` 16384u -> 24576u (entry_stubs.c): 998 hit 16384 MID-DYLD (tombstone
#      `xnu_live_capped=0x4000`), so the instrument - not the machine - ended the reading.
#   2. The read dump THINS past ordinal 256: `st_read_single_block` publishes its six locating cells
#      through `ST_LIVE` and its ~70 extended cells through the new `ST_LIVE_RD`, silent when the
#      read's own `rd_full_` flag is clear (78 cells / ~4.2 KB -> 6 cells / ~280 B per read).
#   3. `entry_note_exec_returned`'s three records become DIRECT (uncounted `entry_write_kv`) - a record
#      whose ABSENCE is evidence must be cap-exempt (998's tombstone IS the displaced done_seq) - and
#      the sig/size probe is stashed there and published by the next any-kind direct writer.
#   4. The WDT is fed from the STORAGE READ DOOR (`entry_storage_driver_read`'s first statement,
#      under `#if STAGE90_XNU_RESIDENT`, argument = the monotone `st_read_count`): 998 had ZERO
#      `xnu_live_wdt_*` records - the handed-off kernel never idled, so the idle-site pet never ran.
#      `entry_wdt_pet` is non-static for this second caller (entry_trace.c), and the reference is
#      WEAK + null-guarded in entry_storage.c: the first build of this rung died at the final link
#      (`multiple definition of entry_wdt_pet`) because entry_storage.o is in pass 1's link while
#      entry_trace.o joins only after it - a strong reference makes pass 1 report the pet undefined
#      and the generator emits a stand-in stub that then collides (the documented trap at
#      build_entry.sh:29101-29112; the FIRST pre-pass-1 -> post-pass-1 reference this build's
#      history has). The guard is asserted at the link site (build_entry.sh's 999 clause) and the
#      WEAK class itself is asserted here in the by-value block (`w entry_wdt_pet` in the storage
#      object; run 3 rejected the object only because the check predated the ruling) - neither can
#      be dropped silently. See park_999.sh's record block.
#   5. `999 AMENDMENT #2 - the timer discriminator` (added 2026-10-13): the C helper
#      `entry_timer_edge_probe` (pe_msm8974.c, beside the raw tick counter; DIRECT/cap-exempt records,
#      gated on the raw count >= 64 - the measured frozen value - and capped at four events per site,
#      with the readiness pattern entry_wdt_pet uses) is called from TWO staged assembly hooks:
#      `machine_routines_asm.s`'s enable edge (site 1, right after `cpsie i`; r1 = `mrs cpsr` read
#      back) and `cswitch.s`'s `return_to_user` (site 2; r1 = the CPSR the edge installs). Staged by
#      `tools/stage_d13_timer_edge.sh`; gated by the EXISTING `STAGE90_XNU_RESIDENT` (no new switch -
#      `assemble_arm_layer.sh` now appends `-DSTAGE90_XNU_RESIDENT=$RESIDENT` to ASFLAGS, which is why
#      the switch is also exported below).
# ONE PAYLOAD edit: `STAGE90_HW_WATCHDOG_TIMEOUT_S` 25u -> 29u (stage90.h) - the largest value whose
# 3s bark->bite gap still fits the 20-bit registers (32 * 32765 = 1048480 <= 0xfffff); 999's run must
# outlive the old ~28 s wall. The loaded payload bytes move.
# ONE POOL edit: the three slide-half prints inside `_shared_region_slide` (`bsd/vm/vm_unix.c`),
# under the SAME `STAGE90_XNU_SHREG_PROBE` gate as the existing IOS7SHREG probes, staged by the
# extended `tools/stage_d13_shreg_probe.py` (10 edits / 8 literals; shreg-hang's report #2 verbatim).
#
# WHY (998's press, 2026-10-12, arm armed-d13-dee822b0, commit 36bb341, log
# out/stage90/captures/cancro-last_kmsg-998-armed-d13-dee822b0.txt): the G1 geometry fix HELD - the
# geom line prints exactly once (:4098) and `IOS7SHREG map_file kr=0` is the FIRST green in the
# series (:4109), the whole 997 halt chain (libbsm / witness / undefined instruction / `dyld>`) GONE -
# but the reading is bounded by TWO walls: the record cap (16384, hit mid-dyld) and the SoC watchdog
# (0 wdt records -> never fed -> the silent ~28 s reset). Both are the instrument/policy, not the
# machine. The exec_done absence was ROOT-CAUSED to the cap (counter identity 16,384 counted + 42
# direct = 16,426 lines; the tombstone is the displaced done_seq). See
# docs/reference/d13-shreg-frontier.md section 10 and the 998 audit.
#
# THE SWITCH SET is the CURRENT record's lines VERBATIM (STAGE90_XNU_DYLD_WITNESS / SHREG_PROBE /
# SHREG_WIRE / SHREG_GEOM all stay 1; USB ladder PROBE/DEV/ENUM/STREAM stay 1; the RESIDENT rung
# carries POST_END_TICKS=0). The ENTRY BIN MUST MOVE (dee822b0 -> the new sha8) - the entry layer
# re-records the arm keys and relinks the pool objects.
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 999: the instrument reads past the cap (24576 + thinned reads), the exec-done records become cap-exempt, the WDT is fed at the read door, the outer net is 29s - plus the slide-half probe ladder (pool) =="
echo "-- 998's arm was armed-d13-dee822b0 (PRESSED 2026-10-12: geom exactly once, map_file kr=0 first in the series, 997's halt chain gone; bounded by the 16384 cap mid-dyld and the never-fed SoC watchdog); NO new switch, 998's set verbatim --"

EXTRA=""
# NOTE: `STAGE90_XNU_HFS_ROOT_RW` is NOT here - it is a tree-layout define carried by the ENVIRONMENT
# variable of the same name (see the export below), not by `-D`.
for k in STAGE90_XNU_HFS_ROOT_MEDIA STAGE90_XNU_EMMC_STRATEGY STAGE90_XNU_ROOT_FROM_CARD \
         STAGE90_XNU_HDD_WRITE STAGE90_XNU_CARD_TOTAL STAGE90_XNU_FULL_EXTENT \
         STAGE90_XNU_CARD_COW STAGE90_XNU_MEM_TOTAL STAGE90_XNU_KEXTB_LOGIN \
         STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_COMMPAGE_ALIAS \
         STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_DYLD_WITNESS STAGE90_XNU_SHREG_PROBE; do
  v=$(awk -F= -v k="$k" '$1==k{print $2}' "$REC")
  [[ -z $v || $v == '(unset)' ]] && continue
  EXTRA="$EXTRA -D$k=$v"
done
# The record carries every key above (998's own record carries SHREG_GEOM and the rest). The pins
# keep the layers from ever disagreeing with each other or with the ENV exports below
# ([[mi4-one-value-two-definitions]]).
EXTRA="$EXTRA -DSTAGE90_XNU_SP_ALIGN=1"
EXTRA="$EXTRA -DSTAGE90_XNU_VMHINT_GUARD=1"
EXTRA="$EXTRA -DSTAGE90_XNU_COMMPAGE_ALIAS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_IOS7_TRAPS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_DYLD_WITNESS=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_PROBE=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_WIRE=1"
EXTRA="$EXTRA -DSTAGE90_XNU_SHREG_GEOM=1"
# 999 adds NO key: the changed things are constants and source-level (entry cap/reader/probe/door,
# stage90.h's 29u, the pool's three slide prints), and the WDT feed's gate is the EXISTING RESIDENT.
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 1. Stage the tree edits (all TEN idempotent; the shreg-probe driver now stages TEN edits - the
#    three 999 slide prints ride the SAME file and gate as the five existing IOS7SHREG ones - and the
#    timer-edge driver stages the amendment's TWO assembly hooks).
echo "-- stage the __LINKEDIT teardown guard (988) + the SP-alignment edit (990) + the memq_hint guard (991) + the commpage alias + vector-table zeroing (992+993) + the iOS 7 VM wire + -3 SWI (994) + the dyld witness rev-3 (995/996) + the 294/438 shared-region probe + the 999 slide ladder (996/999) + the 32 B shared-region wire (997) + the shared-region geometry + stack top (998) + the timer-discriminator edge hooks (999 am2) --"
tools/stage_d13_kextb_login.sh
tools/stage_d13_sp_align.sh
tools/stage_d13_vmhint_guard.sh
tools/stage_d13_commpage_alias.sh
tools/stage_d13_ios7_traps.sh
tools/stage_d13_dyld_witness.sh
tools/stage_d13_shreg_probe.sh
tools/stage_d13_shreg_wire.sh
tools/stage_d13_shreg_geom.sh
tools/stage_d13_timer_edge.sh

echo "-- HFS (the port is on, hfs files join the pool) --"
export STAGE90_XNU_HFS=1

# 990: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_SP_ALIGN=$SP_ALIGN into machine_routines_asm.o.
# MUST be exported BEFORE assemble_arm_layer.sh runs (it reads the environment, not the record).
export STAGE90_XNU_SP_ALIGN=1

# 991: the POOL reads this one (`build_xnu_arm_kernel.sh` gates the DEFINE of that name on the
# environment variable), so it must be exported BEFORE build_xnu_arm_kernel.sh runs below.
export STAGE90_XNU_VMHINT_GUARD=1

# 992/993: same rule for the commpage alias (all pool files).
export STAGE90_XNU_COMMPAGE_ALIAS=1

# 994: the ASSEMBLY layer reads this to put -DSTAGE90_XNU_IOS7_TRAPS=$IOS7_TRAPS into traps_lo.o, and the
# POOL reads the same variable. MUST be exported BEFORE assemble_arm_layer.sh runs.
export STAGE90_XNU_IOS7_TRAPS=1

# 995: the POOL reads this one - `trap.c` rides the pool, so there is no ASFLAGS half. 999 keeps it at 1.
export STAGE90_XNU_DYLD_WITNESS=1

# 996/999: same rule for the shared-region probe (`bsd/vm/vm_unix.c`, a pool file). 999 keeps it at 1:
# the probe's IOS7SHREG lines stay readable next to the wire's own IOS7LAB cache mapping lines, and the
# three 999 slide prints ride the same gate.
export STAGE90_XNU_SHREG_PROBE=1

# 997: same rule for the 32 B shared-region wire (the SAME pool file). 999 keeps it at 1.
export STAGE90_XNU_SHREG_WIRE=1

# 998: same rule for the shared-region geometry + stack top. 999 keeps it at 1 and touches neither.
export STAGE90_XNU_SHREG_GEOM=1

# 999 (amendment #2): the ASSEMBLY layer reads this to put -DSTAGE90_XNU_RESIDENT=$RESIDENT into
# machine_routines_asm.o and cswitch.o (the timer-discriminator edge hooks). MUST be exported BEFORE
# assemble_arm_layer.sh runs; the SAME value reaches build_entry.sh through EXPORTS below
# ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_RESIDENT=1

echo "-- arm asm layer (994: traps_lo.o gets the -3 timebase; 999+am2: machine_routines_asm.o and cswitch.o get the timer-discriminator edge hooks) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# Export the switch as an ENV var as well as via `EXTRA`, because `build_xnu_arm_kernel.sh` gates the
# pool define on the environment variable of that name ([[mi4-one-value-two-definitions]]).
export STAGE90_XNU_KEXTB_LOGIN=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN STAGE90_XNU_SP_ALIGN=$STAGE90_XNU_SP_ALIGN STAGE90_XNU_VMHINT_GUARD=$STAGE90_XNU_VMHINT_GUARD STAGE90_XNU_COMMPAGE_ALIAS=$STAGE90_XNU_COMMPAGE_ALIAS STAGE90_XNU_IOS7_TRAPS=$STAGE90_XNU_IOS7_TRAPS STAGE90_XNU_DYLD_WITNESS=$STAGE90_XNU_DYLD_WITNESS STAGE90_XNU_SHREG_PROBE=$STAGE90_XNU_SHREG_PROBE STAGE90_XNU_SHREG_WIRE=$STAGE90_XNU_SHREG_WIRE STAGE90_XNU_SHREG_GEOM=$STAGE90_XNU_SHREG_GEOM (pool DEFINES + asm ASFLAGS + EXTRA agree) STAGE90_XNU_RESIDENT=$STAGE90_XNU_RESIDENT (asm ASFLAGS + build_entry EXPORTS + the ungated helper's runtime gate; no pool object reads it) --"
echo "-- the WHOLE D13 kernel pool + platform block (vm_unix.c carries the 32 B wire (997) + the 294/438 probe + the 999 slide ladder; vm_shared_region.c the iOS 7 geometry (998); status.c the 0x2BE00000 stack top (998); trap.c the witness rev-3; mach_kernelrpc.c the wire adapters; ios7_compat.h the two selectors; pmap.c + commpage.c + arm_vm_init.c the alias/zeroing; vm_resident.c the hint guard; pcb.c the alignment) --"
# **The 905 B2 rw clear is a TREE-layout define, not a raw one.** Same trap as 987
# ([[mi4-one-value-two-definitions]]): the record's key name and the tree's guard name differ by one
# word, and only the environment path bridges them.
export STAGE90_XNU_HFS_ROOT_RW=$(awk -F= '$1=="STAGE90_XNU_HFS_ROOT_RW"{print $2}' "$REC")
echo "-- STAGE90_XNU_HFS_ROOT_RW=$STAGE90_XNU_HFS_ROOT_RW (the pool's hfs_vfsops.o carries the rw clear) --"
XNU_TREE="$XNU_TREE" \
XNU_KERNEL_CONFIG=STAGE90_XNU \
XNU_MASTER_LOCAL="$PWD/tools/xnu_config/boot/STAGE90_XNU.local" \
XNU_KERNEL_EXTRA_DEFINES="$EXTRA" \
    ./tools/build_xnu_arm_kernel.sh

echo "-- entry image (998's switch set re-exported verbatim; build_entry only LINKS the pool) --"
EXPORTS=()
while IFS= read -r line; do
  line=${line%%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  k=${line%%=*}; v=${line#*=}
  [[ $v == '(unset)' ]] && continue
  case $k in
    STAGE90_XNU_ENTRY_SHA256|STAGE90_XNU_ENTRY_BYTES) continue ;;
  esac
  EXPORTS+=("$k=$v")
done < "$REC"
# The record (998's) carries every tree-shaping switch including SHREG_GEOM and RESIDENT; the pins
# name them explicitly so build_entry.sh's ENTRY_ARM_KEYS read-back records the SAME values the build
# layers compiled with ([[mi4-one-value-two-definitions]]).
EXPORTS+=("STAGE90_XNU_KEXTB_LOGIN=1")
EXPORTS+=("STAGE90_XNU_SP_ALIGN=1")
EXPORTS+=("STAGE90_XNU_VMHINT_GUARD=1")
EXPORTS+=("STAGE90_XNU_COMMPAGE_ALIAS=1")
EXPORTS+=("STAGE90_XNU_IOS7_TRAPS=1")
EXPORTS+=("STAGE90_XNU_DYLD_WITNESS=1")
EXPORTS+=("STAGE90_XNU_SHREG_PROBE=1")
EXPORTS+=("STAGE90_XNU_SHREG_WIRE=1")
EXPORTS+=("STAGE90_XNU_SHREG_GEOM=1")
EXPORTS+=("STAGE90_XNU_RESIDENT=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob AND carries the 29s watchdog constant; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   998 was dee822b0 - a DIFFERENT value here means the entry-side edits reached the image."
echo "-- 999's own four entry-side constants/keys BY VALUE (source side, what the image check cannot split) --"
printf '   ENTRY_LIVE_CAP:  '; grep -m1 '^#define ENTRY_LIVE_CAP' src/entry/entry_stubs.c
printf '   WDT timeout:     '; grep -m1 '^#define STAGE90_HW_WATCHDOG_TIMEOUT_S' src/stage90.h
[[ $(grep -c '^#define ENTRY_LIVE_CAP 24576u$' src/entry/entry_stubs.c) == 1 ]] || { echo "FAIL: the cap constant is not 24576u - the rebuild would re-hit the 998 wall"; exit 1; }
[[ $(grep -c '^#define STAGE90_HW_WATCHDOG_TIMEOUT_S 29u$' src/stage90.h) == 1 ]] || { echo "FAIL: the watchdog timeout is not 29u - the outer net is not the arithmetic ceiling"; exit 1; }
# The robust use-count: a bare `grep 'ST_LIVE_RD('` also matches the two `#define ST_LIVE_RD(...)`
# lines themselves (and any prose naming the macro), measured 72 - statement USES start after
# whitespace with the macro name, measured 70.
rd_thin=$(grep -cE '^\s*ST_LIVE_RD\(' src/entry/entry_storage.c || true)
rd_full=$(sed -n '/^static __attribute__((noinline, noclone)) void st_read_single_block/,/^}/p' src/entry/entry_storage.c | grep -cE '(^|[^_])ST_LIVE\(' || true)
echo "   read body: $rd_thin thinned cell site(s) (want 70), $rd_full locating cell site(s) that never thin (want 8)"
[[ "$rd_thin" == "70" && "$rd_full" == "8" ]] || { echo "FAIL: the thinning is not 70/8 - either the six locating cells were gated (a record whose absence is evidence would move) or the thinning did not reach the reader"; exit 1; }
probe_keys=$(strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_exec_done_probe_' || true)
echo "   exec-done probe keys in the image: $probe_keys (want 5)"
[[ "$probe_keys" == "5" ]] || { echo "FAIL: the five probe keys are not in the image - the sig/size read-back did not reach it"; exit 1; }
done_keys=$(strings out/stage90/xnu_arm_entry.elf | grep -cE 'xnu_live_exec_done_(seq|who|current)' || true)
echo "   exec-done record keys in the image: $done_keys (want 3, still the SAME names)"
[[ "$done_keys" == "3" ]] || { echo "FAIL: the done trio's names moved - the pre-registration greps for them"; exit 1; }
echo "-- by value: the DOOR'S PET (entry_storage_driver_read references entry_wdt_pet WEAKLY, null-guarded; the trace object defines it as a global T) --"
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E '^[0-9a-f]+ [Tt] entry_wdt_pet$' || { echo "FAIL: entry_wdt_pet is not a defined text symbol in the image (RESIDENT=1 carries the pet; 977)"; exit 1; }
# The reference class is part of the contract: `w` (weak undefined), NOT `U`. The first 999 build died on
# the strong form - pass 1 stubs the pet and the final link refuses with `multiple definition` (the trap
# at build_entry.sh:29101-29112); measured on the third run, this exact line REJECTED the weak object
# (the check had been written for `U` before the 2026-10-13 ruling). A revert to strong here means the
# ruling was undone, and the name count is still 1.
[[ $(arm-none-eabi-nm out/stage90/xnu_arm_entry_storage.o | grep -c '^[[:space:]]*w entry_wdt_pet$' || true) == 1 ]] || { echo "FAIL: the storage object does not carry exactly one WEAK 'w entry_wdt_pet' reference - either the read-door feed did not compile in, or the reference went back to the strong 'U' shape (the pass-1 stub collision of 2026-10-13)"; exit 1; }
door_a=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="entry_storage_driver_read"{print "0x" $1; found=1} END{exit(found?0:1)}') || { echo "FAIL: entry_storage_driver_read is not in the image (the probe>=60 door is compiled out - the whole resident pet hangs off it)"; exit 1; }
# Same introduced-defect guard as 998's reader below: NO early `exit` in the next-symbol awk (an early
# exit SIGPIPEs `nm`, and under `set -o pipefail` the 141 is a silent death); read the stream, keep the
# first match.
door_x=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${door_a#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
door_body=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$door_a" --stop-address="${door_x:-0xffffffff}")
door_pets=$(grep -c 'bl.*<entry_wdt_pet>' <<<"$door_body" || true)
echo "   the door body calls entry_wdt_pet $door_pets time(s) (want 1)"
[[ "$door_pets" == "1" ]] || { echo "FAIL: entry_storage_driver_read does not call the pet exactly once (0 = the feed is not at the door; >1 = the body moved)"; exit 1; }
echo "-- by value: the 999 SLIDE LADDER in the POOL object (bsd_vm_vm_unix.o must carry the three new printf literals) --"
slide_lits=$(strings out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o 2>/dev/null | grep -c 'IOS7SHREG slide_' || true)
echo "   IOS7SHREG slide_* literals: $slide_lits (want 3)"
[[ "$slide_lits" == "3" ]] || { echo "FAIL: the three slide prints are not in the pool object - stage_d13_shreg_probe.sh did not put them in bsd/vm/vm_unix.c or the pool was not rebuilt"; exit 1; }
echo "-- by value: the TIMER-DISCRIMINATOR hooks (amendment #2) - the C helper in the image, BOTH asm objects referencing it (the ASFLAGS half of the switch, the 933/990 trap), exactly ONE \`bl\` inside each staged site's own body, and the six direct record keys --"
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -E '^[0-9a-f]+ [Tt] entry_timer_edge_probe$' || { echo "FAIL: entry_timer_edge_probe is not a defined text symbol in the image - the pe_msm8974.c helper did not link (the bl would be undefined)"; exit 1; }
for eobj in machine_routines_asm cswitch; do
  e_u=$(arm-none-eabi-nm out/xnu_asm_obj_d13/$eobj.o 2>/dev/null | grep -c '^[[:space:]]*U entry_timer_edge_probe$' || true)
  echo "   out/xnu_asm_obj_d13/$eobj.o references entry_timer_edge_probe: $e_u (want 1)"
  [[ "$e_u" == "1" ]] || { echo "FAIL: $eobj.o does not reference the helper - the -DSTAGE90_XNU_RESIDENT=\$RESIDENT ASFLAGS line did not reach this object (export the switch before tools/assemble_arm_layer.sh and rebuild the assembly layer)"; exit 1; }
done
for esym in ml_set_interrupts_enabled thread_exception_return; do
  ea=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk -v s="$esym" '$3 == s { print "0x" $1; found = 1 } END { exit(found ? 0 : 1) }') || { echo "FAIL: $esym not in the image - the site the edge probe rides is gone"; exit 1; }
  # The range ends at the next GLOBAL `T`, NOT the next `T/t` the door reader uses: both hook sites
  # sit BEHIND an internal label the assembler gave a local symbol (`astloop` at mlie+0xc,
  # `return_to_user` at texr+0x28), so a first-T/t reader would stop 4 bytes into the function and
  # report 0 calls on a CORRECT image (measured 2026-10-13 on the 998 elf: mlie 12 bytes, texr 4).
  # The next global is the function's true end here - `ml_get_interrupts_enabled` at mlie+0x80,
  # `L_CurrentThread`'s successor T at texr+0x4c - and it contains the site. Same no-early-exit awk
  # as the door reader above (998's measured SIGPIPE/141 defect).
  ex=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${ea#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 == "T" { if (!d) { print "0x" $1; d = 1 } }')
  ebody=$(arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$ea" --stop-address="${ex:-0xffffffff}")
  ecalls=$(grep -c 'bl.*<entry_timer_edge_probe>' <<<"$ebody" || true)
  echo "   $esym ($ea..${ex:-end}) calls entry_timer_edge_probe: $ecalls (want 1)"
  [[ "$ecalls" == "1" ]] || { echo "FAIL: $esym does not carry exactly one edge-probe call site (0 = the hook did not reach the assembly layer; >1 = the body moved)"; exit 1; }
done
edge_keys=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -cE 'xnu_live_edge_(mlie|texr)_(seq|cpsr|tick)' || true)
echo "   timer-discriminator record keys in the image: $edge_keys (want 6)"
[[ "$edge_keys" == "6" ]] || { echo "FAIL: the six edge keys are not in the image - the pre-registration greps for xnu_live_edge_mlie_seq/_cpsr/_tick and xnu_live_edge_texr_seq/_cpsr/_tick verbatim"; exit 1; }
echo "-- by value: the checkers (PASS only if the linked image carries each fix) --"
python3 tools/check_sp_alignment.py out/stage90/xnu_arm_entry.elf
python3 tools/check_vmhint_guard.py out/stage90/xnu_arm_entry.elf
python3 tools/check_commpage_alias.py out/stage90/xnu_arm_entry.elf
python3 tools/check_d13_ios7_traps.py out/stage90/xnu_arm_entry.elf --expect on
python3 tools/check_d13_ios7_vmwire.py out/stage90/xnu_arm_entry.elf
python3 tools/check_d13_dyld_witness.py out/stage90/xnu_arm_entry.elf --expect on
echo "-- by value: the same witness checker against the FROZEN 994 image as a NEGATIVE CONTROL (must FAIL ON and PASS OFF) --"
python3 tools/check_d13_dyld_witness.py out/stage90/xnu_arm_entry.elf --expect on --control out/stage90/captures/994-xnu_arm_entry.elf
echo "-- by value: the 996 SHREG checker ON the 999 image (via --ladder; the frozen 996/998 runs below stay base-5) - its frozen 20 B clause ('996 measures the misparse, it does not fix it') is the ONE by-design FAIL, and NOTHING else may fail: the EIGHT IOS7SHREG literals in the image (5 x 996 + 3 x 999), the 294/438 sysent rows still name the real bodies, no IOS7SHREG reference outside the three probed functions, and the SOURCE-side gate check on the staged tree file --tree bsd/vm/vm_unix.c (10 guard regions; NOTE: --tree takes the FILE, not the tree root - measured) --"
# NOTE (coordination, 2026-10-13): the three 999 slide literals are enforced ONLY under the new
# `--ladder` flag (recommended to the vmabi-recon lane: base PROBE_LITERALS stay 5 and the frozen
# 996/998 home-image runs above/below keep passing; `--ladder` adds the 3 image clauses AND makes
# --tree expect 10 guard regions instead of 7). If the lane lands a different spelling, this one
# line and the echo above change - nothing else here depends on it.
sp_rc=0
sp_out=$(python3 tools/check_d13_shreg_probe.py out/stage90/xnu_arm_entry.elf --expect on --ladder --tree external/xnu-hd2-darwin13/xnu/bsd/vm/vm_unix.c 2>&1) || sp_rc=$?
printf '%s\n' "$sp_out"
sp_fails=$(printf '%s\n' "$sp_out" | grep -c '^  FAIL ' || true)
if [[ $sp_rc -eq 0 ]] || [[ $sp_fails -ne 1 ]] || ! grep -q '^  FAIL shared_region_copyin_mappings does not compute' <<<"$sp_out"; then
  echo "FAIL: the 996 checker did not fail on exactly its by-design 20 B clause (rc=$sp_rc, $sp_fails clause FAIL line(s)) - rc 0 means the 32 B wire did not reach the image; any other failing clause means the probe/ladder is damaged or the tree is not staged"
  exit 1
fi
echo "   (by design: the ONLY failing clause is the frozen 20 B straddle one)"
echo "-- by value: the SAME frozen checker against its OWN 996 image, with the FROZEN 994 image as a NEGATIVE CONTROL (on must FAIL, off must PASS) - 997/998/999 do not weaken it --"
python3 tools/check_d13_shreg_probe.py out/stage90/frozen/armed-d13-cea0c1c2/xnu_arm_entry.elf --expect on --control out/stage90/captures/994-xnu_arm_entry.elf
echo "-- by value: the 997 shreg-WIRE CHECKER (switch ON: the IOS7LAB cache mapping literal in shared_region_copyin_mappings reaching a printf; count*32 (\`lsl #5\` + \`add rX,rX,#32\`); the 20 B \`count*5<<2\` pair ABSENT; 294's copyout r2=8 for the full uint64; no stray literal) + the SOURCE-side gate check --tree --"
python3 tools/check_d13_shreg_wire.py out/stage90/xnu_arm_entry.elf --expect on --tree external/xnu-hd2-darwin13/xnu/bsd/vm/vm_unix.c
echo "-- by value: the same wire checker against the FROZEN 996 image as a NEGATIVE CONTROL (it must FAIL ON and PASS OFF) and the 994 capture as a second --"
python3 tools/check_d13_shreg_wire.py out/stage90/xnu_arm_entry.elf --expect on --control out/stage90/frozen/armed-d13-cea0c1c2/xnu_arm_entry.elf --control out/stage90/captures/994-xnu_arm_entry.elf
echo "-- by value: the 998 shreg-GEOM CHECKER, SOURCE side (NOTE: --tree takes the TREE ROOT here, unlike the wire/probe checkers' FILE argument - measured): the three files all in the staged shape, the ABI constants intact in BOTH states, no v1 inline-#if wrap --"
python3 tools/check_d13_shreg_geom.py --tree external/xnu-hd2-darwin13/xnu --expect staged
echo "-- by value: the 998 IMAGE-side clauses, standalone (the same three properties build_entry.sh's 998 clause asserts at the link site): (b) the IOS7LAB shared region printf literal is in the image (it sits INSIDE the opened gate), (c) thread_userstack + thread_userstackdefault materialize the 0xBE0 stack top and NOT the 0xFE0 default --"
gl_lit='IOS7LAB shared region 0x2c000000..0x40000000; stack below 0x2be00000'
gl_n=$(arm-none-eabi-strings out/stage90/xnu_arm_entry.elf | grep -cF "$gl_lit" || true)
echo "   (b) IOS7LAB shared region literal copies: $gl_n (want 1)"
[[ $gl_n -eq 1 ]] || { echo "FAIL: the image-side literal reader found $gl_n copies of the geometry printf literal (want exactly 1)"; exit 1; }
for gfn in thread_userstack thread_userstackdefault; do
  gfa=$(arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk -v s="$gfn" '$3 == s { print "0x" $1; found = 1 } END { exit(found ? 0 : 1) }') || { echo "FAIL: $gfn not in the image"; exit 1; }
  # NO early `exit` in this awk (998's measured defect: an early exit SIGPIPEs `nm` and under
  # `set -o pipefail` the 141 becomes a silent death). Read the whole stream, keep the first match.
  gfnx=$(arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | awk -v s="${gfa#0x}" '
    BEGIN { sv = strtonum("0x" s) }
    { a = strtonum("0x" $1) }
    a > sv && $2 ~ /^[Tt]$/ { if (!d) { print "0x" $1; d = 1 } }')
  echo "   (c) $gfn ($gfa..${gfnx:-end}):"
  arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address="$gfa" --stop-address="${gfnx:-0xffffffff}" | grep -E 'movt.*; 0x2be0|movt.*; 0x2fe0' | sed 's/^/       /'
done
echo "-- by value: the SAME image-side reader against the FROZEN 998 elf as a NEGATIVE CONTROL (both readers must still work on it; 999 moves neither constant) --"
gc_frz=$(arm-none-eabi-strings out/stage90/frozen/armed-d13-dee822b0/xnu_arm_entry.elf | grep -cF "$gl_lit" || true)
echo "   frozen 998 elf: IOS7LAB shared region literal copies: $gc_frz (want 1)"
[[ $gc_frz -eq 1 ]] || { echo "FAIL: the frozen 998 elf does not carry the geometry literal the reader looks for - the reader is stale"; exit 1; }
echo "-- by value: the POOL objects FIRST ([[mi4-981-press-gate-kernel-staleness-hole-closed]]: the pool is what build_entry only LINKS) --"
echo "-- by value: the 997/999 WIRE+PROBE object (bsd_vm_vm_unix.o must carry the IOS7LAB cache mapping literal, the count*32 shape, the 996 IOS7SHREG probe AND the 999 slide ladder) --"
strings -a out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o 2>/dev/null | grep -E "IOS7LAB cache mapping|IOS7SHREG" | head -14 || true
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/bsd_vm_vm_unix.o 2>/dev/null | grep -E "lsl.*#5|#32" | head -8 || true
echo "-- by value: the 998 GEOMETRY object (osfmk_vm_vm_shared_region.o must carry the IOS7LAB shared region literal + 0x2C000000/0x14000000) --"
strings -a out/xnu_kernel_obj_d13/osfmk_vm_vm_shared_region.o 2>/dev/null | grep -F 'IOS7LAB shared region' || true
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_vm_vm_shared_region.o 2>/dev/null | grep -E '#0x2c00|#0x1400' | head -6 || true
echo "-- by value: the 998 STACK object (osfmk_arm_status.o must carry the 0x2BE00000 stack top) --"
arm-none-eabi-objdump -d out/xnu_kernel_obj_d13/osfmk_arm_status.o 2>/dev/null | grep -B1 -A1 '; 0x2be0' | head -12 || true
echo "-- by value: the 995 PROBE object (trap.o must carry the witness REV-3) --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_arm_trap.o 2>/dev/null | grep -E "ios7wit_dyld_message|IOS7WITNESS|IOS7WIT_AI_|copyin|copyinstr" | head -20 || true
echo "-- by value: the 994 POOL object (mach_kernelrpc.o must carry the wire adapters) --"
arm-none-eabi-objdump -dr out/xnu_kernel_obj_d13/osfmk_ipc_mach_kernelrpc.o 2>/dev/null | grep -E "IOS7LAB|vm_allocate64|vm_deallocate64|vm_protect64|ios7_traps_enabled" | head -20 || true
echo "-- by value: the ASSEMBLY object (traps_lo.o must carry the mach_absolute_time call, NOT the cmn r12,#3 fast path) --"
arm-none-eabi-objdump -d out/xnu_asm_obj_d13/traps_lo.o 2>/dev/null | grep -B2 -A2 -E "mach_absolute_time|swi_exit64" | head -20 || true
echo "-- by value: the 999 PAYLOAD constant AT THE ARM SITE (stage90_main.o's call must pass 29 - the register carries SECONDS; measured 998: 'mov r0, #25' immediately before 'bl stage90_hw_watchdog_arm') --"
# The 0xE7FA9 (29*32765) / 0xFFFA0 (32*32765) tick values are RUNTIME arithmetic
# (hw_watchdog.c:223 `bark_ticks = timeout_s * MSM8974_WDT_HZ` + BITE_EXTRA_TICKS), so NO object
# contains them - the ARMED log's `hw_watchdog_timeout_s=0x0000001d` and
# `hw_watchdog_bite_ticks_written=0x000FFFA0` are the press-side observables, and this is the
# build-side one that makes them arithmetically inevitable.
arm_site=$(arm-none-eabi-objdump -dr out/stage90/stage90_main.o 2>/dev/null | grep -B3 'bl.*stage90_hw_watchdog_arm' | grep -E 'mov[a-z]*[[:space:]]+r0, #(25|29)' || true)
printf '%s\n' "${arm_site:-   (no mov r0,#25|#29 found near the arm call - the reader is stale)}"
grep -qE 'mov[a-z]*[[:space:]]+r0, #29' <<<"$arm_site" || { echo "FAIL: the payload's arm site does not pass 29 - the loaded payload would arm the OLD 25 s net and the run would die at 998's wall"; exit 1; }
echo "-- 999 build done: park with scripts/park_999.sh (HELD until the lead lifts the timer-freeze hold) --"