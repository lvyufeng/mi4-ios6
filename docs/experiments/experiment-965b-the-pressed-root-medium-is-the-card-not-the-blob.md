# 965b — the pressed root medium is the CARD image, not the blob (2026-10-09)

**A defect in 965a, found by reading the arm the press would actually run.** 965a flipped the *committed*
blob `src/entry/blob/xnu_arm_entry_root_hfs.img` from HFS+ v4 to HFSX v5 and parked arm
`armed-d13-b9c224c0`. But that arm carries `STAGE90_XNU_ROOT_FROM_CARD=1`, and on that arm the blob is
**never read**: the filesystem the kernel mounts is a *separate* image, `out/stage90/xnu_card_hfs.img`,
which 965a left HFS+ v4. So pressing `b9c224c0` would have mounted **HFS+**, not HFSX — the one thing
965a existed to change. 965b makes the medium the arm really reads HFSX, and makes it reproducible.

## 1. The defect, against the code

On a `STAGE90_XNU_ROOT_FROM_CARD=1` arm:

- `src/entry/entry_trace.c:5117-5135` — `#if STAGE90_XNU_ROOT_FROM_CARD` compiles
  `r = entry_root_media_register_card()` (`:5131`); the `#else` that would call
  `entry_root_media_register(devid)` (disk 0) is **not compiled**. `entry_root_media_register_card`
  registers only `ST_MEDIA_DRIVER` = 2 (`stage90_root_media.c:277,1367`), so **disk 0 gets no `bdevsw`
  major** and the blob's strategy branch (`stage90_root_media.c:574-575`) is unreachable.
- `src/entry/build_entry.sh:29016-29020` is a linked-image clause that **refuses** any image where
  `__wrap_mdevlookup` calls *both* `entry_root_media_register` and `entry_root_media_register_card` — so
  the blob is not merely unregistered, it is structurally forbidden from being registered on this arm.
- The mounted bytes are the card unit's: `base_lba = entry_storage_selected_lba()`
  (`stage90_root_media.c:829`) = the GPT-selected **userdata** partition (`_card_lba=0x400000`, 892/903);
  `bsd/vfs/vfs_conf.c:120`'s `hfs_mountroot` is the only live mountroot row, and it reads that device
  through `st_media_strategy`'s card branch (`:821-895`).
- Those bytes come from `out/stage90/xnu_card_hfs.img` — **HFS+ v4** (`0x482b 0004`, sha `d9d2ade4…`,
  last rebuilt for 906, before 965a) — written to `userdata`'s head by `scripts/press_906.sh:34-35`.

The blob 965a changed is `present-but-unread` (`.incbin` at `xnu_arm_entry_root_hfs.S:31`): it is linked
in, but no boot path on this arm dereferences it. Three independent readers of the code confirmed this
with high confidence, and an adversarial reviewer confirmed the fix direction is correct and build-safe.

**Corollary — the blob is not a *root* on any arm.** The earlier framing ("the blob is the medium on the
non-card HFS arm") is wrong: with `ROOT_FROM_CARD=0` the root is `g_stage90_ramdisk` (the Mach-O exec
image), not the HFS volume; `HFS_ROOT_MEDIA=1` only moves disk 0's *strategy*/`DKIOCGETMEMDEVINFO`
bytes. So the flippable, *load-bearing* root medium for a real iOS rootfs is the **card** — which is
exactly `ROOT_FROM_CARD`, the arm already pressed (903).

## 2. Why the card image was the wrong family, and how it went silent

The card image is produced by **no committed script** (`grep` finds only `press_905b/c/d.sh`,
`press_906.sh` — which `dd` it — plus docs). It lives under `out/`, which is gitignored
(`.gitignore:22`), and `press_906.sh:38` **hard-codes** its sha256 `d9d2ade4…`. Every check in the tree
(`make check`'s `check_hfs_root_blob.py`, `build_entry.sh`'s `hfs_size == 524288` clause) reads the
**committed blob**, and `check_hfs_root_blob.py` accepts *both* families — so a stale HFS+ card passed
every gate while the tree's fixture was HFSX. That is `mi4-a-self-written-record-is-not-a-constraint`
(a hard-coded sha in a press script is not a check) and `mi4-a-claim-in-a-comment-is-not-a-check` (the
`.S` header's `sha256 3fb9273c…` was stale, enforced by nothing) at once.

## 3. The fix

**One new builder, one new refusal, one new press script; no kernel change.**

- **`tools/build_root_volumes.sh`** (new) — extracts the one launchd fixture from
  `out/stage90/xnu_arm_entry_ramdisk.o`'s `.data.ramdisk`, builds the **card** volume as **HFSX**
  (`tools/build_hfs_root_image.sh -X`), and then **verifies the committed blob agrees**: both must be
  HFSX `0x4858 v5` and both must carry the same launchd at the generator's layout offset `0x22000`. It
  builds only `out/`; it does not touch a tracked file (the committed blob is a non-reproducible `mkfs`
  artifact and regenerating it every build would churn a tracked file for no gain — and it is inert on
  the arm that matters).
- **`scripts/preflight_boot_check.sh`** (new clause, after the entry-config keys) — when the record
  names `HFS_ROOT_MEDIA=1` **and** `ROOT_FROM_CARD=1`, read `out/stage90/xnu_card_hfs.img` and **refuse**
  unless its volume header is HFSX `0x4858 v5`. This grounds the family on **the card bytes the mount
  reads**, not on the record. Verified both ways: it passes on the HFSX card and **refuses a
  deliberately-HFS+ card** (rc=1) — the falsifier §6 states.
- **`scripts/press_965b.sh`** (new) — device gate, `build_root_volumes.sh`, `dd` the card image to
  `userdata`'s head (`/dev/block/mmcblk0p25`, `seek=0`, `conv=fsync` — the same write 905b–906 already
  do, touching **only** `userdata`), then the project's own gate + runner
  (`--expect-arm=armed-d13-b9c224c0`). No sha is hard-coded: it prints the built sha and compares the
  device read-back to it.
- **Stale labels corrected**: `xnu_arm_entry_root_hfs.S:2,6` (title said "HFS+"; the sha read
  `3fb9273c…`) and `build_entry.sh:770-771,1734` (the missing-image message said "HFS+"). These are
  comments and one error string — **byte-neutral** (proved by rebuild+cmp, §4) — but the `.S` title
  directly contradicted the one property 965a moved.

**The entry image is unchanged.** 965b edits no compiled source; the arm's switch set is exactly
`armed-d13-b9c224c0`'s. The press boots the same entry bin (`b9c224c0`) and only the medium on the device
moves — the smallest possible variable.

## 4. Verification (host-side, no press)

- `tools/build_root_volumes.sh` rc=0: card and blob both `HFSX 0x4858 v5`, both carry the launchd at
  `0x22000`; the card's launchd is byte-identical to the fixture extracted from the current ramdisk object.
- `tools/check_hfs_root_blob.py` rc=0 on the card image (`HFSX 0x4858 v5`, 128 blocks, nodeSize 4096).
- preflight **refusal** fires on an HFS+ card (rc=1) and **passes** on the HFSX card (rc=0).
- the entry image **rebuild** on the recorded switch set reproduces `b9c224c0` **byte-for-byte** — the
  comment/string edits are byte-neutral and the parked arm is untouched (`mi4-linked-code-order-is-not-source-order`
  is not in play: no code moved).
- `make check` rc=0.

**PRESS IS THE OPERATOR'S.** Nothing here is pressed and nothing here writes a device.

## 5. What this proves, and what it does not

- **Proves (on press):** on a card-root arm, `hfs_mountroot` parses an **HFSX** volume (`0x4858 v5`) for
  the first time — the driver branch `bsd/hfs/hfs_vfsutils.c:339-345` no prior arm reached. If the HFSX
  parse path is broken, it fails here, at the proven 512 KiB size, with the medium change the only
  variable.
- **Does not prove:** the **896 MiB** scale. The real iOS 7.1.2 rootfs is 896.0 MiB; the card branch is
  already size-generic (its loop is 64-bit, `stage90_root_media.c:861,884`, and the card unit's length
  is `entry_storage_selected_count() * 512`, `:609` — the *selected partition's* extent, not 524288), so
  the 512 KiB arm exercises the **family**, not the scale. Serving the real volume is a distinct, larger
  rung: the card branch issues **one `entry_storage_driver_read` per 512-byte block**, so an 896 MiB
  mount is a *throughput/driver* problem, not a size cap. **965b is the signature rung; the 896 MiB path
  is §965.3b.**

## 6. What would falsify this

- The press's RAM console still reads a **v4** volume header where the mount reads the card → the medium
  identification (§1) is wrong.
- The preflight passes with a **deliberately HFS+ card** → the new clause is not artifact-grounded (it
  refuses, §4).
- A **signature-only** change fails to mount where v4 succeeded → the HFSX branch
  (`hfs_vfsutils.c:339-345`) or a case-sensitive path is broken; that becomes the next rung, isolated
  from the medium change.
- The card's launchd at `0x22000` ≠ the ramdisk object's `.data.ramdisk` after `build_root_volumes.sh` →
  the builder is not deriving both from one source.

*Provenance: `src/entry/entry_trace.c:5117-5135`, `src/platform/stage90_root_media.c:277,557-579,604-610,821-895`,
`src/entry/entry_storage.c:10184-10189,3319,3410`, `src/entry/build_entry.sh:770-771,1726-1746,29016-29020,37299-37300`,
`external/xnu-hd2-darwin13/xnu/bsd/vfs/vfs_conf.c:120`, `vfs_subr.c:1039-1075`;
`tools/check_hfs_root_blob.py`; `scripts/press_905b/c/d.sh`, `press_906.sh`;
`out/stage90/xnu_card_hfs.img` (HFS+ v4 `d9d2ade4…`, → HFSX v5); experiment-903, experiment-965.
Device unmodified; adb showed `4a2fe00b` present, `33e80afe` absent — idle, no action taken. Follows
[[mi4-965-real-rootfs-is-hfsx]] and [[mi4-903-xnu-mounts-the-emmc-goal-met]].*