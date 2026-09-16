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
# Usage: ./preflight_boot_check.sh [--allow-preflight] [--allow-full] [--allow-selftest] [--allow-attr-normal-nc] [--allow-hw-watchdog-selftest] [--allow-fault-inject]

set -euo pipefail

cd "$(dirname "$0")"
STAGE_DIR=$PWD
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)
OUT=$REPO_ROOT/out/stage90

ALLOW_PREFLIGHT=0
ALLOW_FULL=0
ALLOW_SELFTEST=0
ALLOW_ATTR=0
ALLOW_HW_SELFTEST=0
ALLOW_FAULT_INJECT=0

for arg in "$@"; do
  case "$arg" in
    --allow-preflight)   ALLOW_PREFLIGHT=1 ;;
    --allow-full)        ALLOW_FULL=1 ;;
    --allow-selftest)    ALLOW_SELFTEST=1 ;;
    --allow-attr-normal-nc) ALLOW_ATTR=1 ;;
    --allow-hw-watchdog-selftest) ALLOW_HW_SELFTEST=1 ;;
    --allow-fault-inject) ALLOW_FAULT_INJECT=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

fail() { echo "REFUSING: $*" >&2; exit 1; }

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
# The payload must never reference storage-controller code. It writes MMIO, IMEM
# and PS_HOLD only; any storage symbol means something changed that should not have.
if arm-none-eabi-nm -a "$OUT/stage90.elf" 2>/dev/null \
     | grep -iE 'sdcc|emmc|\bmmc\b|ufs|partition|flash_|nand' ; then
  fail "payload references storage symbols (see above)"
fi
echo "no storage symbols in the payload"

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
  [[ $ALLOW_HW_SELFTEST -eq 1 ]] || fail "the hardware-watchdog SELFTEST spins forever on purpose; needs --allow-hw-watchdog-selftest"
  echo "HW WATCHDOG SELFTEST: allowed. The payload spins and the hardware countdown"
  echo "          should reboot it at ~33s. If the watchdog does NOT fire, the spin is"
  echo "          bounded and PS_HOLD returns the device at ~90s instead - so this run"
  echo "          cannot leave the phone dark either way. Time-to-return IS the result:"
  echo "          ~33s = watchdog fired, ~90s = it did not (and the log says which)."
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
echo "== mapping attributes =="
case "$(value_of STAGE90_PMAP_ATTR_MODE)" in
  STAGE90_PMAP_ATTR_MODE_SO_ONLY|0|0u)
    echo "SO_ONLY: every mapping Strongly-ordered, as in every stage so far."
    ;;
  STAGE90_PMAP_ATTR_MODE_NORMAL_NC|1|1u)
    [[ $ALLOW_ATTR -eq 1 ]] || fail "NORMAL_NC changes DRAM memory types and is not allowed without --allow-attr-normal-nc"
    echo "NORMAL_NC: DRAM is Normal/Non-cacheable, MMIO stays Strongly-ordered."
    echo "           This is the Phase 1a exclusives change - it alters the memory"
    echo "           model of the whole payload, so run it on its own and read the log."
    ;;
  *)
    fail "unrecognised STAGE90_PMAP_ATTR_MODE: $(value_of STAGE90_PMAP_ATTR_MODE)"
    ;;
esac

echo
echo "All checks passed. To run the non-persistent boot (writes nothing to storage):"
echo
echo "  sudo adb -s 4a2fe00b reboot bootloader"
echo "  sudo fastboot boot $IMAGE"
echo
echo "Recover afterwards with:"
echo "  sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-last_kmsg.txt"
