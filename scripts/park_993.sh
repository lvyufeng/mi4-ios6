#!/usr/bin/env bash
# Park 993 and append its block to records/revert-set.txt.
#
# 993 = 992's switch set UNCHANGED (no new config key) + three staged tree edits: `arm_vm_init` ZEROES
# the stolen high-vector L2 table (the 992 regression's root cause), and `pmap_create_sharedpage`
# MEASURES (and fail-soft clears) slot 0xf4 before the alias enter. The ENTRY BIN moves
# (fce6e8dd -> ?). TWO tree SOURCE files move: `osfmk/arm/arm_vm_init.c` (new in this rung) and
# `osfmk/arm/pmap.c` (probe + pre/post reports); `osfmk/arm/commpage/commpage.c` is 992's, unchanged.
# A fresh checkout needs ALL FOUR stage scripts: tools/stage_d13_kextb_login.sh (988's),
# tools/stage_d13_sp_align.sh (990's), tools/stage_d13_vmhint_guard.sh (991's) and
# tools/stage_d13_commpage_alias.sh (992+993's - it now stages the vector-table zeroing too).
#
# WHY (992's press, 2026-10-12, arm armed-d13-fce6e8dd, commit d5facfc, evidence
# /tmp/cancro-last_kmsg.txt): the rung REGRESSED. 992 and 991's logs are identical through line 4043;
# 992 then panics at 4044, inside vm_commpage_init -> pmap_create_sharedpage:
#   panic(cpu 0 caller 0x8046b6b0): Kernel data abort. (faulting address: 0xc1233b14)
#   r4=0xc0837000 r6=0x80000000 r7=0xffff4000 r9=0x80b813d0 r10=0x003fffcc r11=0x000ffd5a
#   pc=0x80028b90 lr=0x80049abc fsr=0x7 (translation fault, page, READ); sleh seq1 user=0 recover=0.
#
# THE ROOT CAUSE, read BY VALUE (every number arithmetic against that log):
#   pc=0x80028b90 is `ldr r0, [r0, #12]` in pmap_enter_options+0x29c - the `pv_h->pmap` load before
#   `panic("pmap_enter_options: null pv_list\n")`, taken because the alias slot held a nonzero "old
#   mapping". fp = old_pte>>12 = r11 = 0x000FFD5A (a leftover PA 0xFFD5A000); pai = 0xFFD5A - 0x80000 =
#   0x7FD5A (523,610); pv_h = r4 + pai*20 = 0xC1233B08; the load of [pv_h+12] = 0xC1233B14 = the fault
#   address exactly. The pv_head_table holds only 123,904 entries (484 MiB / 4 KiB) - the walk left it.
#   WHY the slot held garbage: `arm_vm_init` steals vecpt_start = first_avail as the high-vector L2 table
#   and writes ONLY slot 0xF0 (VECTORS_BASE = 0xFFFF0000). boot_args->topOfKernel = 0x80a00000 (992 log
#   line 3966) => vecpt_start = 0x80A00000 + 0x8000 + 0x79000 + 0x100000 = 0x80B81000, and
#   r9 = 0x80b813d0 = vecpt_start + 0xF4*4 = exactly pmap_pte(kernel_pmap, 0xFFFF4000): the 992 alias
#   enter was the FIRST EVER enter into that slot, whose 255 neighbours were raw DRAM. Every OTHER table
#   in the boot path is zeroed - l2_cache_to_range(zero=TRUE), whose own comment (arm_vm_init.c:250)
#   warns about exactly this "may have been used by the bootloader" case, and pmap_grab_page's bzero
#   (:2769) for pmap_expand; the stolen vector page was the sole exception.
#
# THE FIX (`tools/stage_d13_commpage_alias.sh` stages all three, gated on the SAME switch):
#   1. arm_vm_init.c: after `va_vecpt` is computed, count+print the stolen table's nonzero slots, bzero
#      the whole L2_SIZE page, count+print again ("IOS7LAB vecl2:" markers).
#   2. pmap.c's alias block: print slot 0xf4 BEFORE the enter ("L2 slot pre = 0x%08x"), fail-soft clear
#      it if nonzero ("stale slot cleared"), print "L2 slot post" after.
# VERIFIED BY VALUE: tools/check_commpage_alias.py's checks 6-7 assert both in the linked image (the
# vecl2 report + a bzero/memset with L2_SIZE in r1/r2 inside arm_vm_init; the probe's reports loaded in
# pmap_create_sharedpage before its last pmap_enter call) and REFUSE a 992-shaped image - rehearsed
# against the frozen 992 elf: exit 1, exactly checks 1/6/7 fail.
#
# PRE-REGISTERED READING: `IOS7LAB vecl2: N nonzero slot(s) ... before zeroing` (a small N; slot 0xf4's
# 0xFFD5A0xx neighbourhood among the printed lines), then `... 0 nonzero slot(s) after zeroing`; then
# `IOS7LAB commpage alias: L2 slot pre = 0x00000000` (the arm_vm_init zero ran AND reached this pmap),
# the 992 markers `IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN` and
# `IOS7LAB commpage cpus: ...`; then bsd_init -> launchd; the 231x `0xffff4090 ... Translation fault`
# burst MUST NOT recur.
# FALSIFICATION: the same 231x 0xffff4090 burst recurs (then the alias sits in a pmap the user context
# does not consult - 994 = the user-pmap/context side), or a NEW kernel abort/panic appears (the checker's
# checks 6-7 still hold for the alias itself, so its pc names the next wall).
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

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse to re-park the spent
# 991 or the regressed 992 arms under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-13355bf0 ]] && { echo "REFUSING: $LIVE holds 991's arm (13355bf0); build 993 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 993 first"; exit 1; }
PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  993: THE STOLEN HIGH-VECTOR L2 TABLE IS ZEROED BEFORE THE ALIAS ENTER (992-s REGRESSION FIXED).\n' "$ARM"
  printf '# Switch set is 992-s, VERBATIM (no new config key); the ENTRY BIN moved (fce6e8dd -> %s) and TWO tree\n' "${ARM#armed-d13-}"
  printf '# SOURCE files move: osfmk/arm/arm_vm_init.c (NEW in this rung: the zeroing) and osfmk/arm/pmap.c\n'
  printf '# (the slot probe + pre/post reports); osfmk/arm/commpage/commpage.c is 992-s, unchanged - this rung\n'
  printf '# has NO assembly-layer, NO pcb.c and NO vm_resident.c change (a fresh checkout needs ALL FOUR\n'
  printf '# tools/stage_d13_kextb_login.sh, tools/stage_d13_sp_align.sh, tools/stage_d13_vmhint_guard.sh and\n'
  printf '# tools/stage_d13_commpage_alias.sh - the last now stages the vector-table zeroing too).\n#\n'
  printf '# 992 (pressed 2026-10-12, arm armed-d13-fce6e8dd, commit d5facfc, evidence /tmp/cancro-last_kmsg.txt)\n'
  printf '# REGRESSED: 992 and 991-s logs are identical through line 4043, then 992 panics at 4044, inside\n'
  printf '# vm_commpage_init -> pmap_create_sharedpage: panic(cpu 0 caller 0x8046b6b0): Kernel data abort.\n'
  printf '# (faulting address: 0xc1233b14); r4=0xc0837000 r6=0x80000000 r7=0xffff4000 r9=0x80b813d0\n'
  printf '# r10=0x003fffcc r11=0x000ffd5a pc=0x80028b90 lr=0x80049abc fsr=0x7 (translation fault, page, READ).\n#\n'
  printf '# ROOT CAUSE, read BY VALUE: pc=0x80028b90 is `ldr r0, [r0, #12]` in pmap_enter_options+0x29c - the\n'
  printf '# pv_h->pmap load before panic("pmap_enter_options: null pv_list"); fp = old_pte>>12 = r11 =\n'
  printf '# 0x000FFD5A (leftover PA 0xFFD5A000); pai = 0xFFD5A - 0x80000 = 0x7FD5A (523,610); pv_h = r4 +\n'
  printf '# pai*20 = 0xC1233B08; [pv_h+12] = 0xC1233B14 = the fault address exactly. pv_head_table holds only\n'
  printf '# 123,904 entries - the walk left it. WHY: arm_vm_init steals vecpt_start = first_avail as the\n'
  printf '# high-vector L2 table and writes ONLY slot 0xF0 (VECTORS_BASE 0xFFFF0000); topOfKernel 0x80a00000\n'
  printf '# => vecpt_start = 0x80B81000, and r9 = 0x80b813d0 = vecpt_start + 0xF4*4 = exactly\n'
  printf '# pmap_pte(kernel_pmap, 0xFFFF4000): the alias enter was the FIRST EVER enter into that slot; its 255\n'
  printf '# neighbours were raw DRAM. Every other table in the boot path is zeroed (l2_cache_to_range\n'
  printf '# zero=TRUE - arm_vm_init.c:250-s own "used by the bootloader" comment - and pmap_grab_page :2769);\n'
  printf '# the stolen vector page was the sole exception.\n#\n'
  printf '# 993: tools/stage_d13_commpage_alias.sh now also stages (a) in arm_vm_init.c, after va_vecpt is\n'
  printf '# computed: count+print the stolen table-s nonzero slots, bzero the whole L2_SIZE page, count+print\n'
  printf '# again (IOS7LAB vecl2: markers); (b) in pmap.c-s alias block: print slot 0xf4 BEFORE the enter\n'
  printf '# (L2 slot pre), fail-soft clear it if nonzero (stale slot cleared), print L2 slot post after.\n'
  printf '# tools/check_commpage_alias.py checks 6-7 assert both in the LINKED elf and REFUSE a 992-shaped\n'
  printf '# image (rehearsed against the frozen 992 elf: exit 1, exactly checks 1/6/7 fail).\n#\n'
  printf '# READ: IOS7LAB vecl2: N nonzero slot(s) ... before zeroing (small N; slot 0xf4-s 0xFFD5A0xx\n'
  printf '# neighbourhood among the printed lines), then 0 nonzero slot(s) after zeroing; then\n'
  printf '# IOS7LAB commpage alias: L2 slot pre = 0x00000000 (the arm_vm_init zero ran AND reached this pmap),\n'
  printf '# the 992 markers IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN and\n'
  printf '# IOS7LAB commpage cpus: ...; then bsd_init -> launchd; the 231x 0xffff4090 Translation fault burst\n'
  printf '# MUST NOT recur.\n'
  printf '# FALSIFICATION: the same 231x 0xffff4090 burst recurs (the alias sits in a pmap the user context\n'
  printf '# does not consult - 994 = the user-pmap/context side), or a NEW kernel abort/panic appears (its pc\n'
  printf '# names the next wall).\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never written -\n'
  printf '# fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=993-vecl2-zeroing-and-slot-probe;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3