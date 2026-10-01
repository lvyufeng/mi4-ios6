# 880 — THE ROOT FILESYSTEM AND THE MEDIUM'S FILESYSTEM MUST MATCH, AND THE ONLY ONE THIS REPO CAN PORT IS HFS+

**A host-side reading: no device, no boot, no arm, no park, no switch, no build.** The rung-57/58/59/60
arms stay parked and unpressed. **THE PRESS IS THE OPERATOR'S; the goal is NOT met.**

In one paragraph: the medium track (867) is wired toward a driver that lets XNU's `vfs_mountroot` read a
device, and the filesystem track (868–879) has ported 2050's **HFS+** into 4570 and given it a static root
row. Read together they are still **one filesystem short of each other**: the partition rung 57 *selects* is
the **largest extent**, which 863's own finding names as `userdata` (**ext4**, p25), and this device has
**no HFS volume at all** (it is an Android phone; `docs/reference/cancro-platform.md` lists only
ext4/f2fs targets). So the ported filesystem and the medium's contents must be made to **match** before any
driver rung can mount the root — and of the two filesystems this repository could carry, **only HFS+ has
portable source** (529: "HFS is in this repository three times over"). ext4 has **no** XNU source at any
copyright date (867 §4.2, 523 §"the phone's userdata is ext4 or f2fs").

## 1. The two facts, each already measured, that were never put side by side

- **What rung 57 selects.** `st_gpt_best_extent` (entry_storage.c:3215, `if (extent > st_gpt_best_extent)`
  at :8012) keeps the entry with the **largest `EndingLBA - StartingLBA`**. 863 §"What the rung asks" states
  the consequence in its own words: *"the largest extent is `userdata` (p25, ext4) while the first partition
  is `sbl1` (p1)."* So the selected root device is an **ext4** volume.
- **What this repository can mount.** 529 measured that the only filesystem with portable source here is
  **HFS+** (2050's `bsd/hfs`, ported 868–879). 523 records (twice, quoted in 529) that the phone's userdata
  is ext4 or f2fs, *"neither is in XNU at any copyright date."* 867 §4.2 states the pair plainly: *"no XNU
  tree links an ext4/f2fs/HFS driver … porting 2050's HFS+ is eight small drifts and one droppable file."*

Each fact was recorded. **Neither note put them in the same sentence**, and the sentence is a constraint:
the driver clause (867 §4.1) *and* the filesystem clause (867 §4.2) are both necessary, but they are
**only sufficient together if they name the same filesystem** — and the device offers none that XNU can read.

## 2. Why this is a decision and not another driveless rung

A driver rung makes `vfs_mountroot` reach the HFS row 879 placed. It then calls into the HFS sources, which
will look for an HFS volume at the partition's first sector. **The selected partition is ext4.** HFS+ would
not recognize it, `hfs_mountroot` would fail, and the root walk would fall through to mockfs — the same place
it is now. So the driver rung alone does **not** move the goal; it moves the *failure* from "no strategy" to
"`hfs_mountroot` sees no HFS".

To actually reach "XNU mounts storage and brings up userland from it", the medium's filesystem must be one
XNU can mount, which here means **HFS+** — and that means the medium must **carry an HFS+ volume**, which
this device does not. The routes that would create one each have a cost worth naming before any of them is
started:

| route | what it needs | the cost |
| --- | --- | --- |
| format a scratch partition HFS+ | `mkfs.hfsplus`, a partition to sacrifice | the relaxed envelope allows repartitioning, but writing a filesystem to the device is a **destructive device action**, larger than any parked ladder rung, and must be the operator's |
| stage an HFS+ image in RAM | a host-built HFS+ image, a payload `bdevsw` over it | no device write, but it is an **HFS** medium: it tests the port, not the eMMC path — that is the mockfs path with an HFS filesystem on top |
| port an ext4 driver | an XNU ext4/f2fs source | **none exists** in this repo or in any XNU tree (523/529); this is not a port, it is a from-scratch driver, out of proportion to the goal |

The first two are host-side oroperator-side and honest; they are different experiments and neither is a
ladder rung. The third is the one the *device's own data* would want and the one there is no source for.

## 3. The honest bound

**This is a reading, not a measurement, and it is a reading of the PORT's subject, not of the device's raw
bytes.** It rests on 863's recorded finding (`userdata` = ext4) and on `cancro-platform.md` (an Android
partition set), **not** on a GPT dump in this repository — the rung-57/58/59/60 presses that would carry the
selected entry's `PartitionTypeGUID` are **parked and unpressed**, so the device's own answer to "what type is
the selected partition" is not yet in hand. **The type it would return is the thing the first press would
settle** — and no HFS type GUID appears anywhere in the recorded material (searched: `48465300`,
`Apple_HFS`, `AF00`, `Apple_Boot`).

**THE GOAL IS NOT MET.** What this adds is that the **medium (867) and the medium's filesystem (880) are one
decision**, and that decision is the operator's, because the only route that uses the device's real eMMC
storage as XNU's root formats a partition, and the only route that needs no device write tests the HFS port
over a RAM image rather than over the phone's storage.