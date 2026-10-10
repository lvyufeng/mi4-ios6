#!/usr/bin/env bash
# Park 975 (armed-window-1091566c) and append its block to records/revert-set.txt.
set -euo pipefail
cd "$(dirname "$0")/.."

ARM=armed-window-1091566c
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
  printf '# %s  —  975: the 484 MiB ENTRY WINDOW on the WORKING entry line (971/973/974), so\n' "$ARM"
  printf '# iOS userspace can map the 301 MiB dyld cache with NO 915-B pmap port.\n#\n'
  printf '# WHY. 969 measured that the real iOS 7.1.2 /sbin/launchd links libSystem/libbsm out of\n'
  printf '# the 301 MiB dyld shared cache, while XNU managed only the 16 MiB window -> iOS could\n'
  printf '# not run without the whole-kernel 915-B port. 970 designed this one-switch rung and\n'
  printf '# pressed it, but ONLY on entry bin 7107b998, which NEVER BOOTED (970 6c: the 484 MiB arm\n'
  printf '# and its 16 MiB control failed IDENTICALLY). So the window was never tested on a WORKING\n'
  printf '# entry. 975 is the missing measurement: the SAME switch on the entry line that boots.\n#\n'
  printf '# THE ARM IS 974 (armed-d13-73475747) with ONE key changed:\n'
  printf '#   STAGE90_XNU_ENTRY_WINDOW  0x01000000 (16 MiB) -> 0x1e400000 (484 MiB)\n'
  printf '# The window reaches XNU ONLY through the payload generated header (build_entry.sh\n'
  printf '# substitutes @ENTRY_SIZE@ into xnu_arm_entry.h, read at xnu_entry_jump.c:150 as\n'
  printf '# `a->memSize = STAGE90_XNU_ENTRY_SIZE`), so the ENTRY BIN IS UNCHANGED (73475747) and the\n'
  printf '# name comes from the qcdt (the armed-window-* family, 970 4).\n#\n'
  printf '# CEILING: build_entry.sh refuses any D13 window >= 0x1e500000 = RAM_CONSOLE_BASE(0xde500000)\n'
  printf '# - MANAGED_BASE(0xC0000000). 0x1e400000 (484 MiB) is the safe max, one 16 MiB step below;\n'
  printf '# the console and the MMIO above it (GIC/USB/WDT/SMCC/GCC/TLMM, SMEM 0xe0000000) stay intact.\n'
  printf '# Free after topOfKernelData 0x80A00000: 0x9E400000-0x80A00000 ~= 470 MiB (>= the 301 MiB cache).\n#\n'
  printf '# VERIFIED BY VALUE in out/stage90/stage90.elf: e3a08579 (`mov r8,#0x1e400000`) occurs once.\n'
  printf '# stage90-build-config.txt is BYTE-IDENTICAL to 968/970h/971/972/973/974 6c2b6038.\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is\n'
  printf '# never written - fully reversible. Resident rung (POST_END_TICKS=0): budget a black screen +\n'
  printf '# power-cycle capture. fastboot boot only, NEVER flash. PRESS IS THE OPERATOR S.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=975-the-484-MiB-entry-window-on-the-working-D13-entry-line;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3