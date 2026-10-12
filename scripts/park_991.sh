#!/usr/bin/env bash
# Park 991 and append its block to records/revert-set.txt.
#
# 991 = 990's switch set + STAGE90_XNU_VMHINT_GUARD=1: `vm_page_lookup` treats an implausible
# `object->memq_hint` as absent FOR THAT CALL ONLY (a local shadow; the field is never written). ONE tree
# object moves (unlike 988/989/990): the POOL's osfmk_vm_vm_resident.o - this rung has NO assembly-layer
# and NO pcb.c change. The ENTRY BIN moves (0d531453 -> ?) and build_entry.sh gains the ENTRY_ARM_KEY,
# the record emission and a by-value refusing clause (tools/check_vmhint_guard.py). A fresh checkout
# needs ALL THREE stage scripts: tools/stage_d13_kextb_login.sh (988's), tools/stage_d13_sp_align.sh
# (990's) and tools/stage_d13_vmhint_guard.sh (991's).
#
# WHY (990's press, 2026-10-12, arm armed-d13-0d531453, commit ce0195e, evidence
# /tmp/cancro-last_kmsg.txt): a READ-CONFIRMED SUCCESS in the pre-registered sense - the SP-alignment fix
# HELD (sp = 0xcfdbba88, 8-aligned; 989's `vld1.32 {d18}` alignment fault did NOT recur), and the boot
# ran the WHOLE mount + bsd_init and reached the pid-1 launchd exec: `attempting to start init of
# /sbin/launchd` -> NEW first abort INSIDE vm_page_lookup:
#     pc = 0x80020838, instruction `ldrd r0,[r5,#24]`, r5 = object->memq_hint = 0x1,
#     object = r0 = r4 = 0xc0005b48, lr = 0x8003d8ec = vm_map_pmap_enter's vm_page_lookup call (offset 0),
#     far = 0x19 (= 1 + 24), fsr = 1 (alignment fault).
# Then 214x `recursive abort` at pc=0x8046a464 (Debugger trap) and a watchdog reset ~46 s later; the USB
# console capture was EMPTY and the log came from /proc/last_kmsg.
#
# ROOT CAUSE (991), read BY VALUE. `memq_hint` sits at vm_object+0x20 (`ldr r5,[r0,#32]`), and NO
# legitimate writer of `memq_hint` can produce 0x1 - `vm_object_template` copies in NULL via
# `_vm_object_allocate`, VM_PAGE_INSERT/VM_PAGE_REMOVE write a valid page or NULL, and `vm_page_lookup`'s
# own hint paths write a real page - so either the field was clobbered after allocation, or `object` is
# not a vm_object at all (it sits ~11 MB below every other heap pointer in the log, consistent with an
# early zalloc allocation).
#
# THE FIX (`tools/stage_d13_vmhint_guard.sh`, gated `#if STAGE90_XNU_VMHINT_GUARD` with the original text
# in the #else, so switch-OFF is byte-identical): `vm_page_lookup` treats `(vm_offset_t)mem < 0x80000000u
# || (mem & 3u)` as absent FOR THAT CALL ONLY (a local shadow; the field is never written), after a
# bounded (max 8) kprintf beginning `VMHINT991: ` that records obj/mem/lr and the object's words
# w00/w04/w18/w1c/w24. The switch threads through build_xnu_arm_kernel.sh (pool DEFINES) and
# build_entry.sh (ENTRY_ARM_KEY + record + the by-value clause that runs tools/check_vmhint_guard.py and
# REFUSES the 990 image).
#
# READ: the log carries `VMHINT991:` lines (at most 8) naming the object's words and lr, and the boot
# moves PAST the exec wall. If w00 (memq.next) equals obj and the other words look like a live vm_object,
# the next rung hunts the clobberer; if the words are nonsense, the object identity chain
# (ubc_getobject -> moc_object) is the bug and lr names the path.
# FALSIFICATION: the SAME fault recurs (`ldrd r0,[r5,#24]` at pc=0x80020838 with r5=1) => the guard did
# not reach the pool (the 991 clause should have refused such an image) or an object's hint was
# plausible-range-but-bad (the guard models only <0x80000000 | &3).
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
# bytes twice or to re-park 990's arm under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-0d531453 ]] && { echo "REFUSING: $LIVE holds 990's arm (0d531453); build 991 first"; exit 1; }
PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  991: VM_PAGE_LOOKUP TREATS AN IMPLAUSIBLE MEMQ_HINT AS ABSENT, FOR THAT CALL ONLY.\n' "$ARM"
  printf '# Switch set is 990-s + STAGE90_XNU_VMHINT_GUARD=1; the ENTRY BIN moved (0d531453 -> %s) and ONE tree\n' "${ARM#armed-d13-}"
  printf '# object moves: the pool-s osfmk_vm_vm_resident.o - this rung has NO assembly-layer and NO pcb.c\n'
  printf '# change (a fresh checkout needs ALL THREE tools/stage_d13_kextb_login.sh, tools/stage_d13_sp_align.sh\n'
  printf '# and tools/stage_d13_vmhint_guard.sh).\n#\n'
  printf '# 990 (pressed 2026-10-12, arm armed-d13-0d531453, commit ce0195e, evidence /tmp/cancro-last_kmsg.txt)\n'
  printf '# is a READ-CONFIRMED SUCCESS in the pre-registered sense: the SP-alignment fix HELD (sp = 0xcfdbba88,\n'
  printf '# 8-aligned; 989-s vld1.32 {d18} alignment fault did NOT recur), and the boot ran the WHOLE mount +\n'
  printf '# bsd_init and reached the pid-1 launchd exec: attempting to start init of /sbin/launchd -> NEW first\n'
  printf '# abort INSIDE vm_page_lookup: pc = 0x80020838, instruction ldrd r0,[r5,#24], r5 = object->memq_hint\n'
  printf '# = 0x1, object = r0 = r4 = 0xc0005b48, lr = 0x8003d8ec = vm_map_pmap_enter-s vm_page_lookup call\n'
  printf '# (offset 0), far = 0x19 (= 1 + 24), fsr = 1 (alignment fault). Then 214x recursive abort at\n'
  printf '# pc=0x8046a464 (Debugger trap) and a watchdog reset ~46 s later; the USB console capture was EMPTY\n'
  printf '# and the log came from /proc/last_kmsg.\n#\n'
  printf '# ROOT CAUSE (991), read BY VALUE: memq_hint sits at vm_object+0x20 (ldr r5,[r0,#32]), and NO\n'
  printf '# legitimate writer of memq_hint can produce 0x1 - vm_object_template copies in NULL via\n'
  printf '# _vm_object_allocate, VM_PAGE_INSERT/VM_PAGE_REMOVE write a valid page or NULL, and vm_page_lookup-s\n'
  printf '# own hint paths write a real page. So either the field was clobbered after allocation, or object is\n'
  printf '# not a vm_object at all (it sits ~11 MB below every other heap pointer in the log, consistent with\n'
  printf '# an early zalloc allocation).\n#\n'
  printf '# 991: tools/stage_d13_vmhint_guard.sh (gated #if STAGE90_XNU_VMHINT_GUARD, original text in the #else -\n'
  printf '# switch-OFF byte-identical) makes vm_page_lookup treat a hint with (vm_offset_t)mem < 0x80000000u OR\n'
  printf '# (mem & 3u) as absent FOR THAT CALL ONLY (local shadow; the field is never written), after a bounded\n'
  printf '# (max 8) kprintf beginning VMHINT991: that records obj/mem/lr and the object-s words\n'
  printf '# w00/w04/w18/w1c/w24. tools/check_vmhint_guard.py walks the LINKED elf and REFUSES the 990 image.\n#\n'
  printf '# READ: the log carries VMHINT991: lines (at most 8) naming the object-s words and lr, and the boot\n'
  printf '# moves PAST the exec wall. If w00 (memq.next) equals obj and the other words look like a live\n'
  printf '# vm_object, the next rung hunts the clobberer; if the words are nonsense, the object identity chain\n'
  printf '# (ubc_getobject -> moc_object) is the bug and lr names the path.\n'
  printf '# FALSIFICATION: the SAME fault recurs (ldrd r0,[r5,#24] at pc=0x80020838 with r5=1) => the guard did\n'
  printf '# not reach the pool (the 991 clause should have refused such an image) or an object-s hint was\n'
  printf '# plausible-range-but-bad (the guard models only <0x80000000 | &3).\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=991-vm-page-lookup-memq-hint-guard;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3