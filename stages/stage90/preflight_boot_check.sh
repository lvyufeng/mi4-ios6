#!/usr/bin/env bash
#
# Gate a hardware run of the Stage90 payload.
#
# The 2026-09-16 PREFLIGHT_WATCHDOG_ONLY run ended with the device hung and a
# manual power-button hold needed. Knowing which switches an image was actually
# built with, and refusing the risky ones by default, is the cheapest way to stop
# that happening again - a rebuild that silently picks up a risky mode is exactly
# how an unintended hang gets booted.
#
# This script never runs fastboot and never touches the device. It verifies the
# image and prints the command to run, or refuses and says why.
#
# Usage: ./preflight_boot_check.sh [--allow-preflight] [--allow-full] [--allow-selftest] [--allow-attr-normal-nc] [--allow-attr-normal-wb] [--allow-icache] [--allow-dcache] [--allow-xnu-entry] [--allow-hw-watchdog-selftest] [--allow-fault-inject]

set -euo pipefail

cd "$(dirname "$0")"
STAGE_DIR=$PWD
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)
OUT=$REPO_ROOT/out/stage90

ALLOW_PREFLIGHT=0
ALLOW_FULL=0
ALLOW_SELFTEST=0
ALLOW_ATTR=0
ALLOW_ATTR_WB=0
ALLOW_ICACHE=0
ALLOW_DCACHE=0
ALLOW_XNU_ENTRY=0
ALLOW_HW_SELFTEST=0
ALLOW_FAULT_INJECT=0

for arg in "$@"; do
  case "$arg" in
    --allow-preflight)   ALLOW_PREFLIGHT=1 ;;
    --allow-full)        ALLOW_FULL=1 ;;
    --allow-selftest)    ALLOW_SELFTEST=1 ;;
    --allow-attr-normal-nc) ALLOW_ATTR=1 ;;
    --allow-attr-normal-wb) ALLOW_ATTR_WB=1 ;;
    --allow-icache)       ALLOW_ICACHE=1 ;;
    --allow-dcache)       ALLOW_DCACHE=1 ;;
    --allow-xnu-entry)    ALLOW_XNU_ENTRY=1 ;;
    --allow-hw-watchdog-selftest) ALLOW_HW_SELFTEST=1 ;;
    --allow-fault-inject) ALLOW_FAULT_INJECT=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

fail() { echo "REFUSING: $*" >&2; exit 1; }

# The tools this gate calls. PYTHON is configurable for the same reason build.sh's is -
# the host may have python3 under another name.
PYTHON=${PYTHON:-python3}

CONFIG=$OUT/stage90-build-config.txt
IMAGE=$OUT/stage90-qcdt.img

[[ -f $CONFIG ]] || fail "no $CONFIG - run ./build.sh first to record the build switches"
[[ -f $IMAGE  ]] || fail "no $IMAGE - run ./build.sh first"

echo "== build configuration =="
cat "$CONFIG"

value_of() {
  sed -n "s/^#define $1 //p" "$CONFIG"
}

MODE=$(value_of STAGE90_HANDOFF_MODE)
SELFTEST=$(value_of STAGE90_DEADMAN_SELFTEST)
DEADMAN=$(value_of STAGE90_DEADMAN_ENABLE)
LADDER=$(value_of STAGE90_ENTRY_LADDER_LEVEL)
HWWDT=$(value_of STAGE90_HW_WATCHDOG)
HWSELFTEST=$(value_of STAGE90_HW_WATCHDOG_SELFTEST)
FAULT_INJECT=$(value_of STAGE90_HANDOFF_FAULT_INJECT_VA)

[[ -n $MODE ]] || fail "STAGE90_HANDOFF_MODE missing from $CONFIG"

echo
echo "== image integrity =="
( cd "$OUT" && sha256sum -c SHA256SUMS.txt ) || fail "image does not match SHA256SUMS.txt; rebuild before booting"
echo "sha256 verified against $OUT/SHA256SUMS.txt"
# **The hash, computed here rather than written into the prose below, and 520 is why.** 519c's edit put
# the literal `40bf8a0b...` into the XNU-entry block, where it was true of the image it was written for -
# and then 520's build produced `25c4bc7d...` and the gate went on telling the operator that the image
# being checked was 519's. The manifest check above does not catch it: `40bf8a0b...` is not compared with
# anything, so a hash in a comment is a claim, which is this project's oldest rule about checks. This is
# the value the block below prints, and it is of the file about to be booted.
IMAGE_SHA=$(sha256sum "$OUT/stage90-qcdt.img" | cut -d' ' -f1)
IMAGE_SHA16=${IMAGE_SHA:0:16}

echo
echo "== image freshness =="
# The manifest check above proves the image matches SOMETHING, but not that the something
# is the current source. If a source file was edited and not rebuilt, the image and its
# manifest are both stale and agree with each other - so the gate would report the OLD
# switches and approve the OLD image, which is precisely the failure this gate exists to
# prevent, in the direction it was blind to. Verified: without this check, touching
# stage90.h and running the gate passes.
STALE=$(find "$STAGE_DIR" -maxdepth 1 -type f \
         \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.ld' \) \
         -newer "$IMAGE" -printf '%f\n' 2>/dev/null | sort)
BUILD_TOOLS_NEWER=$(find "$REPO_ROOT/tools" -maxdepth 1 -name 'mkmacho_fixture.py' \
                    -newer "$IMAGE" -printf '%f\n' 2>/dev/null)
if [[ -n $STALE || -n $BUILD_TOOLS_NEWER ]]; then
  echo "source newer than the image:"
  [[ -n $STALE ]] && echo "$STALE" | sed 's/^/  /'
  [[ -n $BUILD_TOOLS_NEWER ]] && echo "  tools/$BUILD_TOOLS_NEWER"
  fail "the image is stale - run ./build.sh, then re-run this gate"
fi
echo "no source file is newer than the image"

echo
echo "== storage tripwire =="
# Two independent checks, because they catch different things and only one of them is
# sufficient. Symbols catch a NAMED storage reference. Addresses catch an unnamed one - a raw
# store to a controller register - which is the plausible case here, since the payload
# already writes raw literals to PS_HOLD and IMEM.
#
# Verified by negative test: with a deliberate `*(volatile uint32_t *)0xf9824000u = 1;`
# added to the payload, the symbol check below found NOTHING and would have approved the
# run. The address check caught it. Both are kept because the symbol check is cheap and
# catches a different shape.
if arm-none-eabi-nm -a "$OUT/stage90.elf" 2>/dev/null \
     | grep -iE 'sdcc|emmc|\bmmc\b|ufs|partition|flash_|nand' ; then
  fail "payload references storage symbols (see above)"
fi
echo "no storage symbols in the payload"

if [[ -x $REPO_ROOT/tools/check_storage_refs.py ]] || [[ -f $REPO_ROOT/tools/check_storage_refs.py ]]; then
  "$PYTHON" "$REPO_ROOT/tools/check_storage_refs.py" "$OUT/stage90.elf" \
    || fail "payload addresses a storage controller (see above)"
else
  fail "tools/check_storage_refs.py missing - the address half of the storage tripwire cannot run"
fi

echo
echo "== recovery net =="
# Two independent nets. The hardware watchdog is the one that matters, because it does
# not depend on the GIC, the timer, IRQ delivery or IRQs being unmasked - any of which
# may be exactly what broke in a given hang.
case "$HWWDT" in
  STAGE90_HW_WATCHDOG_ARMED|1|1u)
    echo "hardware watchdog: ARMED. The SoC resets itself if the payload stops making"
    echo "                   progress, whatever the CPU is doing - and platform_reboot()"
    echo "                   forces a bite so the reboot does not depend on PS_HOLD."
    ;;
  STAGE90_HW_WATCHDOG_DISABLED|0|0u)
    echo "WARNING: STAGE90_HW_WATCHDOG=disabled - there is NO hardware reset net."
    echo "         A hang that the software dead-man cannot see will need a manual"
    echo "         power-button hold."
    ;;
  *)
    fail "unrecognised STAGE90_HW_WATCHDOG: $HWWDT"
    ;;
esac

if [[ $DEADMAN != "1u" ]]; then
  echo "WARNING: STAGE90_DEADMAN_ENABLE=$DEADMAN - the software dead-man net is off."
else
  echo "software dead-man: armed (60s), as a second net."
fi

case "$HWSELFTEST" in 1|1u)
  [[ $ALLOW_HW_SELFTEST -eq 1 ]] || fail "the hardware-watchdog SELFTEST skips the normal boot path and spins until a net reboots it; needs --allow-hw-watchdog-selftest"
  echo "HW WATCHDOG SELFTEST: allowed. Three outcomes, and the time it takes is the result:"
  echo "          ~33s  the hardware watchdog fired - it works"
  echo "          ~90s  it did NOT, but the bounded spin's PS_HOLD reset brought the"
  echo "                device back - the watchdog needs investigating, PS_HOLD is fine"
  echo "          never both failed. Note this third case is reachable: platform_reboot()"
  echo "                falls back to the same watchdog, so a dead watchdog plus a PS_HOLD"
  echo "                that does not land means a manual power press. That is a real"
  echo "                finding, not a lost run, but it is the one outcome with no log."
  ;;
esac

echo
echo "== mode policy =="
# A -D override records the numeric value rather than the symbolic name, so accept
# either form. Reading the build's own config is only worth anything if the gate can
# actually recognise what it finds there.
case "$MODE" in
  STAGE90_HANDOFF_MODE_HARD_SKIP|0|0u)
    echo "HARD_SKIP: stops before the candidate L1, the watchdog loop and the jump."
    ;;
  STAGE90_HANDOFF_MODE_PREFLIGHT_WATCHDOG_ONLY|1|1u)
    [[ $ALLOW_PREFLIGHT -eq 1 ]] || fail "PREFLIGHT_WATCHDOG_ONLY is not allowed without --allow-preflight"
    echo "PREFLIGHT_WATCHDOG_ONLY: allowed."
    ;;
  STAGE90_HANDOFF_MODE_FULL|2|2u)
    [[ $ALLOW_FULL -eq 1 ]] || fail "FULL is not allowed without --allow-full"
    echo "FULL: allowed. This installs the candidate L1 and jumps to the high-VA target."
    ;;
  *)
    fail "unrecognised STAGE90_HANDOFF_MODE: $MODE"
    ;;
esac

if [[ $SELFTEST == "1u" ]]; then
  [[ $ALLOW_SELFTEST -eq 1 ]] || fail "dead-man SELFTEST hangs the payload on purpose; needs --allow-selftest"
  echo "SELFTEST: allowed. The payload will NOT reach platform_reboot();"
  echo "          the dead-man is the only route back to Android (~60s)."
fi

echo
echo "== ladder =="
# The ladder level decides how much of the payload runs, so it decides how much a green
# run tells you. Printing the raw macro said nothing useful: a level-0 build under HARD_SKIP
# exercises the device tree, the watchdog arm and kernel_entry's early checks and then
# returns - skipping the whole arm_init ladder - and the gate presented that identically to
# a FULL run. Spelling out what each level reaches makes the run's value visible before it
# is booted rather than after.
case "$LADDER" in
  STAGE90_ENTRY_LADDER_FULL|4|4u)
    echo "FULL (4): boot-args, early pmap, PE_init_platform, post-PE bootstrap, arm_vm_init,"
    echo "          the high-VA windows and the loader. The only level that reaches the handoff."
    ;;
  STAGE90_ENTRY_LADDER_POST_PE_BOOTSTRAP|3|3u)
    echo "POST_PE_BOOTSTRAP (3): stops before arm_vm_init - no candidate L1, no high-VA"
    echo "          windows, no loader, no handoff. Tests boot-args through post-PE only."
    ;;
  STAGE90_ENTRY_LADDER_PE_INIT_PLATFORM|2|2u)
    echo "PE_INIT_PLATFORM (2): stops before the post-PE bootstrap. Tests boot-args, early"
    echo "          pmap and PE_init_platform only."
    ;;
  STAGE90_ENTRY_LADDER_EARLY_PMAP|1|1u)
    echo "EARLY_PMAP (1): stops after the early pmap step. A thin test - boot-args plus one"
    echo "          stage of the ladder."
    ;;
  STAGE90_ENTRY_LADDER_BOOT_ARGS_ONLY|0|0u)
    echo "BOOT_ARGS_ONLY (0): the stub returns after validating boot-args. This run exercises"
    echo "          almost none of the ladder - useful only for isolating an early failure."
    ;;
  *)
    fail "unrecognised STAGE90_ENTRY_LADDER_LEVEL: $LADDER"
    ;;
esac

echo
echo "== fault injection =="
case "$FAULT_INJECT" in
  0|0u|"")
    echo "off: the handoff targets the Stage-owned high-VA function as usual."
    ;;
  *)
    [[ $ALLOW_FAULT_INJECT -eq 1 ]] || fail "this build jumps at an intentionally unmapped VA ($FAULT_INJECT); needs --allow-fault-inject"
    echo "FAULT INJECTION: this build will jump at $FAULT_INJECT, which is expected to be"
    echo "          unmapped, and the abort path should log the fault and reboot."
    echo "          Expected evidence: an 'exception pabort ... lr=$FAULT_INJECT' line."
    echo "          A silent hang or a boot loop instead means the address IS mapped -"
    echo "          the failure mode this mode exists to avoid."
    ;;
esac

echo
echo "== entering XNU =="
case "$(value_of STAGE90_XNU_ENTRY)" in
  0|0u|"")
    echo "off: the payload runs its own ladder and reboots, as in every stage so far."
    ;;
  *)
    [[ $ALLOW_XNU_ENTRY -eq 1 ]] || fail "this build jumps into XNU's _start and never returns; needs --allow-xnu-entry"
    echo "ENTERING XNU: the payload copies a linked image containing XNU's real osfmk/arm/start.s"
    echo "          to PA 0x80000000 (experiment 241's base; 0x00200000 before it), hands it a"
    echo "          boot_args (physBase == virtBase), and jumps."
    echo "          Nothing after that jump is the payload's: the page tables, vectors, caches"
    echo "          and MMU state are XNU's, and the payload never runs again."
    echo
    echo "          Endings, and the log says which:"
    echo "            '...real XNU entry: _start ran to completion and branched to arm_init'"
    echo "                 XNU's own entry sequence ran on this device, end to end."
    echo "            '...real XNU entry: exception: <which>'"
    echo "                 it faulted, and the vector names itself."
    echo "            neither, ending at 'jumping to XNU's _start'"
    echo "                 it hung; the hardware watchdog brings the phone back in ~28s."
    echo "            no log at all, and the device does not come back"
    echo "                 the net below did not recover it and the ram_console died with the"
    echo "                 cold boot; the phone needs a power-button press and the run is lost."
    echo "                 This is not hypothetical and it is not rare enough to ignore: it is"
    echo "                 experiment 517's first run (2026-09-21). One observation, and the"
    echo "                 cause is not known - the run had two changes in it, so the next image"
    echo "                 carries one (see STAGE90_XNU_EXIT_POC_FLUSH in build_entry.sh)."
    echo "                 A candidate mechanism exists and is now measured rather than"
    echo "                 reasoned (experiments 517/518): the interrupt handler's stack starts"
    echo "                 at cpu_data->istackptr, which is also where the idle loop runs, so"
    echo "                 the handler's 5th and 6th pushed words land on the return address"
    echo "                 the idle exit is about to pop - and 518's run shows it popping a"
    echo "                 timebase value into the PC. 518's arm (move istackptr to the middle"
    echo "                 of the interrupt stack) RAN on 2026-09-22 and could not test it:"
    echo "                 istackptr is read by the vector AND by the context switch that puts"
    echo "                 the idle thread on that stack, so the arm moved both and the gap is"
    echo "                 invariant (SS_SP == istackptr - 16 in 516 and in 518 alike). The"
    echo "                 same panic returned, with the device recovering on its own. The arm"
    echo "                 cannot be another value of that field, so 519 changes where the idle"
    echo "                 thread's stack comes from instead: Idle_context is the one place that"
    echo "                 choice is made, and the arm rewrites its ldr sp from"
    echo "                 cpu_data->istackptr into a load of this image's own 16 KB array - so"
    echo "                 the handler keeps the whole interrupt stack and the idle body runs"
    echo "                 outside [intstack_top - 16384, intstack_top), which is exactly what"
    echo "                 ml_at_interrupt_context() tests. (518 section 5 proposed"
    echo "                 __wrap_Idle_load_context; that hook restores sp from the thread's pcb,"
    echo "                 so it cannot choose the idle body's stack - experiment 519 section 1.)"
    echo "                 **519 RAN on 2026-09-22 and obtained both of its predictions.**"
    echo "                 The arm worked: xnu_live_idlestack_sp = 0x8054fea8 is inside"
    echo "                 [stage90_idle_stack, +16384) with xnu_live_idlestack_inwin = 0 and"
    echo "                 _calls = 1, so the idle body is out of the window the kernel's"
    echo "                 predicate tests - and the live chain agrees with the panic's own sp"
    echo "                 from two sides (array top 0x8054fee0, cpu_idle's 'sub sp, sp, #8',"
    echo "                 the exit wrapper's 8-byte frame => the exit's entry sp is"
    echo "                 0x8054fed0). And the run ends in the *diagnosed* panic, not"
    echo "                 Apple's flat one: 'sleh_abort: prefetch abort in kernel mode:"
    echo "                 fault_addr=0x7152a6c' with frame_ok = 1, fsr_frame = 5,"
    echo "                 sp 0x8054fed0, lr 0x800462dc, pc = far = 0x07152a6c - i.e."
    echo "                 ml_at_interrupt_context() answered false. The panel is the idle"
    echo "                 path's own registers one instruction before the pop (r0/r1 are"
    echo "                 the exit's own str, r5 and r4 are cpu_idle's, r4 = the value of"
    echo "                 cpu_data->rtcPop), so the two words the pop read are two counter"
    echo "                 readings one tick apart, the later one the deadline the idle loop"
    echo "                 held. 519 section 10 removes 518's mechanism by arithmetic:"
    echo "                 EXC_CTX_SIZE is 360 (80 + 264 + 16, genassym.c:188) and the VFP"
    echo "                 area never ends above the interrupted sp, so no exception frame"
    echo "                 writes above the sp it interrupted - the writer has to be"
    echo "                 WATCHED, not reasoned about. **520 RAN on 2026-09-22 and watched it.**"
    echo "                 That arm was 519's switch set unchanged with seven live-channel keys"
    echo "                 added per site and no state change anywhere. The death reproduced at"
    echo "                 the same instruction - xnu_live_sleh_storm = 9, sp = 0x8054fed0,"
    echo "                 lr = 0x800462dc, numerically identical to 519's - and the writer is"
    echo "                 now a number rather than a candidate: xnu_live_slot_rtcpre_pop ="
    echo "                 0x04b79075 is cpu_data->rtcPop read immediately before the fatal"
    echo "                 call, and the fault's pc = 0x04b79074 is rtcPop - 1, the Thumb"
    echo "                 relationship 519 read off its own r4/far pair. lr = 0x800462dc is"
    echo "                 the return address of platform_cache_idle_exit's bl FlushPoU_Dcache"
    echo "                 (0x800462d8) and its bcc at 0x80046300 is taken, so the faulting"
    echo "                 instruction is that function's own pop {fp, pc} at 0x8004633c - a"
    echo "                 pop, which is why lr still names the flush. The deadline reaches"
    echo "                 that word only via strd r4, [sp, #-12]! in"
    echo "                 __wrap_platform_cache_idle_enter (0x8047c8d4) and"
    echo "                 __wrap_cpu_idle_wfi (0x8047c888): pre-indexed 12 from the same X"
    echo "                 the exit wrapper's 8-byte frame lands on, so r4 goes to E-4, the"
    echo "                 slot's UPPER word. The exit's own push writes it first and still"
    echo "                 loses, because the window is open: caches.c:406 clears SCTLR.C,"
    echo "                 so the push reaches DRAM and touches neither the valid L1 line nor"
    echo "                 the L2 - 516's enter-side CleanPoC_Dcache is DCCSW (c7,c10,2),"
    echo "                 which CLEANS and leaves the line VALID - then caches.c:490 sets"
    echo "                 SCTLR.C again and the pop's load MISSES nothing, it hits. Apple's"
    echo "                 FlushPoU_Dcache is DCCISW, the right operation, but L1 only."
    echo "                 The repair is FlushPoC_Dcache (DCCISW over L1 AND L2), which is"
    echo "                 exactly the call 517 added at this site behind"
    echo "                 STAGE90_XNU_EXIT_POC_FLUSH and which has never been run on its own."
    echo "                 **So the arm to run is the exit-side PoC flush, switch on, with 520's"
    echo "                 instrument repaired per its section 11** - the two slot loads moved"
    echo "                 into the wrapper's inline asm (they were read through"
    echo "                 entry_slot_note's own frame: pre_m4 = 0x8047c974 is that note's own"
    echo "                 saved lr), the abort site publishing every abort instead of a"
    echo "                 geometric subsequence (the fatal seq 9 was neither <= 4 nor a power"
    echo "                 of two and its SS_SP 0x8054fed0 was INSIDE the guard - the one"
    echo "                 reading the arm existed for was sampled away), and the windows"
    echo "                 widened to take this boot's 0xc8xxxxxx stacks with the rtc note's"
    echo "                 inner refusals counted. The prediction is the panic's ABSENCE, not"
    echo "                 a further reading: the push then writes the only copy there is and"
    echo "                 the pop reads DRAM. Reading 4 answered its own question unused:"
    echo "                 ml_get_timebase was called >= 1024 times, 517's instrument"
    echo "                 published no xnu_live_tb_* record at all (its early return on"
    echo "                 SCTLR.C set), and its own pair spans exactly 24 ticks - so it is"
    echo "                 not the source of a 1-tick pair. The image this preflight is being"
    echo "                 run against is"
    echo "                 ${IMAGE_SHA16}... , computed from the file"
    echo "                 itself two screens up (label the switches above before reading"
    echo "                 this hash as a verdict - the FLUSH switch is the one that has to"
    echo "                 be 1 for the arm just described, and a 0 there is 520's arm again"
    echo "                 and re-reads the same death)."
    echo
    echo "          Safety: the hardware watchdog is the only net across the jump, deliberately"
    echo "          - the software dead-man needs the payload's GIC and vector state, which are"
    echo "          gone the moment _start switches tables. The watchdog needs neither."
    echo "          Its record, read from the ledgers rather than from this text: it recovered"
    echo "          every run from 506 to 515, including runs parked in the kernel's own idle"
    echo "          WFI and runs in an abort storm, and 516's two runs came back on XNU's own"
    echo "          'MACH Reboot'. It then did not recover 517's first run. So: a net that has"
    echo "          held many times and is not proved to hold always. A hang here may need a"
    echo "          power press, and the device is never at risk of being bricked - nothing in"
    echo "          this project is ever written to storage."
    ;;
esac

echo
echo "== later-phase probes =="
# These two run inside kernel_entry and are non-fatal by construction: each reports and the boot
# continues, because they are the next phase's work rather than a precondition for this run. They
# are listed rather than ignored so that "the run passed" and "the shim passed" cannot be
# confused - the payload says so in the log too. Neither changes any mapping or boot decision, so
# there is nothing to allow; if either ever does, it needs a flag of its own.
case "$(value_of STAGE90_XNU_BOOT_ARGS)" in
  0|0u|"") echo "conforming boot_args (Phase 2): off - only the ladder's own identity-based args exist." ;;
  *)       echo "conforming boot_args (Phase 2): ON, as a second object alongside the ladder's. It"
           echo "          is built and its invariants checked against the real __stage90_image_end."
           echo "          Non-fatal: read xnu_ba_checks/xnu_ba_failures in the log for its verdict." ;;
esac
case "$(value_of STAGE90_XNU_MSM8974_SHIM)" in
  0|0u|"") echo "MSM8974 platform shim (Phase 3): off." ;;
  *)       echo "MSM8974 platform shim (Phase 3): ON. Registers its tbd_ops and checks the EOI"
           echo "          pairing, the measured CNTP interrupt number and the validated CNTFRQ."
           echo "          Non-fatal: read msm8974_shim_failures in the log for its verdict." ;;
esac

case "$(value_of STAGE90_XNU_MSM8974_FIQ_PROBE)" in
  0|0u|"") echo "FIQ availability probe: off." ;;
  *)       echo "FIQ availability probe: ON. Unmasks CPSR.F with the timer armed and a bounded"
           echo "          spin, to measure whether non-secure PL1 can take an FIQ on this SoC."
           echo "          If a FIQ IS delivered the vector logs 'exception: fiq' and reboots,"
           echo "          which is the expected successful outcome, not a hang." ;;
esac

echo
echo "== mapping attributes =="
case "$(value_of STAGE90_PMAP_ATTR_MODE)" in
  STAGE90_PMAP_ATTR_MODE_SO_ONLY|0|0u)
    echo "SO_ONLY: every mapping Strongly-ordered, as in every stage so far."
    ;;
  STAGE90_PMAP_ATTR_MODE_NORMAL_NC|1|1u)
    [[ $ALLOW_ATTR -eq 1 ]] || fail "NORMAL_NC changes DRAM memory types and is not allowed without --allow-attr-normal-nc"
    echo "NORMAL_NC: DRAM is Normal/Non-cacheable, MMIO stays Strongly-ordered."
    echo "           This changes the memory model of the whole payload, so run it"
    echo "           on its own and read the log."
    ;;
  STAGE90_PMAP_ATTR_MODE_NORMAL_WB|2|2u)
    [[ $ALLOW_ATTR_WB -eq 1 ]] || fail "NORMAL_WB marks DRAM cacheable and is not allowed without --allow-attr-normal-wb"
    echo "NORMAL_WB: DRAM is Normal/Write-Back/Write-Allocate and SHAREABLE, MMIO stays"
    echo "           Strongly-ordered. The section descriptor also drops PL0 access"
    echo "           (AP 11 -> 01), which is F-AM2 and safe because everything in the"
    echo "           payload runs at PL1. Cacheable descriptors do nothing on their own:"
    echo "           the cache switches below decide whether they are used."
    ;;
  *)
    fail "unrecognised STAGE90_PMAP_ATTR_MODE: $(value_of STAGE90_PMAP_ATTR_MODE)"
    ;;
esac

echo
echo "== caches =="
case "$(value_of STAGE90_CACHE_MODE)" in
  STAGE90_CACHE_MODE_NONE|0|0u)
    echo "none: SCTLR.C and SCTLR.I both clear, as in every stage so far."
    echo "      Cacheable DRAM descriptors, if the mode above sets them, are then"
    echo "      treated as non-cacheable by the hardware."
    ;;
  STAGE90_CACHE_MODE_ICACHE|1|1u)
    [[ $ALLOW_ICACHE -eq 1 ]] || fail "the I-cache is enabled and that is not allowed without --allow-icache"
    echo "I-cache: SCTLR.I set at MMU enable time, after an ICIALLU. This is XNU's own"
    echo "         order - its start.s enables the I-cache in its first instructions."
    echo "         It is safe here only because nothing in this payload writes code at"
    echo "         runtime; the Mach-O loader is the path that eventually would, and it"
    echo "         currently refuses to copy at all. What to look for in the log: the"
    echo "         same kernel_entry ok as the default build, and on a real regression a"
    echo "         prefetch abort with a plausible pc rather than a silent difference."
    ;;
  STAGE90_CACHE_MODE_ICACHE_DCACHE|2|2u)
    [[ $ALLOW_DCACHE -eq 1 ]] || fail "the D-cache is enabled and that is not allowed without --allow-dcache"
    echo "I-cache + D-cache: SCTLR.I and SCTLR.C, the D-cache after the MMU is already on"
    echo "         and after a whole-cache clean-and-invalidate. This is the full Phase 1"
    echo "         configuration. It is the first run where the payload is not"
    echo "         immediately-visible memory, so the things to read in the log are the"
    echo "         ones that depend on cache maintenance being right:"
    echo "           - ram_console must still log, and the whole log must be there (it is"
    echo "             mapped non-cacheable even in NORMAL_WB, so it should not need any"
    echo "             maintenance - a truncated or stale log would mean that failed);"
    echo "           - the candidate-L1 window and the TTBR0 roundtrip must still verify"
    echo "             their translations, which is what the table cleans are for;"
    echo "           - the timer IRQ must still be delivered and the device must come back"
    echo "             on its own."
    echo "         A device that hangs here costs a power press, and the two nets are both"
    echo "         armed, so it comes back either way - but the log is how the failure is"
    echo "         told apart from a stale-cache symptom, which is why it is read first."
    ;;
  *)
    fail "unrecognised STAGE90_CACHE_MODE: $(value_of STAGE90_CACHE_MODE)"
    ;;
esac

echo
echo "== mapping attributes: consistency =="
if [[ ! "$(value_of STAGE90_CACHE_MODE)" =~ ^(STAGE90_CACHE_MODE_NONE|0|0u)$ ]] &&
   [[ ! "$(value_of STAGE90_PMAP_ATTR_MODE)" =~ ^(STAGE90_PMAP_ATTR_MODE_NORMAL_WB|2|2u)$ ]]; then
  fail "the I-cache is enabled but DRAM is not marked cacheable, so it would cache nothing"
fi
echo "ok: cache and attribute switches agree"

echo
echo "All checks passed. To run the non-persistent boot (writes nothing to storage):"
echo
echo "  sudo adb -s 4a2fe00b reboot bootloader"
echo "  sudo fastboot boot $IMAGE"
echo
echo "Recover afterwards with:"
echo "  sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-last_kmsg.txt"
