# The path to a mounted root — a runbook, and what it still needs

**Nothing in this file has run. It is a plan, and it names who does each step.** As of 2026-10-01 the
goal's storage clause is NOT met: XNU enters, the base drivers run, and the eMMC driver does not. This
runbook is the shortest honest path from here to "XNU mounts its root off the device", written down once
so that each step after the press is mechanical. **The press itself is the operator's** — no line here
authorizes it (`[[mi4-hardware-run-safety-gate]]`: one gate, one runner, `fastboot boot` only, never
flash, unplug `33e80afe` first).

The chain has **one operator action** and then a short host-side tail. The order is fixed by 884 §4: the
destructive reformat is LAST.

---

> **UPDATE 2026-10-01 (890): step 0's press is SPENT and the runbook below is changed.** The rung-58
> press answered rung 57's question: the selected largest partition is **`userdata`, carrying ext4**
> (`_dp_name_p0/p1` = `user`/`data`, `_pt_ext_magic = 0xef53`, `_pg_which = 1`). So the destructive
> **HFS+ reformat of step 3 is no longer obviously necessary** — the mountable partition already holds
> a filesystem, and an ext2/3/4 client exists in the XNU tree. Steps 1 and 4 stand; step 3 is now a
> *choice* (mount the existing ext4 vs. reformat), and the cheaper branch is live. See experiment 890.

## 1. Why the order is what it is (the three facts that fix it)

- **The tree holds only the press.** `build_entry.sh` hardcodes `OUT=out/stage90`, and the gate requires
  the live `src/entry/` to hash to the entry image's source manifest. So the staged arm cannot be rebuilt
  in place — 888 measured that even a switch-OFF rung-57 rebuild gives `773d49f9…` where the press is
  `5936b246…`. The parked arm is its only copy (**886**, **888**). *(The rung-58 press is now spent;
  the tree is free for the step-1 patch.)*
- **The selection is `userdata` = EXT4 — measured by the press, not inferred** (**890**). The ported
  HFS+ (**885**) and a card-reading strategy are still what serves the mount, but the medium does not
  need an HFS+ filesystem written to it: `userdata` already carries a well-formed ext4. The HFS+ route
  (**880**) remains available if ext4 mounting proves harder than HFS+ (**884**).
- **The ladder is cumulative and unwashed above rung 56** (**stage90-phase-status**): rungs 57/58/59/60
  are built and parked but unpressed. One press washes several (858: one press washed three).

---

## 2. Step 0 — the press (OPERATOR)

The staged arm is **`armed-storage-5936b246`** (rung 58: the selected GPT entry's Name + TypeGUID, plus
863's repair of rung 57's two-witness defect). Readiness is **5 of 5** (verified 2026-10-01).

Confirm readiness first (read-only, no device):

```
tools/verify_press_ready.sh --park out/stage90/frozen/armed-storage-5936b246
```

Then the operator runs the one gate + one runner:

```
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-5936b246
```

The runner refuses to press without `--expect-arm`, and refuses if `out/` does not hold that arm. After
the run, **check the log against the arm's own built-in table** (the readiness row "the arm is named by a
reading" prints it): the row that matters is `_dp_next_is_arg = 1` with `_dp_part_is_first = 0`.

---

## 3. Step 1 — apply the card patch, build and park rung 61 (HOST)

Once the press is spent, the tree is free. The eMMC-driver integration is fully built and held as one
patch (**888**), which **supersedes** 887's door patch:

```
git apply tools/rung61-card-strategy.patch
```

Then rebuild the two halves the arm needs (the module and the entry image) and park the result as a new
arm. The switch set is the rung-58 arm's own plus the three new arms:

```
STAGE90_XNU_STORAGE_PROBE=60 STAGE90_XNU_MOUNT=1 STAGE90_XNU_HFS_ROOT_MEDIA=1 STAGE90_XNU_EMMC_STRATEGY=1
```

- the platform object must be built **with the same defines** — `XNU_KERNEL_EXTRA_DEFINES='-DSTAGE90_XNU_HFS_ROOT_MEDIA=1 -DSTAGE90_XNU_EMMC_STRATEGY=1'` — because `xnu_entry_882` and `xnu_entry_888` refuse a module that disagrees with the build;
- `xnu_entry_888` also refuses a card-ON image whose linked `entry_storage_driver_read` is not a real `T` symbol;
- the payload must be rebuilt too — it **embeds** the entry image (**883**);
- park the arm and record it in `records/revert-set.txt`; `verify_press_ready.sh` should then read 5/5
  for it.

**This arm, pressed, is the one that would make XNU serve its root from the card** — but it reads
`userdata`, which is EXT4, so it cannot mount until step 3.

---

## 4. Step 2 — build the HFS+ medium (HOST, non-destructive, but out of order until now)

`tools/build_hfs_root_image.sh` makes a 512 KiB HFS+ volume deterministically (mkfs.hfsplus + loop mount +
read-back), with `/sbin/launchd` = the same Mach-O `entry_ramdisk.s` carries. **That image is the small
in-payload volume 882's arm already serves** — it tests the *port*, not the device.

For the *device* route the medium must be the `userdata` partition itself, so this step is only the
recipe: the reformat in step 3 writes the same volume into `userdata`'s extent (first LBA 4194304, 884),
and the card unit serves it by LBA. *(The device-side writer is not built yet; it is the last host-side
piece owed and is written after the press frees the tree.)*

---

## 5. Step 3 — reformat `userdata` to HFS+ (OPERATOR, DESTRUCTIVE, LAST)

**This destroys ~13 GB and is why it is last** (884 §4): the HFS+ row is inert until the driver rung is
pressed and working, so doing it earlier spends the data for nothing. The safety envelope explicitly
permits this **provided `fastboot` stays reachable** (the load-bearing interlock), and TWRP/Android may be
sacrificed.

Until the device-side writer exists, the reformat is done **from TWRP** (which is written to `recovery`,
mmcblk0p20, since 860), not from a host tool. The exact command is owed; it will be recorded here when it
is written, together with its own read-back proof, before it is ever run.

---

## 6. Step 4 — press the card arm and read the mount (OPERATOR)

With `userdata` holding HFS+, press the step-1 arm. The reading that closes the clause: the strategy
serves the card (`xnu_live_rootmedia_card_blocks` > 0, `_card_lba` = the selected base), `hfs_mountroot`
succeeds, and the run reaches the exec off that filesystem — i.e. the log shows the root mounted from the
eMMC rather than from the in-payload ramdisk. **That is the clause; nothing so far has reached it.**

---

## 7. What is already true (so this is not a wish)

- The ladder reads real sectors off the card (rungs up to 56 pressed; 57/58 parked).
- The HFS+ port compiles and links into the 4570 kernel (885: 663/662/1, exactly the 37 HFS files).
- The card unit is **built and proven in a linked image**: `st_media_strategy` calls
  `entry_storage_driver_read` (`0x80012688`) → `bcopy` (888).
- The selection is known from the device's own GPT, read-only (884).
- The gate + runner + park are ready; readiness is 5 of 5.

**The only thing between here and the clause is: one press, the patch, and one destructive reformat — in
that order, the press first, and the reformat last.**