#!/usr/bin/env bash
# Park 985 (armed-d13-c36a3e06) and append its block to records/revert-set.txt.
#
# 985 = 984's switch set UNCHANGED, rebuilt with THE BOARD TICK FIX. The ENTRY BIN moved
# (6435778a -> c36a3e06).
#
# WHY (984's press, 2026-10-12, commit 6d2ccdf): 984 instrumented IOKitInitializeTime and the
# reading was decisive - xnu_live_iokitinit_enter present, _exit/cal_enter ABSENT, the publisher DID
# arrive (xnu_live_pub2_key = the "IORTC" string at record 4524, AFTER the park at 4222). The boot
# thread is parked inside waitForService(IORTC, 2s) and the wait never returns. The deeper symptom:
# xnu_live_timerdrv_isr_calls=0, NO xnu_live_dec_* keys at all, xnu_live_tmr_enter_seq capped at 64 -
# XNU's timer interrupt subsystem is NON-FUNCTIONAL on D13.
#
# ROOT CAUSE (985): the D13 boot has no timer interrupt of ANY kind. CONFIG_TICKLESS is defined
# NOWHERE in this tree, so `setPop`'s arming (osfmk/arm/rtclock.c:118-144 - both `clock_decrementer =`
# and `pe_arm_set_timer_enabled`) is compiled out; this file's msm8974_timer_enabled was an EMPTY
# STUB; so nothing ever programs CNTV, PPI 20 never fires, rtclock_intr -> etimer_intr never runs,
# and every deadline sleep is starved - including waitForService's 2 s thread->wait_timer.
#
# WHAT 985 ADDS (pe_msm8974.c): a fixed-period (1 kHz) CNTV tick owned by the PE. msm8974_tick_start()
# is called from the END of PE_init_SocSupport_msm8974 - safe by two reads: PE_init_SocSupport is
# reached via PE_init_platform(TRUE) at osfmk/arm/arm_init.c:178, AFTER cpu_init() (:130) ran
# timer_call_initialize_queue(&cdp->rt_timer.queue) and after arm_vm_init. It enables PPI 20 at the
# distributor through entry_irq_enable_line, then arms CNTV_TVAL = one period and CNTV_CTL = ENABLE;
# msm8974_handle_interrupt's intid-20 branch calls rtclock_intr and RE-ARMS (the boot-time arming is
# not one-shot). The period is derived from gPEClockFrequencyInfo.timebase_frequency_hz (19.2 MHz),
# never a literal.
#
# READ: xnu_live_tick_arm_rc=1 and xnu_live_tick_line_rc=1 => the tick armed; xnu_live_tick_irq
# climbing (powers of two) => the tick is FIRING; IOKitInitializeTime passing to clock_initialize_calendar
# (xnu_live_cal_enter) => the wait returned and the boot is past the 984 frontier.
#
# VERIFIED BY VALUE: PE_init_SocSupport_msm8974 tail-branches (b 290) into msm8974_tick_start, whose
# body bl 0 <entry_irq_enable_line>(20, 1) then `mcr 15,0,r4,cr14,cr3,{0}` (CNTV_TVAL = 19200 =
# 19.2 MHz / 1000) and `mcr 15,0,r0,cr14,cr3,{1}` (CNTV_CTL ENABLE), read back into the rc key.
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible. fastboot boot only, NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

ARM=armed-d13-c36a3e06
LIVE=out/stage90
PARK=$LIVE/frozen/$ARM
REC=records/revert-set.txt
MEMBERS=(xnu_arm_entry.bin xnu_arm_entry.elf xnu_arm_entry.h xnu_arm_entry-config.txt \
         xnu_arm_entry-sources.txt stage90.bin stage90.elf stage90.img stage90-qcdt.img \
         stage90_fixture.macho stage90-build-config.txt SHA256SUMS.txt)

[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  985: 984 + the BOARD TICK FIX (start the D13 clock).\n' "$ARM"
  printf '# Switch set is 984-s UNCHANGED; the ENTRY BIN moved (6435778a -> c36a3e06).\n#\n'
  printf '# 984 (pressed 2026-10-12, commit 6d2ccdf) instrumented IOKitInitializeTime; the reading was\n'
  printf '# decisive: xnu_live_iokitinit_enter present, _exit/cal_enter ABSENT, and the publisher DID\n'
  printf '# arrive (xnu_live_pub2_key = the "IORTC" string at record 4524, AFTER the park at 4222). The\n'
  printf '# boot thread parks inside waitForService(IORTC, 2s) and the wait never returns. Deeper:\n'
  printf '# xnu_live_timerdrv_isr_calls=0, no xnu_live_dec_* keys, xnu_live_tmr_enter_seq capped at 64 -\n'
  printf '# XNU-s timer interrupt subsystem is NON-FUNCTIONAL on D13.\n#\n'
  printf '# ROOT CAUSE: the D13 boot has no timer interrupt of any kind. CONFIG_TICKLESS is defined\n'
  printf '# nowhere, so setPop-s arming (osfmk/arm/rtclock.c:118-144) is compiled out; msm8974_timer_enabled\n'
  printf '# was an empty stub; nothing programs CNTV, PPI 20 never fires, rtclock_intr -> etimer_intr never\n'
  printf '# runs, and every deadline sleep (thread->wait_timer, timer_call) is starved.\n#\n'
  printf '# 985 ADDS a fixed-period (1 kHz) CNTV tick. msm8974_tick_start() runs from the END of\n'
  printf '# PE_init_SocSupport_msm8974, which is reached via PE_init_platform(TRUE) at arm_init.c:178 -\n'
  printf '# AFTER cpu_init() (:130, the rt_timer.queue) and after arm_vm_init. PPI 20 enabled through\n'
  printf '# entry_irq_enable_line; msm8974_handle_interrupt-s intid-20 branch re-arms after rtclock_intr.\n'
  printf '# PERIOD from gPEClockFrequencyInfo.timebase_frequency_hz (19.2 MHz), never a literal.\n#\n'
  printf '# VERIFIED BY VALUE: PE_init_SocSupport_msm8974 tail-branches (b 290) into msm8974_tick_start,\n'
  printf '# whose body is bl entry_irq_enable_line(20,1) then `mcr 15,0,r4,cr14,cr3,{0}` (CNTV_TVAL =\n'
  printf '# 19200 = 19.2 MHz/1000) and `mcr 15,0,r0,cr14,cr3,{1}` (CNTV_CTL ENABLE), read back into the key.\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=985-d13-clock-tick-arm;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3