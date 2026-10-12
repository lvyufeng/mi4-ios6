#!/usr/bin/env bash
# Build 985: 984's switch set UNCHANGED, rebuilt with the BOARD TICK FIX (start the D13 clock).
#
# WHY 984 IS NOT THE ONE. 984's press (2026-10-12, commit 6d2ccdf) instrumented IOKitInitializeTime
# and the reading was decisive: `xnu_live_iokitinit_enter` present, `_exit`/`cal_enter` ABSENT, and
# the publisher DID arrive (`xnu_live_pub2_key` = the "IORTC" string at record 4524, AFTER the park
# at 4222). So the boot thread is parked inside `waitForService(IORTC, 2s)` and the wait never
# returns. The deeper symptom: `xnu_live_timerdrv_isr_calls=0`, NO `xnu_live_dec_*` keys at all, and
# `xnu_live_tmr_enter_seq` capped at 64. XNU's timer interrupt subsystem is NON-FUNCTIONAL on D13.
#
# ROOT CAUSE (985, found 2026-10-12, three independent source readings). The D13 boot has no timer
# interrupt of ANY kind: `CONFIG_TICKLESS` is defined NOWHERE in this tree, so `setPop`'s arming in
# `osfmk/arm/rtclock.c:118-144` (both `clock_decrementer =` and `pe_arm_set_timer_enabled`) is
# compiled out; this file's `msm8974_timer_enabled` was an EMPTY STUB; so nothing ever programs the
# CNTV countdown, PPI 20 never fires, `rtclock_intr` -> `etimer_intr` never runs, and every deadline
# sleep is starved - including `waitForService`'s 2 s `thread->wait_timer`, which cannot expire.
#
# THE FIX (pe_msm8974.c, 985), ONE site, once: a fixed-period (1 kHz) CNTV tick owned by this PE.
#   `msm8974_tick_start()` is called from the END of `PE_init_SocSupport_msm8974`. That site is safe
#   by two reads: `PE_init_SocSupport` is reached through `PE_init_platform(TRUE)` at
#   `osfmk/arm/arm_init.c:178`, AFTER `cpu_init()` (`:130`) has run
#   `timer_call_initialize_queue(&cdp->rt_timer.queue)` (so the queue `etimer_intr` walks is
#   initialized) and after `arm_vm_init` (so the GIC VA this file maps is the one the MMU walks).
#   It enables PPI 20 at the distributor through the entry image's own `entry_irq_enable_line`, then
#   arms `CNTV_TVAL` = one period and `CNTV_CTL` = ENABLE. `msm8974_handle_interrupt`'s intid-20
#   branch calls `rtclock_intr` and then RE-ARMS (tickless is off, so the kernel never re-arms the
#   hardware itself - the boot-time arming is not one-shot). The period is derived from
#   `gPEClockFrequencyInfo.timebase_frequency_hz` (19.2 MHz), never a literal.
#
# The switch set is 984's UNCHANGED (window 0x1e400000 = 484 MiB, RESIDENT=1, USB ladder,
# MEM_TOTAL/CARD_TOTAL=1, HFS root from card, HDD_WRITE=0 = base never written). Only the ENTRY BIN
# moves. This script re-reads the current record and re-exports it verbatim, overriding nothing
# ([[mi4-build-variant-comes-from-an-env-default]]).
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 985: rebuild the board PE object, then the entry image (start the D13 clock tick) =="

# The switch set the entry link will enforce, re-exported here so the platform block and the entry
# link compile `stage90_root_media.c` under the SAME defines ([[mi4-one-value-two-definitions]] -
# `build_entry.sh` refuses the module if they disagree, at :27854). MEM_SIZE_MAX is `(unset)` in the
# record, so it is correctly absent from the define list.
EXTRA=""
for k in STAGE90_XNU_HFS_ROOT_MEDIA STAGE90_XNU_EMMC_STRATEGY STAGE90_XNU_ROOT_FROM_CARD \
         STAGE90_XNU_HDD_WRITE STAGE90_XNU_CARD_TOTAL STAGE90_XNU_FULL_EXTENT \
         STAGE90_XNU_CARD_COW STAGE90_XNU_MEM_TOTAL; do
  v=$(awk -F= -v k="$k" '$1==k{print $2}' "$REC")
  [[ -z $v || $v == '(unset)' ]] && continue
  EXTRA="$EXTRA -D$k=$v"
done
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# The board PE is compiled by `tools/build_xnu_arm_kernel.sh`'s platform block, and
# `build_entry.sh` REFUSES a stale `pe_msm8974.o` (`platform_obj_fresh`,
# `src/entry/build_entry.sh:27851`) - so the object must be rebuilt BEFORE the entry link. This is
# the same "content, not mtime" discipline the 980 staleness fix put around the kernel tree.
echo "-- platform block (rebuild out/xnu_platform_obj_d13/pe_msm8974.o) --"
XNU_TREE="$XNU_TREE" \
XNU_KERNEL_CONFIG=STAGE90_XNU \
XNU_MASTER_LOCAL="$PWD/tools/xnu_config/boot/STAGE90_XNU.local" \
XNU_KERNEL_EXTRA_DEFINES="$EXTRA" \
    ./tools/build_xnu_arm_kernel.sh --platform-only

echo "-- entry image (984's switch set, re-exported verbatim) --"
EXPORTS=()
while IFS= read -r line; do
  line=${line%%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  k=${line%%=*}; v=${line#*=}
  [[ $v == '(unset)' ]] && continue
  case $k in
    STAGE90_XNU_ENTRY_SHA256|STAGE90_XNU_ENTRY_BYTES) continue ;;  # outputs, not inputs
  esac
  EXPORTS+=("$k=$v")
done < "$REC"
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   984 was 6435778a - a DIFFERENT value here means the board tick moved the image, as it must."
grep -E "ENTRY_WINDOW" out/stage90/xnu_arm_entry-config.txt