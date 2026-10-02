# Experiment 899 — the mount-path markers pin the block in `hfs_getnewvnode`

**Press, autonomous, NO BRICK.** Arm `armed-storage-e4e9324a` SPENT. Capture
`out/stage90/captures/…last_kmsg.txt` (625547 B) — read from `/tmp/cancro-last_kmsg.txt`.

## What the arm was

897 (arm `armed-storage-59ec5fd6`) served exactly ONE strategy read — the HFS+ volume header at
offset 1024 (`xnu_live_rootmedia_strategy_offset=0x400`, `_served=1`) — and the mount never returned,
with userland lost. 898 bounded the block to `hfs_MountHFSPlusVolume` between the VCB fill and the
extents `BTOpenPath` but could not tell WHICH step blocked: the volume-header read, the VCB fill,
`hfs_getnewvnode`, or `BTOpenPath` are all invisible to a static reading if the block is not a fault.

899 adds five live step markers, each `entry_live_write("xnu_live_hfs_stage", N)`, guarded by
`STAGE90_HFS_MOUNT_MARKERS` (compiled only when `STAGE90_XNU_HFS_MARKERS=1`):

| N | site | source |
|---|------|--------|
| 1 | `hfs_mountfs`: after the volume-header `bcopy` | `hfs_vfsops.c` |
| 2 | `hfs_MountHFSPlusVolume`: after the VCB fill | `hfs_vfsutils.c` |
| 3 | `hfs_getnewvnode`: about to return the vnode (`*vpp = vp;`) | `hfs_cnode.c` |
| 4 | `BTOpenPath`: entered (after its declarations) | `hfscommon/BTree/BTree.c` |
| 5 | `hfs_MountHFSPlusVolume`: after the extents `BTOpenPath` returns | `hfs_vfsutils.c` |

`entry_live_write` APPENDS, so the LAST `xnu_live_hfs_stage=` record names the farthest step reached.
The arm was otherwise byte-identical to 897 (`stage90-build-config.txt` unchanged, `6c2b6038…`); the
markers are a pool compile and the payload record is unchanged.

## The answer

```
xnu_live_hfs_stage=0x00000001
xnu_live_hfs_stage=0x00000002
```

**Step 2 was reached; step 3 was not.** The volume-header `bcopy` ran (1), the VCB fill completed
(2), and `hfs_getnewvnode` — the next call, which alone carries step 3 — never returned. The block is
inside the extents `hfs_getnewvnode` call (`hfs_vfsutils.c:~487`). Nothing after: no `_stage=3`, no
`_stage=4`, no strategy read beyond the one at offset `0x400`, and `hfs_mountroot` never returned
(no `hfs_mountroot failed:` print, no `cannot mount root`).

## Why it is a SPIN, not a sleep

The capture carries an instrumented `thread_block` (`xnu_live_block_seq/now/enter/thr/return/result`).
Its **last** record is seq `0x3e` at line 6166 — **before** the markers at lines 8546–8547:

```
6157: xnu_live_block_enter=0x80028c40
6158: xnu_live_block_thr=0x80641b30
6159: xnu_live_block_now=0x075af45f
6160: xnu_live_block_seq=0x0000003e
6164: xnu_live_block_return=0x80028c40
6165: xnu_live_block_result=0x00000000
6166: xnu_live_block_returns=0x0000000e
```

So after step 2 the mount thread entered **no** blocking call. It is **spinning**, not sleeping — the
one blocking construct between step 2 and step 3 (`hfs_chash_getcnode`'s `msleep`, `hfs_chash.c:323`)
was **not** entered. That falsifies 898's "the mount BLOCKS" reading of 897: 897's post-`BSD root`
timers were not a blocked mount beside a live system, they were the **last** timer records before the
spin, and 899's own log has **no** `xnu_live_post_*`, `xnu_live_idle_*` or `xnu_live_seam_post*`
record at all — the spin deadline (6000 ms, `STAGE90_XNU_POST_END_TICKS=115200000`) never arrived.

## What "the extents `hfs_getnewvnode`" means

`hfs_getnewvnode` calls, in order:

1. `hfs_chash_getcnode` (only blocking construct: `msleep`) — **not** entered (no `thread_block`); the
   hash table is initialized (`hfs_chashinit_finish`, `hfs_vfsops.c:1471`, before the mount at 1840),
   and the `M_HFSNODE`/`M_HFSFORK` zones are `KMZ_MALLOC` rows in 4570's own `kmalloc_zones[]`
   (`bsd/kern/kern_malloc.c`), so allocation cannot block;
2. `vnode_create` (`vfs_subr.c:5013`) — no `msleep` in its body; may `printf` (not seen);
3. `hfs_chashwakeup`, `hfs_removehotfile` — system file (extents is `CD_ISMETA`) skips the hot-file
   path;
4. `*vpp = vp;` — step 3, **not reached**.

The spin is therefore in one of (1)–(3)'s bodies: a loop whose exit condition is never met, with IRQs
masked or on the mount thread's own stack. The `thread_block` count stops at seq `0x3e` and the block's
`enter` is `0x80028c40` = `lck_mtx_lock_spin` — the mount thread's last block was the chash spin lock
itself, taken and released normally before the spin.

**The strategy served exactly one read.** No read is issued by `hfs_getnewvnode`'s own body before
`*vpp = vp`; the extents B-tree read would be `BTOpenPath` (step 4), which was never reached. So the
spin does not require I/O: it is CPU-side, inside `hfs_getnewvnode`.

## Cell that rules out a fault

All `xnu_entry_sleh_*` = 0, `abort_entries=0`, `failures=0`, `No errors detected`. A deref of a bad
pointer would have entered `sleh_abort` and been counted; none was.

## Mechanism (built, tracked, re-derivable)

- `tools/hfs_patch_mount_markers.py` — the tracked definition of the edit to the untracked 4570 tree,
  applied by `tools/stage_hfs.sh` §4b; `--list-sites` prints the required markers; every anchor must be
  a whole statement ending in `;` (an anchor mid-call is refused — it put step 5 inside a two-line call
  once, `expected expression`).
- `tools/check_hfs_staged.sh` §4c re-derives the markers and refuses a re-provision that drops one or
  an unguarded one.
- `tools/build_xnu_arm_kernel.sh` — `STAGE90_XNU_HFS_MARKERS=1` defines `STAGE90_HFS_MOUNT_MARKERS` for
  the pool build; off, the markers compile to nothing.

## Goal status

**GOAL NOT MET.** The root still does not mount: `hfs_mountroot` spins inside `hfs_getnewvnode` for the
extents file. The next step is to name the spin site — its PC is not in a `thread_block`, so the next
arm needs a live PC at a bounded tick (the mount thread's own PC, read from the timer path while the
mount thread is not it). The arm is spent; the markers are pinned and re-appliable.