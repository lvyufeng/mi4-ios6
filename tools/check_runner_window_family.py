#!/usr/bin/env python3
"""Verify the runner REPORTS the 970 entry window - the log's reading of the RAM XNU was handed.

WHY THIS EXISTS.  969 measured that the real iOS launchd links a 301 MiB shared cache while XNU managed
only the 16 MiB window; 970 read `arm_vm_init.c:422 avail_end = gPhysBase + gMemSize` and found the
window IS the allocator's physical-RAM end on the real high bank, so the wide-window arms are about a
NUMBER a reader must SEE.  That number is published as `xnu_entry_args_memSize` (jump.c:172).  Until
this rung `scripts/run_and_capture.sh` read NO `xnu_entry_args*` key, so a press of a wide-window arm
(`armed-window-c74bde1d`) would report nothing about the window itself - the same "unread family" defect
the runner has closed for the USB ladder (963) and the COW root (968) ([[mi4-911-runner-now-reads-all-goal-clauses]]).
This check makes that a build refusal, not a habit.

TWO DIRECTIONS, both structural (no build, no device):

  1.  **The window key is READ BY THE RUNNER.**  The block must name `xnu_entry_args_memSize`, the
      one reading that says how much RAM XNU actually got - the arm's whole instrument.

  2.  **Every entry_args key the runner names is PUBLISHED.**  The reverse direction: the
      `xnu_entry_args*` keys the runner greps for must appear as string literals in
      `src/xnu_entry_jump.c`, or the runner is reading a key nothing writes - the "one value, two
      definitions" class ([[mi4-one-value-two-definitions]]) wearing a log.

Usage:
    check_runner_window_family.py
    check_runner_window_family.py --selftest
Exit 0 when the facts hold, 1 when one has moved, 2 usage.
"""
import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNNER = os.path.join(REPO, "scripts", "run_and_capture.sh")
JUMP = os.path.join(REPO, "src", "xnu_entry_jump.c")

# The marker the block MUST read: the log's reading of the entry window.
MARKERS = ["xnu_entry_args_memSize"]

BLOCK_HEAD = "# **970: the ENTRY WINDOW"
BLOCK_TAIL = "# The OS-entry clause: the kernel's OWN load of its init"
WIN_KEY = re.compile(r"xnu_entry_args[a-zA-Z0-9_]*")


def refuse(msg):
    print("check_runner_window_family: %s" % msg, file=sys.stderr)
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
    """Every `xnu_entry_args*` string literal the jump module publishes."""
    text = open(JUMP, encoding="utf-8", errors="replace").read()
    return set(WIN_KEY.findall(text))


def check(runner_text):
    block = _block(runner_text)
    if block is None:
        return refuse("scripts/run_and_capture.sh has no window block - the summary reads NONE of the "
                      "970 entry-window family's keys (the arm `armed-window-c74bde1d` is unread)")

    read = set(WIN_KEY.findall(block))
    for m in MARKERS:
        if m not in read:
            return refuse("the runner's window block does not name %s - a press of a wide-window arm "
                          "would report nothing about the RAM XNU was actually handed." % m)

    published = _published_keys()
    if not published:
        return refuse("src/xnu_entry_jump.c publishes no xnu_entry_args* key - the module cannot be "
                      "the owner of the keys the runner reads")
    for key in sorted(read):
        if key in published:
            continue
        if any(p.startswith(key) or key.startswith(p) for p in published):
            continue
        return refuse("the runner reads %s, which src/xnu_entry_jump.c does not publish - it is reading "
                      "a key nothing writes (the one-value-two-definitions class)" % key)

    print("check_runner_window_family: ok - the runner's window block reads the entry window from the "
          "log (xnu_entry_args_memSize), and every xnu_entry_args* key it names is published "
          "by src/xnu_entry_jump.c.")
    return 0


def selftest():
    """Feed measured mutations: the marker dropped, an invented key, and no block."""
    real = open(RUNNER, encoding="utf-8", errors="replace").read()
    if check(real) != 0:
        return refuse("selftest: the shipped runner does not pass its own check")

    # mutation 1: drop the marker (rename it, so the block still exists but names no memSize).
    m1 = real.replace("xnu_entry_args_memSize", "xnu_entry_args_MEM_gone")
    if check(m1) == 0:
        return refuse("selftest: a runner that does not read the window marker was ACCEPTED")

    # mutation 2: add a reader for a key the module does not publish (marker kept).
    m2 = real.replace(
        BLOCK_HEAD,
        BLOCK_HEAD + "\n  ghost=$(grep -a -c 'xnu_entry_args_ghostxyz=' \"$log\" || true)",
        1)
    if m2 == real:
        return refuse("selftest: could not build mutation 2 (the block head was not found)")
    if check(m2) == 0:
        return refuse("selftest: a runner reading an unpublished key was ACCEPTED")

    # mutation 3: remove the whole block.
    i, j = real.find(BLOCK_HEAD), real.find(BLOCK_TAIL)
    m3 = real[:i] + real[j:] if 0 <= i < j else real
    if check(m3) == 0:
        return refuse("selftest: a runner with no window block at all was ACCEPTED")

    print("check_runner_window_family: --selftest ok - 3 mutations (the window marker dropped, an "
          "invented key, no block) were all refused")
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