#!/usr/bin/env bash
# Build 989: 988's switch set UNCHANGED, rebuilt with THE CENSUS' RECORDED ADDRESS IDENTITY-CHECKED
# before it is walked. No new switch - this is an entry-image fix, so the arm is the same switch set.
#
# WHY 988 IS NOT THE ONE. 988's press (2026-10-12, commit ed5c3ca, arm armed-d13-707801c4) is a DECISIVE
# SUCCESS: the KEXTB_LOGIN guard took -- no second fault at 0x800b2c90, so `removeKextBootstrap` no longer
# tears down an absent __LINKEDIT. The abort MOVED, which is the pre-registered criterion. The new first
# abort is a data abort at the device-tree census' recorded block, inside `entry_probe_dt_children`
# (`src/entry/entry_trace.c`), which runs from `__wrap_vm_pageout` (kernel_bootstrap's LAST statement):
#     fsr=5, far=0x00010005, pc=0x800c41c0 = _ZN11OSMetaClassD1Ev+0x1c, lr=0x8049e1c0
# `lr` is the return of `bl getChildSetReference` at 0x8049e1bc, i.e. the FIRST call the recorded block
# makes on the recorded address.
#
# ROOT CAUSE (989). The recorded address is 461's `g_dtplane_root` = `xnu_live_dtalloc_ret` = 0xc0b7bd80,
# the object `IODeviceTreeAlloc` returned. At that moment it WAS an `IOService`:
# `xnu_live_cls_vptr=0x804c6714` = `_ZTV9IOService + 8` (cls_site=1). By the census it is something else:
# its `[+12]` is 0x10001, so `getChildSetReference`'s registry-table load dispatched through slot 36 of
# 0x10001 and tail-called into a destructor (`~OSMetaClass`), which dereferenced `[+4]` of 0x10001 and
# faulted at far=0x10005. This is 486's own finding (the doc comment in `entry_stubs.c`): the tree root's
# reference is dropped by `IORegistryEntry::init`'s parent loop and the BLOCK IS REUSED. On 4570 the
# recipient was an `IORootParent` and its walk was benign; on D13 it is not, so the walk must not happen.
#
# THE FIX (entry-side, no switch). `entry_trace.c` gains `entry_dt_recorded_walk` (noinline): it reads
# `[recorded]`, publishes it (`entry_note_dtrecstate` -> `xnu_live_dtrec_obj/_vptr/_same`), and walks the
# recorded address (`entry_xnu_child_count` + `entry_xnu_child_set`) ONLY when `[recorded] ==
# _ZTV9IOService + 8`. The recorded block's class read moves under the same guard. `build_entry.sh` gains
# a by-value clause (989) that REFUSES an image whose probe still walks the recorded address (a claim in a
# comment is not a check).
#
# The switch set is 988's UNCHANGED (window 0x1e400000 = 484 MiB, RESIDENT=1, USB ladder,
# MEM_TOTAL/CARD_TOTAL=1, HFSX root from card, CARD_COW=1, HDD_WRITE=0 = base never written,
# KEXTB_LOGIN=1). This script re-reads the current record and re-exports it verbatim, overriding nothing
# ([[mi4-build-variant-comes-from-an-env-default]]).
set -euo pipefail
cd "$(dirname "$0")/.."

XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu
REC=out/stage90/xnu_arm_entry-config.txt

echo "== 989: identity-check the census' recorded address; rebuild the pool + entry =="

EXTRA=""
# NOTE: `STAGE90_XNU_HFS_ROOT_RW` is NOT here - it is a tree-layout define carried by the ENVIRONMENT
# variable of the same name (see the export below), not by `-D`.
for k in STAGE90_XNU_HFS_ROOT_MEDIA STAGE90_XNU_EMMC_STRATEGY STAGE90_XNU_ROOT_FROM_CARD \
         STAGE90_XNU_HDD_WRITE STAGE90_XNU_CARD_TOTAL STAGE90_XNU_FULL_EXTENT \
         STAGE90_XNU_CARD_COW STAGE90_XNU_MEM_TOTAL STAGE90_XNU_KEXTB_LOGIN; do
  v=$(awk -F= -v k="$k" '$1==k{print $2}' "$REC")
  [[ -z $v || $v == '(unset)' ]] && continue
  EXTRA="$EXTRA -D$k=$v"
done
EXTRA=${EXTRA# }
echo "-- extra defines: $EXTRA --"

# 1. Stage the tree edit (idempotent). 988's __LINKEDIT guard is a POOL object edit
#    (`libkern/c++/OSKext.cpp`), so it must be re-staged or a fresh checkout would compile the teardown
#    back in. This rung adds no tree edit of its own - 989 is entirely in `src/entry/`.
echo "-- re-stage the __LINKEDIT teardown guard (libkern/c++/OSKext.cpp; 988's edit, still needed) --"
tools/stage_d13_kextb_login.sh

echo "-- HFS (the port is on, hfs files join the pool) --"
export STAGE90_XNU_HFS=1

echo "-- arm asm layer (unchanged, still reassembled so the pool is self-consistent) --"
XNU_TREE="$XNU_TREE" XNU_KERNEL_CONFIG=STAGE90_XNU tools/assemble_arm_layer.sh

# Export the switch as an ENV var as well as via `EXTRA`, because `build_xnu_arm_kernel.sh:761` gates
# the define on the environment variable of that name: two spellings of one value would be exactly the
# [[mi4-one-value-two-definitions]] trap (the `-D` in EXTRA and the tree guard could disagree).
export STAGE90_XNU_KEXTB_LOGIN=1
echo "-- STAGE90_XNU_KEXTB_LOGIN=$STAGE90_XNU_KEXTB_LOGIN (pool DEFINES + EXTRA agree) --"
echo "-- the WHOLE D13 kernel pool + platform block (unchanged; 989 is entry-side) --"
# **The 905 B2 rw clear is a TREE-layout define, not a raw one.** `hfs_vfsops.c` is `#if
# STAGE90_HFS_ROOT_RW`, set from the ENVIRONMENT variable `STAGE90_XNU_HFS_ROOT_RW`
# (build_xnu_arm_kernel.sh:720), NOT from `-DSTAGE90_XNU_HFS_ROOT_RW`. Same trap as 987
# ([[mi4-one-value-two-definitions]]): the record's key name and the tree's guard name differ by one
# word, and only the environment path bridges them.
export STAGE90_XNU_HFS_ROOT_RW=$(awk -F= '$1=="STAGE90_XNU_HFS_ROOT_RW"{print $2}' "$REC")
echo "-- STAGE90_XNU_HFS_ROOT_RW=$STAGE90_XNU_HFS_ROOT_RW (the pool's hfs_vfsops.o carries the rw clear) --"
XNU_TREE="$XNU_TREE" \
XNU_KERNEL_CONFIG=STAGE90_XNU \
XNU_MASTER_LOCAL="$PWD/tools/xnu_config/boot/STAGE90_XNU.local" \
XNU_KERNEL_EXTRA_DEFINES="$EXTRA" \
    ./tools/build_xnu_arm_kernel.sh

echo "-- entry image (988's switch set, re-exported verbatim; the 989 guard is compiled from entry_trace.c) --"
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
# The record (988's) carries KEXTB_LOGIN=1 already, but re-export it explicitly for the same reason 988
# did: build_entry.sh's ENTRY_ARM_KEYS read-back must record the SAME value the pool was compiled with
# ([[mi4-one-value-two-definitions]] - the record's key and the -D must not diverge).
EXPORTS+=("STAGE90_XNU_KEXTB_LOGIN=1")
EXPORTS+=("STAGE90_ENTRY_ARM_CHANGE=1")
EXPORTS+=("XNU_TREE=$XNU_TREE")

( cd src/entry && env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}" ./build_entry.sh )

echo "-- payload (embeds the new entry blob; preflight REFUSES a payload carrying a different image) --"
XNU_TREE="$XNU_TREE" STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

echo "== result =="
printf 'entry bin sha (first 8): '
sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8
echo "   988 was 707801c4 - a DIFFERENT value here means the guard moved the image."
echo "-- by value: the recorded address is gated (entry_dt_recorded_walk) and the probe walks only the root --"
GCR='_ZNK15IORegistryEntry20getChildSetReferenceEPK15IORegistryPlane'
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf 2>/dev/null |
  awk -v gcr="$GCR" '
    /<entry_probe_dt_children>:/ { f="probe"; next }
    /<entry_dt_recorded_walk[^>]*>:/ { f="walk"; next }
    /^[0-9a-f]+ </                 { f="" }
    f && $0 ~ ("<" gcr ">")         { refs[f]++ }
    END { printf "   probe getChildSetReference refs = %d (want 2, the tree root)\n", refs["probe"]+0
          printf "   walk  getChildSetReference refs = %d (want 2, the gated recorded address)\n", refs["walk"]+0 }'
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf 2>/dev/null | awk '
    $3=="_ZTV9IOService" { tv=strtonum("0x"$1)+8 }
    $3=="g_dtplane_root_vptr" { gv="0x"$1; gva=strtonum("0x"$1) }
    END { printf "   _ZTV9IOService+8 = 0x%x (the word an IOService carries; 988 was 0x804c6714)\n", tv
          printf "   g_dtplane_root_vptr = %s (the MEASURED word the guard compares [recorded] against - entry_note_dtplane fills it from the live object)\n", gv }'
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf 2>/dev/null | awk '
    /<entry_dt_recorded_walk[^>]*>:/ { f=1; next }
    f && /^[0-9a-f]+ </ { f=0 }
    f && ($3=="movw"||$3=="movt") { printf "   guard address materialization: %s %s %s\n", $3, $4, $5 }' | head -2
