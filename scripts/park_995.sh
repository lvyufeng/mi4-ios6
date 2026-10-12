#!/usr/bin/env bash
# Park 995 and append its block to records/revert-set.txt.
#
# 995 = 994's switch set VERBATIM + STAGE90_XNU_DYLD_WITNESS=1: the dyld halt-message witness in the
# POOL (`osfmk/arm/trap.c` - `ios7wit_dyld_message` + its call in `sleh_undef`'s `udf #0xdefe` path).
# The ENTRY BIN moves (8b4c9a3e -> ?). ONE tree SOURCE file moves in THIS rung: `osfmk/arm/trap.c`
# (a POOL file; 995 does NOT touch the assembly layer - `osfmk/ipc/mach_kernelrpc.c`,
# `osfmk/arm/ios7_compat.h` and `osfmk/arm/traps_lo.s` are 994's, unchanged). A fresh checkout needs
# ALL SIX stage scripts: tools/stage_d13_kextb_login.sh (988's), tools/stage_d13_sp_align.sh (990's),
# tools/stage_d13_vmhint_guard.sh (991's), tools/stage_d13_commpage_alias.sh (992+993's),
# tools/stage_d13_ios7_traps.sh (994's) and tools/stage_d13_dyld_witness.sh (995's - the rung's
# driver; it calls the udf-retry lane's tools/patch_d13_dyld_witness.py and edits nothing itself).
#
# WHY (994's press, 2026-10-12, arm armed-d13-8b4c9a3e, commit a4a2532, evidence
# out/stage90/captures/cancro-last_kmsg-994-armed-d13-8b4c9a3e.txt): 994 CONFIRMED the trap-10 wire
# fix (`IOS7LAB vm_allocate64 size=00000000:00001000 flags=1 result=0 address=000e1000`, log:4101 -
# `flags=1` is `VM_FLAGS_ANYWHERE`, where the pre-994 read produced 0) and moved the frontier one step
# further: dyld still halts, but now at its REAL error -
#     :4103 onward, 235x: `usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x2beb90c4`
# (slide 0xB8000, i.e. pc 0x2be010c4 = `_dyld_fatal_error` = `fe de ff e7` = `udf #0xdefe`), with the
# register block's r5 = 0x2bedc390 = a heap string in dyld's malloc pool - the message `dyld::halt`
# was handed, which NOTHING on CANCRO can print (the tree's own reader, the LEO witness, is compiled
# out under `#if defined(BOARD_CONFIG_QSD8250_LEO)`).
#
# THE FIX (`tools/stage_d13_dyld_witness.sh`): `osfmk/arm/trap.c` grows `ios7wit_dyld_message`, called
# in `sleh_undef` immediately after the `dyld_all_image_info_addr` printf - the SAME path that prints
# A, inside the `udf #0xdefe` branch. The reader takes the message pointer from the word at
# `task->all_image_info_addr + 0x20` (the field `setAlImageInfosHalt`, dyld 0x2be08d04 =
# `str r0, [r3, #32]`, writes; `dyld::halt` `strlcpy`s `_error_string` into it) with `copyin` +
# `copyinstr`, printing `IOS7WITNESS dyld msg=... copyinstr=... len=...` then `dyld> <text>` in
# 48-byte lines, two print slots at most (claimed BEFORE any I/O), every failure branch printing its
# reason. Fail-soft: every user read is a copyin; no panic, allocation, sleep or lock.
# VERIFIED BY VALUE: tools/check_d13_dyld_witness.py --expect on asserts, off the linked image's own
# disassembly and bytes, that `ios7wit_dyld_message` exists, that `sleh_undef`'s region calls it after
# a `bl printf`, that the witness's region calls `copyin`/`copyinstr`/`printf` and names the `#32`
# field offset, and that the image carries the `IOS7WITNESS dyld msg=`, `dyld> %s` and
# `dyld> <copyinstr failed>` literals; the FROZEN 994 image is passed as `--control` and must FAIL
# its ON mode and PASS its OFF mode (the checker tells the two shapes apart).
#
# PRE-REGISTERED READING: `IOS7WITNESS dyld msg=... copyinstr=0 len=...` followed by `dyld> <text>`
# lines in the first two `udf #0xdefe` dumps, the text naming a REAL dyld error (the candidates by
# value are `recursiveLoadLibraries`'s `Library not loaded: %s`, dyld 0x2be0a35e, or a
# `mach_error_string`, 0x2be09220, produced from the IOS7LAB results).
# FALSIFICATION: no `IOS7WITNESS` header at all => the witness was never called (the switch did not
# reach `osfmk_arm_trap.o`, or the trap is not on the path the reader hangs off); a header with
# `copyinstr=<nonzero>` => `A + 0x20` is not the message pointer in this dyld (fall back to reading
# the pool string at r5, or to a frame walk of the halted thread).
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
# confirmed 994, 993 or the regressed 992 arms under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-8b4c9a3e ]] && { echo "REFUSING: $LIVE holds 994's arm (8b4c9a3e); build 995 first"; exit 1; }
[[ $ARM == armed-d13-1d5cf20e ]] && { echo "REFUSING: $LIVE holds 993's arm (1d5cf20e); build 995 first"; exit 1; }
[[ $ARM == armed-d13-fce6e8dd ]] && { echo "REFUSING: $LIVE holds 992's arm (fce6e8dd) - the one that panicked; build 995 first"; exit 1; }
PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  995: THE DYLD HALT-MESSAGE WITNESS (pool: osfmk/arm/trap.c) IS WIRED INTO sleh_undef.\n' "$ARM"
  printf '# Switch set is 994-s VERBATIM + STAGE90_XNU_DYLD_WITNESS=1; the ENTRY BIN moved (8b4c9a3e -> %s) and\n' "${ARM#armed-d13-}"
  printf '# ONE tree SOURCE file moves in THIS rung: osfmk/arm/trap.c - a POOL file; 995 does NOT touch the\n'
  printf '# assembly layer (osfmk/ipc/mach_kernelrpc.c, osfmk/arm/ios7_compat.h and osfmk/arm/traps_lo.s are\n'
  printf '# 994-s, unchanged; osfmk/arm/arm_vm_init.c and osfmk/arm/pmap.c are 993-s; this rung has NO pcb.c\n'
  printf '# and NO vm_resident.c change). A fresh checkout needs ALL SIX tools/stage_d13_kextb_login.sh,\n'
  printf '# tools/stage_d13_sp_align.sh, tools/stage_d13_vmhint_guard.sh, tools/stage_d13_commpage_alias.sh,\n'
  printf '# tools/stage_d13_ios7_traps.sh and tools/stage_d13_dyld_witness.sh drivers.\n#\n'
  printf '# 994 (pressed 2026-10-12, arm armed-d13-8b4c9a3e, commit a4a2532, evidence\n'
  printf '# out/stage90/captures/cancro-last_kmsg-994-armed-d13-8b4c9a3e.txt) CONFIRMED the trap-10 wire fix:\n'
  printf '# `IOS7LAB vm_allocate64 size=00000000:00001000 flags=1 result=0 address=000e1000` (log:4101) -\n'
  printf '# flags=1 is VM_FLAGS_ANYWHERE, where the pre-994 read produced 0 - and the frontier MOVED one step\n'
  printf '# further: dyld still halts, but now at its REAL error -\n'
  printf '#     :4103 onward, 235x: `usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x2beb90c4`\n'
  printf '# (slide 0xB8000, i.e. pc 0x2be010c4 = _dyld_fatal_error = fe de ff e7 = `udf #0xdefe`), with the\n'
  printf '# register block-s r5 = 0x2bedc390 = a heap string in dyld-s malloc pool - the message dyld::halt\n'
  printf '# was handed, which NOTHING on CANCRO can print: the tree-s own reader (the LEO witness,\n'
  printf '# trap.c:71-126) is compiled out (`#if defined(BOARD_CONFIG_QSD8250_LEO)`).\n#\n'
  printf '# 995: tools/stage_d13_dyld_witness.sh stages ONE pool edit - osfmk/arm/trap.c grows\n'
  printf '# ios7wit_dyld_message, called in sleh_undef immediately after the dyld_all_image_info_addr printf\n'
  printf '# (the SAME path that prints A, inside the `udf #0xdefe` branch; trap.c:877 returns early for\n'
  printf '# every other undefined instruction). The reader takes the message pointer from the word at\n'
  printf '# task->all_image_info_addr + 0x20 (the field setAlImageInfosHalt, dyld 0x2be08d04 =\n'
  printf '# `str r0, [r3, #32]`, writes; dyld::halt strlcpy-s _error_string into it) with copyin + copyinstr,\n'
  printf '# printing `IOS7WITNESS dyld msg=... copyinstr=... len=...` then `dyld> <text>` in 48-byte lines,\n'
  printf '# two print slots at most (claimed BEFORE any I/O - the halt re-executes forever and the console\n'
  printf '# block drops silently past 128 KiB), every failure branch printing its reason. Fail-soft: every\n'
  printf '# user read is a copyin; no panic, allocation, sleep or lock.\n'
  printf '# tools/check_d13_dyld_witness.py --expect on asserts, off the linked image-s own disassembly and\n'
  printf '# bytes, that ios7wit_dyld_message exists, that sleh_undef-s region calls it after a `bl printf`,\n'
  printf '# that the witness-s region calls copyin/copyinstr/printf and names the `#32` field offset, and\n'
  printf '# that the image carries the `IOS7WITNESS dyld msg=`, `dyld> %%s` and `dyld> <copyinstr failed>`\n'
  printf '# literals; the FROZEN 994 image (out/stage90/captures/994-xnu_arm_entry.elf) is passed as\n'
  printf '# --control and must FAIL ON and PASS OFF.\n#\n'
  printf '# READING: `IOS7WITNESS dyld msg=... copyinstr=0 len=...` followed by `dyld> <text>` lines in the\n'
  printf '# first two `udf #0xdefe` dumps, the text naming a REAL dyld error (candidates by value:\n'
  printf '# `Library not loaded: %%s`, dyld 0x2be0a35e, or a mach_error_string, 0x2be09220).\n'
  printf '# FALSIFICATION: no IOS7WITNESS header at all => the witness never ran (the switch did not reach\n'
  printf '# osfmk_arm_trap.o, or the trap is not on the reader-s path); a header with copyinstr=<nonzero> =>\n'
  printf '# A + 0x20 is not the message pointer in this dyld (fall back to the pool string at r5).\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never written -\n'
  printf '# fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=995-dyld-halt-message-witness;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3