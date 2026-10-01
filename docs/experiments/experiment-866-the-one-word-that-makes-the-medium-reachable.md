# 866 — THE ONE WORD THAT MAKES THE MEDIUM REACHABLE

Arm `armed-storage-<sha>`, `STAGE90_XNU_STORAGE_PROBE=59` (**VALUE 59 = ORDINAL RUNG 60**) **with
`STAGE90_XNU_MOUNT=1`**. A build and a park; **no device touched, nothing pressed, nothing armed.**
**THE PRESS IS THE OPERATOR'S; the goal is NOT met.**

In one paragraph: 864 built the coupling and 865 found that **nothing ever reaches it.** `mockfs_mountroot`
asks the root device `DKIOCGETMEMDEVINFO`; while that answer carries `mi_mdev = 1`, mockfs maps its one file
node **straight onto raw memory** and `st_media_strategy` never runs. Rung 60 sets **`mi_mdev = 0`** — one
word, one field — and the file node's pages must then come through `cluster_pagein` → `mockfs_strategy` →
`buf_strategy` → `spec_strategy` → `st_media_strategy`. **The call still SUCCEEDS and the base and size are
still filled**, which is the whole safety argument: the device declines the *optimisation* and nothing else.
And the branch cannot lose the exec, because **both branches read the same bytes** — the strategy serves
disk 0 from `g_stage90_ramdisk`, which is the very address `mi_base` names.

## 1. The finding this rung acts on (865's, restated as a build)

865 §1 read the path:

```
mockfs_mountroot (mockfs_vfsops.c:101)
  -> VNOP_IOCTL(rvp, DKIOCGETMEMDEVINFO, &mi, ...)   sets mockfs_memory_backed = mi_mdev,
                                                     mockfs_memdev_base = mi_base
  -> mockfs_fsnode.c:333-344  (inside `if (mockfs_mnt->mockfs_memory_backed)`)
       pager_map_to_phys_contiguous(ubc_getobject(vp,0), 0, mi_base << PAGE_SHIFT, fsnp->size)
```

**`mi_base << PAGE_SHIFT` is the file's offset 0.** There is no filesystem in the path. That is experiment
468's design and the reason process 1 is exec-able; it is also, exactly, the reason the medium 862/864 built
is never read. 865 §4 stated the experiment and declined to build it, on two named risks. This rung builds
the shape that does not carry either risk.

## 2. Why `mi_mdev = 0` and not the two alternatives

There are three shapes that would move mockfs off the memory-backed branch, and only one is safe.

| shape | what it does | why not / why it is the arm |
| --- | --- | --- |
| **drop `DKIOCGETMEMDEVINFO`** | the ioctl returns `ENOTTY` | `mockfs_mountroot` takes its memory-backed setup **inside `if (!VNOP_IOCTL(...))`** — an `ENOTTY` leaves `mockfs_memdev_base` at zero for a root that has no other source, so the run stops at the **mount**. (865 §4 risk 2.) |
| **`mi_phys = 1`** | mockfs skips the branch (`mockfs.h:64-67`'s fields are only meaningful inside it) | the same breakage by a different field: the setup that fills base/size is skipped with the branch |
| **`mi_mdev = 0`, call still succeeds** | only the *optimisation* is declined; base and size are still filled | **this is the arm.** The two arms differ in **one word** rather than two. |

**The call still returns 0.** The only value this rung moves is the one the branch is decided on.

## 3. Why the branch it takes cannot lose the exec

This is the load-bearing claim, and it is a fact about *which bytes each branch serves* rather than a hope.

`mockfs_strategy` (`mockfs_vnops.c:268`) passes the buffer to `vfs_devvp(...)` by `buf_strategy`;
`mockfs_blockmap` (`mockfs_vnops.c:356`) computes `*bpn = foffset / blksize` with
`blksize = vp->v_mount->mnt_devblocksize`, and `mnt_devblocksize` comes from the device's
`DKIOCGETBLOCKSIZE`. **This device answers 512** (`entry_root_media_register` sets
`st_media_blocksize[disk] = ST_MEDIA_BLOCKSIZE = 512u`). So at the strategy layer
`b_blkno = floor(f_offset / 512)`, and the strategy's own arithmetic

```c
off = (uint64_t)(uint32_t)buf_blkno(bp) * ST_MEDIA_BLOCKSIZE;   /* == f_offset */
```

recovers **exactly** `f_offset`. And the base it adds is `st_medium_disk_base(0) == g_stage90_ramdisk` —
**the same array `mi_base` names**, since `st_media_memdev_info` fills `mi_base` from
`(uintptr_t)g_stage90_ramdisk >> 12` and the pager shifts it back. A file byte at offset N therefore comes
from **the same address** whether the pager mapped it or the strategy copied it, and `exec_mach_imgact`
sees the same `MH_MAGIC` either way.

**That is what separates this arm from the one 865 §4 refused to build.** The risk there was that the file
node's bytes would become *somebody else's sector* — `st_medium_virt` holding a partition's superblock,
i.e. mockfs's own TODO (`mockfs_vfsops.c:75-78`) about "EBADMACHO panics further along the boot path". The
fix is not to avoid the strategy; it is to **point it at the bytes the root already had**, which is what
`st_medium_disk_base`/`st_medium_disk_bytes` do.

## 4. What changed, in the module

`src/platform/stage90_root_media.c`:

- **`st_media_memdev_info`: `info->mi_mdev = 0;` unconditionally.** The arm, one word.
- **The bounds test moved above the `mi_phys` store.** 862 wrote `st_media_flags[unit]` before testing
  `unit >= ST_MEDIA_DISKS`, indexing a two-entry array with an out-of-range unit. In practice `unit` is a
  number this device handed out — but "the caller never does that" is the reasoning that lets an
  out-of-range read survive until a caller does.
- **`st_medium_disk_base` / `st_medium_disk_bytes`** replace the per-unit size array the first draft used:
  one rule (staged ⇒ `st_medium_virt` / `ST_MEDIA_BLOCKSIZE`; else the RAM disk / `st_media_bytes()`), so
  the base and the length cannot disagree about which medium they describe. `length == blockcount ×
  blocksize` is then true **by construction** for the staged unit.
- **`st_media_strategy`** now serves from those two accessors, publishes `xnu_live_rootmedia_strategy_medium`
  (0 = RAM disk, 1 = staged) derived from the same `st_medium_staged` cell `DKIOCGETMEMDEVINFO` reads, and
  keeps its three refusals (`ENXIO` no medium, `EROFS` past the medium, `EINVAL` unknown shape) with
  `_rootmedia_served`/`_rootmedia_refused` published on every call.

`src/entry/entry_storage.c`: the ladder bound `> 58` → `> 59`, plus the `#error` paragraph.

## 5. The build clause — and the two defects it caught in itself

`xnu_entry_866` reads **the link, not the source** (`[[mi4-a-claim-in-a-comment-is-not-a-check]]`), and it
reads **one function's body**, so its property is layout-independent
(`[[mi4-linked-code-order-is-not-source-order]]`).

**The first draft was wrong in both directions, and the build proved it.** It asked whether the constant `1`
appeared **anywhere** in `st_media_memdev_info`'s body, on the reasoning that 862's answer was the one that
carried it. But:

- **(a) it would REFUSE A CORRECT BODY.** `mi_size` is a *page* count with a floor of one
  (`(st_medium_pages != 0u) ? st_medium_pages : 1u`), so GCC emits a `#1` in the staged branch that has
  nothing to do with `mi_mdev`.
- **(b) it MISSED the encoding this compiler actually uses.** The instruction is **`movweq r2, #1`**, and a
  pattern listing `movw` does not match `movweq` — there is no boundary before `eq`. So the check would
  have **accepted 862's answer while the body's `#1` sat right there**.

The repair is to **anchor the check to the store itself**: find the body's offset-0 store (`str rX, [rY]` —
`mi_mdev` is the first field of `dk_memdev_info_t`, `bsd/sys/disk.h:291`), then find the nearest preceding
instruction that **writes X**, and require it to be `mov X, #0`. 862's answer has `mov r2, #1` in that
position and is refused; the size floor is above the store and is never consulted.

A third defect was mechanical: the `awk` that does the anchoring was written without its `<<<"$mi_body"`
input redirection, so it read its own stdin and returned empty — the build **refused, correctly, and for the
wrong reason**. Both repairs are verified in both directions against the real linked body.

## 6. What this changes, and what it does not

- **THE PARK BACKLOG IS NOT TOUCHED.** Rungs 57 (`armed-storage-71b54d73`), 58 (`armed-storage-b00b87bb`)
  and 59 (`armed-storage-7189e9b`) stay **parked and unpressed**. This document spends none of them.
- **Nothing is claimed about the unpressed arms.** Whether the strategy serves anything is a question only a
  press answers.
- **The XNU eMMC driver clause is still open.** This rung makes a *synthetic* medium reachable through the
  OS's own `strategy` path; it does not give XNU a driver that moves a byte off the card. TWRP-to-storage
  stays done-and-separate (860).
- **THE GOAL IS NOT MET.**

## 7. Safety

Unchanged and untouched: **no device, no `fastboot`, no switch, no press.** The arm is a build and a park.
`fastboot boot` only, never flash; the parked arms are records and not a queue. `make check` is **exit 0**.
The arm adds **no device action** — the one read it depends on is rung 57's, already spent.