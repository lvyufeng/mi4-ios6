#!/usr/bin/env bash
# Park 982 (armed-d13-8cfb7ca8) and append its block to records/revert-set.txt.
#
# 982 = 981's switch set, REBUILT on the kernel sources with the GIC interrupt fix. Two kernel fixes
# landed after 981's embed and are NOT in 981's image:
#
#   EDIT A (pe_msm8974.c, msm8974_gic_map + msm8974_handle_interrupt). 981's press took a **section
#   translation fault in interrupt context** -- `sleh_abort: Abort in interrupt handler`,
#   `r0=0xf900200c pc=0x801ae3ac fsr=0x00000005 far=0xf900200c`. The faulting `ldr r1,[r0]` read the
#   RAW PHYSICAL GICC_IAR (0xf900200c); XNU's pmap does not translate 0xf9002000 (arm_vm_init copies
#   the boot L1 then overwrites only [physBase,physBase+gMemSize) and the managed window, so the entry
#   image's 0xf9000000 device section does not survive set_mmu_ttb). The fix ml_io_maps GICC from
#   PE_init_SocSupport_msm8974 and reads IAR/EOIR through the returned VA, with a NULL-guard that
#   returns rather than repeating the abort. Every peer (pe_apq8060.c:128-129) and the reference
#   (HTC-Leo-Revival-Project/xnu pe_msm8974.c) map-then-read; ours read physical.
#
#   EDIT B (pe_msm8974.c, same handler, the timer line). The handler is the live post-XNU dispatcher
#   (EDIT A's fault proves it), so it must also SERVICE the timer PPI -- INTID 20 (src/entry/entry_gic.h)
#   -- by calling rtclock_intr(context) after EOI, exactly as every peer (pe_apq8060.c:246) and the
#   reference do. Before 982 rtclock_intr had NO caller in the image: a handler that only acks+EOIs
#   never re-arms the countdown, so the kernel tick would freeze at the first timer interrupt.
#
# VERIFIED BY VALUE in the linked ELF: msm8974_handle_interrupt is
#   movw/movt r1,#0x8084bc24; ldr r1,[r1]; cmp; beq +0x48; ldr r2,[r1,#12]; bfc; cmp #0x3ff; bxeq lr;
#   cmp r2,#20; str r2,[r1,#16]; dsb; b 8001b3a0 <rtclock_intr>
# -- loads the base from the g_msm8974_gicc global (NOT `movt r0,#0xf900`), NULL-guards, and branches
# to rtclock_intr on INTID 20; PE_init_SocSupport_msm8974 has `bl 80018324 <ml_io_map>`.
#
# THE SWITCH SET IS 981's, unchanged (window 0x1e400000 = 484 MiB, RESIDENT=1, USB_PROBE/DEV/ENUM/
# STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1, HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD/CARD_COW=1,
# HDD_WRITE=0, SMEM_PROBE=1, IDLE_NO_SLEEP=0). The ENTRY BIN MOVED (7fbd7009 -> 8cfb7ca8) and so did
# the payload (the qcdt now embeds the fixed entry bin). Medium: 968 real iOS 7.1.2 HFSX rootfs over
# hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never written - fully reversible. fastboot boot only,
# NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

ARM=armed-d13-8cfb7ca8
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
  printf '# %s  —  982: 981 rebuilt on the kernel sources with the GIC interrupt fix. Two kernel\n' "$ARM"
  printf '# fixes 981 predates: the GIC CPU interface is read through an ml_io_map VA, not the raw\n'
  printf '# physical (EDIT A); the same handler services the timer PPI via rtclock_intr (EDIT B).\n#\n'
  printf '# EDIT A (pe_msm8974.c msm8974_gic_map + msm8974_handle_interrupt). 981 took a section\n'
  printf '# translation fault in interrupt context: sleh_abort, r0=0xf900200c pc=0x801ae3ac fsr=0x5\n'
  printf '# far=0xf900200c. The faulting ldr read the RAW PHYSICAL GICC_IAR; XNU pmap does not translate\n'
  printf '# 0xf9002000 (arm_vm_init copies the boot L1 then overwrites only [physBase,physBase+gMemSize)\n'
  printf '# and the managed window, so the entry 0xf9000000 device section does not survive set_mmu_ttb).\n'
  printf '# The fix ml_io_maps GICC (PE_init_SocSupport_msm8974) and reads IAR/EOIR through the VA, with a\n'
  printf '# NULL-guard returning rather than repeating the abort. Peers (pe_apq8060.c:128-129) and the\n'
  printf '# reference (HTC-Leo-Revival-Project/xnu pe_msm8974.c) map-then-read; ours read physical.\n#\n'
  printf '# EDIT B (pe_msm8974.c same handler, timer line). The handler is the live post-XNU dispatcher\n'
  printf '# (EDIT A s fault proves it), so it must also service the timer PPI INTID 20 (entry_gic.h) by\n'
  printf '# calling rtclock_intr(context) after EOI, as every peer (pe_apq8060.c:246) and the reference\n'
  printf '# do. Before 982 rtclock_intr had NO caller: ack+EOI alone never re-arms the countdown.\n#\n'
  printf '# VERIFIED BY VALUE in the linked ELF: msm8974_handle_interrupt loads the g_msm8974_gicc global\n'
  printf '# (movw/movt r1,#0x8084bc24; ldr r1,[r1]; cmp; beq; ldr r2,[r1,#12]; ...cmp r2,#20;\n'
  printf '# b <rtclock_intr>), NOT movt r0,#0xf900; PE_init_SocSupport_msm8974 has bl <ml_io_map>.\n#\n'
  printf '# Switch set is 981 s UNCHANGED; the ENTRY BIN moved (7fbd7009 -> 8cfb7ca8). VERIFIED: preflight\n'
  printf '# ok, make check 0. Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1,\n'
  printf '# HDD_WRITE=0, base never written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=982-981-rebuilt-with-gic-interrupt-fix-map-and-timer;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3