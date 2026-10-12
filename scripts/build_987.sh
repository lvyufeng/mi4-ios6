#!/usr/bin/env bash
# Build 987: 986's switch set UNCHANGED, rebuilt with THE HFS MALLOC_ZONE REDIRECT GATED TO 4570.
#
# WHY 986 IS NOT THE ONE. 986's press (2026-10-12, commit bcd4039, arm armed-d13-2171f0d1) is a
# DECISIVE SUCCESS for the mutex fix: the boot leapt from 985's stop in IOKitInitializeTime (no
# cal_enter, no ubc_init) all the way past EVERY BSD-init line to `bsd_init: vfs_mountroot` ->
# `hfs_mountfs` -> `hfs_chash_getcnode`, and it READ A BLOCK OFF THE CARD (HFSX volume header at LBA
# 0x400002: w0=0x05005848 'HX' + 0x100 -> HFSX 0x4858 v5). The lost-wakeup fix HELD. The boot then
# stopped with a `stub_hit`: `xnu_live_stub_hit_name_ptr=0x80561fec` -> "__MALLOC",
# `xnu_live_stub_hit_caller=0x801c90b4` -> `hfs_chash_getcnode+0x100`, right after
# `bl 8049bbc4 <__MALLOC>`. `entry_stub_hit` ends in the noreturn `entry_epilogue`, so this is terminal.
#
# ROOT CAUSE (987). A 4570-only necessity, applied unconditionally, that is actively wrong on D13.
# `src/shims/hfs/hfs_port_force.h` §3 rewrites the HFS `MALLOC_ZONE`/`FREE_ZONE` onto
# `__MALLOC`/`_FREE`. That exists because 4570's `kern_malloc.c` cannot see `struct cnode` (it does NOT
# include `<hfs/hfs_cnode.h>`), so its `kmzones[]` rows 75/76/77/91/92/95 are the literal
# `{0, KMZ_MALLOC}` and `__MALLOC_ZONE` PANICS (`_malloc_zone ZONE`) at the first cnode allocation
# (experiment 900). The Darwin-13 tree's `kern_malloc.c` DOES `#include <hfs/hfs_cnode.h>` (line 100)
# and its rows are the real `{SOS(cnode),KMZ_CREATEZONE,TRUE}` / `{SOS(filefork),...}` /
# `{SOX(hfsmount),KMZ_LOOKUPZONE}` / `{SOS(journal),...}`, with `HFS 1` and `JOURNALING 1` in the
# options - so D13 serves every HFS type through its OWN zone path and the redirect is not owed.
# And it is WORSE than unneeded on D13: the two trees name the allocator differently. 4570 declares
# `__MALLOC(size, type, flags, vm_allocation_site_t *)` (4-arg); D13 declares `_MALLOC(size,type,flags)`
# (3-arg, kern_malloc.c:551) and `MALLOC`'s own macro targets `_MALLOC`. So the redirect's `__MALLOC`
# names a symbol the D13 pool never defines; the linker turns it into one of
# `out/stage90/xnu_arm_entry_undef.txt`'s stub bodies (`__MALLOC` is line 14 there) and the boot dies
# at the mount's first allocation. This is `mi4-one-value-two-definitions`: 4570's name used as D13's.
#
# THE FIX. Gate §3 on a build guard, `STAGE90_HFS_MALLOC_ZONE`, defined iff the tree's own
# `kern_malloc.c` does NOT see `hfs_cnode.h` (i.e. 4570). With the guard off, `hfs_port_force.h`
# leaves the tree's own `MALLOC_ZONE`/`FREE_ZONE` (D13's `_MALLOC_ZONE`, which reaches the live zone
# rows) standing. Same authoring rule as the adjacent `STAGE90_HFS_KMEM_TAG` gate: judge the tree by
# the definition it actually carries, not by which tree the port was written against.
#
# The switch set is 986's UNCHANGED (window 0x1e400000 = 484 MiB, RESIDENT=1, USB ladder,
# MEM_TOTAL/CARD_TOTAL=1, HFSX root from card, CARD_COW=1, HDD_WRITE=0 = base never written). This
# script re-reads the current record and re-exports it verbatim, overriding nothing
# ([[mi4-build-variant-comes-from-an-env-default]]).
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 987: gate the HFS MALLOC_ZONE redirect to 4570; rebuild the WHOLE pool + arm asm + entry =="

EXTRA=""
# NOTE: `STAGE90_XNU_HFS_ROOT_RW` is NOT here - it is a tree-layout define carried by the ENVIRONMENT
# variable of the same name (see the export below), not by `-D`; passing `-DSTAGE90_XNU_HFS_ROOT_RW`
# would name a macro no source reads and leave the pool's `hfs_vfsops.o` with the clear compiled out.
for k in STAGE90_XNU_HFS_ROOT_MEDIA STAGE90_XNU_EMMC_STRATEGY STAGE90_XNU_ROOT_FROM_CARD \
         STAGE90_XNU_HDD_WRITE STAGE90_XNU_CARD_TOTAL STAGE90_XNU_FULL_EXTENT \
         STAGE90_XNU_CARD_COW STAGE90_XNU_MEM_TOTAL; do
  v=$(awk -F= -v k="$k" '$1==k{print $2}' "$REC")
  [[ -z $v || $v == '(unset)' ]] && continue
  EXTRA="$EXTRA -D$k=$v"
done
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

echo "-- HFS (the port is on, hfs files join the pool) --"
export STAGE90_XNU_HFS=1

echo "-- arm asm layer (unchanged hw_lock.s, still reassembled so the pool is self-consistent) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

echo "-- the WHOLE D13 kernel pool + platform block (the HFS objects must stop referencing __MALLOC) --"
# **The 905 B2 rw clear is a TREE-layout define, not a raw one.** `hfs_vfsops.c:186` is `#if
# STAGE90_HFS_ROOT_RW`, and the build sets that from the ENVIRONMENT variable `STAGE90_XNU_HFS_ROOT_RW`
# (build_xnu_arm_kernel.sh:720) - NOT from `-DSTAGE90_XNU_HFS_ROOT_RW`, which no source reads. The
# entry-record key is `STAGE90_XNU_HFS_ROOT_RW=1` (986's arm has it), so it must be exported here too,
# or the rebuilt pool's `hfs_vfsops.o` has the clear compiled OUT and `build_entry.sh` REFUSES (the
# guard-vs-key clause: an arm that promises a writable root whose pool cannot deliver one). This is
# exactly the [[mi4-one-value-two-definitions]] trap: the record's key name and the tree's guard name
# differ by one word, and only the environment path bridges them.
export STAGE90_XNU_HFS_ROOT_RW=$(awk -F= '$1=="STAGE90_XNU_HFS_ROOT_RW"{print $2}' "$REC")
echo "-- STAGE90_XNU_HFS_ROOT_RW=$STAGE90_XNU_HFS_ROOT_RW (the pool's hfs_vfsops.o carries the rw clear) --"
XNU_TREE="$XNU_TREE" \
XNU_KERNEL_CONFIG=STAGE90_XNU \
XNU_MASTER_LOCAL="$PWD/tools/xnu_config/boot/STAGE90_XNU.local" \
XNU_KERNEL_EXTRA_DEFINES="$EXTRA" \
    ./tools/build_xnu_arm_kernel.sh

echo "-- entry image (986's switch set, re-exported verbatim) --"
EXPORTS=()
while IFS= read -r line; do
  line=${line%%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  k=${line%%=*}; v=${line#*=}
  [[ $v == '(unset)' ]] && continue
  case $k in
    STAGE90_XNU_ENTRY_SHA256|STAGE90_XNU_ENTRY_BYTES) continue ;;
  esac
  EXPORTS+=("$k=$v")
done < "$REC"
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   986 was 2171f0d1 - a DIFFERENT value here means the redirect fix moved the image."
echo "-- by value: is ANY hfs pool object still referencing __MALLOC? (must be EMPTY) --"
for o in out/xnu_kernel_obj_d13/bsd_hfs*.o; do
  arm-none-eabi-nm "$o" 2>/dev/null | awk -v o="$o" '/__MALLOC$/{print "  STILL UNDEREFINED: "o" "$3}'
done
echo "-- and in the LINKED image: xnu_arm_entry_undef must no longer list __MALLOC --"
grep -n '^__MALLOC$' out/stage90/xnu_arm_entry_undef.txt || echo "  __MALLOC is NOT a stub any more (good)"
echo "-- by value: the linked __MALLOC stub body should be GONE (no 'b entry_stub_hit' under <__MALLOC>) --"
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf 2>/dev/null | awk '/<__MALLOC>:/{f=1;n=0} f&&n<8{print "  "$0;n++} n==8{exit}'