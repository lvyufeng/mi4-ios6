#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_storage_size.py - host known-answer test for the 911d storage-capacity half.

The goal clause 「安装好的xnu，能够正确识别xiaomi 4的3GB内存，16GB/32GB存储」 asks XNU to
recognise the Mi 4's *storage size*.  The ladder already reads the card's true
density -- EXT_CSD SEC_CNT -- into `st_ext_sec_count` and the media layer publishes
it through `DKIOCGETBLOCKCOUNT` on the raw whole-card unit (ST_MEDIA_CARD_RAW).
This tool is the HOST half: given a SEC_CNT it computes the medium's size and
decides which of the two parts the phone shipped in (16 GB / 32 GB) the number is.

WHAT IS DERIVED BY VALUE, AND WHERE FROM
----------------------------------------
* The sector count of a block is 512 -- `ST_MEDIA_BLOCKSIZE` at
  `src/platform/stage90_root_media.c:271` (and `DEV_BSIZE` is 512 here).
* SEC_CNT is a little-endian 32-bit field at EXT_CSD offset 212 --
  `ST_EXT_CSD_OFF_SEC_CNT` at `src/entry/entry_storage.c:3107`, assembled by
  `ST_EXT_CSD_WORD` (`src/entry/entry_storage.c:3118`).
* The value THIS 16 GB part reports is 0x01d5a000 = 30,777,344 sectors, read out of
  the card at `src/entry/entry_storage.c:7035` (`st_ext_sec_count =
  ST_EXT_CSD_WORD(ST_EXT_CSD_OFF_SEC_CNT)`) and quoted as a measured reading at
  `src/entry/entry_storage.c:2861` (`_ext_sec_count = 0x01d5a000`).  The media layer
  hands it to the block layer at `src/platform/stage90_root_media.c:1739`
  (`st_media_blockcount[ST_MEDIA_CARD_RAW] = sectors;`).
* The 32 GB part's SEC_CNT is NOT measured on this device (we hold a 16 GB unit).
  It is the 16 GB value doubled, which is what doubling the die density does; the
  tool treats it as a REFERENCE class value, not a reading, and says so.

THE BOUNDARY
------------
The two parts' usable sizes are far apart: the 16 GB part is 30,777,344 * 512 =
15,758,000,128 B = 14.676 GiB, and a 32 GB part is ~2x that = 29.35 GiB.  The
separator is placed at 20 GiB: each class sits >= 5.3 GiB from it, well clear of
the +/-7% that real eMMC nominal densities spread over (so a genuine 16 GB die
never reads as 32 GB and vice versa).  The boundary is a DECISION ABOUT THE TWO
CLASSES, not a value read off the card, and it is written as one named constant.

`--selftest` runs the mutation: it feeds a SEC_CNT that is the WRONG class for the
label it carries and asserts the checker REFUSES it -- so the test is proven to
bite, not merely to pass.
"""

import argparse
import sys

# 512 bytes per sector.  Source: src/platform/stage90_root_media.c:271 (ST_MEDIA_BLOCKSIZE).
BYTES_PER_SECTOR = 512

# The measured SEC_CNT of THIS phone's 16 GB part.  Source: src/entry/entry_storage.c:7035
# (the read) and :2861 (the value, 0x01d5a000).
SEC_CNT_16GB = 0x01D5A000            # 30,777,344 sectors
# A 32 GB part's SEC_CNT as a REFERENCE (not measured here): doubling the density.
SEC_CNT_32GB = SEC_CNT_16GB * 2      # 61,554,688 sectors == 0x03AB4000

GIB = 1 << 30
# The class separator.  See the module docstring: 20 GiB sits >= 5.3 GiB from either class.
BOUNDARY_BYTES = 20 * GIB

CLASS_16 = "16GB"
CLASS_32 = "32GB"


def size_bytes(sec_cnt):
    """The medium's size in bytes for a whole-card sector count."""
    return sec_cnt * BYTES_PER_SECTOR


def classify(sec_cnt):
    """Which part a SEC_CNT describes: the 16 GB class or the 32 GB class.

    Everything at or above the boundary is the larger part; everything below is
    the smaller.  A zero SEC_CNT is the ladder's own refusal (EXT_CSD never read,
    `entry_storage_card_sectors() == 0`) and is reported as its own answer rather
    than silently folded into the 16 GB class.
    """
    if sec_cnt == 0:
        return "none"
    return CLASS_32 if size_bytes(sec_cnt) >= BOUNDARY_BYTES else CLASS_16


def describe(sec_cnt):
    n = size_bytes(sec_cnt)
    return "{} (0x{:08x}) sectors = {} B = {:.3f} GiB".format(
        sec_cnt, sec_cnt, n, n / GIB)


def check(sec_cnt, expect):
    """Return (ok, message).  `expect` is CLASS_16 / CLASS_32 / 'none'."""
    got = classify(sec_cnt)
    msg = "SEC_CNT {} -> class {}".format(describe(sec_cnt), got)
    if expect == "none":
        return (got == "none"), msg
    return (got == expect), msg + " (expected {})".format(expect)


def selftest():
    """Known-answer checks plus a mutation that MUST be refused."""
    failures = []

    def expect_ok(label, sec_cnt, want_class):
        ok, msg = check(sec_cnt, want_class)
        print(("  PASS  " if ok else "  FAIL  ") + label + ": " + msg)
        if not ok:
            failures.append(label)

    print("known-answer checks:")
    expect_ok("16 GB part (measured)", SEC_CNT_16GB, CLASS_16)
    expect_ok("32 GB part (reference)", SEC_CNT_32GB, CLASS_32)
    expect_ok("zero is a refusal", 0, "none")
    # The 16 GB value must NOT be in the 32 GB class and vice versa -- the one
    # discrimination the clause turns on.
    expect_ok("16 GB part is NOT 32 GB", SEC_CNT_16GB, CLASS_16)
    expect_ok("32 GB part is NOT 16 GB", SEC_CNT_32GB, CLASS_32)

    print("mutation (the test must bite):")
    # MUTATION A: label the 16 GB card's SEC_CNT as if it were a 32 GB part.
    # A checker that always or never passes would accept this; the real one must NOT.
    ok_a, msg_a = check(SEC_CNT_16GB, CLASS_32)
    print(("  PASS  " if not ok_a else "  FAIL  ") +
          "16 GB SEC_CNT mislabeled as 32 GB is rejected: " + msg_a)
    if ok_a:
        failures.append("mutation A not caught")

    # MUTATION B: a truncated SEC_CNT (the low 24 bits -- a byte-offset/truncation
    # bug in SEC_CNT assembly) must read as the WRONG size, i.e. it is not the
    # 32 GB class the true doubled value is.
    truncated = SEC_CNT_16GB & 0xFFFFFF     # 0x00d5a000 = 13,996,032 sectors
    ok_b, msg_b = check(truncated, CLASS_32)
    print(("  PASS  " if not ok_b else "  FAIL  ") +
          "truncated SEC_CNT is rejected as 32 GB: " + msg_b)
    if ok_b:
        failures.append("mutation B not caught")

    print()
    if failures:
        print("SELFTEST FAILED: " + ", ".join(failures))
        return 1
    print("SELFTEST PASSED: 16/32 GB discrimination holds and the mutations are caught.")
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("sec_cnt", nargs="?",
                    help="EXT_CSD SEC_CNT (accepts 0x hex or decimal); omit with --selftest")
    ap.add_argument("--expect", choices=[CLASS_16, CLASS_32, "none"],
                    help="assert the SEC_CNT is in this class; exit 1 otherwise")
    ap.add_argument("--selftest", action="store_true",
                    help="run the known-answer checks and the mutation")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    if args.sec_cnt is None:
        ap.error("give a SEC_CNT, or --selftest")

    try:
        sec_cnt = int(args.sec_cnt, 0)      # base 0 honours 0x..
    except ValueError:
        print("not an integer: {!r}".format(args.sec_cnt), file=sys.stderr)
        return 2

    print(describe(sec_cnt))
    print("class: {}".format(classify(sec_cnt)))

    if args.expect is not None:
        ok, msg = check(sec_cnt, args.expect)
        print(("OK: " if ok else "MISMATCH: ") + msg)
        return 0 if ok else 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))