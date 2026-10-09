#!/usr/bin/env python3
"""Verify the runner REPORTS the clause-3 USB ladder - the four rungs' families are actually read.

WHY THIS EXISTS.  959-962 built the whole clause-3 ladder (「可以通过usb进行调试」) and until this
rung `scripts/run_and_capture.sh`'s summary read NONE of its keys - the recorded defect this project
keeps closing (five goal families once reached the runner unread; the USB ladder is the one that grew
since).  A press of any of the four arms would have produced a log whose USB outcome the operator had
to grep out by hand.  This check makes that a build refusal rather than a habit.

TWO DIRECTIONS, both structural (no build, no device):

  1.  **Each family's marker key is READ BY THE RUNNER.**  The runner's USB block
      (`# --- clause 3 (USB debug)` up to the idle-arm `if`) must name a marker key for every one of
      the four families.  A family whose marker is not in the block is a family a press cannot report.

  2.  **Every USB key the runner names is PUBLISHED by the entry sources.**  The reverse direction:
      the keys the runner greps for must appear as string literals in `src/entry/entry_usb*.c`, or the
      runner is reading a key nothing writes - the "one value, two definitions" class
      ([[mi4-one-value-two-definitions]]) wearing a log.

The four markers are chosen to be keys each family publishes at the TOP of its body, so their presence
is not conditional on a deeper state the run may not reach (except the enum arm, which publishes
`_armed` on the EP0 path and `_gated` on the mode-gate refusal - the check accepts EITHER).

Usage:
    check_runner_usb_families.py
    check_runner_usb_families.py --selftest
Exit 0 when the facts hold, 1 when one has moved, 2 usage.
"""
import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNNER = os.path.join(REPO, "scripts", "run_and_capture.sh")
ENTRY = os.path.join(REPO, "src", "entry")

# family label, source file, marker keys (any ONE present in the runner's block satisfies the family).
FAMILIES = [
    ("probe  (rung 1)",  "entry_usb.c",         ["xnu_live_usb_loaded"]),
    ("device (rung 2)",  "entry_usb_dev.c",     ["xnu_live_usb_dev_loaded"]),
    ("enum   (rung 3)",  "entry_usb_enum.c",    ["xnu_live_usb_enum_armed", "xnu_live_usb_enum_gated"]),
    ("stream (rung 4)",  "entry_usb_stream.c",  ["xnu_live_usb_stream_state"]),
]

BLOCK_HEAD = "# --- clause 3 (USB debug)"
BLOCK_TAIL = "if (( idle_no_sleep_arm == 1 )); then"
USB_KEY = re.compile(r"xnu_live_usb[a-z0-9_]*")


def refuse(msg):
    print("check_runner_usb_families: %s" % msg, file=sys.stderr)
    return 1


def _block(runner_text):
    i = runner_text.find(BLOCK_HEAD)
    if i < 0:
        return None
    j = runner_text.find(BLOCK_TAIL, i)
    if j < 0:
        return None
    return runner_text[i:j]


def _published_keys():
    """Every `xnu_live_usb*` string literal any entry_usb*.c publishes."""
    keys = set()
    for name in sorted(os.listdir(ENTRY)):
        if not name.startswith("entry_usb") or not name.endswith(".c"):
            continue
        text = open(os.path.join(ENTRY, name), encoding="utf-8", errors="replace").read()
        keys.update(USB_KEY.findall(text))
    return keys


def check(runner_text):
    block = _block(runner_text)
    if block is None:
        return refuse("scripts/run_and_capture.sh has no USB block - the summary reads NONE of the "
                      "clause-3 ladder's keys (the 959-962 families are unread)")

    read = set(USB_KEY.findall(block))
    for label, src, markers in FAMILIES:
        if not any(m in read for m in markers):
            return refuse("the runner's USB block names no %s marker key (%s). A press of an image "
                          "carrying this arm would report nothing about it."
                          % (label, " or ".join(markers)))

    published = _published_keys()
    if not published:
        return refuse("no src/entry/entry_usb*.c publishes any xnu_live_usb* key - the sources cannot "
                      "be the owner of the keys the runner reads")
    for key in sorted(read):
        # the runner may name a family by prefix (`xnu_live_usb_enum_*` -> `xnu_live_usb_enum_`); accept
        # a key that is a prefix of some published key, or that some published key starts with.
        if key in published:
            continue
        if any(p.startswith(key) or key.startswith(p) for p in published):
            continue
        return refuse("the runner reads %s, which no src/entry/entry_usb*.c publishes - it is reading "
                      "a key nothing writes (the one-value-two-definitions class)" % key)

    print("check_runner_usb_families: ok - the runner's USB block reports all four clause-3 families "
          "(%s), and every xnu_live_usb* key it names is published by a src/entry/entry_usb*.c."
          % ", ".join(f[0].split()[0] for f in FAMILIES))
    return 0


def selftest():
    """Feed measured mutations: a block with a family dropped, and a block reading an invented key."""
    real = open(RUNNER, encoding="utf-8", errors="replace").read()
    if check(real) != 0:
        return refuse("selftest: the shipped runner does not pass its own check")

    # mutation 1: drop the enum family's marker lines (the defect this check exists for).
    m1 = real.replace("xnu_live_usb_enum_armed", "xnu_live_usb_ENUM_gone").replace(
        "xnu_live_usb_enum_gated", "xnu_live_usb_ENUM_gone2")
    if check(m1) == 0:
        return refuse("selftest: a runner with the enum family's marker removed was ACCEPTED")

    # mutation 2: add a reader for a key the sources do not publish (without removing a real one, so
    # this exercises the reverse-direction branch and not the missing-family one).
    m2 = real.replace(
        BLOCK_HEAD,
        BLOCK_HEAD + "\n  ghost=$(grep -a -c 'xnu_live_usb_ghostxyz=' \"$log\" || true)",
        1)
    if m2 == real:
        return refuse("selftest: could not build mutation 2 (the block head was not found)")
    if check(m2) == 0:
        return refuse("selftest: a runner reading an unpublished key was ACCEPTED")

    # mutation 3: remove the whole block.
    i, j = real.find(BLOCK_HEAD), real.find(BLOCK_TAIL)
    m3 = real[:i] + real[j:] if 0 <= i < j else real
    if check(m3) == 0:
        return refuse("selftest: a runner with no USB block at all was ACCEPTED")

    print("check_runner_usb_families: --selftest ok - 3 mutations (a dropped family, an invented key, "
          "no block) were all refused")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    return check(open(RUNNER, encoding="utf-8", errors="replace").read())


if __name__ == "__main__":
    sys.exit(main())