# 898 — 897's gap, read host-side: the walk reaches HFS, the converters are safe, and the mount BLOCKS (the system stays live)

**A HOST-SIDE READ. No device, no press.** It answers 897 §6's next question ("what lies between the
volume-header read and the next read") from the **linked artifact** plus 4570 source. It retires two
wrong chains — one from a pre-compaction reading of 897, one from this session's own first draft.

## 1. RETIRED: the descriptor table and the walk are CORRECT; `hfs_init` RUNS

Two false leads are dead:

- **"`hfs_vfsops` is shifted"** — no. `hfs_vfsops` is 80 bytes (20 pointers × 4); both 2050 and 4570
  `struct vfsops` are 20 slots, and 4570 only appends `vfs_ioctl`/`vfs_vget_snapdir`/`vfs_reserved`
  *after* the 13 named members, which sit at 0..12 in both. The object bytes prove it: slot 10 at
  `0x8061b404` = `0x802c36c0` = `hfs_init`. The earlier names (`hfs_fhtovp` at slot 8) came from
  misattributing symbols.
- **"no `vfs_init`, so `hfs_init` never runs"** — a name confusion. The entry image has no symbol
  named `vfs_init`; the vfsconf walk lives in **`vfsinit(void)`** (`vfs_init.c:326`), and **`bsd_init`
  calls it** (`bsd_init.c:735`, `bl 801d3930 <vfsinit>` at `0x80050a38`).

The walk itself (`vfs_init.c:428-458`): `for (vfsp = vfsconf, i=0; i<maxvfsslots; i++, vfsp++)`, and
`if (vfsp->vfc_vfsops == NULL) break;` — it breaks at the **first** NULL row and otherwise calls
`(*vfsp->vfc_vfsops->vfs_init)(&vfsc)`. The linked `vfstbllist` (`0x80626f30`, stride 0x40 = 64) reads:

| slot | `vfc_vfsops` | name | `vfc_mountroot` |
|---|---|---|---|
| 0 | `0x806188c8` devfs | devfs | — |
| **1** | **`0x8061b3dc` hfs_vfsops** | **hfs** | **`0x802bbd40` hfs_mountroot** |
| 2 | `0x8061fab4` mockfs | mockfs | mockfs_mountroot |
| 3 | `0x8061fc2c` routefs | routefs | — |
| 4,5 | `NULL` | `<unassigned>` | — |

No NULL before HFS ⇒ the walk calls devfs's and HFS's `vfs_init`. **`hfs_init` runs.** It is a real
function (172 B, `0x802c36c0`, HFS=1, no stub — the `#else` stubs in `hfs_encodings.c` are not compiled).
`hfs_init` calls `hfs_converterinit()`, which does `MALLOC` + `SLIST_INSERT_HEAD` and then
`SLIST_FIRST(...)->refcount++` — safe, because it inserted.

## 2. RETIRED: the converters are NOT the stop

`hfs_getconverter` (`hfs_encodings.c:166`) — the one `hfs_mountfs:1834` calls before the HFS+ mount —
is `lck_mtx_lock; SLIST_FOREACH(...) { if match ... }; lck_mtx_unlock; if (!found) { *get_unicode = NULL;
return EINVAL; }`. **`SLIST_FOREACH` over an empty list does nothing; it does not deref the first node.**
The earlier "empty-list NULL deref at the first node" is wrong: there is no unconditional deref in
`hfs_getconverter`. Even if the list were empty (and it isn't), the call returns EINVAL, which
`hfs_mountfs` ignores (`(void) hfs_getconverter(...)`), and the mount proceeds. **Converters are cleared.**

## 3. The stop: the mount BLOCKS — the system is alive

The single 896 strategy call served the volume header (`dev=0x05000000`, `blkno=2`, `offset=0x400`) and
**completed cleanly** — `st_media_served` went 0→1, `st_medium_refused` 1→0, which the strategy does only
*after* `buf_map`/`bcopy`/`buf_setresid`/`buf_biodone`. So `hfs_mountfs`'s volume-header `buf_meta_bread`
returned. No second strategy call followed.

**It is not a hang.** The 896 capture does not stop at `BSD root: md0`; after it the run continues with
a full block of **live** records — `xnu_live_slot_tb_calls` climbing, `xnu_live_tmr_setup_seq` 1→6,
`xnu_live_irq_armed=1`, `xnu_live_gic_*`, `xnu_live_tmr_enter_seq=1`, `xnu_live_dec_*`. **Timers and
interrupt paths keep executing**, so the mount thread is **blocked/asleep**, not dead-looping, and the
kernel can still print — it simply prints nothing, because `HFS_MOUNT_DEBUG=0` and no error is reached.

**And `vfs_mountroot` proves the mount never returned.** Its loop (`vfs_subr.c:1069-1177`) calls the
row's `vfc_mountroot`; on error it prints `"%s_mountroot failed: %d"` for every error **except EINVAL**,
and on EINVAL (or empty rows) returns `ENODEV` → `bsd_init` prints `cannot mount root` and retries. The
896 console shows **one** `BSD root` and **no** `hfs_mountroot failed:` and **no** `cannot mount root`.
So `(*hfs_mountroot)(...)` **did not return** — the block is inside `hfs_mountroot` → `hfs_mountfs` →
`hfs_MountHFSPlusVolume`, after the volume-header read and before the first btree read.

Location: `hfs_MountHFSPlusVolume` (`hfs_vfsutils.c:311`) verifies the header (sig `H+`, version 4,
blockSize 4096, dirty check — all pass on the fsck-clean blob, attributes `0x80000100` = Unmounted set),
fills the VCB, then sets up the **extents** vnode (`hfs_getnewvnode`, `:487`) and opens it
(`BTOpenPath`, `:498`) — the open is the read that would be `blkno=16` (extents header node at byte
8192). The block is between `:441` (VCB fill, no I/O) and `:498`.

The prime suspects are the port's own additions in that window: `hfs_getnewvnode`
(`hfs_cnode.c:957-1500`) — `hfs_chash_getcnode`, `MALLOC_ZONE(fp, ..., M_HFSFORK, M_WAITOK)`,
`vnode_create`, `hfs_valid_cnode` — and, given §4, the `M_HFS*` malloc types themselves.

## 4. What the M_HFS* collision can and cannot cause (`mi4-one-value-two-definitions`)

`kmzones[]` (`kern_malloc.c:320`), read for the port's five types:

- 75 `M_HFSMNT`, 76 `M_HFSNODE`, 77 `M_HFSFORK`: **`KMZ_MALLOC`** ⇒ a `MALLOC(...)` uses the general
  kmem allocator (waits for pages). Not inherently a block.
- 95 `M_HFSDIRHINT`: `KMZ_MALLOC`. ✓ correct.
- **96 `M_HFSBITMAP`: `SOS(cl_readahead)` + `KMZ_CREATEZONE`** — a **wrong row**. 4570 defines
  `M_CLRDAHEAD = 96`; the port's `#define M_HFSBITMAP 96` collides. Found by comparing the port's
  claimed source (`2050 bsd/sys/malloc.h:189`) against 4570: **neither 2050:189 nor 4570:189 is
  `M_HFSBITMAP`** — 4570:189 is `M_CLRDAHEAD`. The define is **spurious**: `grep M_HFSBITMAP` over the
  staged HFS tree finds **zero** call sites (and 2050's malloc.h never declares it either). So it
  allocates nothing today and cannot cause the block — but it is a live loaded gun: any future
  `MALLOC(x, sizeof(struct cl_readahead), M_HFSBITMAP, M_WAITOK)` would run `kmz_malloc` on a
  `KMZ_CREATEZONE` row of element-size `sizeof(cl_readahead)`, and a wrong-size class is how a
  `.kz_zalloczone` element-size check misfires. Fix: delete the define (or point it at a genuinely free
  type); it is caught by the port's own rule (a value with two definitions).

The name-table check in `kern_malloc`'s `_zone_init` is not the block either: rows 75/76/77/95/96 all
carry **non-NULL** `memname` strings ("unused"/"cluster_read"), so the `NULL`-name panic cannot fire.

## 5. Consequence and the next step

The goal's storage clause stays **NOT MET**. The scope is now sharp and cheap to finish:

- **Not owed**: any change to `hfs_vfsops`, the row, or `hfs_init`/the converters (all correct/reached).
- **Not owed**: a blob repair (the volume is fsck-clean; its btrees are inside range).
- **Owed (host-side, next build)**: a **live step marker** in the mount path — `entry_live_write` at
  (a) `hfs_mountfs` after the header `bcopy`, (b) `hfs_MountHFSPlusVolume` after the VCB fill, (c) after
  the extents `hfs_getnewvnode`, (d) at the extents `BTOpenPath` entry — so the next press reports the
  **last step reached** instead of "one volume-header read, then silence". The step marker is the only
  way to tell a block in `hfs_getnewvnode` from a block before its read from a block in `BTOpenPath`.

**Falsification:** if a press with these markers reports step (c) reached and `(d)` not reached, the
block is `hfs_getnewvnode`/`vnode_create` (a port malloc/vnode concern); if it reports (d) but no
`blkno=16` strategy record, `buf_meta_bread`'s wait is the block (the strategy's completion path); if it
reports neither (a) nor (b), the block is in the `hfs_mountfs` prologue above the VCB fill.

**Also owed (cheap, independent):** delete `#define M_HFSBITMAP 96` from `src/shims/hfs/hfs_port_force.h`
(§4) — a spurious, colliding value with no caller.

See `docs/experiments/experiment-897-the-press-of-the-hfs-port-arm.md` and
`docs/experiments/experiment-896-the-hfs-port-links-into-the-entry-image.md`.