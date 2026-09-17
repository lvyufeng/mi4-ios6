#!/usr/bin/env python3
"""
What the first XNU boot attempt would do, computed instead of guessed.

    ./tools/stub_reach.py                 # RELEASE
    ./tools/stub_reach.py --min           # STAGE90_BOOT
    ./tools/stub_reach.py --from arm_init # start somewhere else
    ./tools/stub_reach.py --list 20       # how many stubs to print

Why this exists. `measure_link.sh` produces an image whose 427 missing symbols are stubs that
return zero. That image is not bootable — but it *is* enough to answer the question that decides
what to do next: **walking the call graph from `_start`, which stub does the boot path reach
first, and through what chain?** Phase 4's exit criterion is "crash #1 captured, fix one thing per
cycle"; this computes which crash that would be, and does it without a device.

Two things it is not:

  * **Not a scheduler.** An indirect call through a function pointer is invisible — the graph only
    follows direct `bl` targets that resolve to a known symbol. So the answer is a *lower bound* on
    how deep the boot gets, and the first stub it finds is a stub the boot does reach, not
    necessarily the first one it *hits*.
  * **Not a substitute for the device.** It says what the code would do; it cannot say what the
    hardware hands it. Everything this project has learned from hardware runs came from hardware
    runs.

What it does give is a work order. The stubs are ranked by how few edges away they are from the
entry, and the closest ones are what a boot attempt needs first.
"""

import argparse
import collections
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
OBJDUMP = os.environ.get("OBJDUMP", "arm-none-eabi-objdump")
NM = os.environ.get("NM", "arm-none-eabi-nm")

# `08030d24 <_start>:` and `803a7080:  eb00003f  bl  803a7184 <arm_init>`
LABEL = re.compile(r"^([0-9a-f]{8}) <([^>]+)>:")
# The raw opcode is present or not depending on `--no-show-raw-insn`, so it is optional in the
# pattern rather than assumed: requiring it made every edge in the image parse as nothing, and the
# failure looked like "this function calls nothing" rather than "this regex does not match".
INSN = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{8}\s+)?(bl|blx)\s+([0-9a-f]+)\s+<([^>]+)>")


def load(elf):
    """(labels, calls) from one disassembly: the function names, and the edges between them."""
    dis = subprocess.run([OBJDUMP, "-d", "--no-show-raw-insn", elf],
                         capture_output=True, text=True).stdout

    labels = []          # (address, name) in ascending order
    cur = None
    calls = collections.defaultdict(set)   # name -> set of callee names
    for line in dis.splitlines():
        m = LABEL.match(line)
        if m:
            cur = m.group(2)
            labels.append((int(m.group(1), 16), cur))
            continue
        if cur is None:
            continue
        m = INSN.match(line)
        if m:
            calls[cur].add(m.group(4))
    return labels, calls


def stub_names(stub_file):
    """The names measure_link.sh stubbed, read from the .s it wrote."""
    if not os.path.isfile(stub_file):
        return None
    out = set()
    for line in open(stub_file):
        m = re.match(r"\s*\.weak\s+(\S+)", line)
        if m:
            out.add(m.group(1))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--min", action="store_true")
    ap.add_argument("--from", dest="entry", default="_start")
    ap.add_argument("--list", type=int, default=15)
    args = ap.parse_args()

    config = "STAGE90_BOOT" if args.min else "RELEASE"
    out = os.path.join(REPO_ROOT, "out", "link")
    elf = os.path.join(out, f"{config}-measure.elf")
    stub_file = os.path.join(out, f"{config}-stubs.s")

    if not os.path.isfile(elf):
        print(f"no {elf} - run ./tools/measure_link.sh --keep-stubs first", file=sys.stderr)
        return 2

    stubs = stub_names(stub_file)
    if stubs is None:
        print(f"no {stub_file} - run ./tools/measure_link.sh --keep-stubs first", file=sys.stderr)
        return 2

    labels, calls = load(elf)
    # Membership is against the LABELS, not against `calls`: a function that makes no directly
    # resolved `bl` never becomes a key in `calls`, and `arm_init`'s first instructions are a
    # `bl` chain that the parser sees — but `_start`'s are `ldr lr, [pc, #N]` followed by a tail
    # call, so `_start` has no parsed edges at all. Checking the wrong collection reported "entry
    # symbol not found" for a symbol that is right there in the disassembly.
    names = {n for _a, n in labels}
    if args.entry not in names:
        print(f"entry symbol {args.entry} not found in {elf}", file=sys.stderr)
        return 2
    calls.setdefault(args.entry, set())

    # BFS from the entry. A stub is a leaf by construction, so the walk stops there - which is the
    # point: the frontier of the walk is the set of stubs the boot path reaches.
    dist = {args.entry: 0}
    parent = {args.entry: None}
    queue = collections.deque([args.entry])
    reached_stubs = []
    total_funcs = len(calls)

    while queue:
        fn = queue.popleft()
        for callee in sorted(calls.get(fn, ())):
            if callee in dist:
                continue
            dist[callee] = dist[fn] + 1
            parent[callee] = fn
            if callee in stubs:
                reached_stubs.append(callee)
                continue        # a stub calls nothing
            queue.append(callee)

    reached_stubs.sort(key=lambda s: (dist[s], s))

    print(f"== {config}: what a boot would reach from {args.entry} ==")
    print(f"  functions in the image:      {total_funcs}")
    print(f"  reachable without a stub:    {len(dist) - len(reached_stubs)}")
    print(f"  stubs reached:               {len(reached_stubs)} of {len(stubs)}")
    print()

    if not reached_stubs:
        print("  (none - the whole reachable graph is real code)")
        return 0

    print(f"== the first {args.list} stubs, nearest to the entry first ==")
    print("  edges  stub                              via")
    for s in reached_stubs[:args.list]:
        # The shortest path back to the entry, so the chain is readable rather than implied.
        chain = []
        n = parent[s]
        while n is not None and len(chain) < 6:
            chain.append(n)
            n = parent[n]
        print(f"  {dist[s]:5d}  {s:34s} {' <- '.join(chain)}")

    print()
    print("== the same, grouped by how far from the entry ==")
    buckets = collections.Counter(dist[s] for s in reached_stubs)
    for d in sorted(buckets)[:8]:
        print(f"  {buckets[d]:4d} stub(s) at {d} edge(s)")

    print()
    print("  An indirect call is invisible to this walk, so the real first hit is at least this")
    print("  deep. What it does say is which stubs are *on* the path and how far in.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
