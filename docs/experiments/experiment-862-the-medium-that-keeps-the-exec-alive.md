# 862 — THE MEDIUM THAT KEEPS THE EXEC ALIVE: the mount arm, and the RAM disk it is memory-backed over

**A build and a park, and nothing else. NO DEVICE WAS TOUCHED; THE ARM IS PARKED and the PRESS IS THE
OPERATOR'S.** This is the second step of the XNU-mount track (861 opened it) and the first arm of that
track that a press could send.

## What 861 left out, and why it would have regressed the boot

861 gave the mount path a device: `src/platform/stage90_root_media.c`, a payload-owned `bdevsw`, and a
`__wrap_mdevlookup` that answers `makedev(major, unit)`. Its `strategy` is `eno_strat` — it moves no
byte — and 861's doc was careful to say so. What neither 861 nor 530 §9 said is *which buffer* the
stand-in must answer from, and reading `mockfs` answers that.

`mockfs` is the root filesystem in this configuration (`tools/xnu_config/boot/STAGE90_XNU.local`): its
`vfc_mountroot` is the only non-NULL one in `vfstbllist`, so `vfs_mountroot` mounts it over whatever
`rootdev` names. And `mockfs_mountroot` (`bsd/miscfs/mockfs/mockfs_vfsops.c:101`) asks the root device
`DKIOCGETMEMDEVINFO` before it builds anything:

```c
if (!VNOP_IOCTL(rvp, DKIOCGETMEMDEVINFO, (caddr_t)&memdev_info, 0, NULL)) {
    if (!mockfs_mount_data->mockfs_physical_memory) {
        mockfs_mount_data->mockfs_memory_backed = memdev_info.mi_mdev;   /* <-- the switch */
        mockfs_mount_data->mockfs_memdev_base    = memdev_info.mi_base;
        ...
```

Only if the call **succeeds** does mockfs become `mockfs_memory_backed`, and only then does its one
file node point straight at the memory (`mockfs_fsnode.c:333-344`,
`pager_map_to_phys_contiguous(ubc_mem_object, 0, mockfs_mnt->mockfs_memdev_base << PAGE_SHIFT, fsnp->size)`).
The memory-backed root today is `/dev/md0`, made by `mdevadd` over `g_stage90_ramdisk` — the bytes of
`/sbin/launchd` (`entry_ramdisk.s`) — and **that is why process 1 can be exec'd at all**.

So a device that answered `DKIOCGETMEMDEVINFO` with `ENOTTY` would still **mount** — mockfs leaves
`mockfs_memory_backed` FALSE and builds the three nodes — but its file node's bytes would have to come
through `cluster_pagein` → the device's `strategy`, and `eno_strat` moves no byte. **The mount would
succeed and the exec would fail.** That is exactly the trade 861's arm, parked as-built, would have
made, and 862 refuses to make it.

## What 862 adds: the same RAM disk, behind a `bdevsw`

862 does not invent a buffer (530 §9 says "a buffer"; mockfs says *which* buffer). It maps the SAME
`g_stage90_ramdisk` the RAM disk does, and answers `DKIOCGETMEMDEVINFO`:

- `mi_mdev = 1` — yes, a memory device.
- **`mi_phys = 0`** — copied from memdev's own choice: `mdevadd` is called with `phys = 0`
  (`IOKitBSDInit.cpp:447`), and mockfs **skips** the memory-backed path when `mi_phys` is true
  (`mockfs_vfsops.c:104-109`). Answering "physical" would re-open the `cluster_pagein` path this whole
  experiment exists to close.
- `mi_base = g_stage90_ramdisk >> 12`, `mi_size = (end - start) >> 12` — a page count of the RAM disk,
  which `mockfs_fsnode.c:342` shifts back with `<< PAGE_SHIFT` and lands on the array.

The answer is a named `noinline` function, `st_media_memdev_info`, so its body can be read **by value**
out of the object — 855's build clause read a two-arm `if`'s stores out of the *linked* disassembly and
was refuted by the linker's own tail-merge (`mi4-linked-code-order-is-not-source-order`).

**The geometry is derived, not chosen.** `mi_size` needs the RAM disk's byte length; the module does
not carry it. `entry_ramdisk.s` now exports `g_stage90_ramdisk_end` (an alias of the address the array's
own `.size` already computes), and `build_entry.sh` passes it to the module as
`-DSTAGE90_ROOT_MEDIA_SIZE_SYM`; the module computes `end - start` from the two symbols. `0x2000` exists
in exactly ONE place. `verify_root_device` gains a clause the alias and the array's own `nm -S` size
must agree on — a clause that reads the **alias**, which no earlier check did, because every earlier
check read the array. (A second definition of `0x2000` would be this project's oldest defect; the
alias is what makes the round trip single-source.)

## What was measured (all host-side, none from a device)

- **The module compiles** (`--platform-only`, pool config `STAGE90_XNU`); its undefined symbols resolve
  to text in the linked image (`bdevsw_add`, `enodev`, `enodev_strat`, `entry_live_write`,
  `g_stage90_ramdisk`, `g_stage90_ramdisk_end`).
- **The `noinline` helper's body, by value** (`arm-none-eabi-objdump` of the module object): `mi_mdev
  = 1`, `mi_phys = (flags >> 1) & 1 = 0`, `mi_base = ramdisk_va >> 12`, `mi_size = (end - start) >> 12`
  — the four fields at offsets 0/4/8/16 of `struct dk_memdev_info_t`, matching `bsd/sys/disk.h:289-295`.
- **The entry image links** with rung 57's switches **plus `STAGE90_XNU_MOUNT=1`**, all checks green;
  the new 862 clause passes (`g_stage90_ramdisk_end` = `0x80523000` = `0x80521000 + 0x2000`).
- **The wrapper's callee, read out of the LINKED image**: `__wrap_mdevlookup` calls
  `bl entry_root_media_register` (`0x80485354`), not `__real_mdevlookup`. **And the two-arm switch was
  NOT tail-merged away** — `st_media_ioctl` carries `bl st_media_memdev_info` (`0x80287b74`), the
  defect class that bit rung 57's build clause.
- **The payload builds** (`scripts/build.sh` with `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1'`), exit
  0, exit-0 gate run green.

## The arm, parked

`out/stage90/frozen/armed-storage-bf9408ca/` — named by `xnu_arm_entry.bin`'s own sha prefix
`bf9408ca`, 11 members, `verify_revert_set.sh --set=armed-storage-bf9408ca` = 11/11.

| member | sha256 | bytes |
| --- | --- | --- |
| `xnu_arm_entry.bin` | `bf9408cae2d72422d35492484c6f1de342cd9ce602c71c41c3526f05807a2878` | 5585564 |
| `xnu_arm_entry.elf` | `c86a002aa0800cec3d330f0a25e4e20986d57d9c4209e82ff6898d927fb3547e` | 6767024 |
| `xnu_arm_entry-config.txt` | `cc348efd98436777f2eef67edbfd99827821a052a7b677605cc005156cb9dd85` | 920 |
| `xnu_arm_entry-sources.txt` | `1139876c0e847d3b7b6bdd283f283671fed173cd124f2b2e2c0a7c923c5ebc52` | 2335 |
| `stage90-qcdt.img` | `d9588e6592eb895df870edeb941baeb1200742c46b2411233cc39958de961524` | 8605696 |
| `stage90.bin` | `4c5dcceab4e619448513d08280e46e439feaa937fc0315e9e2b97c9ec7d12916` | 6081060 |
| `stage90.elf` | `a80e398f90da37417fa4e7e60b693db048d8aa6e15dd45fb73aae3b37f560faa` | 6143132 |
| `stage90.img` | `7218ad9cf1ed9cbcfa30b5ee8f6dc17b9b0f3dcbbb0ef40c387eaf003c905f04` | 6084608 |
| `stage90_fixture.macho` | `52bc9c357068b791f777bb9abd2e879fa5b0b27364267220b01215dc5faec97b` | 1744 |
| `stage90-build-config.txt` | `6c2b6038d3bcbe30da60084c571682100946a15f191437161dbf1ed1385b547b` | 682 |
| `SHA256SUMS.txt` | `e0d63324ff2e2c59f8bd9cd6bf3d44f6caeb2e131db6deb8752e8a57be06421a` | 560 |

**Two records moved because an entry switch was added, and both are mechanical.**

- **`scripts/preflight_boot_check.sh`'s `ENTRY_CFG_KEYS`** — the gate's own two-way check refused the
  first readiness run: *"this gate does not print: STAGE90_XNU_MOUNT"*. The key was added to the list,
  which is the check earning its place (the same refusal `build_entry.sh`'s record writer made in 861).
- **`src/entry/build_entry.sh`'s `verify_root_device`** — the 862 clause above.

`STAGE90_XNU_STORAGE_PROBE=56` (rung 57) is carried unchanged; the entry image grew 32 bytes
(5585532 → 5585564), the payload record is byte-identical to every rung since 23 (`6c2b6038…`), and
the entry-group page move did NOT fire (`STAGE90_XNU_SEAM_LR` stays `0x8004d2dc`).

## What this does and does not do

- **Does**: parks an arm that hands `vfs_mountroot` a payload-owned `dev_t` whose device is
  memory-backed over the same RAM disk `/dev/md0` uses — so a mounting mockfs still serves
  `/sbin/launchd`, and the exec does not die on a `strategy` that moves no byte.
- **Does not**: mount anything on a device. `xnu_live_rootmedia_pages` (the page count mockfs maps
  the file node onto) and `xnu_live_rootmedia_blocks` are published on the entry instrument's channel,
  but no run has read them. **The XNU storage goal is not met** — this is the arm that moves the
  frontier from *does the mount path accept a payload device* to *does the exec survive it*.

## Next

1. **Press this arm** (the operator's) and read `xnu_live_rootmedia_pages` / `_blocks` beside the
   exec's own cells — the question is whether process 1 is still reached when the root device is the
   payload's.
2. Couple the arm to the ladder's selection: `st_gpt_data_sector`/`st_gpt_best_extent` are `static` in
   `entry_storage.c`, so an accessor is needed for the root to be a *different* disk than the RAM
   disk's — the "select by extent" shape 861 keyed the geometry for, which 862 replaced with a derived
   geometry and will re-key when the eMMC is the medium.

## Safety

No file outside `src/`, `tools/`, `scripts/` and `docs/` was written; `make check` exits 0; no device
was touched, nothing was flashed, and **no arm is armed or owed** — the park is a **RECORD**, not a
queue. The rung-57 arm `armed-storage-71b54d73` is unchanged on disk; `out/` holds this arm, which is
the parked one.