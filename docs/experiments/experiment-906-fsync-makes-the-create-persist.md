# Experiment 906 — the fixture fsyncs, and the create persists on the eMMC

**Arm `armed-storage-f347d060` (entry bin sha `f347d060…`), PRESSED 2026-10-06, run exit 0, device
returned, no brick. Arm now SPENT (`armed-storage-f347d060-spent`).** It is 905d
(`armed-storage-0e6eb4b4`) rebuilt with **one fixture change and nothing else**: the pid-1 program
calls `fsync(0)` after the write. It closes 905d's open question — *why a create that returned 0 left
the volume's private directory but not the file*.

## The finding

905d proved the **write path**: the volume header changed on the device's own eMMC and persisted
across the run, and the card accepted every block. But the new file's **name** never appeared. The
cause is HFS+'s delayed write of B-tree nodes: `hfs_btreeio.c` writes a node with
`bdwrite_internal(bp, 1)`, which marks it **dirty in the buffer cache** and writes it later — on
`fsync` or on `close`. 905's fixture went straight from `write` to 512's park, so the catalog record
the create made died with the run. The only blocks that reached the medium were the ones HFS syncs on
its own: the volume header (`hfs_metasync_all`'s DKIOCSYNCHRONIZECACHE on the rw mount) and the
private directory the rw mount creates.

**`fsync(fd)` is the flush.** Syscall **95** (`95 AUE_FSYNC ALL { int fsync(int fd); }`, one 4-byte
argument — `munge_w`, r0). It reaches `hfs_vnop_fsync` → `hfs_fsync(vp, waitfor, 0, ...)`, and that
function unconditionally does:

```c
if ((retval == 0) && wait && !fullsync && cp->c_hint && ...)
        hfs_metasync(VTOHFS(vp), (daddr64_t)cp->c_hint, p);   /* hfs_vnops.c:2380 */
```

`hfs_metasync` looks up exactly that delayed catalog node and `VNOP_BWRITE`s it (`hfs_vnops.c:2436`).
The call is **user-driven and reliable** — it does not depend on the create's hash path happening to
match `c_childhint`, which is why `fsync` is the right lever and not a hope that the node is already
addressed.

## What this arm is

905d, byte-for-byte, except `entry_ramdisk.s` gaining three words after the write:

```
    svc     #0x80       /* +316: the write */
    mov     r0, #0      /* +320: fd = 0 - the descriptor open returned */
    mov     r12, #SYS_FSYNC  /* +324: 95 */
    svc     #0x80       /* +328: the flush */
    b       park        /* +332: 508's shared park */
```

The fd is the **literal 0** because that is what `open` returned in 905's run
(`xnu_live_open_fd = 0`). The program went **88 → 91 words**; no arm switch changed
(`STAGE90_XNU_POST_END_TICKS = 115200000` preserved — see 905d).

The **card image** moved too (`b321db0d` → `d9d2ade4`): the card's `/sbin/launchd` **is** this same
fixture (`tools/build_hfs_root_image.sh` extracts it from `xnu_arm_entry_ramdisk.o`), so a press with
the old card image would have exec'd the old launchd — the easy-to-miss third part.

## Verification

- **Built.** Entry image `f347d060…` (0 refusals, 24 arm keys); `make check` **exit 0**;
  `tools/host_ramdisk_macho_check.py --selftest` — **all 151 mutations refused** (the 3 new fsync
  ones included). `PROGRAM_WORDS` 91, all shifted word indices (`PARK_*` 84–89, `FAILED_WORD` 90)
  updated; `syscall_sync_family("fsync")` reads the number from `syscalls.master`.
- **Parked** at `out/stage90/frozen/armed-storage-f347d060/` (11 members), renamed `-spent` after the
  press; `verify_revert_set.sh` and `check_set_name_rule.sh` both pass.
- **Gate 5/5** (`verify_press_ready.sh`); device `4a2fe00b` live.

## The press — 2026-10-06 (arm `armed-storage-f347d060-spent`)

Run **exit 0**, device back, `4a2fe00b` re-enumerated, no brick. Self-ending fired
(`xnu_live_post_end_calls = 0x4`).

### The write path (unchanged from 905d, still clean)

`xnu_live_storage_wr_calls = 1`, `_wr_complete = 1`, **`_wr_err = 0`**, **`_wr_data_err = 0`**,
`_wr_prog_timeout = 0`. **Three** write ranges this run: `0x00400002` (volume header),
`0x004000d0–df` (catalog B-tree node), and **`0x00400390–397`** (8 blocks = one 4096-byte HFS node at
partition offset `0x72000`) — the third is the new one, and it is the file's **data** block.

### The catalog now holds the file

Read the `userdata` head back host-side (`adb dd`, LBA `0x400000`, `seek=0`) and parsed the catalog
leaf at `0x1b000` against its own offset table:

| | clean (pre-image) | after 906 |
| --- | --- | --- |
| `numRecords` | 10 | **12** |
| records | sbin, HFS+ Private Data, private dir, threads | … **+ `newfile`** |

The new record is a **file** (`recordType = 2`):

- key: `parentID = 2` (root), `nameLength = 7`, name **`newfile`** (UTF-16-BE);
- `fileID` (cnid) = **20**; a matching empty-name **thread record** for cnid 20 also present;
- **`dataFork logicalSize = 4`**, `totalBlocks = 1`, extent `startBlock = 0x72` (byte `0x72000`);
- the data block at `0x72000` holds **`00 03 00 00`** = the 4 bytes the fixture wrote, read back.

Volume header: `fileCount` **0 → 1**, `folderCount` 3, `nextCatalogID` 20, `writeCount` 2,
`lastMountedVersion` `10.0`, unmounted bit cleared. **451 bytes differ** from the clean pre-image, in
the volume header, the catalog node and the file's data block.

> The file's data is `0x00030000`, not the page address the fixture stores at `mmap`'s return
> (`0x00102000`). That is correct and not a defect: the program re-uses the same page for `wait4`'s
> status pointer, and `wait4` writes the child's exit status `W_EXITCODE(3,0) = 0x300` into it
> **before** the write. So `write(fd, page, 4)` sends exactly `00 03 00 00` — the fixture's own bytes,
> chosen by the program, round-tripped through HFS+ and the eMMC.

## Verdict

**The goal's storage clause is met on both halves.** XNU mounts the device's own eMMC read-write and
now **writes a file to it that persists** — the name is in the catalog and the data is in the block,
both read back off the medium after the run returned. `fsync` was the missing step; it is not
incidental — a Unix file write that is not synced is, by design, not guaranteed to persist.

**Honest scope.** The HFS+ volume on `userdata` was written by the operator host-side; 906 writes to
it *from XNU* and proves persistence, but the volume's existence is still operator-supplied (the eMMC
is not reformatted to HFS+ by XNU). The goal's read and write storage clauses are both closed; the
root still comes from the card image at `userdata`'s head, and adding `fsync` to the fixture is the
whole of this arm.

Doc for the lineage: `experiment-905-write-through-the-emmc.md` (905),
`experiment-905d-the-write-rungs-defect-was-a-build-switch.md` (905d).