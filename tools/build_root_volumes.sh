#!/bin/bash
# Build the CARD root volume, and verify the COMMITTED blob matches it (experiment 965b).
#
#     tools/build_root_volumes.sh [-s CARD_SIZE_BYTES]
#
# WHY THIS EXISTS. The HFS root is served to XNU by two DIFFERENT media on two different arms, and until
# 965b only one of them was produced on demand:
#
#   * disk 0 on the blob arm           -> src/entry/blob/xnu_arm_entry_root_hfs.img  (COMMITTED, .incbin'd)
#   * the CARD unit (ST_MEDIA_DRIVER)  -> out/stage90/xnu_card_hfs.img               (gitignored, out/)
#
# The card medium is what `hfs_mountroot` reads on the arm actually pressed (STAGE90_XNU_ROOT_FROM_CARD=1:
# `__wrap_mdevlookup` answers the card unit, `src/entry/entry_trace.c:5131`), and on that arm disk 0 is
# NEVER registered - so the embedded blob is inert and the CARD image is the whole medium. Yet the card
# image was produced by NO committed script: `scripts/press_906.sh` and friends `dd` it to userdata's head
# and hard-code its sha256, so it went stale in silence. 965a flipped the *blob* to HFSX (itself inert on
# this arm) and left the card HFS+ v4, i.e. the pressed arm still mounted HFS+ while the tree's fixture was
# HFSX. This script closes that: it BUILDS the card volume from the one launchd fixture, and it VERIFIES
# the committed blob is the same filesystem family carrying the same launchd - so the two media cannot
# drift. It does not rewrite the committed blob: the blob is a tracked artifact, `mkfs.hfsplus` is not
# byte-reproducible (it stamps the volume), and regenerating it on every build would make a tracked file
# churn for no gain. The blob is checked; the card is built.
#
# WHY HFSX AND NOT HFS+. The real, decrypted iOS 7.1.2 rootfs (`/mnt/data/ios7-payload/v2/ios7/rootfs.hfs`)
# is HFSX (signature 0x4858, version 5), not HFS+ (0x482B, v4) - measured in experiment 965. The driver's
# HFSX branch (`bsd/hfs/hfs_vfsutils.c:339-345`) is what a real iOS root exercises. `tools/build_hfs_root_image.sh -X`
# formats HFSX (`mkfs.hfsplus -s`), so the card this makes is the family a real iOS root uses.
#
# WHY THE LAUNCHD IS EXTRACTED ONCE. The two volumes must carry the SAME `/sbin/launchd`, and the fixture
# lives in exactly one place: `out/stage90/xnu_arm_entry_ramdisk.o`'s `.data.ramdisk` section (the object
# `src/entry/build_entry.sh` assembles from `src/entry/entry_ramdisk.s`). This script extracts it once,
# hands it to the generator for the card, and reads it back out of the COMMITTED blob to prove the two
# agree - the lockstep is checked against the artifact, not asserted (`mi4-one-value-two-definitions`).
#
# WHAT IT TOUCHES. The card image under out/ (gitignored) and, inside the generator, a loop mount under
# sudo - the same `losetup`+`mount -t hfsplus` the generator already uses, on the image file alone. No
# device, no network, and no edit to any tracked file.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)

CARD_SIZE=524288          # the proven size class (882/903); the real 896 MiB rootfs is a later rung
OUT_BLOB="$REPO_ROOT/src/entry/blob/xnu_arm_entry_root_hfs.img"
OUT_CARD="$REPO_ROOT/out/stage90/xnu_card_hfs.img"
RAMDISK_OBJ="$REPO_ROOT/out/stage90/xnu_arm_entry_ramdisk.o"

while [[ $# -gt 0 ]]; do
    case $1 in
        -s) CARD_SIZE=$2; shift 2 ;;
        -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
        -*) echo "build_root_volumes: unknown option $1" >&2; exit 2 ;;
        *)  echo "build_root_volumes: unexpected argument $1" >&2; exit 2 ;;
    esac
done

say() { printf 'build_root_volumes: %s\n' "$*"; }

# The one launchd fixture, extracted once. `objcopy` selection mirrors `scripts/build.sh` (`OBJCOPY=`);
# `ARM_NONE_EABI_OBJCOPY` is accepted as the same override `build_entry.sh`'s tooling uses.
OBJCOPY=${OBJCOPY:-${ARM_NONE_EABI_OBJCOPY:-arm-none-eabi-objcopy}}
command -v "$OBJCOPY" >/dev/null 2>&1 || { echo "build_root_volumes: $OBJCOPY not on PATH" >&2; exit 2; }
[[ -f $RAMDISK_OBJ ]] || {
    echo "build_root_volumes: $RAMDISK_OBJ is absent - run the entry build first (its section .data.ramdisk" >&2
    echo "                    IS the /sbin/launchd both volumes must carry)" >&2; exit 2; }
[[ -f $OUT_BLOB ]] || {
    echo "build_root_volumes: the committed blob $OUT_BLOB is absent - it is the tracked artifact the blob" >&2
    echo "                    arm serves; restore it (git checkout) before building the card beside it" >&2; exit 2; }

LAUNCHD=$(mktemp /tmp/root_volumes_launchd.XXXXXX.bin)
trap 'rm -f "$LAUNCHD"' EXIT
"$OBJCOPY" -O binary --only-section=.data.ramdisk "$RAMDISK_OBJ" "$LAUNCHD"
BLOB_BYTES=$(stat -c %s "$LAUNCHD")
[[ $BLOB_BYTES -eq 8192 ]] || {
    echo "build_root_volumes: the extracted launchd is $BLOB_BYTES B, not the 8192 B entry_ramdisk.s carries" >&2
    echo "                   - refusing to build a root volume around an unexpected process 1" >&2; exit 2; }
say "one launchd fixture: $BLOB_BYTES B, sha256 $(sha256sum "$LAUNCHD" | cut -d' ' -f1)"

# Build the CARD volume (HFSX, from that one launchd). The generator verifies /sbin/launchd round-trips
# through the mounted volume before it returns, so a success here is a success for the card.
say "building the CARD volume (ST_MEDIA_DRIVER): $OUT_CARD"
tools/build_hfs_root_image.sh -X -s "$CARD_SIZE" -l "$LAUNCHD" "$OUT_CARD"

# The finished bytes, read from the files themselves rather than trusted from the generator: the card must
# be HFSX (0x4858 v5), the committed blob must be the SAME family, and the two must carry the SAME launchd
# at the layout offset the generator writes it to (0x22000 - the first allocation block of the 4096-byte
# volume after the volume header; verified by inspection of the built images in 965b). Any one of those
# failing means the pressed root and the tree's fixture have drifted - the defect 965b exists to close.
python3 - "$OUT_BLOB" "$OUT_CARD" "$LAUNCHD" <<'PY'
import struct, sys
blob_path, card_path, launchd_path = sys.argv[1:4]
launchd = open(launchd_path, "rb").read()
LAUNCHD_OFF = 0x22000

def family(d):
    sig = struct.unpack(">H", d[1024:1026])[0]
    ver = struct.unpack(">H", d[1026:1028])[0]
    return sig, ver

ok = True
imgs = {}
for p in (blob_path, card_path):
    d = open(p, "rb").read()
    sig, ver = family(d)
    imgs[p] = d
    fam = "HFSX" if sig == 0x4858 else ("HFS+" if sig == 0x482B else "0x%04x" % sig)
    has_l = d[LAUNCHD_OFF:LAUNCHD_OFF + len(launchd)] == launchd
    print("build_root_volumes: %s: %s 0x%04x v%d (%d bytes), launchd@0x%x %s"
          % (p, fam, sig, ver, len(d), LAUNCHD_OFF, "present" if has_l else "MISSING"))
    if not (sig == 0x4858 and ver == 5):
        print("build_root_volumes: %s is NOT HFSX 0x4858 v5" % p, file=sys.stderr); ok = False
    if not has_l:
        print("build_root_volumes: %s does not carry the current launchd fixture at 0x%x - the medium and the"
              " fixture disagree about process 1" % (p, LAUNCHD_OFF), file=sys.stderr); ok = False

if not ok:
    sys.exit("build_root_volumes: refusing to leave a mixed pair on disk - the pressed root and the tree's"
             " fixture must be one family carrying one launchd (experiment 965b)")
PY

say "card and blob agree: HFSX 0x4858 v5, same launchd"
say "  card (write THIS to userdata's head): $(sha256sum "$OUT_CARD" | cut -d' ' -f1)  $OUT_CARD"
say "  blob (committed, served by the blob arm): $(sha256sum "$OUT_BLOB" | cut -d' ' -f1)"
say "a card-root press must write the card image to userdata's head - see scripts/press_965b.sh"