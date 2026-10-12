#!/usr/bin/env bash
# Park 997 and append its block to records/revert-set.txt.
#
# 997 = 996's switch set VERBATIM (STAGE90_XNU_DYLD_WITNESS and STAGE90_XNU_SHREG_PROBE stay 1) +
# STAGE90_XNU_SHREG_WIRE=1: G3, the 32-byte shared-region wire (`bsd/vm/vm_unix.c` - the SAME pool file
# the 996 probe rides). The ENTRY BIN moves (cea0c1c2 -> ?). ONE tree SOURCE file moves in THIS rung
# (`bsd/vm/vm_unix.c`, from the 996 probe-staged 516df232... to 4b3ec2b8...); 997 does NOT touch the
# assembly layer (`osfmk/arm/traps_lo.s` is 994's, unchanged). A fresh checkout needs ALL EIGHT stage
# scripts: tools/stage_d13_kextb_login.sh (988's), tools/stage_d13_sp_align.sh (990's),
# tools/stage_d13_vmhint_guard.sh (991's), tools/stage_d13_commpage_alias.sh (992+993's),
# tools/stage_d13_ios7_traps.sh (994's), tools/stage_d13_dyld_witness.sh (995's - 996 re-staged it to
# rev-3), tools/stage_d13_shreg_probe.sh (996's) and tools/stage_d13_shreg_wire.sh (997's - the rung's
# driver; it calls the vmabi-recon lane's tools/patch_d13_shreg_wire.py and edits nothing itself).
#
# WHY (996's press, 2026-10-12, arm armed-d13-cea0c1c2, commit 821fb40, log
# out/stage90/captures/cancro-last_kmsg-996-armed-d13-cea0c1c2.txt:4101-4105): the probe MEASURED the
# §2 by-value prediction - `IOS7SHREG check_np ret=12`, `map_slide count=4 slide=0x3811000
# slide_start=0x3f9bd000 size=0x89000`, then `IOS7SHREG map_file kr=4` (KERN_INVALID_ARGUMENT) +
# `map_slide ret=22` (EINVAL): `shared_region_copyin_mappings` computed `count * 5 << 2` against dyld
# 239's 32-byte-stride array (0x2be057ba `add.w r0, r4, r3, lsl #5`), m[0].size read 0 and
# `vm_map_enter_mem_object`'s `initial_size == 0` check (vm_map.c:2344) returned EINVAL before any
# slide work. Same press: the witness read the FULL 102-char halt message and `cacheBase=0` - the
# shared cache was never mapped BECAUSE 438 died, so 997 opens the tree's OWN 32 B branch (already in
# vm_unix.c:1190-1220, two gates deep: `#if IOS7_COMPAT_PLATFORM` = 0 on CANCRO, the runtime selector
# returns 0). The fix is exactly three gate lines (the 994 spelling) + a gated `ios7lab_cache_abi()`
# body; the MACRO itself is NOT touched (the 994 "do not widen the macro" lesson: 13 files reference
# it, and widening would wake exec/fork/ipc wholesale). See docs/reference/d13-shreg-frontier.md.
#
# THE FIX (`tools/stage_d13_shreg_wire.sh`): the tree's own 32 B branch compiles in, with its
# per-mapping `IOS7LAB cache mapping %u va=... size=... offset=... max=... init=...` printf; the 996
# probe's IOS7SHREG lines stay. VERIFIED BY VALUE:
# tools/check_d13_shreg_wire.py out/stage90/xnu_arm_entry.elf --expect on asserts, off the linked
# image's own disassembly and bytes, the 32 B wire shape (literal in
# `shared_region_copyin_mappings` reaching a printf, `count * 32`, the 20 B pair GONE, 294's copyout
# at 8 bytes) plus its --tree source-side check (3 gated `#if` lines + 1 body gate); the FROZEN 996
# image (and the 994 capture) is passed as `--control` and must FAIL its ON mode and PASS its OFF mode
# (the checker tells the two shapes apart).
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
# pressed 996 or the confirmed 995/994/993 or the regressed 992 arms under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-cea0c1c2 ]] && { echo "REFUSING: $LIVE holds 996's arm (cea0c1c2); build 997 first"; exit 1; }
[[ $ARM == armed-d13-4efcabae ]] && { echo "REFUSING: $LIVE holds 995's arm (4efcabae); build 997 first"; exit 1; }
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 997 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 997 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 997 first"; exit 1; }
PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  997: THE 32 B SHARED-REGION WIRE (G3) IS OPENED - THE TREE-S OWN IOS 7 CACHE ABI COMPILES IN.\n' "$ARM"
  printf '# Switch set is 996-s VERBATIM (STAGE90_XNU_DYLD_WITNESS stays 1, STAGE90_XNU_SHREG_PROBE stays 1)\n'
  printf '# + STAGE90_XNU_SHREG_WIRE=1; the ENTRY BIN moved (cea0c1c2 -> %s) and ONE tree SOURCE file moves in\n' "${ARM#armed-d13-}"
  printf '# THIS rung: bsd/vm/vm_unix.c (516df232... -> 4b3ec2b8..., the SAME pool file the 996 probe rides -\n'
  printf '# three `#if IOS7_COMPAT_PLATFORM` lines gain the 994 spelling and the file-local\n'
  printf '# `ios7lab_cache_abi()` body is gated to `return 1;`, so the tree-s OWN 32 B branch at :1190-1220\n'
  printf '# (shared_region_copyin_mappings + its per-mapping `IOS7LAB cache mapping ...` printf) compiles in;\n'
  printf '# the MACRO itself is NOT touched - the 994 "do not widen the macro" lesson; vm_unix.c is a POOL\n'
  printf '# file and 997 does NOT touch the assembly layer, NO trap.c change, NO pmap.c/commpage.c/\n'
  printf '# arm_vm_init.c change, NO pcb.c, NO vm_resident.c, NO mach_kernelrpc.c change). A fresh checkout\n'
  printf '# needs ALL EIGHT tools/stage_d13_*.sh drivers (988/990/991/992+993/994/995+996/996/997).\n#\n'
  printf '# 996 (pressed 2026-10-12, arm armed-d13-cea0c1c2, commit 821fb40, evidence\n'
  printf '# out/stage90/captures/cancro-last_kmsg-996-armed-d13-cea0c1c2.txt:4101-4105) MEASURED the G3\n'
  printf '# fix-s target by value: IOS7SHREG check_np ret=12; map_slide count=4 slide=0x3811000\n'
  printf '# slide_start=0x3f9bd000 size=0x89000; map_file kr=4 (KERN_INVALID_ARGUMENT) + map_slide ret=22\n'
  printf '# (EINVAL) = shared_region_copyin_mappings computed count*5<<2 against dyld 239-s 32-byte-stride\n'
  printf '# array, m[0].size read 0, vm_map_enter_mem_object-s initial_size==0 check returned EINVAL before\n'
  printf '# any slide work. The witness read the FULL 102-char halt message and cacheBase=0: the shared\n'
  printf '# cache was never mapped BECAUSE 438 died.\n#\n'
  printf '# THE 997 PRESS- PRE-REGISTRATION (frozen; deviations reported, not reinterpreted) is embedded in\n'
  printf '# scripts/press_997.sh: up to 4x `IOS7LAB cache mapping <n> va=... size=... offset=... max=...\n'
  printf '# init=...` BEFORE 438 fails again; the va fields answer SLID OR NOT; three mutually\n'
  printf '# distinguishable signatures (a) PREDICTED geometry-blocked kr=3 + ret=12, (b) the branch-s own\n'
  printf '# bound trips (NO map_file line, ret=22) - stop and re-model, (c) full success. FALSIFICATION:\n'
  printf '# zero `IOS7LAB cache mapping` lines == the define did not reach the compiled object.\n'
  printf '# tools/check_d13_shreg_wire.py --expect on asserts the 32 B wire off the linked image-s own bytes\n'
  printf '# (plus --tree 3 gates + 1 body gate); the FROZEN 996 image and the 994 capture are --controls\n'
  printf '# and must FAIL ON and PASS OFF.\n#\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=997-shreg-wire;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3