#!/usr/bin/env bash
# Park 994 and append its block to records/revert-set.txt.
#
# 994 = 993's switch set VERBATIM + STAGE90_XNU_IOS7_TRAPS=1: the iOS 7 VM wire-width fix for traps
# 10/12/14 in the POOL (`osfmk/ipc/mach_kernelrpc.c` + `osfmk/arm/ios7_compat.h`) and the -3 SWI
# timebase wiring in the ASSEMBLY layer (`osfmk/arm/traps_lo.s`). The ENTRY BIN moves
# (1d5cf20e -> ?). THREE tree SOURCE files move in THIS rung: `osfmk/ipc/mach_kernelrpc.c`,
# `osfmk/arm/ios7_compat.h` and `osfmk/arm/traps_lo.s` (`osfmk/kern/syscall_sw.c` is NOT one of them:
# traps 10/12/14 already carry arg counts 5/5/7 and on `__arm__` the mungers are ignored, and traps
# 15/24/25/41/42 stay `kern_invalid`);
# `osfmk/arm/arm_vm_init.c` and `osfmk/arm/pmap.c` are 993's, unchanged. A fresh checkout needs ALL
# FIVE stage scripts: tools/stage_d13_kextb_login.sh (988's), tools/stage_d13_sp_align.sh (990's),
# tools/stage_d13_vmhint_guard.sh (991's), tools/stage_d13_commpage_alias.sh (992+993's) and
# tools/stage_d13_ios7_traps.sh (994's - the rung's driver; it calls swi's
# tools/patch_d13_ios7_traps.py and vmwire's tools/patch_d13_ios7_vmwire.py and edits nothing itself).
#
# WHY (993's press, 2026-10-12, arm armed-d13-1d5cf20e, commit 160650b, evidence
# /tmp/cancro-last_kmsg.txt): 993 CONFIRMED the commpage alias AND the high-vector L2 zeroing. The log
# carries `IOS7LAB vecl2: N nonzero slot(s) ... before zeroing`, then `... 0 nonzero slot(s) after
# zeroing`, then `IOS7LAB commpage alias: L2 slot pre = 0x00000000`, the 992 marker
# `IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN` and `IOS7LAB commpage cpus: ...`;
# the 991 `0xffff4090 ... Translation fault` burst is GONE (0 commpage faults) and there is NO panic.
#
# THE NEW FRONTIER, read BY VALUE: the boot HALTS inside dyld at dyld's own deliberate trap -
#     launchd[1]: usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x2be7e0c4 ...   (236x)
# `0x2be7e0c4 - slide 0x7d000 = 0x2be010c4` = `_dyld_fatal_error` (`fe de ff e7` at file 0x10c4, i.e.
# `udf #0xdefe`), whose ONLY caller is `dyld::halt`. `dyld::halt` is reached from `dyld::throwf`'s
# out-of-memory path: `__simple_salloc`'s 4 KiB `vm_allocate(..., VM_FLAGS_ANYWHERE)` (or the following
# `strdup`) returned NULL, so the real error string was destroyed before it could print.
#
# THE ROOT CAUSE: the trap-10 wire-width defect. The tree's own `struct ios7lab_vm_allocate_wire`
# (`mach_kernelrpc.c:50-54`) is FIVE 32-bit words - `{ target, addr, size_low, size_high, flags }` -
# the iOS 7 ARMv7 wire. The kernel's `mach_vm_size_t` is 32 bits on this legacy ABI, so a FOUR-word read
# makes `args->flags` read the size's HIGH half (0) instead of the real flags word, and dyld's
# `vm_allocate` looks like a failure.
#
# THE FIX (`tools/stage_d13_ios7_traps.sh`): (a) POOL - `mach_kernelrpc.c`'s file-wide
# `#if IOS7_COMPAT_PLATFORM` (:39) opens under the switch and the three `_kernelrpc_*` VM trap bodies
# (10/12/14) route through their real-width iOS 7 wire adapters (5/5/7 words) when
# `ios7_traps_enabled()` - the narrow selector added to `ios7_compat.h` - answers 1;
# `osfmk/kern/syscall_sw.c` needs NO edit and traps 15/24/25/41/42 stay `kern_invalid`; (b) ASSEMBLY -
# `traps_lo.s`'s :116 gate closes the `cmn r12, #3` / `swi_trap_tb: movs pc,
# lr` fast path and :162/:264 open `ios7lab_timebase: blx _mach_absolute_time; b swi_exit64`, so a -3
# SWI returns the real 64-bit ns clock.
# VERIFIED BY VALUE: tools/check_d13_ios7_traps.py --expect on asserts, off the linked image's own
# disassembly, that the `cmn r12,#3` / `beq swi_trap_tb` pair is ABSENT, that a `cmn r11,#3` branches to
# the `mach_absolute_time` call, and that this call is immediately followed by `b swi_exit64`; the
# rehearsal (--expect off against the pre-fix image) PASSES and --expect on FAILS there.
#
# PRE-REGISTERED READING: `IOS7LAB vm_allocate64 size=... flags=1 result=0 address=...` markers (the
# trap-10 wire now reads the real flags word - `flags=1` is `VM_FLAGS_ANYWHERE`, where the pre-994 read
# produced 0), the trap-12/-14 equivalents (`IOS7LAB vm_deallocate64` / `IOS7LAB vm_protect64`)
# reading their real width, the OOM gone, and EITHER the real halt message appears (`dyld: <msg>`) OR
# the boot PROCEEDS PAST dyld.
# FALSIFICATION: the `udf` at 0x2be7e0c4 (`_dyld_fatal_error`) RECURS - the wire still misreads (the
# switch did not reach the pool) or the OOM has a different cause.
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
# confirmed 993 or the regressed 992 arms under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 994 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 994 first"; exit 1; }
PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  994: THE iOS 7 MACH-TRAP WIRE (pool) + THE -3 SWI TIMEBASE (asm) ARE WIRED IN.\n' "$ARM"
  printf '# Switch set is 993-s VERBATIM + STAGE90_XNU_IOS7_TRAPS=1; the ENTRY BIN moved (1d5cf20e -> %s) and\n' "${ARM#armed-d13-}"
  printf '# THREE tree SOURCE files move in THIS rung: osfmk/ipc/mach_kernelrpc.c,\n'
  printf '# osfmk/arm/ios7_compat.h and osfmk/arm/traps_lo.s (osfmk/kern/syscall_sw.c is NOT one: traps\n'
  printf '# 10/12/14 already carry arg counts 5/5/7 and on __arm__ the mungers are ignored, and traps\n'
  printf '# 15/24/25/41/42 stay kern_invalid); osfmk/arm/arm_vm_init.c and osfmk/arm/pmap.c are\n'
  printf '# 993-s, unchanged - this rung has NO pcb.c and NO vm_resident.c change (a fresh checkout needs ALL\n'
  printf '# FIVE tools/stage_d13_kextb_login.sh, tools/stage_d13_sp_align.sh, tools/stage_d13_vmhint_guard.sh,\n'
  printf '# tools/stage_d13_commpage_alias.sh and tools/stage_d13_ios7_traps.sh).\n#\n'
  printf '# 993 (pressed 2026-10-12, arm armed-d13-1d5cf20e, commit 160650b, evidence /tmp/cancro-last_kmsg.txt)\n'
  printf '# CONFIRMED the commpage alias AND the high-vector L2 zeroing: the log carries `IOS7LAB vecl2: N\n'
  printf '# nonzero slot(s) ... before zeroing`, then `... 0 nonzero slot(s) after zeroing`, then\n'
  printf '# `IOS7LAB commpage alias: L2 slot pre = 0x00000000`, the 992 marker `IOS7LAB commpage alias\n'
  printf '# 0xffff4000: same page, user read-only, XN` and `IOS7LAB commpage cpus: ...`; the 991\n'
  printf '# `0xffff4090 ... Translation fault` burst is GONE (0 commpage faults) and there is NO panic.\n#\n'
  printf '# THE NEW FRONTIER, read BY VALUE: the boot HALTS inside dyld at dyld-s own deliberate trap -\n'
  printf '# `launchd[1]: usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x2be7e0c4 ...` (236x).\n'
  printf '# 0x2be7e0c4 - slide 0x7d000 = 0x2be010c4 = _dyld_fatal_error (fe de ff e7 at file 0x10c4, i.e.\n'
  printf '# `udf #0xdefe`), whose ONLY caller is dyld::halt. dyld::halt is reached from dyld::throwf-s\n'
  printf '# out-of-memory path: __simple_salloc-s 4 KiB vm_allocate(..., VM_FLAGS_ANYWHERE) (or the following\n'
  printf '# strdup) returned NULL, so the real error string was destroyed before it could print.\n#\n'
  printf '# ROOT CAUSE: the trap-10 wire-width defect. The tree-s own struct ios7lab_vm_allocate_wire\n'
  printf '# (mach_kernelrpc.c:50-54) is FIVE 32-bit words - { target, addr, size_low, size_high, flags } -\n'
  printf '# the iOS 7 ARMv7 wire. The kernel-s mach_vm_size_t is 32 bits on this legacy ABI, so a FOUR-word\n'
  printf '# read makes args->flags read the size-s HIGH half (0) instead of the real flags word, and dyld-s\n'
  printf '# vm_allocate looks like a failure.\n#\n'
  printf '# 994: tools/stage_d13_ios7_traps.sh - (a) POOL: mach_kernelrpc.c-s file-wide\n'
  printf '# `#if IOS7_COMPAT_PLATFORM` (:39) opens under the switch and the three _kernelrpc_* VM trap\n'
  printf '# bodies (10/12/14) route through their real-width wire adapters (5/5/7 words) when the\n'
  printf '# ios7_traps_enabled selector in ios7_compat.h answers 1 (syscall_sw.c needs NO edit; traps\n'
  printf '# 15/24/25/41/42 stay kern_invalid);\n'
  printf '# (b) ASSEMBLY: traps_lo.s-s :116 gate closes the `cmn r12, #3` / `swi_trap_tb: movs pc, lr` fast\n'
  printf '# path and :162/:264 open `ios7lab_timebase: blx _mach_absolute_time; b swi_exit64`, so a -3 SWI\n'
  printf '# returns the real 64-bit ns clock.\n'
  printf '# tools/check_d13_ios7_traps.py --expect on asserts, off the linked image-s own disassembly, that\n'
  printf '# the `cmn r12,#3` / `beq swi_trap_tb` pair is ABSENT, that a `cmn r11,#3` branches to the\n'
  printf '# mach_absolute_time call, and that this call is immediately followed by `b swi_exit64`.\n#\n'
  printf '# READ: IOS7LAB vm_allocate64 size=... flags=1 result=0 address=... markers (the trap-10 wire now\n'
  printf '# reads the real flags word - flags=1 is VM_FLAGS_ANYWHERE, where the pre-994 read produced 0), the\n'
  printf '# trap-12/-14 equivalents (IOS7LAB vm_deallocate64 / vm_protect64) reading their real width, the\n'
  printf '# OOM gone, and EITHER the real halt message appears (dyld: <msg>) OR the boot PROCEEDS PAST dyld.\n'
  printf '# FALSIFICATION: the udf at 0x2be7e0c4 (_dyld_fatal_error) RECURS - the wire still misreads (the\n'
  printf '# switch did not reach the pool) or the OOM has a different cause.\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never written -\n'
  printf '# fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=994-ios7-trap-wire-and-minus3-timebase;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3
