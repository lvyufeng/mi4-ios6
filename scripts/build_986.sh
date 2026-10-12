#!/usr/bin/env bash
# Build 986: 985's switch set UNCHANGED, rebuilt with THE MLCKWAIT WAITERS-BIT FIX.
#
# WHY 985 IS NOT THE ONE. 985's press (2026-10-12, commit cfe99f4, arm armed-d13-c36a3e06) ARMED the
# D13 clock: tick_arm_rc=1, tick_line_rc=1, xnu_live_tick_irq=1->2->4->8->16, and the boot thread's 2 s
# deadline sleep (lck_mtx_sleep_deadline @0x8001ccb0) now RETURNS (block_returns=1) - 984's wall is
# broken. But the boot still stops in IOKitInitializeTime: no cal_enter, no ubc_init. After the wait
# returns, the SAME boot thread enters `mlckwait` (0x80497510 = lck_mtx_lock_wait+0x4, the return
# address of mlckwait's `bl lck_mtx_lock_wait`) and never comes back; a second thread (0xc0bbfa70)
# parks at the same site.
#
# ROOT CAUSE (986, confirmed by an adversarial pass - CLAIM CONFIRMED). A LOST WAKEUP in the D13 ARM
# mutex wait, in `osfmk/arm/hw_lock.s`'s `mlckwait`:
#   * `lck_mtx_lock`'s slow path reaches `mlckwait` only with owner bits in the lock word, and MUST
#     record that a waiter is queued - bit 1, ARM_LCK_WAITERS - before it sleeps.
#   * The linked image instead has `80497504: e3833000  orr r3, r3, #0` - bit 0 (interlock) and bit 1
#     (waiters) BOTH clear, so the waiter leaves NO trace.
#   * `lck_mtx_unlock`'s fast path is `(word & 3) == 0` (hw_lock.s:387-394). With no bit set, the owner
#     takes it, `lck_mtx_unlock_wakeup` -> `thread_wakeup_one` is never called, and the parked thread is
#     stranded. `lck_mtx_ilk_unlock` clears bit 0 only (machine_routines_asm.s:50-54), so nothing else
#     in the tree sets bit 1.
#   * The unlock slow path PANICS unless bit 1 is set (`lmuslow: ands r2,r3,#2; beq lmupanic`) - the
#     invariant, in the tree's own words: "unlock needs bit 1 to wake waiters".
#
# WHY IT WAS #0. `hw_lock.s:457` gates the correct `orr r3, r3, #3` behind
# `#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO`. Upstream Apple's file is plain `#0` (identical
# in the imbushuo/HTC-Leo/winocm mirrors); the `#3` is a 4-line addition by the fork. Apple's Mach-O
# build defines `-DBOARD_CONFIG_$(MACHINE_CONFIG)` (default ARMPBA8) and takes `#3`; this project's ELF
# assembly path gets NO board macro (`tools/xnu_config/arm_asm_defines.sh` emits only the
# configuration's CONFIG_* options), so the gate closed and the defective arm linked.
#
# THE FIX: `tools/stage_d13_hw_lock_986.sh` makes `mlckwait`'s `orr r3, r3, #3` UNCONDITIONAL. Safe
# because `hw_lock.s` is D13-only - 4570 uses `osfmk/arm/locks_arm.c` and ships no hw_lock.s (0 entries
# in the 4570 manifest, 1 in the D13 manifest). `build_entry.sh` gains a by-value clause that REFUSES a
# linked image whose first `mlckwait` instruction is not `e3833003 orr r3,r3,#3` (a claim in a comment
# is not a check).
#
# The switch set is 985's UNCHANGED (window 0x1e400000 = 484 MiB, RESIDENT=1, USB ladder,
# MEM_TOTAL/CARD_TOTAL=1, HFS root from card, HDD_WRITE=0 = base never written). Only the ENTRY BIN
# moves. This script re-reads the current record and re-exports it verbatim, overriding nothing
# ([[mi4-build-variant-comes-from-an-env-default]]).
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 986: stage the mlckwait WAITERS-bit fix, rebuild the asm layer, then the entry image =="

# The switch set the entry link will enforce, re-exported here so the platform block and the entry
# link compile `stage90_root_media.c` under the SAME defines ([[mi4-one-value-two-definitions]]).
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

# 1. Stage the tree edit (idempotent), then rebuild the ARM asm layer so `out/xnu_asm_obj_d13/hw_lock.o`
#    carries the fix. `build_entry.sh` only CHECKS this object (it globs the pool), so the reassembly is
#    the load-bearing step - without it the linked image keeps the old hw_lock.o and the patch is
#    invisible (933/stage_d13_boot_path_970h's rule).
echo "-- stage the fix + rebuild the ARM asm layer (out/xnu_asm_obj_d13/hw_lock.o) --"
tools/stage_d13_hw_lock_986.sh
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# 2. The platform PE object is UNCHANGED from 985, but `build_entry.sh` REFUSES a stale
#    `pe_msm8974.o` (platform_obj_fresh) and the entry link needs it present, so rebuild it under the
#    same defines - the same "content, not mtime" discipline 980/981 put around the pools.
echo "-- platform block (rebuild out/xnu_platform_obj_d13/pe_msm8974.o) --"
XNU_TREE="$XNU_TREE" \
XNU_KERNEL_CONFIG=STAGE90_XNU \
XNU_MASTER_LOCAL="$PWD/tools/xnu_config/boot/STAGE90_XNU.local" \
XNU_KERNEL_EXTRA_DEFINES="$EXTRA" \
    ./tools/build_xnu_arm_kernel.sh --platform-only

echo "-- entry image (985's switch set, re-exported verbatim) --"
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
echo "   985 was c36a3e06 - a DIFFERENT value here means the mlckwait fix moved the image, as it must."
grep -E "ENTRY_WINDOW" out/stage90/xnu_arm_entry-config.txt
echo "-- by value: the linked mlckwait instruction --"
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf |
  awk '/<mlckwait>:/ { seen = 1 } seen && /orr/ { print "  " $0; exit }'