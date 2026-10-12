#!/usr/bin/env bash
# Park 996 and append its block to records/revert-set.txt.
#
# 996 = 995's switch set VERBATIM (STAGE90_XNU_DYLD_WITNESS stays 1) + STAGE90_XNU_SHREG_PROBE=1:
# the dyld witness REV-3 (chunker save/restore + six `IOS7WIT_AI_*` reads of the SAME
# `dyld_all_image_infos` struct) and the 294/438 shared-region syscall probe, BOTH in the POOL
# (`osfmk/arm/trap.c` + `bsd/vm/vm_unix.c`). The ENTRY BIN moves (4efcabae -> ?). TWO tree SOURCE
# files move in THIS rung; 996 does NOT touch the assembly layer (`osfmk/arm/traps_lo.s` is 994's,
# unchanged). A fresh checkout needs ALL SEVEN stage scripts: tools/stage_d13_kextb_login.sh (988's),
# tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's - 996 re-stages it to rev-3) and
# tools/stage_d13_shreg_probe.sh (996's - the rung's driver; it calls the vmabi-recon lane's
# tools/patch_d13_shreg_probe.py and edits nothing itself).
#
# WHY (995's press, 2026-10-12, arm armed-d13-4efcabae, commit 947a75d, evidence
# out/stage90/captures/cancro-last_kmsg-995-armed-d13-4efcabae.txt:4111-4138): the witness FIRED
# exactly twice (`MAX_PRINTS=2` works) and READ dyld's real halt message by measurement - len=103,
# BOTH live pointers (A+0x20 and r5) byte-identical, text beginning
# `Library not loaded: /usr/lib/libbsm.0.dylib` (only the first 48 bytes printed - a chunker bug in
# `ios7wit_lines` zeroed the byte after each chunk; fixed in rev-3). `/usr/lib/libbsm.0.dylib` is
# launchd's FIRST `LC_LOAD_DYLIB`; it has no file on the volume but IS in the shared cache (696
# images), so the minimal explanation is that dyld's shared cache was NEVER MAPPED: `mapSharedCache`
# failed silently (no fd 2 on CANCRO) and every cached dylib degraded to a file lookup, whose first
# miss halted dyld. dyld's two entry points are BSD syscalls 294/438, BOTH wired in this tree
# (`init_sysent.c:440`/`:649`) - so 996 measures their runtime returns while reading the full
# message and dyld's own `sharedCacheBaseAddress` word. THREE INDEPENDENT READINGS, pre-registered in
# scripts/press_996.sh and docs/reference/d13-shreg-frontier.md sec 7.
#
# THE FIX (`tools/stage_d13_shreg_probe.sh` + the rev-3 re-stage through
# `tools/stage_d13_dyld_witness.sh`): bounded (first 4 calls), fail-soft printfs only; no behaviour
# change. VERIFIED BY VALUE: tools/check_d13_dyld_witness.py --expect on (rev-3) and
# tools/check_d13_shreg_probe.py --expect on --tree <bsd/vm/vm_unix.c> assert, off the linked image's
# own disassembly and bytes, the witness rev-3 shape and the five `IOS7SHREG` literals (plus the
# source-side 7 guard regions); the FROZEN 994 image is passed as `--control` and must FAIL its ON
# mode and PASS its OFF mode for BOTH checkers (they tell the two shapes apart).
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible. fastboot boot only, NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

LIVE=out/stage90
REC=records/revert-set.txt
MEMBERS=(xnu_arm_entry.bin xnu_arm_entry.elf xnu_arm_entry.h xnu_arm_entry-config.txt \
         xnu_arm_entry-sources.txt stage90.bin stage90.elf stage90.img stage90-qcdt.img \
         stage90_fixture.macho stage90-build-config.txt SHA256SUMS.txt)

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse to re-park the
# confirmed 995/994/993 or the regressed 992 arms under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 996 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 996 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 996 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 996 first"; exit 1; }
PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  996: THE 294/438 SHARED-REGION PROBE + THE DYLD WITNESS REV-3 ARE WIRED IN.\n' "$ARM"
  printf '# Switch set is 995-s VERBATIM (STAGE90_XNU_DYLD_WITNESS stays 1) + STAGE90_XNU_SHREG_PROBE=1; the\n'
  printf '# ENTRY BIN moved (4efcabae -> %s) and TWO tree SOURCE files move in THIS rung:\n' "${ARM#armed-d13-}"
  printf '# osfmk/arm/trap.c (the witness REV-3: the ios7wit_lines chunker save/restore fix + six\n'
  printf '# IOS7WIT_AI_* reads of the SAME dyld_all_image_infos struct - A+0x40 errKind, +0x44/0x48/0x4c\n'
  printf '# char* fields, +0x50 slide, +0x64 sharedCacheBaseAddress) and bsd/vm/vm_unix.c (bounded,\n'
  printf '# fail-soft IOS7SHREG printfs at the entry and every return of shared_region_check_np (294) and\n'
  printf '# shared_region_map_and_slide_np (438), plus the kr immediately after vm_shared_region_map_file\n'
  printf '# inside _shared_region_map). BOTH are POOL files; 996 does NOT touch the assembly layer\n'
  printf '# (osfmk/arm/traps_lo.s is 994-s, unchanged; pmap.c/commpage.c/arm_vm_init.c are 993-s; this rung\n'
  printf '# has NO pcb.c, NO vm_resident.c, NO mach_kernelrpc.c change). A fresh checkout needs ALL SEVEN\n'
  printf '# tools/stage_d13_kextb_login.sh, tools/stage_d13_sp_align.sh, tools/stage_d13_vmhint_guard.sh,\n'
  printf '# tools/stage_d13_commpage_alias.sh, tools/stage_d13_ios7_traps.sh, tools/stage_d13_dyld_witness.sh\n'
  printf '# and tools/stage_d13_shreg_probe.sh drivers.\n#\n'
  printf '# 995 (pressed 2026-10-12, arm armed-d13-4efcabae, commit 947a75d, evidence\n'
  printf '# out/stage90/captures/cancro-last_kmsg-995-armed-d13-4efcabae.txt:4111-4138) FIRED the witness\n'
  printf '# exactly twice (MAX_PRINTS=2 works) and READ dyld-s real halt message by measurement: len=103,\n'
  printf '# BOTH live pointers (A+0x20 and r5) byte-identical, text beginning\n'
  printf '# `Library not loaded: /usr/lib/libbsm.0.dylib` (only the first 48 bytes printed - a chunker bug\n'
  printf '# in ios7wit_lines zeroed the byte after each chunk; fixed in rev-3). /usr/lib/libbsm.0.dylib is\n'
  printf '# launchd-s FIRST LC_LOAD_DYLIB; it has no file on the volume but IS in the shared cache (696\n'
  printf '# images), so the minimal explanation is that dyld-s shared cache was NEVER MAPPED: mapSharedCache\n'
  printf '# failed silently (no fd 2 on CANCRO) and every cached dylib degraded to a file lookup, whose\n'
  printf '# first miss halted dyld. dyld-s two entry points are BSD syscalls 294/438, BOTH wired in this\n'
  printf '# tree (init_sysent.c:440/:649), so 996 measures their runtime returns.\n#\n'
  printf '# THREE INDEPENDENT READINGS (pre-registered in scripts/press_996.sh + docs/reference/\n'
  printf '# d13-shreg-frontier.md sec 7): (1) the FULL message text from the witness rev-3; (2) dyld-s own\n'
  printf '# sharedCacheBaseAddress word (A+0x64; 0 = never mapped); (3) the IOS7SHREG syscall returns\n'
  printf '# (check_np expected 12 first; the pre-registered prediction is map_slide ret=22 with map_file\n'
  printf '# kr=4 = the 32 B-wire misparse, sec 2; ret=12 + kr=3 would finger sec 6 geometry instead).\n'
  printf '# tools/check_d13_dyld_witness.py --expect on (rev-3) and tools/check_d13_shreg_probe.py --expect\n'
  printf '# on --tree <the staged bsd/vm/vm_unix.c file> assert, off the linked image-s own disassembly and\n'
  printf '# bytes, the witness rev-3 shape and the five IOS7SHREG literals (plus the source-side 7 guard\n'
  printf '# regions); the FROZEN 994 image (out/stage90/captures/994-xnu_arm_entry.elf) is passed as\n'
  printf '# --control and must FAIL ON and PASS OFF for BOTH checkers.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=996-shreg-probe;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3