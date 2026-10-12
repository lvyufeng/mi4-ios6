#!/usr/bin/env bash
# Park 989 and append its block to records/revert-set.txt.
#
# 989 = 988's switch set UNCHANGED (the arm-defining set does not move: no new switch, no ENTRY_ARM_KEY
# added) + THE CENSUS' RECORDED ADDRESS IDENTITY-CHECKED before it is walked. The ENTRY BIN moves
# (707801c4 -> ?) and the D13 pool is rebuilt UNCHANGED (989 is entirely in `src/entry/`; the only tree
# edit a fresh checkout needs is 988's `tools/stage_d13_kextb_login.sh`, re-staged here).
#
# WHY (988's press, 2026-10-12, arm armed-d13-707801c4, commit ed5c3ca, evidence /tmp/press988.log):
# a DECISIVE SUCCESS in the pre-registered sense - the abort MOVED. The KEXTB_LOGIN guard took: there is
# no second fault at 0x800b2c90, so `removeKextBootstrap` no longer tears down an absent __LINKEDIT.
# The NEW first abort is a data abort at the device-tree census' recorded block:
#     fsr=5, far=0x00010005, pc=0x800c41c0 = _ZN11OSMetaClassD1Ev+0x1c, lr=0x8049e1c0
# `lr` is the return of `bl getChildSetReference` at 0x8049e1bc inside `entry_probe_dt_children`, which
# runs from `__wrap_vm_pageout` (kernel_bootstrap's LAST statement).
#
# ROOT CAUSE (989). The recorded address is `g_dtplane_root` = `xnu_live_dtalloc_ret` = 0xc0b7bd80, the
# object `IODeviceTreeAlloc` returned. While alive it WAS an `IOService` (`xnu_live_cls_vptr=0x804c6714`
# = `_ZTV9IOService + 8`, cls_site=1). By the census it has been FREED AND REUSED (486's doc comment:
# `IORegistryEntry::init`'s parent loop drops the tree root's last reference) - its `[+12]` is 0x10001,
# so `getChildSetReference`'s registry-table load dispatched through slot 36 of 0x10001 and tail-called
# into a destructor (`~OSMetaClass`), which dereferenced `[+4]` of 0x10001 and faulted at far=0x10005.
# On 4570 the recipient was an `IORootParent` and its walk was benign; on D13 it is not.
#
# THE FIX: `entry_trace.c` gains `entry_dt_recorded_walk` (noinline): it reads `[recorded]` ONCE,
# publishes it (`entry_note_dtref` -> `xnu_live_dtref_obj/found/want/ok`), and walks the recorded address
# (`entry_xnu_child_count` + `entry_xnu_child_set`) ONLY when `[recorded]` equals the word the object
# carried while it was alive (`g_dtplane_root_vptr`, MEASURED in `entry_note_dtplane` at the one safe
# moment - not a vtable constant, which would be a second definition of one fact and would go stale with
# the vtable; it moved 0x804c6714 -> 0x804c67f4 between 988 and 989). The recorded block's class read
# moves under the same guard. `build_entry.sh` gains a by-value clause (989) that REFUSES an image whose
# helper does not materialize the global's address (either form: movw/movt or a literal-pool word) or
# whose probe still walks the recorded address.
#
# READ: past the census' recorded block (`vm_pageout` returns) into `load_init_program` / the pid-1 exec
# path => the reused recorded address was 988's frontier. FALSIFICATION: the SAME abort recurs
# (pc=0x800c41c0-class with far=0x10005) => the guard did not take, or the guard passed and the address
# was reused by an object with the SAME first word (the residual the doc comment names).
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

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse to park the same
# bytes twice or to re-park 988's arm under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-707801c4 ]] && { echo "REFUSING: $LIVE holds 988's arm (707801c4); build 989 first"; exit 1; }
PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  989: 988 + THE CENSUS- RECORDED ADDRESS IDENTITY-CHECKED BEFORE IT IS WALKED.\n' "$ARM"
  printf '# Switch set is 988-s UNCHANGED (no new switch, no ENTRY_ARM_KEY added: 989 is entry-side); the\n'
  printf '# ENTRY BIN moved (707801c4 -> %s) and the D13 pool is rebuilt unchanged (only a fresh checkout\n' "${ARM#armed-d13-}"
  printf '# needs 988-s tools/stage_d13_kextb_login.sh, re-staged here).\n#\n'
  printf '# 988 (pressed 2026-10-12, arm armed-d13-707801c4, commit ed5c3ca, evidence /tmp/press988.log) is a\n'
  printf '# DECISIVE SUCCESS in the pre-registered sense - the abort MOVED. The KEXTB_LOGIN guard took: no\n'
  printf '# second fault at 0x800b2c90 (removeKextBootstrap no longer tears down an absent __LINKEDIT). The\n'
  printf '# NEW first abort is at the device-tree census- recorded block: fsr=5, far=0x00010005,\n'
  printf '# pc=0x800c41c0 = ~OSMetaClass()+0x1c, lr=0x8049e1c0 = the return of `bl getChildSetReference` at\n'
  printf '# 0x8049e1bc inside entry_probe_dt_children, which runs from __wrap_vm_pageout (kernel_bootstrap-s\n'
  printf '# LAST statement).\n#\n'
  printf '# ROOT CAUSE (989): the recorded address (461-s g_dtplane_root = xnu_live_dtalloc_ret = 0xc0b7bd80,\n'
  printf '# the object IODeviceTreeAlloc returned) WAS an IOService while alive (xnu_live_cls_vptr=0x804c6714\n'
  printf '# = _ZTV9IOService+8) and has been FREED AND REUSED by the census (486-s doc comment:\n'
  printf '# IORegistryEntry::init-s parent loop drops the tree root-s last reference). Its [+12] is 0x10001,\n'
  printf '# so getChildSetReference-s registry-table load dispatched through slot 36 of 0x10001 and tail-called\n'
  printf '# into ~OSMetaClass, which dereferenced [+4] of 0x10001 and faulted at far=0x10005. On 4570 the\n'
  printf '# recipient was an IORootParent and its walk was benign; on D13 it is not.\n#\n'
  printf '# 989: entry_trace.c gains entry_dt_recorded_walk (noinline): it reads [recorded] ONCE, publishes it\n'
  printf '# (entry_note_dtref -> xnu_live_dtref_obj/found/want/ok), and walks the recorded address only when\n'
  printf '# [recorded] equals the word the object carried while alive (g_dtplane_root_vptr, MEASURED in\n'
  printf '# entry_note_dtplane - not a vtable constant; it moved 0x804c6714 -> 0x804c67f4 between 988 and 989).\n'
  printf '# build_entry.sh gains a by-value clause that REFUSES an image whose helper does not materialize the\n'
  printf '# global-s address (either form: movw/movt or a literal-pool word) or whose probe still walks the\n'
  printf '# recorded address.\n#\n'
  printf '# READ: past the census- recorded block (vm_pageout returns) into load_init_program / the pid-1 exec\n'
  printf '# path => the reused recorded address was 988-s frontier. FALSIFICATION: the same abort recurs\n'
  printf '# (pc=0x800c41c0-class with far=0x10005) => the guard did not take, or it passed and the address was\n'
  printf '# reused by an object with the SAME first word (the residual the doc comment names).\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=989-d13-dt-census-recorded-address-identity-guard-out;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3