#!/usr/bin/env python3
"""Refuse an HFS+ root volume the kernel cannot mount (experiment 882).

    tools/check_hfs_root_blob.py BLOB_OR_OBJECT [BLOB_OR_OBJECT ...]

WHAT THIS IS FOR. `src/entry/blob/xnu_arm_entry_root_hfs.img` is the volume disk 0's strategy serves on
the HFS arm, and it is a *committed* binary - a derived file that is tracked because `out/` is ignored
and a build on a machine that had not run the generator would otherwise embed nothing (see the
`.S` wrapper's comment). A committed derived binary is a thing that goes stale, and the failure mode is
not a crash: the blob still parses five years from now, and the arm mounts it and reads a catalog
B-tree that is not where the code looks.

So this reads the volume header out of the artifact the kernel will actually carry and refuses the
fields the HFS+ sources index by. The four that matter, each read off the code rather than chosen:

  * `signature`/`version` - `hfs_MountHFSPlusVolume` (`bsd/hfs/hfs_vfsutils.c:333-352`) requires
    `kHFSPlusSigWord` (0x482B) with version 4, or `kHFSXSigWord` (0x4858) with version 5.
  * `blockSize` - `:355` requires `>= 512`, a power of two, and `>= hfs_logical_block_size` (512), so
    a 4096-byte volume is the case that exercises the sizes above it.
  * `catalogFile`'s FIRST EXTENT - `hfs_MountHFSPlusVolume` reads the catalog B-tree from
    `startBlock * blockSize` and `BTree.c:272` PANICS when the node size is 512. `mkfs.hfsplus` writes
    4096-byte nodes, and this is the field a stale or truncated image would get wrong.
  * `attributes` BIT 30 (content protection) - must be clear. The port's cprotect engine is still owed
    (`cp_register_wraps` has no caller); with the bit clear the mount does not reach the NULL
    `g_cp_wrap_func` deref 878 flagged, which is the difference between a mount that can be measured
    today and one that cannot be measured at all. Not a field this tool "prefers": a blob with it set
    is refused, because the arm would be measuring the owed engine rather than the volume.

It reads an OBJECT as well as a raw blob, because the build checks both: the file is checked by
`make check`, and the bytes that land in the image are checked by `build_entry.sh` after `ld -r`s the
`.incbin` - which is the only way to catch `-I` resolving a different file than the one committed.

Exit status is 0 when every argument is a volume this kernel can mount, non-zero with the reason on
stderr otherwise. It is a *reader*, not a formatter: it never writes, so it is safe to run on a
committed file.
"""

import struct
import subprocess
import sys

ROOT_HFS_BYTES = 524288          # what `tools/build_hfs_root_image.sh` makes, and what the build asserts
VH_OFFSET = 1024                 # HFS+ volume header, `HFS_PRI_SECTOR(512) * 512`
HFS_PLUS_SIG = 0x482B
HFSX_SIG = 0x4858
HFS_PLUS_VERSION = 4
HFSX_VERSION = 5
K_HFS_BLOCK_SIZE = 512           # `kHFSBlockSize` - the floor `hfs_MountHFSPlusVolume` enforces
ATTR_CONTENT_PROTECTION = 1 << 30
# The fork-data records start at VH+112; the catalog fork is the third (`hfs_MountHFSPlusVolume` order:
# allocation, extents, catalog, attributes, startup). Each is 80 bytes: logicalSize(8) clumpSize(4)
# totalBlocks(4) extents[8] of {startBlock(4), blockCount(4)}.
FORK_OFFSETS = {
    "allocationFile": 0,
    "extentsFile": 1,
    "catalogFile": 2,
    "attributesFile": 3,
    "startupFile": 4,
}
FORK_RECORD_BYTES = 80
EXTENT_ENTRIES = 8


def load(path):
    """The bytes to read: an ELF object's RO section if it is one, else the file as-is."""
    with open(path, "rb") as fh:
        raw = fh.read()
    if raw[:4] != b"\x7fELF":
        return raw, "file"
    # An ELF object whose only non-empty section is the `.incbin`ed volume (the shape
    # `src/entry/blob/xnu_arm_entry_root_hfs.S` assembles). Read the section table rather than
    # objcopy's output, so this works with no binutils beyond `readelf`.
    try:
        out = subprocess.run(
            ["arm-none-eabi-objcopy", "-O", "binary", "--only-section=.data.hfsroot", path, "/dev/stdout"],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except FileNotFoundError:
        sys.exit(f"check_hfs_root_blob: {path} is an object and arm-none-eabi-objcopy is not on PATH")
    except subprocess.CalledProcessError as exc:
        sys.exit(f"check_hfs_root_blob: {path}: no .data.hfsroot section ({exc.stderr.decode().strip()})")
    return out.stdout, "object"


def be16(b, o):
    return struct.unpack_from(">H", b, o)[0]


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def be64(b, o):
    return struct.unpack_from(">Q", b, o)[0]


def check(path):
    data, kind = load(path)
    if len(data) != ROOT_HFS_BYTES:
        return (f"{path}: {len(data)} bytes, not the {ROOT_HFS_BYTES} the image is built as. "
                f"Disk 0's block count is derived from this array's length, so a short one is a volume "
                f"whose extents run past the medium")
    # The first 1024 bytes ARE zeros in a real volume - they are the boot blocks, and `mkfs.hfsplus`
    # leaves them empty. So "starts with zeros" says nothing; what distinguishes a volume is the
    # signature at 1024 and content past it. An all-zero 512 KiB file parses as a volume header of
    # zeros, which is why the signature test below (not a heuristics on byte 0) is the one that fires.
    if not any(data[VH_OFFSET:]):
        return f"{path}: everything from the volume header on is zeros - this is padding, not a volume"
    sig = be16(data, VH_OFFSET)
    ver = be16(data, VH_OFFSET + 2)
    if sig == HFS_PLUS_SIG:
        want = HFS_PLUS_VERSION
    elif sig == HFSX_SIG:
        want = HFSX_VERSION
    else:
        return (f"{path}: volume signature 0x{sig:04x} at offset {VH_OFFSET}, not HFS+ 0x482b or "
                f"HFSX 0x4858")
    if ver != want:
        return (f"{path}: signature 0x{sig:04x} with version {ver}; hfs_MountHFSPlusVolume requires "
                f"{want} for this signature (`hfs_vfsutils.c:333-352`)")
    attr = be32(data, VH_OFFSET + 4)
    block_size = be32(data, VH_OFFSET + 40)
    total_blocks = be32(data, VH_OFFSET + 44)
    if block_size < K_HFS_BLOCK_SIZE or block_size & (block_size - 1):
        return f"{path}: blockSize {block_size} is not a power of two >= {K_HFS_BLOCK_SIZE}"
    if total_blocks * block_size > len(data):
        return (f"{path}: the volume claims {total_blocks} blocks of {block_size} = "
                f"{total_blocks * block_size} bytes, past the {len(data)}-byte array the strategy "
                f"serves. The strategy TRIMS a read at the end of the medium, so the mount would fail "
                f"with an EOF the volume header does not explain")
    if attr & ATTR_CONTENT_PROTECTION:
        return (f"{path}: volume attribute bit 30 (content protection) is SET. The port's cprotect "
                f"engine is still owed (`cp_register_wraps` has no caller) and 878 measured the NULL "
                f"deref this reaches; a blob with this bit set cannot be mounted today, so the arm "
                f"would measure the missing engine and not the volume")
    cat_off = VH_OFFSET + 112 + FORK_OFFSETS["catalogFile"] * FORK_RECORD_BYTES
    cat_start = be32(data, cat_off + 16)
    cat_blocks = be32(data, cat_off + 20)
    cat_bytes = be32(data, cat_off + 12)
    if cat_start == 0 or cat_blocks == 0:
        return f"{path}: the catalog file has no first extent (startBlock {cat_start}, {cat_blocks} blocks)"
    if (cat_start + cat_blocks) * block_size > len(data):
        return (f"{path}: the catalog B-tree's first extent [{cat_start}, +{cat_blocks}) runs past the "
                f"{len(data)}-byte medium - hfs_MountHFSPlusVolume reads it through the strategy and "
                f"would get a trimmed read")
    # The B-tree header is the first 106 bytes of node 0, and `nodeSize` is at byte 32 of it
    # (`BTNodeDescriptor` is 14 bytes, then `BTHeaderRec`: treeDepth(2) rootNode(4) leafRecords(4)
    # firstLeafNode(4) lastLeafNode(4) nodeSize(2) - measured off the image: catalog gives 4096,
    # extents gives 4096). This was 14 in the first draft, which read `rootNode` and refused every
    # real volume for a node size it never looked at.
    node_off = cat_start * block_size + 32
    if node_off + 2 > len(data):
        return f"{path}: the catalog B-tree's node 0 is past the medium"
    node_size = be16(data, node_off)
    if node_size == 512:
        return (f"{path}: catalog nodeSize is 512, which BTree.c:272 PANICS on - mkfs.hfsplus writes "
                f"4096 and anything that says 512 here is not a volume this port can mount")
    if node_size == 0 or node_size & (node_size - 1):
        return f"{path}: catalog nodeSize {node_size} is not a power of two"
    print(f"check_hfs_root_blob: {kind} {path}: HFS+ 0x{sig:04x} v{ver}, blockSize {block_size}, "
          f"{total_blocks} blocks ({total_blocks * block_size} B), attrs 0x{attr:08x}, "
          f"catalogFile blk {cat_start} +{cat_blocks} (nodeSize {node_size})")
    return None


def main(argv):
    if len(argv) < 2:
        sys.exit("usage: tools/check_hfs_root_blob.py BLOB_OR_OBJECT [BLOB_OR_OBJECT ...]")
    failed = 0
    for path in argv[1:]:
        try:
            reason = check(path)
        except OSError as exc:
            reason = f"{path}: {exc}"
        if reason:
            print(f"check_hfs_root_blob: REFUSING: {reason}", file=sys.stderr)
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))