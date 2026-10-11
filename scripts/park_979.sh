#!/usr/bin/env bash
# Park 979 (armed-d13-97d6a511) and append its block to records/revert-set.txt.
#
# 979 = 977 (armed-d13-d693864f) with the entry.ld PAGE-ALIGNMENT fix. The press of 977's arm was
# recovered (experiment-979) and it is a REAL XNU boot that PANICKED in `ipc_bootstrap`: pc =
# `lock_set_init`+0, lr = `ipc_bootstrap`+0x194, fsr=0x0f (permission fault, prefetch). Root cause: the
# entry image's `.text` ended at `__entry_text_end` = 0x805699a0 (NOT page-aligned: `. = ALIGN(32)`),
# while `pmap_init` (`osfmk/arm/pmap.c:3620`) starts its data-NX loop at `sectDATAB =
# __entry_data_start = 0x805699a0`. Its FIRST iteration, `pmap_pte(0x805699a0)`, resolved to the last
# TEXT page `[0x80569000, 0x8056a000)` -- which holds `lock_set_init` -- and set `L2_NX_BIT` on it, so
# the kernel then faulted fetching that function's first instruction.
#
# THE FIX (src/entry/entry.ld). Pad the `.text` output section to a page (`STAGE90_ENTRY_ALIGN`
# 0x1000, was ALIGN(32)) so `__entry_text_end == __entry_data_start == 0x8056a000` -- page-aligned and
# equal to where the linker already put the real `.data` output section (4 KB-aligned: `objdump -h`).
# Then `__DATA.vmaddr` names the first WRITABLE page and the NX loop marks only data.
#
# THE ARM = 977 (armed-d13-d693864f) with the one entry.ld change. Every switch is 977's (window
# 0x1e400000, RESIDENT=1, USB_PROBE/DEV/ENUM/STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1, SMEM_PROBE=1,
# IDLE_NO_SLEEP=0). The ENTRY BIN MOVED (d693864f -> 97d6a511). stage90-build-config.txt is unchanged.
#
# VERIFIED BY VALUE: `nm` shows `__entry_text_end == __entry_data_start == 0x8056a000` and
# `__TEXT` vmsize 0x56a000 (was 0x5699a0); `host_entry_macho_check.sh` OK (every segment arm_vm_init
# asks for is present with the right numbers); `make check` 0. Medium: 968 real iOS 7.1.2 HFSX rootfs
# over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never written - fully reversible. fastboot
# boot only, NEVER flash. PRESS IS THE OPERATOR S.
set -euo pipefail
cd "$(dirname "$0")/.."

ARM=armed-d13-97d6a511
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
  printf '# %s  —  979: 977 with the entry.ld PAGE-ALIGNMENT fix that stops pmap_init NX-ing the last\n' "$ARM"
  printf '# text page (the `ipc_bootstrap` panic experiment-979 recovered).\n#\n'
  printf '# WHY. The 977 press (armed-d13-d693864f) was recovered by recover_last_kmsg.sh and is a real\n'
  printf '# XNU boot: it passed arm_vm_init, pmap_bootstrap, vm_page_bootstrap, printed Darwin Kernel\n'
  printf '# Version, then PANICKED `Kernel prefetch abort` at pc=lock_set_init+0 (lr=ipc_bootstrap+0x194,\n'
  printf '# fsr=0x0f). ROOT CAUSE: the entry `.text` ended at __entry_text_end = 0x805699a0 (ALIGN(32),\n'
  printf '# NOT page-aligned) while the linker put the real `.data` output section on the next page\n'
  printf '# 0x8056a000. pmap_init (`osfmk/arm/pmap.c:3620`) runs a data-NX loop from sdata = __entry_data_start\n'
  printf '# = 0x805699a0; its first iteration pmap_pte(0x805699a0) resolved to the LAST TEXT PAGE\n'
  printf '# [0x80569000,0x8056a000) -- holding lock_set_init -- and set L2_NX_BIT on it, making it\n'
  printf '# non-executable. pmap_asid_configure (also in pmap_init) still printed its line, so the loop\n'
  printf '# is on the path. A real kernel __DATA.vmaddr is page-aligned, so this never happens there.\n#\n'
  printf '# THE FIX (src/entry/entry.ld). Pad `.text` to a page (STAGE90_ENTRY_ALIGN 0x1000, was ALIGN(32))\n'
  printf '# so __entry_text_end == __entry_data_start == 0x8056a000 = the real .data page. pad is INSIDE\n'
  printf '# the .text section so __TEXT covers it and no uncovered gap is left between the segments.\n#\n'
  printf '# THE ARM = 977 with the one entry.ld change. Every switch is 977 s. The ENTRY BIN MOVED\n'
  printf '# (d693864f -> 97d6a511).\n#\n'
  printf '# VERIFIED BY VALUE: nm __entry_text_end == __entry_data_start == 0x8056a000, __TEXT vmsize\n'
  printf '# 0x56a000 (was 0x5699a0); host_entry_macho_check OK; make check 0. Medium: 968 real iOS 7.1.2\n'
  printf '# HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never written - fully\n'
  printf '# reversible. fastboot boot only, NEVER flash. PRESS IS THE OPERATOR S.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=979-the-977-arm-with-the-entry-ld-page-alignment-fix;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3