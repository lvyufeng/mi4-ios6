#!/usr/bin/env python3
"""Refuse an HFS-root arm whose safety split has been undone (experiment 882).

    tools/check_hfs_root_arm_split.py [SOURCE]

WHAT THE PROPERTY IS. On the HFS arm disk 0 has to answer two readers differently, and the whole
safety argument of the arm is that it does:

    hfs_mountroot    -> buf_strategy -> st_media_strategy   -> the HFS+ VOLUME   (g_stage90_root_hfs)
    mockfs_mountroot -> DKIOCGETMEMDEVINFO -> mi_base << 12 -> the MACH-O        (g_stage90_ramdisk)

`vfs_mountroot` walks `vfstbllist[]` and the HFS row (879) is before mockfs, so a failed
`hfs_mountroot` falls through to mockfs - and the fall-through only reaches a working exec because
`DKIOCGETMEMDEVINFO` still names the Mach-O. If the same bytes answered both, a failed HFS mount would
make mockfs map the VOLUME as process 1's executable and the boot would die at `load_init_program` on
a `MH_MAGIC` that is not there. That is not a bug in one arm; it is the arm's only copied claim.

WHY A CHECK AND NOT A COMMENT. The split lives in two small functions in one file, and the edit that
undoes it is a one-line simplification - "the strategy and the memdev answer are the same device, so
let `st_media_memdev_info` use `st_medium_disk_base`" - which reads as a cleanup and is exactly the
shape `experiment-881` section 3 corrected. This is `mi4-a-claim-in-a-comment-is-not-a-check`: the
property is made structural, in `make check`, so it stops the build instead of being remembered.

IT READS THE SOURCE, and says so. That is the honest bound: the linked object is where the property
would be *proved*, and 855 measured what reading a linked disassembly for a property costs - a clause
that reads layout as order (`mi4-linked-code-order-is-not-source-order`). The build's own
`nm`-based clause in `build_entry.sh` proves which arm an object was compiled for; this proves the two
bodies still have the shape the arm needs. Neither is sufficient alone and both are cheap.

Exit 0 when the split holds, 1 with the reason on stderr otherwise.
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_SOURCE = REPO / "src" / "platform" / "stage90_root_media.c"


def body_of(source, name):
    """The text of a C function definition, from its `{` to the matching `}`.

    Braces are counted rather than regex-matched because these bodies carry preprocessor arms whose
    braces are balanced and whose `#if`/`#else` the count must not be confused by - and because a
    non-greedy `\\{.*?\\}` would stop at the first nested block, which for `st_medium_disk_base` is the
    staged-unit `if` - the arm this check must see past.
    """
    # The DEFINITION is the one whose name starts a line - this file writes every return type on the
    # line above (`static const uint8_t *`, `__attribute__((noinline)) static int`), and a name that
    # starts a line is a definition rather than a call or a mention in a comment. A first draft
    # required the name to follow a type on the SAME line and found neither body, which is the failure
    # this check exists to catch aimed at itself.
    m = re.search(r"(?m)^" + re.escape(name) + r"\s*\([^;{]*\)\s*\{", source)
    if not m:
        return None
    start = m.end() - 1
    depth = 0
    for i in range(start, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start : i + 1]
    return None


def strip_comments(text):
    """Drop `/* ... */` and `// ...` so a name MENTIONED in prose is not read as a use.

    This is not tidiness. `st_media_memdev_info`'s own comment explains that it and
    `st_medium_disk_base` return the same two addresses on the baseline - so a check that scans the raw
    body refuses the correct code for a sentence describing it. The first run of this tool did exactly
    that, which is the "a comment is not a check" defect in the other direction: a check reading a
    comment as an implementation.
    """
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def main(argv):
    src_path = Path(argv[1]) if len(argv) > 1 else DEFAULT_SOURCE
    try:
        src = src_path.read_text()
    except OSError as exc:
        print(f"check_hfs_root_arm_split: {exc}", file=sys.stderr)
        return 1

    failures = []

    code = strip_comments(src)
    base = body_of(code, "st_medium_disk_base")
    if base is None:
        failures.append("st_medium_disk_base is gone - the strategy's base is decided elsewhere, so this "
                        "check cannot see which medium disk 0 serves")
    else:
        if "g_stage90_root_hfs" not in base or "g_stage90_ramdisk" not in base:
            failures.append(
                "st_medium_disk_base does not name BOTH g_stage90_root_hfs (the volume) and "
                "g_stage90_ramdisk (the Mach-O). On the HFS arm the strategy must serve the volume and "
                "off it the Mach-O; a body naming one of them is either always-the-volume (the arm's "
                "fall-through maps a volume as process 1) or never-the-volume (the arm mounts nothing)")
        if "STAGE90_XNU_HFS_ROOT_MEDIA" not in base:
            failures.append(
                "st_medium_disk_base chooses its disk-0 medium without naming STAGE90_XNU_HFS_ROOT_MEDIA, "
                "so the choice is not the arm switch")

    memdev = body_of(code, "st_media_memdev_info")
    if memdev is None:
        failures.append("st_media_memdev_info is gone - mockfs's fall-through has no source for its bytes")
    else:
        if "g_stage90_ramdisk" not in memdev:
            failures.append(
                "st_media_memdev_info no longer names g_stage90_ramdisk, so DKIOCGETMEMDEVINFO does not "
                "answer with the Mach-O. That is the ONE thing the split exists to keep: mockfs's "
                "fall-through maps `mi_base << 12` as process 1's executable, and a base pointing at the "
                "HFS+ volume is not a Mach-O")
        if "g_stage90_root_hfs" in memdev:
            failures.append(
                "st_media_memdev_info names g_stage90_root_hfs - the memdev answer has been put on the "
                "volume. This is the edit experiment-881 section 3 corrected: with the volume answering "
                "both readers, a failed hfs_mountroot makes mockfs map the volume as process 1 and the "
                "boot dies at load_init_program instead of falling through")
        if "st_medium_disk_base" in memdev:
            failures.append(
                "st_media_memdev_info derives its answer from st_medium_disk_base. That reads as a "
                "cleanup - one accessor instead of two - and it is the defect: the two accessors exist "
                "PRECISELY because the two readers must disagree on this arm")

    if failures:
        print("check_hfs_root_arm_split: REFUSING - the HFS-root arm's split is not intact:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        print("  The property is in docs/experiments/experiment-881-the-hfs-root-image-and-the-arm-plan.md", file=sys.stderr)
        print("  section 3. It is not a style rule: without it the arm cannot fall through to mockfs.", file=sys.stderr)
        return 1

    print("check_hfs_root_arm_split: the strategy serves the volume on STAGE90_XNU_HFS_ROOT_MEDIA and "
          "DKIOCGETMEMDEVINFO answers with the Mach-O on every arm - the fall-through still reaches an exec")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))