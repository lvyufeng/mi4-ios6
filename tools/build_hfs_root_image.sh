#!/bin/bash
# Build the HFS+ root image the payload can be booted with (experiment 881).
#
#     tools/build_hfs_root_image.sh [-X] [-s SIZE_BYTES] [-l LAUNCHD_BLOB] OUT.img
#
# WHY THIS EXISTS. The ported HFS+ (2050's bsd/hfs, experiments 868-879) and the static root row (879)
# give XNU a filesystem it can mount, and the payload's own block device (stage90_root_media.c) can
# serve it bytes - but there is no HFS+ *volume* anywhere on the phone (880: ext4/f2fs only), so the
# arm needs one built here, on the host, and linked into the payload. This makes it, deterministically,
# with no device and no network: `mkfs.hfsplus` (hfsprogs) formats, a loop mount populates, and a
# read-back proves the bytes.
#
# WHAT IT MAKES. A volume whose `/sbin/launchd` is `LAUNCHD_BLOB` - which for this project is the
# 0x2000-byte static ARM Mach-O that `entry_ramdisk.s` already carries (the same bytes mockfs maps
# today), so the SAME process 1 that boots now can be exec'd from HFS+ instead. The image is 512 KiB
# by default: 880 measured that a 512 KiB HFS+ volume holds `/sbin/launchd` with a byte-exact
# round-trip, and 512 KiB is small enough to sit in the payload's own region alongside the entry image.
#
# `-X` MAKES IT **HFSX** (case-sensitive, signature `0x4858` v5), NOT HFS+ (`0x482B` v4). This is the
# format the REAL iOS rootfs uses: `/mnt/data/ios7-payload/v2/ios7/rootfs.hfs` (the decrypted iOS 7.1.2
# root) begins `HX 0005` = `kHFSXSigWord` (experiment 965). The driver's HFSX branch
# (`bsd/hfs/hfs_vfsutils.c:339-345`) accepts it but no mi4 arm has ever exercised it, because every
# fixture built here was HFS+ v4. `-X` closes that gap: `mkfs.hfsplus -s` = case-sensitive = HFSX, and
# the root volume is served by the strategy as raw bytes, so NOTHING else in the port needs to change
# (the same `.img`, the same `STAGE90_XNU_HFS_ROOT_MEDIA` arm, the same 512 KiB size class).
#
# WHY A LOOP MOUNT AND NOT `hpmount`. `hfsutils`' `hpmount` refuses the images `mkfs.hfsplus` writes
# ("This is not a HFS+ volume"), measured. The Linux kernel's own HFS+ driver reads them, so the
# populate step is `losetup` + `mount -t hfsplus`, which needs root (the repo already uses sudo for
# adb/fastboot). It touches nothing but the image file.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)

SIZE=524288
LAUNCHD=""
MKFS_FAMILY=""      # "" = HFS+ (0x482B v4); "-s" = HFSX (0x4858 v5) - `-s` is mkfs's case-sensitive flag
while [[ $# -gt 0 ]]; do
    case $1 in
        -X|--hfsx) MKFS_FAMILY="-s"; shift ;;
        -s) SIZE=$2; shift 2 ;;
        -l) LAUNCHD=$2; shift 2 ;;
        -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
        -*) echo "build_hfs_root_image: unknown option $1" >&2; exit 2 ;;
        *)  break ;;
    esac
done
OUT=${1:-}
[[ -n $OUT ]] || { echo "usage: tools/build_hfs_root_image.sh [-X] [-s SIZE] [-l LAUNCHD] OUT.img" >&2; exit 2; }

[[ $SIZE -ge 524288 ]] || { echo "build_hfs_root_image: SIZE=$SIZE < 524288, mkfs.hfsplus refuses" >&2; exit 2; }
(( SIZE % 4096 == 0 )) || { echo "build_hfs_root_image: SIZE=$SIZE is not a multiple of 4096" >&2; exit 2; }

# The launchd blob: if not given, extract it from the ramdisk object the entry build already makes,
# so the two never disagree about what process 1 is (one definition, [[mi4-one-value-two-definitions]]).
if [[ -z $LAUNCHD ]]; then
    OBJ=$REPO_ROOT/out/stage90/xnu_arm_entry_ramdisk.o
    [[ -f $OBJ ]] || { echo "build_hfs_root_image: $OBJ absent and no -l given; run the entry build first" >&2; exit 2; }
    LAUNCHD=$(mktemp /tmp/launchd_blob.XXXXXX.bin)
    arm-none-eabi-objcopy -O binary --only-section=.data.ramdisk "$OBJ" "$LAUNCHD"
fi
[[ -f $LAUNCHD ]] || { echo "build_hfs_root_image: launchd blob $LAUNCHD is absent" >&2; exit 2; }
BLOB_BYTES=$(stat -c %s "$LAUNCHD")
[[ $BLOB_BYTES -eq 8192 ]] || echo "build_hfs_root_image: WARNING launchd blob is $BLOB_BYTES B, not the 8192 B entry_ramdisk.s carries" >&2

MKFS=${MKFS_HFSPLUS:-/usr/sbin/mkfs.hfsplus}
[[ -x $MKFS ]] || { echo "build_hfs_root_image: no $MKFS (apt-get install hfsprogs)" >&2; exit 2; }

say() { printf 'build_hfs_root_image: %s\n' "$*"; }

rm -f "$OUT"
truncate -s "$SIZE" "$OUT"
# shellcheck disable=SC2086  # MKFS_FAMILY is a deliberate word-split: empty (HFS+) or `-s` (HFSX).
"$MKFS" -v STAGE90ROOT $MKFS_FAMILY "$OUT" >/dev/null
say "formatted $OUT as $([[ -n $MKFS_FAMILY ]] && echo HFSX || echo HFS+) ($SIZE bytes)"

# --- populate, then prove the bytes round-trip ----------------------------------------------------
LOOP=$(sudo losetup --find --show "$OUT")
MNT=$(mktemp -d)
cleanup() { sudo umount "$MNT" 2>/dev/null || true; sudo losetup -d "$LOOP" 2>/dev/null || true; rmdir "$MNT" 2>/dev/null || true; }
trap cleanup EXIT

sudo mount -t hfsplus "$LOOP" "$MNT"
sudo mkdir -p "$MNT/sbin"
sudo cp "$LAUNCHD" "$MNT/sbin/launchd"
# `/sbin/launchd` is exec'd, so it must look exec'able; the VFS layer checks the mode on the way in,
# and a `cp` under umask 077 leaves it 0600.
sudo chmod 0755 "$MNT/sbin" "$MNT/sbin/launchd"
sudo sync
# Read the bytes back through the filesystem, not from the source file.
if ! sudo cmp -s "$MNT/sbin/launchd" "$LAUNCHD"; then
    echo "build_hfs_root_image: sbin/launchd did not round-trip" >&2
    exit 1
fi
say "populated sbin/launchd ($BLOB_BYTES bytes), round-trip verified"
cleanup
trap - EXIT

# --- the volume header, read from the finished image ----------------------------------------------
# The volume header is at byte 1024 (sector 2): signature (2), version (2), then blockSize at 1064 and
# totalBlocks at 1068. Two families are valid, and the SIGNATURE AND VERSION are checked as a pair
# because the kernel does exactly that (`hfs_vfsutils.c:339-345`): HFS+ = 0x482B v4; HFSX = 0x4858 v5.
python3 - "$OUT" <<'PY'
import struct, sys, hashlib
p = sys.argv[1]
d = open(p, "rb").read()
sig = struct.unpack(">H", d[1024:1026])[0]
ver = struct.unpack(">H", d[1026:1028])[0]
blk = struct.unpack(">I", d[1064:1068])[0]
tot = struct.unpack(">I", d[1068:1072])[0]
FAMILIES = {0x482B: ("HFS+", 4), 0x4858: ("HFSX", 5)}
if sig not in FAMILIES:
    sys.exit("build_hfs_root_image: signature 0x%04x at offset 1024 is neither HFS+ 0x482B nor HFSX 0x4858" % sig)
name, want_ver = FAMILIES[sig]
if ver != want_ver:
    sys.exit("build_hfs_root_image: %s signature 0x%04x but version %d, not %d - the kernel refuses this pair "
             "(hfs_vfsutils.c:340)" % (name, sig, ver, want_ver))
print("build_hfs_root_image: volume header ok - %s 0x%04x v%d, blockSize %d, totalBlocks %d (%d bytes)"
      % (name, sig, ver, blk, tot, blk * tot))
PY
say "sha256 $(sha256sum "$OUT" | cut -d' ' -f1)  $OUT"