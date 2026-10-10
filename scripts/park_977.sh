#!/usr/bin/env bash
# Park 977 (armed-d13-d693864f) and append its block to records/revert-set.txt.
#
# 977 = 975 (armed-window-1091566c) with the D13 residence rung's watchdog pet LINKED. The pet was
# inside the 4570-only `#if !STAGE90_ENTRY_D13` idle block, so a RESIDENT=1 D13 image armed the SoC
# watchdog (the payload does so ~28 s before the jump, stage90_main.c:1247) and could not feed it ->
# the run reset mid-boot, before the 959-962 USB stream started -> 975's empty USB capture and
# did-not-return. Fix: the pet compiles under `#if STAGE90_XNU_RESIDENT` alone and is called from
# `__wrap_machine_idle` (D13's one wrapper every idle pass reaches). See experiment-977.
#
# THE ENTRY BIN MOVED (73475747 -> d693864f), so unlike 975 this arm is a `armed-d13-*` set named from
# `xnu_arm_entry.bin`, NOT a payload-only `armed-window-*` set named from the qcdt. The qcdt moves too
# (1091566c -> 47fb6590) because the payload embeds the entry blob and recompiles xnu_entry_jump.o
# against the regenerated xnu_arm_entry.h, but the ENTRY IMAGE is the arm's defining artifact.
set -euo pipefail
cd "$(dirname "$0")/.."

ARM=armed-d13-d693864f
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
  printf '# %s  —  977: the 975 window arm with the D13 residence rung WATCHDOG PET linked, so a\n' "$ARM"
  printf '# resident D13 press is no longer dark (the SoC watchdog is fed from __wrap_machine_idle).\n#\n'
  printf '# WHY. 975 (armed-window-1091566c) was pressed 2026-10-10 on the WORKING entry bin 73475747:\n'
  printf '# RUN_RC=2 (ran, did NOT return) and the USB console capture was EMPTY (idProduct=0910 seen 0\n'
  printf '# times). ROOT CAUSE (experiment-977): the payload arms the MSM8974 WDT ~28 s before the jump\n'
  printf '# and never disarms it; the D13 entry line could not feed it because the whole entry_wdt_pet\n'
  printf '# machinery sat inside the 4570-only `#if !STAGE90_ENTRY_D13` idle block, so a RESIDENT=1 D13\n'
  printf '# image linked NO pet at all. The boot therefore reset mid-boot, before the 959-962 USB ladder\n'
  printf '# reached __wrap_machine_idle - hence the empty capture AND the non-return.\n#\n'
  printf '# THE FIX (src/entry/entry_trace.c). The pet compiles under `#if STAGE90_XNU_RESIDENT` alone\n'
  printf '# (declaration guard drops `&& !STAGE90_ENTRY_D13`); the pet block moves out of the 4570 gate\n'
  printf '# to sit before __wrap_machine_idle; a D13 call site is added inside __wrap_machine_idle. The\n'
  printf '# two 4570 call sites stay; the pet body is unchanged. tools/test_resident_guard.py (run by\n'
  printf '# build_entry.sh for any RESIDENT=1 image) now asserts the pet IS in a D13 image and called\n'
  printf '# once from __wrap_machine_idle, with its own two --selftest mutations.\n#\n'
  printf '# THE ARM = 975 (armed-window-1091566c) with the pet linked. Every switch is 975 s (window\n'
  printf '# 0x1e400000 = 484 MiB, RESIDENT=1, USB_PROBE/DEV/ENUM/STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1,\n'
  printf '# SMEM_PROBE=1, IDLE_NO_SLEEP=0). The ENTRY BIN MOVED (73475747 -> d693864f) because the pet\n'
  printf '# is now in the link, so this is an `armed-d13-*` set named from xnu_arm_entry.bin, NOT a\n'
  printf '# payload-only `armed-window-*` set. The qcdt moves too (1091566c -> 47fb6590).\n#\n'
  printf '# VERIFIED BY VALUE: arm-none-eabi-nm shows `entry_wdt_pet` defined and one `bl` to it inside\n'
  printf '# __wrap_machine_idle; the payload embeds entry blob d693864f; stage90.elf carries exactly one\n'
  printf '# e3a08579 (mov r8,#0x1e400000). make check 0. Medium: 968 real iOS 7.1.2 HFSX rootfs over\n'
  printf '# hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never written - fully reversible. fastboot\n'
  printf '# boot only, NEVER flash. PRESS IS THE OPERATOR S.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=977-the-975-window-arm-with-the-D13-residence-watchdog-pet-linked;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3