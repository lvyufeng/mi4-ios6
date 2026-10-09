#!/usr/bin/env python3
"""Verify the runner REPORTS the 968 COW writable root - the family's keys are actually read.

WHY THIS EXISTS.  968 built the COW writable root (the storage half the goal was still missing: XNU
mounts the real iOS rootfs READ-WRITE over a RAM shadow, `STAGE90_XNU_CARD_COW=1`).  Its ENTIRE
adequacy claim lives in three published keys - and until this rung `scripts/run_and_capture.sh`'s
summary read NONE of them, the same "unread family" defect the runner has closed four times before
([[mi4-911-runner-now-reads-all-goal-clauses]]; the USB ladder was the one before this).  A press of
`armed-d13-7107b998` would have produced a log whose COW outcome - served, or refused for a full
arena - the operator had to grep out by hand.  This check makes that a build refusal, not a habit.

TWO DIRECTIONS, both structural (no build, no device):

  1.  **Both markers are READ BY THE RUNNER.**  The COW block must name `xnu_live_rootmedia_cow_wr_blocks`
      (the SERVED marker - published on every write the shadow served, so its presence is the arm's
      success reading) AND `xnu_live_rootmedia_cow_refused` (the FAILURE marker - published ONLY when a
      write hit a page the arena could not hold, so its ABSENCE is the success reading and a
      presence-only check would miss the failure branch).  A block reading only one of the two cannot
      report the other outcome.

  2.  **Every COW key the runner names is PUBLISHED by the platform module.**  The reverse direction:
      the `xnu_live_rootmedia_cow*` keys the runner greps for must appear as string literals in
      `src/platform/stage90_root_media.c`, or the runner is reading a key nothing writes - the
      "one value, two definitions" class ([[mi4-one-value-two-definitions]]) wearing a log.

The block is bounded by its own header comment and the next clause's header, so it is found wherever it
sits inside the root-media block (it must, because the COW only exists on a card-root arm).

Usage:
    check_runner_cow_family.py
    check_runner_cow_family.py --selftest
Exit 0 when the facts hold, 1 when one has moved, 2 usage.
"""
import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNNER = os.path.join(REPO, "scripts", "run_and_capture.sh")
MEDIA = os.path.join(REPO, "src", "platform", "stage90_root_media.c")

# The two markers the block MUST read: the served reading and the failure reading.
MARKERS = ["xnu_live_rootmedia_cow_wr_blocks", "xnu_live_rootmedia_cow_refused"]

BLOCK_HEAD = "# **968: the COW WRITABLE ROOT"
BLOCK_TAIL = "# The OS-entry clause: the kernel's OWN load of its init"
COW_KEY = re.compile(r"xnu_live_rootmedia_cow[a-z0-9_]*")


def refuse(msg):
    print("check_runner_cow_family: %s" % msg, file=sys.stderr)
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
    """Every `xnu_live_rootmedia_cow*` string literal the platform module publishes."""
    text = open(MEDIA, encoding="utf-8", errors="replace").read()
    return set(COW_KEY.findall(text))


def check(runner_text):
    block = _block(runner_text)
    if block is None:
        return refuse("scripts/run_and_capture.sh has no COW block - the summary reads NONE of the "
                      "968 writable-root family's keys (the arm `armed-d13-7107b998` is unread)")

    read = set(COW_KEY.findall(block))
    for m in MARKERS:
        if m not in read:
            return refuse("the runner's COW block does not name %s. A press would report only half of "
                          "the arm: %s is the served reading and %s the refused one, and a block "
                          "reading one cannot report the other outcome."
                          % (m, MARKERS[0], MARKERS[1]))

    published = _published_keys()
    if not published:
        return refuse("src/platform/stage90_root_media.c publishes no xnu_live_rootmedia_cow* key - "
                      "the module cannot be the owner of the keys the runner reads")
    for key in sorted(read):
        if key in published:
            continue
        # the runner may name a family by prefix; accept a key that is a prefix of some published key,
        # or that some published key starts with.
        if any(p.startswith(key) or key.startswith(p) for p in published):
            continue
        return refuse("the runner reads %s, which src/platform/stage90_root_media.c does not publish - "
                      "it is reading a key nothing writes (the one-value-two-definitions class)" % key)

    print("check_runner_cow_family: ok - the runner's COW block reports the 968 writable root both "
          "ways (the served key `%s` and the refused key `%s`), and every xnu_live_rootmedia_cow* key "
          "it names is published by src/platform/stage90_root_media.c." % (MARKERS[0], MARKERS[1]))
    return 0


def selftest():
    """Feed measured mutations: the served marker dropped, the refused marker dropped, no block, and an
    invented key."""
    real = open(RUNNER, encoding="utf-8", errors="replace").read()
    if check(real) != 0:
        return refuse("selftest: the shipped runner does not pass its own check")

    # mutation 1: drop the SERVED marker (rename it, so the block still exists but names no wr_blocks).
    m1 = real.replace("xnu_live_rootmedia_cow_wr_blocks", "xnu_live_rootmedia_cow_WR_gone")
    if check(m1) == 0:
        return refuse("selftest: a runner that does not read the served marker was ACCEPTED")

    # mutation 2: drop the REFUSED marker.
    m2 = real.replace("xnu_live_rootmedia_cow_refused", "xnu_live_rootmedia_cow_REF_gone")
    if check(m2) == 0:
        return refuse("selftest: a runner that does not read the refused marker was ACCEPTED")

    # mutation 3: add a reader for a key the module does not publish (both markers kept, so this
    # exercises the reverse-direction branch and not the missing-marker one).
    m3 = real.replace(
        BLOCK_HEAD,
        BLOCK_HEAD + "\n      ghost=$(grep -a -c 'xnu_live_rootmedia_cow_ghostxyz=' \"$log\" || true)",
        1)
    if m3 == real:
        return refuse("selftest: could not build mutation 3 (the block head was not found)")
    if check(m3) == 0:
        return refuse("selftest: a runner reading an unpublished key was ACCEPTED")

    # mutation 4: remove the whole block.
    i, j = real.find(BLOCK_HEAD), real.find(BLOCK_TAIL)
    m4 = real[:i] + real[j:] if 0 <= i < j else real
    if check(m4) == 0:
        return refuse("selftest: a runner with no COW block at all was ACCEPTED")

    print("check_runner_cow_family: --selftest ok - 4 mutations (the served marker dropped, the refused "
          "marker dropped, an invented key, no block) were all refused")
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