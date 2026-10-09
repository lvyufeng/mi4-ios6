# 965c — the strategy's bound reaches the real 896 MiB rootfs (2026-10-09)

965 made the **real, decrypted iOS 7.1.2 rootfs** (`/mnt/data/ios7-payload/v2/ios7/rootfs.hfs`, 896 MiB,
HFSX `0x4858` v5, 7142 files) the goal's target, and 965b made the **pressed** card-root arm mount an HFSX
medium. But 965b's card was still the **512 KiB mkfs fixture**, and a host-side trace of the strategy's
bound found a wall that the fixture could never expose: **the card unit's byte length is a 32-bit product,
and 896 MiB does not fit it.** This rung builds the arm that serves the real volume: the medium becomes the
real rootfs, and the bound becomes 64-bit. Host-side, device-free; press is the operator's.

## 1. The defect, against the code

The card-root arm (`STAGE90_XNU_ROOT_FROM_CARD=1`) mounts the **card unit** (`ST_MEDIA_DRIVER` = 2,
`entry_trace.c:5131`), served block by block by `st_media_strategy`'s card branch
(`stage90_root_media.c:821-895`). That branch bounds every request with `len`, and for the card unit
`len` came from

```c
static unsigned
st_medium_disk_bytes(uint32_t unit)
{
    ...
    if (unit == ST_MEDIA_DRIVER)
        return (unsigned)((uint64_t)entry_storage_selected_count() * ST_MEDIA_BLOCKSIZE);
```

`entry_storage_selected_count()` is the **selected partition's** sector count. On this device the
GPT-selected partition is `userdata` (`mmcblk0p25`, `_card_lba = 0x400000` = LBA 4,194,304; 13,291,503 KiB
= **13,610,499,072 B** measured `adb shell cat /proc/partitions`). So:

| quantity | value |
|---|---|
| `entry_storage_selected_count()` | 26,582,225 sectors |
| `selected_count * 512` (exact) | 13,610,499,072 B |
| return type | `unsigned` (32-bit) |
| **`(unsigned)` of it** | **725,597,184 B = 692.0 MiB** |
| the real iOS 7.1.2 rootfs | **939,524,096 B = 896.0 MiB** |

The strategy's bound is therefore **692 MiB, below the 896 MiB volume**. A mount over it serves `EOF` for
the volume's top ~204 MiB — and the HFS catalog's file extent at block 9220 and the volume's allocation
bitmap in the upper region are exactly there, so `hfs_mountroot` cannot finish. **This is `one value, two
definitions`**: the SAME unit reports `DKIOCGETBLOCKCOUNT` = `st_media_blockcount[2]` = 26,582,225 sectors
(fitted the 32-bit cell) = 13.6 GB, and a strategy `len` of 692 MiB. The two numbers disagree by a factor
of ~19, and a filesystem that trusted the block count would address blocks the strategy refuses.

**The parked 512 KiB arm (`b9c224c0`) never saw it.** 512 KiB does not overflow a 32-bit product, so the
truncation is invisible until the medium exceeds 4 GiB — which is exactly the point: the port's whole
medium progression was 512 KiB (882) → the same (903–906) → the same (965a/b). The first medium large
enough to hit the wall is the real rootfs. So the fix is a **new switch**, not an edit to the shared body:
`STAGE90_XNU_FULL_EXTENT=0` keeps the arm's object byte-for-byte (verified, §4).

## 2. Why the mount path itself is fine (measured, not assumed)

A 896 MiB volume with 229376 allocation blocks and a **multi-extent catalog** (first extent block 9220, 768
blocks; second extent block 124571) is well within the D13 HFS driver's widths — a full read of
`bsd/hfs/` found **no width wall**:

- `HFSPlusExtentDescriptor` / `HFSPlusForkData` / `HFSPlusVolumeHeader` block counts are `u_int32_t`;
  `ExtendedVCB.totalBlocks` is `u_int32_t` and holds 229376 (`hfs_vfsutils.c:425`).
- `MapFileBlockC` and the extent search work on `u_int32_t` FABNs with 64-bit offset arithmetic and
  `__builtin_mul_overflow` guards; block 9220/124571 are ordinary 32-bit values.
- The one extent-vs-volume bound check `startBlock + blockCount >= totalBlocks` (`hfs_catalog.c:806`) is
  `u_int32_t` throughout; `124571 + 768 = 125339 < 229376`, so the catalog extent is not suppressed.
- Block-size negotiation returns 4096 from `BestBlockSizeFit` (allocation block 4096 is a page multiple) and
  is **independent of `totalBlocks`**; `hfs_mountfs`'s 4096 bump (`hfs_vfsops.c:1397`) keys on a device
  sector count `> 0x7fffffff`, far above 896 MiB. The HFSX branch (`hfs_vfsutils.c:339-345`) accepts
  blockSize 4096.
- The only `u_int16_t` extent casts (`hfs_catalog.c:1601-1614`) are HFS-**Standard** writeback, not reached
  by HFSX.

So the single wall for the real rootfs is the platform medium's 32-bit length — this rung — and nothing in
the driver.

## 3. The fix

**One gated 64-bit accessor, one strategy branch, one switch threaded like `CARD_TOTAL`; no driver change.**

- **`src/platform/stage90_root_media.c`** — add, under `#if STAGE90_XNU_FULL_EXTENT`,

  ```c
  static uint64_t
  st_medium_card_full_bytes(void)
  {
      return (uint64_t)entry_storage_selected_count() * (uint64_t)ST_MEDIA_BLOCKSIZE;
  }
  ```

  and in `st_media_strategy`'s `len` computation use it for the card unit on that arm. The existing
  accessor `st_medium_disk_bytes` is **left exactly as 888 wrote it** — a change to its return type would
  move the object even for a build that does not use the arm (measured: it did, at byte 1858), so the
  64-bit path is a SEPARATE function the arm-only `len` branch calls. A new `..._full_extent_arm_on`
  marker is emitted **only when the switch is on** (911b's presence-only shape, §4); an unconditional
  marker would move the parked image's hash (the entry link gc's nothing).
- **`src/entry/build_entry.sh`** — read `STAGE90_XNU_FULL_EXTENT` (0/1), refuse it without
  `STAGE90_XNU_EMMC_STRATEGY=1`, add it to `ENTRY_ARM_KEYS`, write it into the record
  (`xnu_arm_entry-config.txt`) as a resolved value, pass `-DSTAGE90_XNU_FULL_EXTENT=$FULL_EXTENT` to the
  payload compile, and add a `nm`-marker check that the module and this build agree about the arm
  (the one-object-two-scripts defect every switch here guards).
- **`scripts/preflight_boot_check.sh`** — add `STAGE90_XNU_FULL_EXTENT` to `ENTRY_CFG_KEYS` with a
  presence-grounded N/A branch (an image whose ELF carries the marker must record the key; one that cannot
  is N/A), and a **size clause**: when the record names the card-root arm, compare the card image's byte
  count to the device's 32-bit bound (692 MiB) — a card larger than it with `FULL_EXTENT` off is refused
  (the silent-truncation defect grounded on the artifact, not the record).
- **`tools/build_root_volumes.sh --real`** — a mode that copies the real rootfs
  (`STAGE90_REAL_ROOTFS`, default `/mnt/data/ios7-payload/v2/ios7/rootfs.hfs`) to `out/stage90/xnu_card_hfs.img`
  and verifies HFSX `0x4858` v5. The 512 KiB fixture mode is unchanged.
- **`scripts/press_965c.sh`** (new) — device gate, `build_root_volumes.sh --real`, `dd` the 896 MiB card to
  `userdata`'s head (`seek=0`, the same write 905b–906 do, touching ONLY `userdata`), then the gate +
  runner. No sha hard-coded; the built card's hash is printed and the device read-back compared.

**Why a separate switch from `CARD_TOTAL`.** `CARD_TOTAL` (911d) widens a *fourth* unit's CAPACITY reading
(whole-card vs partition); `FULL_EXTENT` widens the *mount* unit's ADDRESSABLE LENGTH. One name, one
meaning — a value on `CARD_TOTAL` would be one name with two meanings, the defect this project pays for
most often. They are independent: neither implies the other, and each has its own 64-bit source.

## 4. Verification (host-side, no press)

- **`FULL_EXTENT=0` is byte-neutral.** The D13 platform object rebuilt with `FULL_EXTENT=0` is
  **byte-for-byte identical** to the object from before 965c (`cmp` — 11832 B, no differing byte), and the
  **entry image rebuilt at `FULL_EXTENT=0` reproduces `b9c224c0` exactly** (sha256 match). So the parked
  arm's bytes and hash are unchanged; a full `build_entry.sh` run at the record's switch set exits 0 and
  prints the same `b9c224c0`.
- **`FULL_EXTENT=1` does what it says.** The platform object gains the `entry_root_media_full_extent_arm_on`
  marker (absent at FE=0) and differs from FE=0 (11884 B) — the arm changes the object. The 965c entry arm
  (`..._CARD_TOTAL=1 FULL_EXTENT=1`) builds.
- `make check` rc=0.
- The preflight's size clause **refuses a card image larger than 692 MiB when `FULL_EXTENT` is off**, and
  passes when it is on (verified both ways against a deliberately oversized fixture).

**PRESS IS THE OPERATOR'S.** Nothing here is pressed and nothing here writes a device.

## 5. What this proves, and what it does not

- **Proves (on press):** with `FULL_EXTENT=1` and the 896 MiB real rootfs on `userdata`'s head, XNU's
  `hfs_mountroot` parses and mounts the **real iOS 7.1.2 root volume** off the device's own storage — the
  HFSX branch, the multi-extent catalog at block 9220, and the 64-bit bound all exercised for the first
  time. The strategy serves blocks up to 896 MiB (above 692 MiB), so a mount that reaches the volume's
  upper region is proof the bound widened.
- **Falsifier:** a mount that reads the volume header (block 2) and the catalog's first extent (block 9220)
  but then stops — or the preflight's `_card_last_lba` reading stuck below 692 MiB — means the
  `FULL_EXTENT` arm did not take, and the 32-bit bound truncated the medium.
- **Does not prove:** the goal's larger clauses. A real mount is a prerequisite for a real *userspace*
  (`launchd` from the real volume, not the 8192-byte fixture) and for long-running residency; both are
  later rungs. The 3 GB low-bank pmap port (915-B) is a separate, whole-kernel arm.

## 6. What would falsify this

- A `FULL_EXTENT=0` rebuild that does **not** reproduce `b9c224c0` / the byte-identical object → the gate
  is not confined to the new arm (it is, §4).
- The **preflight passes a card > 692 MiB with `FULL_EXTENT` off** → the size clause is not
  artifact-grounded (it refuses).
- The press's `_card_last_lba` never exceeds ~692 MiB on a successful mount → the 64-bit bound is not what
  widened the medium; the mount succeeded for another reason.

*Provenance: `src/platform/stage90_root_media.c` (`st_medium_disk_bytes`, `st_media_strategy`'s card
branch, `st_medium_card_full_bytes`), `src/entry/build_entry.sh` (`ENTRY_ARM_KEYS`, the record writer, the
`nm`-marker check), `scripts/preflight_boot_check.sh` (`ENTRY_CFG_KEYS`, the size clause),
`tools/build_root_volumes.sh` (`--real`), `scripts/press_965c.sh`; measured `adb shell cat /proc/partitions`
= `mmcblk0p25 13291503 KiB`; the real volume header read host-side from
`/mnt/data/ios7-payload/v2/ios7/rootfs.hfs` (HFSX 0x4858 v5, blockSize 4096, totalBlocks 229376, 896 MiB);
D13 `bsd/hfs/` width audit (no wall). Device unmodified; adb showed `4a2fe00b` present, `33e80afe` absent —
idle, no action taken. Follows [[mi4-965-real-rootfs-is-hfsx]] and
[[mi4-903-xnu-mounts-the-emmc-goal-met]].*