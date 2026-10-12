#!/usr/bin/env bash
# Park 988 and append its block to records/revert-set.txt.
#
# 988 = 987's switch set UNCHANGED + THE __LINKEDIT KEXT-BOOTSTRAP TEARDOWN COMPILED OUT (new switch
# STAGE90_XNU_KEXTB_LOGIN). The ENTRY BIN moves (2df6333a -> ?) and the WHOLE D13 pool was rebuilt
# (the fix is a pool edit: `libkern/c++/OSKext.o` must drop the __LINKEDIT dereference).
#
# WHY (987's press, 2026-10-12, arm armed-d13-2df6333a, commit cb2dd66): DECISIVE SUCCESS -- the boot
# ran the WHOLE HFS mount (HFSX vol header read at LBA 0x400002) and EVERY bsd_init line to
# `bsd_init: done`, then panicked at the FIRST statement after bsd_init returns (`startup.c:408`,
# `OSKextRemoveKextBootstrap()`).
#
# ROOT CAUSE (988): `OSKext::removeKextBootstrap()` (`libkern/c++/OSKext.cpp:746`) does, inside a LIVE
# `#if CONFIG_KXLD` block:
#     seg_to_remove = (kernel_segment_command_t *)getsegbyname("__LINKEDIT");   // :843
#     seg_data = (void *) seg_to_remove->vmaddr;                                // :859  (r0 == NULL)
# Our synthetic Mach-O declares ncmds=3 with segments `__TEXT`/`__DATA`/`__PRELINK_TEXT` and NO
# `__LINKEDIT`, so getsegbyname returns NULL and :859 dereferences it. BY VALUE: pc=0x800b2c90 =
# removeKextBootstrap+0x198, `e590501c ldr r5,[r0,#28]`, r0=0, far=0x1c, fsr=0x5. The sibling guarded
# calls at :805 (`__KLD`) and :838 (`__symtab`) do not fault; :843/:859 is the unguarded one.
#
# THE FIX: `tools/stage_d13_kextb_login.sh` (idempotent, text, in the gitignored tree) wraps the whole
# `#if CONFIG_KXLD ... #else ... #endif` block in `#ifndef STAGE90_XNU_KEXTB_LOGIN ... #endif`, so with
# `-DSTAGE90_XNU_KEXTB_LOGIN=1` the teardown is compiled out on D13 and the switch-off text is
# byte-identical to Apple (4570's object does not move). `build_entry.sh` gains a by-value clause that
# REFUSES a linked image still carrying the faulting dereference.
#
# READ: the boot moves PAST `OSKextRemoveKextBootstrap` (a new stop, or further: serial_keyboard_init,
# vm_page_init_local_q, thread_bind, then `vm_pageout()` and the idle loop) => the __LINKEDIT deref was
# 987's frontier. FALSIFICATION: the SAME fault (pc=0x800b2c90-class, r0=0) recurs => the fix did not
# take (stale object / the guard did not reach the compile) or the deref is not the (only) cause.
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
# bytes twice or to re-park 987's arm under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-2df6333a ]] && { echo "REFUSING: $LIVE holds 987's arm (2df6333a); build 988 first"; exit 1; }
PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  988: 987 + the __LINKEDIT KEXT-BOOTSTRAP TEARDOWN COMPILED OUT (STAGE90_XNU_KEXTB_LOGIN).\n' "$ARM"
  printf '# Switch set is 987-s UNCHANGED; the ENTRY BIN moved (2df6333a -> %s) and the WHOLE pool was\n' "${ARM#armed-d13-}"
  printf '# rebuilt (the fix is a pool edit: libkern/c++/OSKext.o must drop the __LINKEDIT dereference).\n#\n'
  printf '# 987 (pressed 2026-10-12, arm armed-d13-2df6333a, commit cb2dd66) is a DECISIVE SUCCESS: the boot\n'
  printf '# ran the WHOLE HFS mount (HFSX vol header read at LBA 0x400002) and EVERY bsd_init line to\n'
  printf '# `bsd_init: done` (55 bsd_init: lines), cal_enter/exit=1, iokitinit_exit=1, then panicked at the\n'
  printf '# FIRST statement after bsd_init returns (startup.c:408 OSKextRemoveKextBootstrap()).\n#\n'
  printf '# ROOT CAUSE (988): `OSKext::removeKextBootstrap()` (libkern/c++/OSKext.cpp:746) has an UNGUARDED\n'
  printf '# lookup+dereference inside a LIVE `#if CONFIG_KXLD` block:\n'
  printf '#   seg_to_remove = (kernel_segment_command_t *)getsegbyname("__LINKEDIT");  // :843\n'
  printf '#   seg_data = (void *) seg_to_remove->vmaddr;                               // :859 (r0 == NULL)\n'
  printf '# Our synthetic Mach-O declares ncmds=3 with segments __TEXT/__DATA/__PRELINK_TEXT and NO\n'
  printf '# __LINKEDIT, so getsegbyname returns NULL and :859 dereferences it. BY VALUE: pc=0x800b2c90 =\n'
  printf '# removeKextBootstrap+0x198, `e590501c ldr r5,[r0,#28]`, r0=0, far=0x1c, fsr=0x5. The sibling\n'
  printf '# guarded calls at :805 (__KLD) and :838 (__symtab) do not fault; :843/:859 is the unguarded one.\n#\n'
  printf '# 988: wrap the whole `#if CONFIG_KXLD ... #else ... #endif` block in `#ifndef\n'
  printf '# STAGE90_XNU_KEXTB_LOGIN ... #endif` (tools/stage_d13_kextb_login.sh, idempotent; switch-off text\n'
  printf '# is byte-identical to Apple so 4570-s object does not move), and refuse a linked image whose\n'
  printf '# removeKextBootstrap still carries the faulting dereference.\n#\n'
  printf '# READ: past OSKextRemoveKextBootstrap (a new stop, or further: serial_keyboard_init,\n'
  printf '# vm_page_init_local_q, thread_bind, then vm_pageout() and the idle loop) => the __LINKEDIT deref\n'
  printf '# was 987-s frontier. FALSIFICATION: the same fault recurs => it did not take (stale object / the\n'
  printf '# guard did not reach the compile) or the deref is not the (only) cause.\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=988-d13-kextb-linkedit-teardown-out;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3