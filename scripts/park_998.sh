#!/usr/bin/env bash
# Park 998 and append its block to records/revert-set.txt.
#
# 998 = 997's switch set VERBATIM (STAGE90_XNU_DYLD_WITNESS / STAGE90_XNU_SHREG_PROBE /
# STAGE90_XNU_SHREG_WIRE all stay 1) + STAGE90_XNU_SHREG_GEOM=1: G1, the iOS 7 shared-region GEOMETRY
# + the process stack top, under ONE switch. THREE tree SOURCE files move in THIS rung (all POOL files,
# no assembly): `osfmk/vm/vm_shared_region.c` (f91c3460... -> 482b4288..., the geometry block at
# :626-635 compiled in), `osfmk/arm/status.c` (d8f41a7b... -> da944439..., the stack half: LAB_USER_STACK
# -> ios7lab_user_stack() -> 0x2BE00000) and `osfmk/arm/ios7_compat.h` (079c7a17... -> 58cc2823...,
# `ios7_shreg_geom_enabled()` beside `ios7_traps_enabled()`). The ENTRY BIN moves (518b6a94 -> ?).
# A fresh checkout needs ALL NINE stage scripts: tools/stage_d13_kextb_login.sh (988's),
# tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's),
# tools/stage_d13_commpage_alias.sh (992+993's), tools/stage_d13_ios7_traps.sh (994's),
# tools/stage_d13_dyld_witness.sh (995's - 996 re-staged it to rev-3), tools/stage_d13_shreg_probe.sh
# (996's), tools/stage_d13_shreg_wire.sh (997's) and tools/stage_d13_shreg_geom.sh (998's - the rung's
# driver; it calls the vmabi-recon lane's tools/patch_d13_shreg_geom.py and edits nothing itself).
#
# WHY (997's press, 2026-10-12, arm armed-d13-518b6a94, commit 9e6307a, log
# out/stage90/captures/cancro-last_kmsg-997-armed-d13-518b6a94.txt:4096-4110): the 32 B wire's four
# mappings arrive (`IOS7LAB cache mapping` x4) with ALREADY SLID addresses - m0 va=0x2cd9c000 =
# 0x2C000000 + slide 0xd9c000; m1 carries VM_PROT_SLIDE (init=0x23) - then `IOS7SHREG map_file kr=1`
# (KERN_INVALID_ADDRESS) + `map_slide ret=14`: the tree's default 0x30000000/0x10000000 window cannot
# hold dyld's slid shared cache. 998 opens the geometry the tree already carries, in ONE switch with its
# stack half (without the stack half every process's FIXED stack allocation at trunc(0x2FE00000 -
# MAXSSIZ) would land INSIDE the new window - audit section 2). See docs/reference/d13-shreg-frontier.md
# and docs/reference/d13-shreg-g1-audit.md.
#
# THE FIX (`tools/stage_d13_shreg_geom.sh`): the tree's own geometry + stack block compiles in, with its
# `IOS7LAB shared region 0x2c000000..0x40000000; stack below 0x2be00000` printf; the 996 probe's
# IOS7SHREG lines and the 997 wire's IOS7LAB cache mapping lines stay. VERIFIED BY VALUE:
# tools/check_d13_shreg_geom.py --tree <root> --expect staged asserts the three staged source files
# (constants intact in BOTH states, no v1 inline-#if wrap); the 998 link clause in build_entry.sh
# additionally reads the linked image's own `IOS7LAB shared region` literal and the 0x2BE00000 stack top
# out of `thread_userstack`/`thread_userstackdefault` (the 0x2FE00000 default gone).
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
# pressed 997 or the confirmed 996/995/994/993 or the regressed 992 arms under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-518b6a94 ]] && { echo "REFUSING: $LIVE holds 997's arm (518b6a94); build 998 first"; exit 1; }
[[ $ARM == armed-d13-cea0c1c2 ]] && { echo "REFUSING: $LIVE holds 996's arm (cea0c1c2); build 998 first"; exit 1; }
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 998 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 998 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 998 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 998 first"; exit 1; }
PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  998: THE iOS 7 SHARED-REGION GEOMETRY + STACK TOP (G1) ARE OPENED - THE TREE-S OWN 0x2C000000/0x14000000 WINDOW AND THE 0x2BE00000 STACK COMPILE IN.\n' "$ARM"
  printf '# Switch set is 997-s VERBATIM (STAGE90_XNU_DYLD_WITNESS, STAGE90_XNU_SHREG_PROBE and\n'
  printf '# STAGE90_XNU_SHREG_WIRE all stay 1) + STAGE90_XNU_SHREG_GEOM=1; the ENTRY BIN moved (518b6a94 -> %s)\n' "${ARM#armed-d13-}"
  printf '# and THREE tree SOURCE files move in THIS rung (all POOL files, NO assembly change):\n'
  printf '# osfmk/vm/vm_shared_region.c (f91c3460... -> 482b4288..., the geometry block at :626-635: both\n'
  printf '# `#if IOS7_COMPAT_PLATFORM`-gated lines gain the 994 spelling + the helper, so base_address\n'
  printf '# 0x2C000000U / size 0x14000000U and the `IOS7LAB shared region ...` printf compile in),\n'
  printf '# osfmk/arm/status.c (d8f41a7b... -> da944439..., the STACK HALF: LAB_USER_STACK ->\n'
  printf '# ios7lab_user_stack() -> 0x2BE00000 instead of USRSTACK 0x2FE00000 - without it the new window\n'
  printf '# covers the stack and EVERY process-s FIXED stack allocation relays out) and\n'
  printf '# osfmk/arm/ios7_compat.h (079c7a17... -> 58cc2823..., ios7_shreg_geom_enabled() beside\n'
  printf '# ios7_traps_enabled()). The substitute is 1:1 in every .c (NO inline #if wrap: this tree-s\n'
  printf '# panic embeds __FILE__:__LINE__, so a line-shifting wrap moves panic strings - measured in\n'
  printf '# the G1 audit rehearsal). The MACRO itself is NOT touched - the 994 "do not widen the macro"\n'
  printf '# lesson; 998 does NOT touch the assembly layer, NO trap.c change, NO pmap.c/commpage.c/\n'
  printf '# arm_vm_init.c change, NO pcb.c, NO vm_resident.c, NO vm_unix.c change). A fresh checkout needs\n'
  printf '# ALL NINE tools/stage_d13_*.sh drivers (988/990/991/992+993/994/995+996/996/997/998).\n#\n'
  printf '# 997 (pressed 2026-10-12, arm armed-d13-518b6a94, commit 9e6307a, evidence\n'
  printf '# out/stage90/captures/cancro-last_kmsg-997-armed-d13-518b6a94.txt:4096-4110) MEASURED the G1\n'
  printf '# fix-s target by value: 4x `IOS7LAB cache mapping` with ALREADY SLID addresses (m0 va=0x2cd9c000\n'
  printf '# = 0x2C000000 + slide 0xd9c000; m1 carries VM_PROT_SLIDE, init=0x23), then map_file kr=1\n'
  printf '# (KERN_INVALID_ADDRESS) + map_slide ret=14 - the default 0x30000000/0x10000000 window cannot\n'
  printf '# hold dyld-s slid shared cache. Regression guards green: VMHINT991 2x, IOS7LAB vm_allocate64\n'
  printf '# flags=1 result=0, check_np ret=12.\n#\n'
  printf '# THE 998 PRESS- PRE-REGISTRATION (frozen once embedded; deviations reported, not reinterpreted)\n'
  printf '# is written by the TEAM-LEAD into scripts/press_998.sh (the build side is the plumbing this rung\n'
  printf '# parks; the pre-registration block follows the 997 pattern: switch set, the point, the failure\n'
  printf '# signature(s), falsification, regression guards). Expected reading per the lead: map_file kr=0\n'
  printf '# and the slide stage proceeding (then dyld halts move past the shared cache).\n#\n'
  printf '# tools/check_d13_shreg_geom.py --tree <root> --expect staged asserts the three staged source files\n'
  printf '# (constants intact in BOTH states, no v1 inline-#if wrap); build_entry.sh-s 998 link clause ALSO\n'
  printf '# reads the linked image-s own `IOS7LAB shared region` literal and the 0x2BE00000 stack top out of\n'
  printf '# thread_userstack/thread_userstackdefault (the 0x2FE00000 default gone; the FROZEN 997 elf is the\n'
  printf '# negative control - literal count 0 there).\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=998-shreg-geom;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3