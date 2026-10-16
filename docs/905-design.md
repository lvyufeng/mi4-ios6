# 905 — WRITE THROUGH THE eMMC: the eMMC write path, an rw HFS+ root, and a fixture that creates a file

**This document is a RE-DERIVATION against the tree as it stands, not a new design.** The plan it
consolidates (`~/.claude/plans/zesty-doodling-rocket.md`) was written **before** rungs 904/905 were
built. They have since been **built, pressed, and read** (`ca4f4aa` 904; `01ba8ce` 905; `c08dd63`
905c; `3393aea`/`c5534a8` 905d; `906`). Every line reference the plan gives was re-checked here; the
ones that are wrong are named in §6, and the corrections are the corrected values, not the plan's.
No build, no press, no device action, no write to `out/stage90` was performed while authoring this.

**Verdict up front.** Rung A (904) **STANDS** — the exec-page read is now in the capture. Rung B (905)
**STANDS on the write half** (the volume header changed on the device and persisted) and needed a
third step the plan did not name — **906's `fsync`**, without which the created file's catalog record
died in the buffer cache and never reached the medium. §4 records that.

---

## 0. WHAT IS ALREADY ON THE DEVICE (the newest datum)

| rung | arm | press | verdict |
| --- | --- | --- | --- |
| 904 | (cap 8192→16384) | 2026-10-06, exit 0 | **exec-page read in the log, no `xnu_live_capped`** (`experiment-904-live-cap-raised-to-16384.md`) |
| 905 | first write arm `bb2269bf` | did not return | early exec failure — the write fix had not yet been reached |
| 905b/905c | `9d2dd2e4` / `d6e2fbe6` | did not return | **the compiled-out self-ending**, not the write code (`905d`) |
| 905d | `armed-storage-0e6eb4b4` | 2026-10-06, exit 0, 28 s | **write path proven**; `/newfile` name NOT in the catalog |
| 906 | `armed-storage-f347d060` | 2026-10-06, exit 0 | **create PERSISTS** — `/newfile` in the catalog, 4 bytes in its data block |

So this doc's Rung B is not a plan; it is a record of what the rungs did, with the plan's own
references corrected against the tree.

The pre-905 baseline still in the tree is the `EROFS` refusal in `st_media_strategy` and the read-only
root; both are **switch-gated** and the OFF build is byte-identical (`905`'s own `#else` arms).

---

## 1. RUNG A (904) — prove the exec-from-card read

### 1.1 The defect as it was

The live-record channel dropped every record past the cap and published `xnu_live_capped` once. 903's
boot needed ~8206 records and the `launchd __TEXT` page read landed **past** the old cap, so the exec
was concluded from console silence, not from a logged read of the page off the card.

### 1.2 The change (one line) — and its correct site

The plan said `src/entry/entry_stubs.c:2256`. **That is wrong** — `:2256` is inside
`stage90_read_l2_word_aliased`. The real site, **as of this writing**, is:

- **`src/entry/entry_stubs.c:4254`** — `#define ENTRY_LIVE_CAP 24576u`. The plan's target (raise 8192
  → 16384) was made in **904** (`ca4f4aa`) and then **raised again by 999 to 24576** on 998's
  measurement (the cap truncated mid-dyld). 904's own contribution is recorded at `:4397` in the
  file's block comment. **Do not "restore" 16384** — that would rebuild 999's wall.
- **`src/entry/entry_stubs.c:4443-4448`** — the drop, in `entry_live_write`: past the cap it publishes
  `xnu_live_capped` **once** and returns without writing the record. (The plan cited `:2432-2451`;
  wrong.)
- **`src/entry/entry_stubs.c:4370`** — `entry_write_kv("xnu_live_cap", ENTRY_LIVE_CAP)`; the header
  publishes the cap. (The plan cited `:2372`; wrong.)
- The plan's "`entry_write_kv` hard bound `max = 0x00200000u - 12u`, `:2518`" is a **real**
  constant at a **wrong** line: the string writer's bound is **`entry_stubs.c:4019`** (in
  `entry_write`). The live channel's writer is **`entry_write_kv` at `entry_stubs.c:4540`**, and it
  carries **its own separate** `const uint32_t max = 0x00200000u - 12u;` at **`:4546`**. The plan
  conflated the two writers.
- **`scripts/run_and_capture.sh` — no edit needed.** Confirmed: it reads `xnu_live_cap` and derives
  the truncation verdict from it (~`:1023-1092`). The plan's `:973,980` / `:978-983` are stale; the
  correct block is the `live channel:` summary around `:1023-1092`.

### 1.3 The pre-registered verdict cells

| # | condition (read from the 904 capture) | verdict | falsification |
| --- | --- | --- | --- |
| **A-PROVEN** | `xnu_live_rootmedia_card_off = 0x00022000` **and** `_card_last_lba` at the exec page, **no `xnu_live_capped`** line, runner says "not truncated" | **903 STANDING** | — |
| **A-FALSIFIED** | still no exec-page read with an un-truncated channel | **903 WRONG** | the page read is not from the card |
| **A-CTL** | no `xnu_live_cap` at all / no `xnu_live_*` records | **INSTRUMENT_UNSOUND** — withhold | — |

**Measured: A-PROVEN** (`experiment-904-live-cap-raised-to-16384.md`, press 2026-10-06, exit 0). The
honest risk the plan named — "A can falsify 903" — did not fire.

### 1.4 Device gate

Low blast radius (no new device behaviour). Still `preflight_boot_check.sh --allow-xnu-entry`,
`fastboot boot` only, `[[mi4-hardware-run-safety-gate]]`.

---

## 2. RUNG B (905) — the write-through rung, in three parts

### 2.1 B1 — a CMD24 write path in the ladder and the strategy

**The ladder** (`src/entry/entry_storage.c`). The plan's references, checked:

| plan says | actual |
| --- | --- |
| `entry_storage_driver_read` `:7578-7593` | **`:7721`** (body `:7721`…`:7733`) |
| `st_read_single_block` `:7193-7562` | **`:7294`** |
| `entry_storage_selected_lba()` `:9871` | **`:10248`** |
| `entry_storage_selected_count()` `:9879` | **`:10256`** |
| read TRANSFER_MODE `ST_SDHCI_TRNS_READ_1BLK = 0x0012` at `:2609/2658-2665`, used `:7322` | defined **`:2730`** = `ST_SDHCI_TRNS_BLK_CNT_EN \| ST_SDHCI_TRNS_READ`; its `_Static_assert(== 0x0012u)` at **`:3638`**; written in the read body at **`:7440-7441`**, and separately in the probe at `:6840-6841`. The plan's `:7322` is wrong. |
| CMD17 = 17 at `:2322` | **`ST_CMD_OP_READ_SINGLE_BLOCK 17u` at `:2365`** |

**What 905 actually added** (the mirror the plan asked for), all under
`#if STAGE90_XNU_HDD_WRITE` (ladder-local `#ifndef … 0` default at `:174-175`; a 0/1 refusal at
`:177-178`; a rung floor refusal `HDD_WRITE needs STORAGE_PROBE>=60` at `:180-181`):

- `#define ST_CMD_OP_WRITE_BLOCK 24u` at **`:2386`** — **the write opcode**, with the argument spelled
  as the **same LBA CMD17 would be given** (`:2396`).
- `#define ST_SDHCI_TRNS_WRITE_1BLK (ST_SDHCI_TRNS_READ_1BLK & ~ST_SDHCI_TRNS_READ)` at **`:2749`** —
  the read bit cleared, exactly the plan's "read bit cleared" (`ST_SDHCI_TRNS_BLK_CNT_EN = 0x0002`,
  no `0x0010`).
- one sector's own array `static uint32_t st_write_block[ST_EXT_CSD_WORDS];` at **`:3177`**.
- **`entry_storage_driver_write(uint32_t lba, const uint32_t *w)` at `:7958`** — the door.
- the write body **`:7743`-`:7971`** (the plan's "`st_write_single_block` mirror of `:7193-7562`").

The plan's v1 body asked for a `BUFFER_WRITE_READY` wait in PRESENT_STATE and **vendor bounds 10 ms /
100 ms / 10 s** (`[[mi4-vendor-bounds]]`) — both are in the built body, and 905's press needed neither:
the card accepted all 17 blocks with `_wr_err = 0`. **905b found the real missing wait** and it was not
the one the plan named: a write that returns before the card finishes **programming** the block makes
the next CMD17 (the `launchd` page read) time out with `rd_err = 0x00010000` (ENOEXEC). The fix is a
bounded **`DOING_WRITE|DAT_LINE_ACTIVE`** poll (`xnu_live_storage_wr_prog_*`, `_wr_prog_polls`).
905c added the write's own **verdict** cells so a write that ended in a DATA error could not be recorded
as success: `xnu_live_storage_wr_int_status_final`, `_wr_int_data_end`, `_wr_data_err`.

New keys published: `xnu_live_storage_wr_lba`, `_wr_calls`, `_wr_err`, `_wr_held`, plus the programming
and verdict cells above.

**The strategy** (`src/platform/stage90_root_media.c`). The plan's references, checked:

| plan says | actual |
| --- | --- |
| `st_media_strategy` `:570-725` | **`:898`** |
| `EROFS` refusal `:629-633` | the refusal is **`:989-1005`**; the `buf_seterror(bp, EROFS)` call is **`:999`** |
| card read branch `:673-702` | the card branch is under `#if STAGE90_XNU_EMMC_STRATEGY` beginning about **`:1024`** |
| `DKIOCISWRITABLE` already returns 1 `:903-905` | `case DKIOCISWRITABLE` is at **`:1432`**; it previously returned **1 unconditionally** and 905 made it per-unit |

**What 905 actually did** (and it is per-unit, exactly as the plan asked):

- the `EROFS` guard at **`:989-1005`** is now `if ((buf_flags(bp) & B_READ) == 0)` with an inner
  `#if STAGE90_XNU_HDD_WRITE || STAGE90_XNU_CARD_COW` widening that refuses only for
  `unit != ST_MEDIA_DRIVER` (`:995`). With the switch off it folds back to the old behaviour.
- the **write branch** under `#if STAGE90_XNU_HDD_WRITE` at **`:1117-1159`**: map the buf, per-512 B
  block copy the caller's map into one local 128-word buffer (`st_write_stage`) and call
  `entry_storage_driver_write(lba, st_write_stage)` at **`:1143`**, then `buf_unmap` / `buf_setresid` /
  `buf_biodone`. The **LBA arithmetic is IDENTICAL to the read branch** (`selected_lba + blkno + i`) —
  the plan's requirement, and the file says so in its comment (`:1123-1126`).
- the mirror door is declared at **`:534`**, and it **returns its own buffer**, which is why the caller
  must copy (the plan's "maps the buf, loops per 512 B block" is right; the local-buffer requirement
  was a detail it did not have).
- `DKIOCISWRITABLE` at **`:1432-1446`**: `#if HDD_WRITE || CARD_COW` → `1` iff `unit ==
  ST_MEDIA_DRIVER`, else `0`. It is now **consistent** with the strategy, and a **build clause** keeps
  the two from disagreeing (`mi4-a-claim-in-a-comment-is-not-a-check`).

New keys: `xnu_live_rootmedia_card_wr_blocks`, `_card_wr_first_lba`, `_card_wr_last_lba`,
`xnu_live_rootmedia_write_served`, `xnu_live_rootmedia_write_refused`.

### 2.2 B2 — the HFS+ root mounts read-write

This is the plan's one-blocker analysis, and it is **correct on the substance**:

- `vfs_subr.c` hard-codes `mp->mnt_flag = MNT_RDONLY | MNT_ROOTFS` at **`vfs_subr.c:989`** — confirmed.
- `hfs_mountfs` latches it once: `ronly = vfs_isrdonly(mp)` at **`hfs_vfsops.c:1318`** (plan said
  `:1312`; the `#if STAGE90_HFS_ROOT_RW` clear 905 inserted shifted the line), and sets
  `HFS_READ_ONLY` at **`:1524`** (plan said `:1517-1518`). The volume's own inconsistent-disk gate
  exempts `MNT_ROOTFS` (`:373`, `:1730`) — confirmed.
- `DKIOCISWRITABLE` is honored at **`hfs_vfsops.c:1564`** (plan said `:1558-1563`; the 905 edit moved it).
- `hfs_mountroot` is at **`:178`**; the `hfs_mountfs` call is at **`:191`** (plan said `:185`).

**The edit as built** is the plan's: a guarded clear immediately before `hfs_mountfs`
(`hfs_vfsops.c:186-190`):

```c
#if STAGE90_HFS_ROOT_RW
	vfs_clearflags(mp, (u_int64_t)MNT_RDONLY);
#endif
```

The plan proposed threading it through `tools/patch_vfs_conf_hfs_row.py` "or a sibling patch". It
became a **sibling**: **`tools/hfs_patch_root_rw.py`** (idempotent; a missing anchor is a loud error;
`tools/check_hfs_staged.sh` re-derives the effect). The plan's `mi4-hfs-staged` note is the right
design. `STAGE90_HFS_ROOT_RW` is a **POOL** define — it rides `tools/build_xnu_arm_kernel.sh:720`
(`HFS_PORT on && STAGE90_XNU_HFS_ROOT_RW=1`, as `-DSTAGE90_HFS_ROOT_RW=1`; the source guard is
`STAGE90_HFS_ROOT_RW`, not the `STAGE90_XNU_` arm-name — the two-carrier split, §7).

### 2.3 B3 — the fixture creates a file

The plan cited `src/entry/entry_ramdisk.s` (8192 B blob) and said "it never writes, opens for write, or
creates". **True when the plan was written; false now.** 905 extended the pid-1 program and 906 added
the flush:

- **`entry_ramdisk.s:1273-1285`** — `open("/newfile", O_CREAT|O_WRONLY, 0644)` (fd returned in `r0`),
  `write(fd, page, 4)`, then (906) `fsync(0)`, then the existing `park` tail.
- `path_new: .asciz "/newfile"` at **`:1341-1342`**; `OPEN_CREATE_WRITE 0x201` at **`:872`**;
  `SYS_FSYNC` is syscall 95.
- The blob is `RAMDISK_BYTES = 0x2000` (**8192**) at **`:966`**, gated by the file-range assertions at
  `:1356-1372`; `/sbin/launchd` in the card image **is this same fixture** (`tools/build_hfs_root_image.sh`
  extracts it from `xnu_arm_entry_ramdisk.o`), so the card image's hash moves whenever the fixture does
  — the "easy-to-miss third part" 906 calls out.

**906's necessity, recorded honestly.** Without `fsync`, HFS+ leaves the catalog B-tree node dirty in
the buffer cache (`bdwrite_internal(bp, 1)`) — the create returned 0, the **volume header and the
private directory** persisted (905d measured 375 bytes changed on the medium), but the **file's name
never reached the card**. `fsync(fd)` reaches `hfs_vnop_fsync → hfs_fsync`, whose `hfs_metasync`
`VNOP_BWRITE`s exactly that node. 906 measured the name in the catalog afterwards
(`experiment-906-fsync-makes-the-create-persist.md`). The plan did **not** name this step; it is the
one substantive addition to Rung B.

### 2.4 The device volume: operator-supplied vs XNU-written (the honest scope)

**Operator-supplied (host-side, not XNU):** the HFS+ **volume exists at all** — it is written onto
`userdata`'s head (byte `0x80000000` = LBA `0x400000`) by the press procedure before the run
(`scripts/press_905d.sh` dd's the clean card image with `seek=0`). XNU never reformats the eMMC to
HFS+; the filesystem's creation, and the `/sbin/launchd` fixture inside it, are the operator's.

**XNU-written (measured on the medium):** every byte XNU changed on that volume — the HFS volume
header (`lastMountedVersion` → `10.0`, `modifyDate`, `writeCount`, `nextCatalogID`, `folderCount`,
unmounted-bit cleared), the private directory the rw mount materialises, the catalog B-tree node, and
(906) the `/newfile` record plus its **data block**. These were read back off the eMMC host-side
(`adb dd`, `seek=0`) and compared to the clean pre-image.

So: the **goal's storage clause is closed on both halves** — XNU mounts the device's own storage and
writes a file to it that **persists**. The volume's *existence* is still operator-supplied, and this
document says so rather than implying otherwise.

### 2.5 The write gate

The plan proposed a **new separate** gated procedure because `scripts/preflight_storage_write.sh`
refuses `userdata` by name (only `boot`/`recovery` are on its low-risk list; `:36-38`). That script
still refuses `userdata` and writes nothing. What the 905/906 presses actually used is the press
script (`scripts/press_905d.sh`), which performs the `userdata`-head rewrite under the **hardware gate
rule**: `fastboot devices` must list `4a2fe00b` and `33e80afe` must be **absent**, `fastboot boot` only
(never flash), and the pre-image is backed up (the clean card image is the backup). This is the plan's
intent; the record of it lives in the press scripts and `docs/reference/recovery-and-rollback.md`
rather than in a second preflight.

---

## 3. PRE-REGISTERED VERDICT CELLS (Rung B)

| # | condition | verdict | falsification |
| --- | --- | --- | --- |
| **B-WROTE** | `xnu_live_storage_wr_calls >= 1`, `_wr_err = 0`, `_wr_data_err = 0`, a `B_WRITE` call at LBA `0x400000+` (`xnu_live_rootmedia_card_wr_*`) | **WRITE PATH PROVEN** | `_wr_err != 0` or `_wr_data_err != 0` |
| **B-PERSISTS** | after a power cycle, `/newfile` is on the medium with its bytes (catalog record + data block) | **GOAL MET (write half)** | `/newfile` absent on read-back |
| **B-MOUNT-RO** | `xnu_live_open_flags = 0x201` but `_open_error != 0`, root is `MNT_RDONLY` | **the rw clear did not reach the pool** | — |
| **B-CTL** | the write arm's marker `entry_root_media_write_arm_on` is in the ELF but no `xnu_live_storage_wr_*` | **INSTRUMENT_UNSOUND** — withhold | — |

**Measured:** 905d = B-WROTE (and, refuted, B-PERSISTS on the *name* only — the volume persisted, the
file's name did not). 906 = B-WROTE **and B-PERSISTS** (`numRecords` 10 → 12, `newfile` recordType 2,
`fileID` 20, `dataFork logicalSize = 4`, data block `0x72000` = `00 03 00 00`, read back).

---

## 4. THE BUILD REFUSALS (the plan's "keys to add", as built)

- **`STAGE90_XNU_HDD_WRITE`** (default 0) — exists and is fully threaded; see §7.
- **Linked-image clauses** (`src/entry/build_entry.sh:30260-30302`):
  - HDD_WRITE=1 → `entry_storage_driver_write` must be a defined `T` symbol (`:30269`) **and**
    `st_media_strategy`'s disassembly must `bl <entry_storage_driver_write>` (`:30287`);
  - HDD_WRITE=0 → the symbol must be **absent** (`:30293`) — the negative direction.
  The plan also asked for a **CMD24-by-value** clause; the built clause checks the **call**, and the
  CMD24 *value* is pinned instead by the ladder's `_Static_assert` at `entry_storage.c:3638` (read
  side) plus the write body's own writes. **If a CMD24-by-value linked clause is wanted, it is not yet
  written** — say so rather than implying it. (The 905 `st_media_strategy` clause reads the `bl`; a
  separate clause over the *strategy*'s CMD24 constant does not exist.)
- **The two-way split** (`DKIOCISWRITABLE=1` **iff** the write branch exists) — a build refusal, not a
  comment: `preflight_boot_check.sh`'s `ENTRY_CFG_KEYS` artifact-grounded block (`:815-825`, `:901`)
  and `build_entry.sh`'s pair checks. `[[mi4-a-claim-in-a-comment-is-not-a-check]]`.
- **The rw switch bound to a record** (905c C2): `STAGE90_XNU_HFS_ROOT_RW` is a recorded arm key,
  **required** by the write arm, and read back out of the linked `hfs_mountroot` in **both** directions
  (`build_entry.sh:30022-30115`). Without it `HDD_WRITE=1` could be recorded while the pool's
  `hfs_mountroot` had the clear compiled out.

---

## 5. VERIFICATION (end to end, as it stands)

- **Build:** `nm out/stage90/xnu_arm_entry.elf | grep entry_storage_driver_write` present under
  `HDD_WRITE=1` and absent under `0`; `xnu_entry_905` green; `check_hfs_staged.sh` exit 0 with the
  `hfs_mountroot` patch; the linked `hfs_mountroot` calls `vfs_clearflags` iff `HFS_ROOT_RW=1`.
- **Press A:** `xnu_live_cap = 0x4000` (16384, the 904 arm), no `xnu_live_capped`, exec-page read in
  the log. **(met)**
- **Press B (906):** `_wr_calls = 1`, `_wr_complete = 1`, `_wr_err = 0`, `_wr_data_err = 0`; write
  ranges `0x00400002`, `0x004000d0–df`, `0x00400390–397`; `open("/newfile")` returns 0; after `adb dd`
  + power cycle the catalog holds `newfile` and the data block holds `00 03 00 00`. **(met)**
- **No brick:** device re-enumerates `4a2fe00b`; `33e80afe` never appears; fastboot reachable; the
  self-ending fires (`xnu_live_post_end_calls = 0x4`) so the run returns.

---

## 6. THE PLAN'S LINE REFERENCES — CONFIRMED OR CORRECTED

**WRONG (corrected):**

| plan reference | correction |
| --- | --- |
| `entry_stubs.c:2256` cap 8192u | **`:4254`**, and the value is **24576u** (904 set 16384; 999 raised it) |
| `entry_stubs.c:2432-2451` `entry_live_write` drop | **`:4433`** (drop at `:4443-4448`) |
| `entry_stubs.c:2518` `entry_write_kv` bound `max = 0x200000-12` | **`:4019`** is `entry_write`'s own bound; `entry_write_kv` is at **`:4540`** with `max` at **`:4546`** |
| `entry_stubs.c:2372` `xnu_live_cap` echo | **`:4370`** |
| `run_and_capture.sh:973,980` / `:978-983` | the cap read / truncation verdict is at **~`:1023-1092`** |
| `stage90_root_media.c:570-725` strategy | **`:898`** |
| `stage90_root_media.c:629-633` EROFS | the guard is **`:989-1005`**, `buf_seterror(..., EROFS)` at **`:999`** |
| `stage90_root_media.c:673-702` card read branch | begins about **`:1024`** (under `#if STAGE90_XNU_EMMC_STRATEGY`) |
| `stage90_root_media.c:903-905` `DKIOCISWRITABLE` | **`:1432`** |
| `entry_storage.c:7578-7593` `entry_storage_driver_read` | **`:7721`** |
| `entry_storage.c:7193-7562` `st_read_single_block` | **`:7294`** |
| `entry_storage.c:9871/9879` selected accessors | **`:10248` / `:10256`** |
| `entry_storage.c:2609/2658-2665`, `:7322` read trns | defined **`:2730`**; written **`:7440-7441`** |
| `entry_storage.c:2322` CMD17=17 | **`:2365`** |
| `hfs_vfsops.c:1312` `ronly=` | **`:1318`** |
| `hfs_vfsops.c:1517-1518` `HFS_READ_ONLY` | **`:1524`** |
| `hfs_vfsops.c:185` `hfs_mountfs` call | **`:191`** |
| `hfs_vfsops.c:1558-1563` `DKIOCISWRITABLE` | **`:1564`** |
| `hfs_vfsops.c:1575` standard-HFS rejection | the `kHFSSigWord` rejection is about **`:1589`** (`if (vfs_isrdwr(mp))` under it); it does not block HFS+ |

**CONFIRMED:** `vfs_subr.c:989` (`mp->mnt_flag = MNT_RDONLY | MNT_ROOTFS`);
`mount.h:881` (`vfs_clearflags` declared); the `hfs_vfsops.c` `MNT_ROOTFS` exemptions at `:373`/`:1730`;
the plan's claim that `DKIOCISWRITABLE` was honored for the mount; `tools/patch_vfs_conf_hfs_row.py`
exists (`ROW_ADD`, `vfc_mountroot = hfs_mountroot`, before mockfs).

---

## 7. APPENDIX — THE SWITCH-THREADING SURVEY (`STAGE90_XNU_HDD_WRITE` and the two-carrier trap)

The hazard is `[[mi4-one-value-two-definitions]]`. `STAGE90_XNU_HDD_WRITE` is an **entry-side**
switch (it changes `src/entry/entry_storage.c` and `src/platform/stage90_root_media.c`), unlike 1020's
`STAGE90_XNU_UNDEF_RAISE` (a **pool** switch). The two have **different carrier sets**, and that is the
point of the survey. Every site, by value:

### 7.1 An ENTRY-side arm switch (the `HDD_WRITE` shape — what a new entry switch must hit)

1. **Resolve + validate.** `src/entry/build_entry.sh:862` `HDD_WRITE=${STAGE90_XNU_HDD_WRITE:-0}`;
   `:865` refuses a non-0/1 value (`[[mi4-off-option-two-spellings]]`).
2. **Cross-switch refusals.** `:869` (needs `EMMC_STRATEGY`), `:875` (needs `ROOT_FROM_CARD`), `:922`
   (needs `HFS_ROOT_RW`), `:976` (mutually exclusive with `CARD_COW`).
3. **`ENTRY_ARM_KEYS`.** `build_entry.sh:2054`… — `STAGE90_XNU_HDD_WRITE` at **`:2064`**. (The record
   carries 24 arm keys; adding one key without this list means the record omits the arm.)
4. **The record resolver.** `build_entry.sh:2207` `STAGE90_XNU_HDD_WRITE) _v=$HDD_WRITE ;;` — the loop
   that turns the key list into record text. A key in `ENTRY_ARM_KEYS` with no `case` here has no value.
5. **The record echo.** `build_entry.sh:40143` `echo "STAGE90_XNU_HDD_WRITE=$HDD_WRITE"` — the
   **RESOLVED** value, never `${STAGE90_XNU_HDD_WRITE}` (a defaulted switch and an unset one are the
   same image and two different records).
6. **The entry `-D`.** `build_entry.sh:2603` and `:2617` (the compile and the syntax-only pass) —
   `-DSTAGE90_XNU_HDD_WRITE="$HDD_WRITE"` on the `entry_storage.c` command.
7. **The platform `-D`.** `stage90_root_media.c` is compiled by **`tools/build_xnu_arm_kernel.sh`**
   (`:1891` sources, `:2025-2027` compile), which does **not** auto-`-D` an arbitrary `STAGE90_XNU_*`
   — it receives it through **`EXTRA_DEFINES`**, i.e. **`XNU_KERNEL_EXTRA_DEFINES`**
   (`build_xnu_arm_kernel.sh:1258-1260`). So the platform half is a **two-carrier** switch (§7.3).
8. **The linked-image clause.** `build_entry.sh:30260-30302` (both directions) + the pair check at
   `:28897-28916` (the platform object's `HDD_WRITE` must equal the entry build's).
9. **`preflight_boot_check.sh`.** `ENTRY_CFG_KEYS` at **`:722`**, the artifact-grounded N/A gate at
   **`:815-825`** and **`:901`**.
10. **No assemble/asm half.** `HDD_WRITE` touches no `.s` file; the plan's "assemble half" question
    resolves to **none** for this switch.

### 7.2 A POOL-side arm switch (the `UNDEF_RAISE` shape, for contrast)

`STAGE90_XNU_UNDEF_RAISE` reaches the **kernel pool** (`out/xnu_kernel_obj_d13/osfmk_arm_trap.o`),
which `build_entry.sh` only **LINKS**. Its carriers, by value:

1. `tools/build_xnu_arm_kernel.sh:872` `[[ ${STAGE90_XNU_UNDEF_RAISE:-0} -eq 1 ]] && DEFINES+=(-DSTAGE90_XNU_UNDEF_RAISE=1)`
   — **a second spelling** the entry `-D` does not drive. (1020's switch already lives here.)
2. `XNU_KERNEL_EXTRA_DEFINES` carries `-DSTAGE90_XNU_UNDEF_RAISE=1` on the build (the pool channel —
   `stage_d13_globalfix.sh:31-32`, measured not assumed).
3. `build_entry.sh:1648` resolve, `:2107` `ENTRY_ARM_KEYS`, `:2296` resolver, `:40251` record echo,
   `:31345-31358` the linked `sleh_undef` clause (ON) with its OFF shape too.
4. `preflight_boot_check.sh:762` `ENTRY_CFG_KEYS`.
5. **Plus the staging tool** (`tools/stage_d13_undef_raise.sh` / `patch_d13_undef_raise.py`) and the
   one-file allow-list coupling with the sibling patch (`records/1020-build-defects.txt`).

### 7.3 THE TWO-CARRIER TRAP (the class the survey exists to catch)

`STAGE90_XNU_HFS_ROOT_RW` is the clean example. It is a **pool** switch whose *name* looks entry-side
(`STAGE90_XNU_…`), and it has **two carriers that must agree**:

- **`build_xnu_arm_kernel.sh:720`** turns the arm into the **source-level** define
  `STAGE90_HFS_ROOT_RW` (the `#if` in `hfs_vfsops.c:186` is over `STAGE90_HFS_ROOT_RW`, **not**
  `STAGE90_XNU_HFS_ROOT_RW`). Env-export + this line is the **pool** carrier.
- **`build_entry.sh:892` / `:2065` / `:2208` / `:40147`** record it as an **arm key** — the **record**
  carrier.

A build that exports `STAGE90_XNU_HFS_ROOT_RW=1` for the record but does **not** rebuild the pool with
it produces a record that names an rw root over a linked `hfs_mountroot` with the clear compiled out —
exactly 905c C2's defect. The guard is the **linked-image** clause (`build_entry.sh:30022-30115`): it
disassembles `hfs_mountroot` and refuses in **both** directions.

**ENV-exported AND `-D`-passed (two carriers):** every switch whose effect is in a **pool** or
**platform** object that `build_entry.sh` does not itself compile — `STAGE90_XNU_HFS_ROOT_RW`,
`STAGE90_XNU_UNDEF_RAISE`, `STAGE90_XNU_GLOBAL_FIX`, `STAGE90_XNU_SHREG_WIRE`, and **`HDD_WRITE`'s
platform half**. Merely setting the env is not enough for those: the pool/platform compile must see it,
via `DEFINES` (build_xnu_arm_kernel.sh:872/901 style) or `EXTRA_DEFINES`.

**Record-only (no `-D` obligation):** none, strictly — every arm key should reach at least one compiled
object. But a key may be **record-only relative to the pool** when its whole effect is in an entry
object `build_entry.sh` compiles itself (`HDD_WRITE`'s ladder half, `ROOT_FROM_CARD`, `CARD_TOTAL`).

**`GLOBAL_FIX` is the outlier: env-only, and NOT in the record, and it cannot be.** It is a pool
switch that **no writer in `src/entry/` echoes** — it is declared only in
`out/stage90/xnu_arm_entry-arm-extra.txt` (`STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix`), the
cross-lane carrier file the press scripts grep (`press_1018.sh:47-51`, `park_1018.sh:62-66`,
`press_1020.sh:48-53`). So an arm's switch set lives in **two** files at press time: the record and the
arm-extra file — a survey that checks only the record misses `GLOBAL_FIX` silently
(`[[mi4-self-written-record-is-not-a-constraint]]`).

### 7.4 The exact line `STAGE90_XNU_HDD_WRITE` must gain, if it ever did not have one

Each of §7.1's ten sites is the clause a **new** entry switch must add. For `HDD_WRITE` all ten already
carry it (verified by grep, sites 1-10 above). The lesson for the *next* entry switch: **the record
list (`ENTRY_ARM_KEYS`, `:2054`…), the resolver (`:2200`-…), and the record echo (`:40130`-…) are three
separate edits** — a switch added to only one produces a record that either omits the arm or prints the
**unresolved** env spelling, and neither is visible in the entry image's hash.