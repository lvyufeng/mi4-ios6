#!/usr/bin/env bash
# Park 986 (armed-d13-2171f0d1) and append its block to records/revert-set.txt.
#
# 986 = 985's switch set UNCHANGED, rebuilt with THE MLCKWAIT WAITERS-BIT FIX. The ENTRY BIN moved
# (c36a3e06 -> 2171f0d1).
#
# WHY (985's press, 2026-10-12, arm armed-d13-c36a3e06, commit cfe99f4): the D13 clock ARMED and FIRED
# (tick_arm_rc=1, tick_line_rc=1, tick_irq 1->2->4->8->16) and the boot thread's 2 s deadline sleep
# (lck_mtx_sleep_deadline @0x8001ccb0) now RETURNS. But the boot still stops in IOKitInitializeTime:
# no cal_enter, no ubc_init. After the wait returns the SAME thread enters `mlckwait` (0x80497510 = the
# return address of mlckwait's `bl lck_mtx_lock_wait`) and never comes back; a second thread parks at
# the same site.
#
# ROOT CAUSE (986, adversarial pass CLAIM CONFIRMED): a LOST WAKEUP in the D13 ARM mutex wait. The
# global `osfmk/arm/hw_lock.s` `mlckwait` must set bit 1 (ARM_LCK_WAITERS) before it sleeps so the
# owner's unlock wakes it. The linked image instead had `0x80497504: e3833000  orr r3, r3, #0` - bit 0
# (interlock) and bit 1 both clear - so lck_mtx_unlock's fast path ((word&3)==0) was always taken,
# lck_mtx_unlock_wakeup -> thread_wakeup_one never ran, and the waiter was stranded. `lmuslow` PANICS
# unless bit 1 is set, which is the invariant. `hw_lock.s:457` gates the correct `orr r3, r3, #3`
# behind `#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO` (a 4-line fork addition; upstream Apple
# is plain #0); this ELF build's assembler gets NO board macro, so the gate closed.
#
# WHAT 986 ADDS: `tools/stage_d13_hw_lock_986.sh` makes the `#3` UNCONDITIONAL (idempotent; the tree is
# a gitignored re-provisionable checkout), then `tools/assemble_arm_layer.sh` rebuilds
# `out/xnu_asm_obj_d13/hw_lock.o`. `build_entry.sh` gains a by-value clause (after the bsd_ast clause)
# that REFUSES a linked image whose first `mlckwait` instruction is not `e3833003 orr r3,r3,#3`.
# hw_lock.s is D13-only (4570 uses osfmk/arm/locks_arm.c), so the edit is clean.
#
# READ: the boot moves PAST `mlckwait` (a new stop, or further: cal_enter / ubc_init) => the lost
# wakeup is CONFIRMED as 985's frontier. It STILL parks at `mlckwait` => the mechanism is not the (or
# not the only) cause and the deadlock hypothesis wins; the next rung is who holds the mutex.
#
# VERIFIED BY VALUE: the linked `xnu_arm_entry.elf` at 0x80497504 is `e3833003 orr r3,r3,#3` (was
# e3833000); `hw_lock.o` `504: e3833003 orr r3, r3, #3`.
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible. fastboot boot only, NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

ARM=armed-d13-2171f0d1
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
  printf '# %s  -  986: 985 + the MLCKWAIT WAITERS-BIT FIX (wake the parked mutex waiter).\n' "$ARM"
  printf '# Switch set is 985-s UNCHANGED; the ENTRY BIN moved (c36a3e06 -> 2171f0d1).\n#\n'
  printf '# 985 (pressed 2026-10-12, arm armed-d13-c36a3e06, commit cfe99f4) ARMED and FIRED the D13 clock\n'
  printf '# (tick_arm_rc=1, tick_line_rc=1, tick_irq 1->2->4->8->16) and the boot thread-s 2 s deadline\n'
  printf '# sleep (lck_mtx_sleep_deadline @0x8001ccb0) now RETURNS. But the boot still stops in\n'
  printf '# IOKitInitializeTime (no cal_enter, no ubc_init): the SAME thread then enters `mlckwait`\n'
  printf '# (0x80497510 = the return address of mlckwait-s `bl lck_mtx_lock_wait`) and never returns.\n#\n'
  printf '# ROOT CAUSE: a LOST WAKEUP in the D13 ARM mutex wait. `mlckwait` must set bit 1\n'
  printf '# (ARM_LCK_WAITERS) before it sleeps; the linked image had `orr r3, r3, #0` (0x80497504:\n'
  printf '# e3833000) - NEITHER bit 0 nor bit 1 - so lck_mtx_unlock-s fast path ((word&3)==0) was always\n'
  printf '# taken, lck_mtx_unlock_wakeup/thread_wakeup_one never ran, and the waiter was stranded.\n'
  printf '# `lmuslow` PANICS unless bit 1 is set (the invariant). The correct `orr r3, r3, #3` is gated\n'
  printf '# behind `#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO` (a 4-line fork addition;\n'
  printf '# upstream Apple is plain #0), and this ELF build-s assembler gets no board macro.\n#\n'
  printf '# 986: make the `#3` UNCONDITIONAL (tools/stage_d13_hw_lock_986.sh; hw_lock.s is D13-only),\n'
  printf '# rebuild out/xnu_asm_obj_d13/hw_lock.o, and refuse a linked image whose first mlckwait\n'
  printf '# instruction is not `e3833003 orr r3,r3,#3`. VERIFIED BY VALUE: 0x80497504 = e3833003.\n#\n'
  printf '# READ: past `mlckwait` => the lost wakeup is CONFIRMED as 985-s frontier; still parked there\n'
  printf '# => the deadlock hypothesis wins and the next rung is who holds the mutex.\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=986-d13-mlckwait-waiters-bit;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3