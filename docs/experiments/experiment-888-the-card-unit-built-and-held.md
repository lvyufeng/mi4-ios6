# 888 — THE CARD UNIT, BUILT END TO END AND HELD: THE STRATEGY THAT READS THE LADDER

**A host-side build in a scratch tree: the three source changes COMPILE, LINK, and the linked
`st_media_strategy` is shown to CALL the ladder's door — and the LIVE tree is untouched, so the staged
rung-58 press stays valid. The code is held as a patch.** No device, no park, no arm, nothing sent.
**THE PRESS IS THE OPERATOR'S; the goal is NOT met.**

In one paragraph: 887 built the ladder's *door* — `entry_storage_driver_read(lba)`, the ladder's own read
at a caller's LBA — and found the join is **per-unit**: disk 0 serves the exec-able Mach-O, so the card
must be a **third unit** (`ST_MEDIA_DRIVER`), which it did not build. This step builds that unit, in the
scratch tree, **and proves it by building the whole arm and reading the linked image**: the card-ON entry
image links clean, `st_media_strategy` (at `0x80287d60`) calls `entry_storage_selected_count` ->
`entry_storage_selected_lba` -> `entry_storage_driver_read` (at `0x80012688`) -> `bcopy`, and 888's own
build clause refuses a card arm whose image lacks the door. **Building it found and fixed two real
defects** — the door's consume was gated too low (it perturbed rungs 53..59, contradicting 887's own
"inert below its rung" claim and tripping a rung-49 clause), and the new switch reached no record. The
result is held as `tools/rung61-card-strategy.patch` (the three files), **not applied**, because a build
— even a switch-OFF rebuild of the rung-57 arm — does not reproduce the staged press's bytes.

## 1. The unit 887 named, built

887 §3: "the driver therefore needs a unit of its own — `ST_MEDIA_DRIVER`, a third disk whose strategy
is the card." This step is that unit. `src/platform/stage90_root_media.c` grows, under a new switch:

- **a third disk** — `ST_MEDIA_DISKS = 3` when `STAGE90_XNU_EMMC_STRATEGY=1` (2 otherwise, so the OFF
  geometry is unchanged), with `ST_MEDIA_DRIVER = 2u`;
- **a card flag** `ST_MEDIA_CARD 0x8u`, and a marker pair `entry_root_media_card_arm_on` /
  `..._off` defined **unconditionally, one of the two** — the same shape 882's HFS marker uses, so the
  OBJECT says which arm it was built for and the two scripts that compile it cannot drift;
- **the strategy's card branch** in `st_media_strategy`: for unit `ST_MEDIA_DRIVER` it computes the LBA
  as `entry_storage_selected_lba() + blkno`, calls `entry_storage_driver_read(lba)` **once per block**,
  and `bcopy`s the returned ladder buffer out — where the RAM-backed units `bcopy` from a contiguous
  array at `st_medium_disk_base(unit)`.

The card unit is **not** a contiguous array (each block must be fetched individually by its LBA), so it
gets its own loop; the two RAM-backed units keep exactly their rung-60 shape.

## 2. The measurement: the whole arm builds, and the strategy is shown to read the card

Built in `/tmp/b887` (a scratch tree; the live tree is untouched):

```
STAGE90_XNU_STORAGE_PROBE=60 STAGE90_XNU_MOUNT=1 STAGE90_XNU_HFS_ROOT_MEDIA=1 \
STAGE90_XNU_EMMC_STRATEGY=1 … ./build_entry.sh        → exit 0
```

and the LINKED image says what it is:

```
80287d60 t st_media_strategy
  80287e34  bl 8001279c <entry_storage_selected_count>
  80287f30  bl 801c0918 <buf_map>
  80287f5c  bl 80012778 <entry_storage_selected_lba>
  80287f64  bl 80012688 <entry_storage_driver_read>     ← the strategy reads the card
  80287f74  bl 80014470 <bcopy>                          ← and copies the block out
  80287fa8  bl 801c0cc8 <buf_biodone>
```

`entry_storage_driver_read` is a real `T` definition at `0x80012688`, not a linker stub — which is
exactly what 888's own clause checks (below). **The two halves of the driver clause meet in one image.**

## 3. Two defects the build found — the reason a built thing beats a mapped one

**(a) The door was gated one rung too low.** 887's patch consumed the caller's LBA inside the
`#if STAGE90_XNU_STORAGE_PROBE >= 53` block of `st_read_single_block`, adding three file-scope objects
(`st_driver_lba`, `st_driver_lba_set`, `st_driver_get`) to that body's image surface from value 53 on.
That **moves rungs 53..59**, contradicting 887's own claim that "rungs 57, 58, 59 compile clean **and
unchanged**", and it trips the rung-49 clause that allow-lists exactly which image objects
`st_read_single_block` may reach ([[mi4-one-value-two-definitions]] wearing a rung guard). **The repair
is the one 887's prose already promised**: gate the consume — and the three cells — at `>= 60`, the rung
that uses them, so value-57..59 builds are byte-for-byte what they were. This is a fact only a build
produces; reading the patch would not have shown it (`mi4-a-claim-in-a-comment-is-not-a-check`).

**(b) The switch reached no record.** Adding `STAGE90_XNU_EMMC_STRATEGY` to `ENTRY_ARM_KEYS` without
adding the `echo` to the config writer made the build's own 678 two-way check refuse: *"a switch that
shapes the image and reaches no record is a run whose arm nobody read."* The record is the only thing the
gate reads about the image, so this is not bookkeeping — an unrecorded arm is an arm no press can be
described as. Repaired by the `echo "STAGE90_XNU_EMMC_STRATEGY=$EMMC_STRATEGY"` writer line.

## 4. Why held as a patch — measured, not assumed

`build_entry.sh` hardcodes `OUT=out/stage90` (no override), and the gate requires the **live `src/entry/`**
to hash to the entry image's source manifest. So a rebuild of the rung-57 arm stales the staged press.
This step **measured** that the rebuild would not even be the same bytes: a switch-OFF rebuild of the
rung-57 arm produces `xnu_arm_entry.bin` = `773d49f9…`, where the staged press is `5936b246…` — same
size (5,585,564), different content, because the card marker symbol and the (dormant) door perturb the
link. **So restoring the press by rebuilding is impossible; the park is the only copy, and the patch is
the only vehicle for the code.** (The 888 arm itself is 6,109,852 bytes — a genuinely different arm.)

The change is held as **`tools/rung61-card-strategy.patch`** (the three files: `entry_storage.c`,
`build_entry.sh`, `stage90_root_media.c`), which `git apply --check` accepts against the live tree.

**This patch SUPERSEDES 887's `tools/rung61-driver-door.patch`, which is retired here.** The door is
*contained* in the card patch (with the gate-(a) repair), so the two overlap in `entry_storage.c`;
applying both would conflict, and 887's own patch carries a malformed header (its `+++` names the
scratch path `/tmp/b887/...`), so it does not even `git apply`. One self-contained patch is the correct
vehicle. With it, the card arm is complete and ready for the session the operator presses into.

## 5. What the build clause holds

`xnu_entry_888` mirrors `xnu_entry_882`: it reads the platform OBJECT (`nm`) to confirm the module was
compiled for the arm this build links (refusing a mismatch in either direction), and — when the arm is ON
— reads the **linked image** to confirm `entry_storage_driver_read` is a `T` definition, refusing a card
arm whose door is a stub. The two strategy refusals (`:33539`, `:33634`) that forbid
`st_read_single_block` in the strategy are **widened, and only for the card arm**: the third unit's read
IS the driver clause, while the RAM-backed units keep the invariant (the card transfers stay off the
OS's schedule except for the one unit the experiment is about) — which is why the widened build must be a
NEW rung's arm and not a re-record of the current one.

## 6. Safety

No device, no `fastboot`, no `adb`, no park, no arm, nothing staged. The build was in a scratch tree
(`/tmp/b887`); the live tree `git status` is clean and the entry image's source manifest is unmoved, so
the staged rung-58 press is valid. Started host-side only. **THE PRESS IS THE OPERATOR'S; THE GOAL IS
NOT MET.**