#!/usr/bin/env bash
# Build 982: 981's switch set, rebuilt on the kernel sources with the GIC interrupt fix.
#
# WHY 981 IS NOT THE ONE. 981's press (2026-10-11) got XNU through arm_vm_init/pmap/VM/zone/scheduler
# and into `bsd_init` (the 981 bsd_init markers fired), then took a **section translation fault in
# interrupt context**:
#     sleh_abort: Abort in interrupt handler
#     r0=0xf900200c pc=0x801ae3ac fsr=0x00000005 far=0xf900200c
# The faulting instruction is `ldr r1, [r0]` inside `msm8974_handle_interrupt`, reading the RAW
# PHYSICAL `GICC_IAR` (0xf900200c). XNU's own pmap does not translate 0xf9002000: `arm_vm_init.c`
# copies the boot L1 and then overwrites only `[physBase, physBase+gMemSize)` and the managed window,
# so the entry image's 0xf9000000 device section does not survive `set_mmu_ttb`. Every peer board
# (`pe_apq8060.c:128-129`) and the mandated reference (`HTC-Leo-Revival-Project/xnu` `pe_msm8974.c`)
# `ml_io_map` the GIC and read the returned VA; ours read physical.
#
# THE FIX (pe_msm8974.c, 982), TWO halves of ONE frontier - the same handler is the live dispatcher
# after XNU owns the tables (the 981 fault proves it), so it must both map AND service:
#   A) `msm8974_gic_map()` calls `ml_io_map(0xf9002000, 0x1000)` from `PE_init_SocSupport_msm8974`
#      (before any line is enabled) and `msm8974_handle_interrupt` reads IAR/EOIR through the mapped
#      VA, with a NULL-guard that returns rather than repeating the abort. VERIFIED BY VALUE: the
#      handler in the object is `movw/movt r0,#0; ldr r0,[r0]; cmp; bxeq lr; ldr r1,[r0,#12]; ...
#      strne r1,[r0,#16]` - NO `movt r0, #0xf900`; `ml_io_map` is a referenced symbol.
#   B) the timer line (INTID 20, `src/entry/entry_gic.h:135`) now calls `rtclock_intr(context)` after
#      EOI, exactly as every peer (pe_apq8060.c:246) and the reference do. Without it the countdown
#      is never re-armed and the kernel tick never advances - a handler that maps the GIC but only
#      acks+EOIs would freeze at the first timer interrupt.
#
# The switch set is 981's UNCHANGED (window 0x1e400000 = 484 MiB, RESIDENT=1, USB ladder,
# MEM_TOTAL/CARD_TOTAL=1, HFS root from card, HDD_WRITE=0 = base never written). Only the ENTRY BIN
# moves. This script re-reads the current record and re-exports it verbatim, overriding nothing
# ([[mi4-build-variant-comes-from-an-env-default]]).
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 982: rebuild the entry image with the GIC interrupt fix (map + timer) =="

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
echo "   981 was 7fbd7009 - a DIFFERENT value here means the PE fix moved the image, as it must."
grep -E "ENTRY_WINDOW" out/stage90/xnu_arm_entry-config.txt