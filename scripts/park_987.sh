#!/usr/bin/env bash
# Park 987 (armed-d13-2df6333a) and append its block to records/revert-set.txt.
#
# 987 = 986's switch set UNCHANGED, rebuilt with THE HFS MALLOC_ZONE REDIRECT GATED TO 4570. The
# ENTRY BIN moved (2171f0d1 -> 2df6333a) and the WHOLE D13 pool was rebuilt (the fix is a pool edit).
#
# WHY (986's press, 2026-10-12, arm armed-d13-2171f0d1, commit bcd4039): 986 is a DECISIVE SUCCESS for
# the mutex fix - the boot leapt from 985's stop in IOKitInitializeTime all the way past EVERY BSD-init
# line to `bsd_init: vfs_mountroot` -> `hfs_mountfs` -> `hfs_chash_getcnode`, and it READ A BLOCK OFF
# THE CARD (HFSX volume header at LBA 0x400002, w0=0x05005848 'HX' + 0x100 -> HFSX 0x4858 v5). The
# lost-wakeup fix HELD. The boot then stopped with `stub_hit`: name_ptr=0x80561fec -> "__MALLOC",
# caller=0x801c90b4 -> `hfs_chash_getcnode+0x100`, right after `bl 8049bbc4 <__MALLOC>`.
#
# ROOT CAUSE (987): a 4570-only necessity applied unconditionally, and actively WRONG on D13.
# `src/shims/hfs/hfs_port_force.h` §3 rewrites the HFS `MALLOC_ZONE`/`FREE_ZONE` onto
# `__MALLOC`/`_FREE`. That exists because 4570's `kern_malloc.c` cannot see `struct cnode` (no
# `<hfs/hfs_cnode.h>` include), so its `kmzones[]` rows 75/76/77/91/92/95 are literal `{0,KMZ_MALLOC}`
# and `__MALLOC_ZONE` PANICS at the first cnode allocation (experiment 900). The Darwin-13 tree's
# `kern_malloc.c` DOES include `<hfs/hfs_cnode.h>` (line 100) and its rows are the real
# `{SOS(cnode),KMZ_CREATEZONE}`..`{SOS(journal),...}` with HFS=1 and JOURNALING=1 - so D13 serves every
# HFS type through its OWN zone path. And the redirect is worse than unneeded: 4570 declares
# `__MALLOC(size,type,flags,vm_allocation_site_t *)` (4-arg); D13 declares `_MALLOC(size,type,flags)`
# (3-arg, kern_malloc.c:551) and its `MALLOC` macro targets `_MALLOC`. So `__MALLOC` names a symbol the
# D13 pool never defines, the linker turns it into a stub (`xnu_arm_entry_undef.txt:14`), and the boot
# dies at the mount's first allocation.
#
# THE FIX: gate §3 on `STAGE90_HFS_MALLOC_ZONE`, defined iff the tree's own `kern_malloc.c` does NOT see
# `hfs_cnode.h` (i.e. 4570). With the guard off the tree's own `MALLOC_ZONE`/`FREE_ZONE` stand and the
# HFS objects call the real `_MALLOC_ZONE`. Same authoring rule as the adjacent `STAGE90_HFS_KMEM_TAG`
# gate: judge the tree by the definition it carries.
#
# READ: the boot moves PAST `hfs_chash_getcnode`'s first allocation (a new stop, or further: the HFS
# mount's later stages, extents, `/sbin/launchd`) => the redirect was the frontier. FALSIFICATION: the
# same `stub_hit` recurs => the mechanism is not the (or not the only) cause.
#
# VERIFIED BY VALUE: `bsd_hfs_hfs_chash.o` references `_MALLOC_ZONE` (was `__MALLOC`); NO
# `bsd_hfs*.o` references `__MALLOC`; the linked `xnu_arm_entry.elf` has a real `_MALLOC_ZONE` at
# 0x80117008 and NO `__MALLOC` symbol; `xnu_arm_entry_undef.txt` no longer lists `__MALLOC`.
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible. fastboot boot only, NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

ARM=armed-d13-2df6333a
LIVE=out/stage90
PARK=$LIVE/frozen/$ARM
REC=records/revert-set.txt
MEMBERS=(xnu_arm_entry.bin xnu_arm_entry.elf xnu_arm_entry.h xnu_arm_entry-config.txt \
         xnu_arm_entry-sources.txt stage90.bin stage90.elf stage90.img stage90-qcdt.img \
         stage90_fixture.macho stage90-build-config.txt SHA256SUMS.txt)

[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  987: 986 + the HFS MALLOC_ZONE redirect GATED TO 4570 (D13 uses its own _MALLOC_ZONE).\n' "$ARM"
  printf '# Switch set is 986-s UNCHANGED; the ENTRY BIN moved (2171f0d1 -> 2df6333a) and the WHOLE pool\n'
  printf '# was rebuilt (the fix is a pool edit: the HFS objects must stop referencing __MALLOC).\n#\n'
  printf '# 986 (pressed 2026-10-12, arm armed-d13-2171f0d1, commit bcd4039) is a DECISIVE SUCCESS for\n'
  printf '# the mutex fix: the boot leapt from 985-s IOKitInitializeTime stop past EVERY BSD-init line to\n'
  printf '# `bsd_init: vfs_mountroot` -> `hfs_mountfs` -> `hfs_chash_getcnode`, and READ A BLOCK OFF THE\n'
  printf '# CARD (HFSX volume header at LBA 0x400002, w0=0x05005848 -> HFSX 0x4858 v5). It then stopped\n'
  printf '# with `stub_hit=__MALLOC` from `hfs_chash_getcnode+0x100` (0x801c90b4).\n#\n'
  printf '# ROOT CAUSE: `hfs_port_force.h` §3 redirects the HFS MALLOC_ZONE/FREE_ZONE onto __MALLOC/_FREE - a\n'
  printf '# 4570-only necessity (4570-s kern_malloc.c cannot see struct cnode, so its kmzones rows are\n'
  printf '# {0,KMZ_MALLOC} and __MALLOC_ZONE PANICS, 900). D13-s kern_malloc.c DOES include hfs_cnode.h and\n'
  printf '# its rows are real KMZ_CREATEZONE rows, so the redirect is unneeded - and WRONG, because D13 names\n'
  printf '# the allocator `_MALLOC` (3-arg), not 4570-s `__MALLOC` (4-arg): the redirect names a symbol the\n'
  printf '# D13 pool never defines, the linker stubs it, and the boot dies at the mount-s first allocation.\n#\n'
  printf '# 987: gate §3 on STAGE90_HFS_MALLOC_ZONE, defined iff the tree-s kern_malloc.c does NOT see\n'
  printf '# hfs_cnode.h (i.e. 4570). With the guard off, D13-s own MALLOC_ZONE/FREE_ZONE stand and the HFS\n'
  printf '# objects call the real _MALLOC_ZONE. VERIFIED: no bsd_hfs*.o references __MALLOC; the linked\n'
  printf '# image has a real _MALLOC_ZONE at 0x80117008 and no __MALLOC symbol.\n#\n'
  printf '# READ: the boot moves PAST hfs_chash_getcnode-s first allocation => the redirect was the frontier;\n'
  printf '# the same stub_hit recurs => the mechanism is not the (or not the only) cause.\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=987-d13-hfs-malloc-zone-redirect-gated;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3