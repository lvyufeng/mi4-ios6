# 887 — THE DRIVER'S DOOR, BUILT AND HELD: THE LADDER'S READ, AT A CALLER'S LBA

**A host-side build in a scratch tree: the entry source changed and COMPILED, the code is held as a
patch, and the LIVE tree is untouched so the staged rung-58 press stays valid.** No device, no park, no
arm, nothing sent. **THE PRESS IS THE OPERATOR'S; the goal is NOT met.**

In one paragraph: 867 §3 mapped the last clause as a *call* — give the OS's own `strategy` the ladder's
read — and 884 washed the premise that gated it. 886 fixed where it lands and found the live tree cannot
**contain** it (a build clobbers the staged press). This step does the other half: it **builds the clause**
in a scratch tree and **holds the code as a patch**, so the page is ready for the session the operator
presses into. The built piece is the ladder's own door — `entry_storage_driver_read(lba)` issues the same
`st_data_reset()` + `st_read_single_block(1u)` pair `st_read_selected` issues, but at an **LBA the caller
supplies** rather than the ordinal's sector — plus the two accessors the strategy's addressing needs
(`entry_storage_selected_lba`/`_count`). It compiles clean at rungs 57–60. **And the step finds the
clause's real shape, which is not what 867 §3 called "one `bl`":** the strategy integration is a
**per-unit** decision whose only safe unit is NOT disk 0, because disk 0 serves the exec-able Mach-O — so
the join is entangled with the filesystem route (880) and waits on the operator's `userdata` reformat.

## 1. The ladder's door, and why the read body needed more than a `bl`

`st_read_single_block` computes its LBA **inside the read body**, from `st_read_count`:
`st_read_lba = st_sector_for_read(st_read_count)` (rung 57's four-arm selector — ordinal 0 = header,
1..N = array, N+1 = the selected superblock). A driver handed a block number has no ordinal that names it,
so the door is a **flag**, `st_driver_lba_set`, that lets the body send a caller-supplied LBA:

```c
st_driver_get = st_driver_lba_set;
st_read_lba = st_driver_lba_set ? st_driver_lba : st_sector_for_read(st_read_count);
st_driver_lba_set = 0u;                 /* the door is spent the moment it is used */
```

Three cells, not two, and the third is the point: `st_read_lba` is the sector the read **sent** (the
published `_rd_lba`), `st_driver_lba` is the caller's **request**, and `st_driver_get` is the reading that
tells a driver read from a ladder read (`xnu_live_storage_rd_driver`, 1 only when the door was used).
Collapsing request and reading would let a refusal look like a transfer — `mi4-one-value-two-definitions`
wearing a log.

The exported function is the same two calls `st_read_selected` makes, argument for argument (`int_enable`
= `1u`, the word every rung passes), differing **only** in where the LBA comes from:

```c
const uint32_t *
entry_storage_driver_read(uint32_t lba)
{
    st_driver_lba = lba; st_driver_lba_set = 1u;
    st_data_reset();
    st_read_single_block(1u);
    st_driver_lba_set = 0u;
    … publish _drv_lba, _drv_get, _drv_w0, _drv_w1, _drv_held …
    return st_read_block;               /* the caller copies out; the ladder keeps its buffer */
}
```

The **reset is not optional** — rung 55's `_rd_inhibit_dat = 0x2` is why — and it is what keeps each block
of a multi-block request clean, since the next read is the very next block the filesystem wants.

## 2. The measurement: it compiles, and it is inert below its rung

- **rung 60** (`STAGE90_XNU_STORAGE_PROBE=60`): `entry_storage.c` compiles clean under the real flags
  (`-mcpu=cortex-a15 -marm -ffreestanding -O2 -Wall -Wextra -Werror -std=gnu11` + the arm's defines).
- **rungs 57, 58, 59**: compile clean **and unchanged** — the door is `#if >= 60`, so the staged rung-58
  press's bytes are exactly its own. A gate that hashes `src/entry/` would refuse a live tree that had
  the door *applied*; it accepts a live tree that does not.
- **the platform half**: `stage90_root_media.c` with the driver branch compiles and its object carries
  `U entry_storage_driver_read` / `U entry_storage_selected_lba` / `U entry_storage_selected_count` — so
  the platform file and the ladder file meet by symbol, as 867 §3 said they would.

The change is **held as `tools/rung61-driver-door.patch`** (95 lines: the cells, the consume, the entry
function and its forward declaration, the two accessors, the `#error` bound and its rung-61 prose). It is
**not applied** — the live tree is byte-for-byte the staged press's.

## 3. What this step found: the join is per-unit, and disk 0 is the wrong unit

867 §3 and 886 both looked at `st_media_strategy` as **one** body. Building the integration shows it is
**two**, and the difference is load-bearing:

- **Disk 0 serves the Mach-O the root is exec'd from.** 861/862/866 keep `st_medium_disk_base(0) ==
  g_stage90_ramdisk` **precisely** so `exec_mach_imgact` sees a valid `MH_MAGIC`. A disk-0 strategy that
  read the card would serve the **selected partition's** bytes (`userdata`, an ext4 volume) where the exec
  expects the Mach-O — **the run would lose the exec**, which is the one thing the whole mount track has
  protected (866 §4). So disk 0's `bcopy` path must NOT be replaced.
- **The driver therefore needs a unit of its own** — `ST_MEDIA_DRIVER`, a third disk whose strategy is the
  card. Disk 0 stays the root; the driver unit is the addition, exactly as 864's staged sector was.

And that entangles the join with the **filesystem** route (880), which is the honest finding: a driver
unit that reads the card serves real medium bytes, but **nothing mounts it** unless a filesystem on that
unit matches the medium's — and the selection is `userdata` = ext4 (884), while the port is HFS+ (868–879,
885). So the join's *hardware* half (this step, built and held) and its *filesystem* half (the HFS+ port,
linked) are both ready, and what remains between them is the operator's reformat ordering (884 §4:
driver + port first, reformat last).

## 4. The contention, stated plainly

`build_entry.sh` hardcodes `OUT=out/stage90` and the gate requires the **live `src/entry/`** to hash to the
entry image's source manifest. Therefore:

- **A rung-61 build and the staged rung-58 press cannot coexist on this tree.** Applying the patch stales
  the press (the gate refuses it, correctly).
- **The patch is the page.** It is applied in the session that follows the operator's rung-58 press — that
  session has a spent arm and a free tree, so it can build, park and stage the driver rung.

## 5. What this changes

- **The clause is BUILT, not merely mapped**: the ladder's driver door is written, compiles at rung 60,
  and meets `stage90_root_media.c` by symbol. It is **held as a patch** because the live tree is the
  press's.
- **The join's shape is corrected**: it is a **per-unit** integration (`ST_MEDIA_DRIVER`), not a
  replacement of disk 0's strategy — because disk 0 serves the exec.
- **The dependency is stated**: the driver's hardware half (built here) and the filesystem half (885) are
  both ready; the operator's press (first) and reformat (last) are what remain between them.
- **NOTHING IS APPLIED, ARMED, PARKED OR SENT.** The live tree is untouched; the staged arm is intact.

## 6. Safety

No device, no `fastboot`, no `adb`, no park, no arm. The build was in a scratch tree (`/tmp/b887`); the
live tree `git status` is clean and the entry image's source manifest is unmoved, so the staged rung-58
press is valid. Started host-side only. **THE PRESS IS THE OPERATOR'S; THE GOAL IS NOT MET.**