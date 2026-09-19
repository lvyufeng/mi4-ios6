#!/usr/bin/env python3
"""Which symbol does the entry image reach first, walking in source order?

    ./tools/entry_frontier.py --from pmap_bootstrap \
        --objects out/stage90/xnu_arm_entry.map ...

Hmm - the object list is better taken from a build. Usage:

    ./tools/entry_frontier.py --from pmap_bootstrap $(cat /tmp/objs.txt)

The frontier method links one XNU object per device run, and the question before every run is the
same: *the probe has moved past the last stop, so what does the image reach next?* The answer is
the first symbol, in source order, that no object in the image defines.

Source order, not fewest edges. A breadth-first walk finds the symbol with the shortest call chain,
which is a different symbol and the wrong one: the device executes `f()`'s body in the order the
compiler emitted it, so the first stop is found by walking each function's calls in address order
and recursing before moving to the next call. Branches are not modelled - an error path that only
runs on a failure the device does not take is walked anyway - so the answer is a *candidate*, and
the run is what decides. It has been wrong before (experiment 192, by one symbol).

Only direct `bl` targets are followed, plus the tail call - `b <symbol>`, which is how a function
whose last statement is a call hands control on. A tail call was a blind spot until experiment 197,
where it is exactly the frontier: `arm_vm_init`'s last instruction is
`b patch_low_glo_static_region`, and the symbol is undefined, so the run stops there. An indirect
call through a function pointer is still invisible here and is the one thing this cannot see.

**A call to a symbol defined in the same object was a second blind spot until experiment 305.**
`objdump -dr` prints such a call as `bl 94c <thread_unblock>` - the resolved local address and a
relocation the disassembly text does not show as `0` - and this tool's pattern required the `0`, so
it followed only *cross-object* calls. That is invisible while the frontier is reached through a
chain of objects, which is most of the boot, and it is exactly wrong where a run of functions in one
object sits between the probe and the stop: 305's stop, `sfi_thread_classify`, is reached from
`sched_startup -> kernel_thread_start_priority` (two objects, followed) and then
`thread_start -> clear_wait -> clear_wait_internal -> thread_setrun -> sfi_thread_classify`, of
which the last four are all `osfmk_kern_sched_prim.o`, so the walk the run was predicted from
reported "no stop in `clear_wait`'s closure" and the device stopped one intra-object hop later.
The pattern now accepts any target address and skips local labels (`foo+0x10`, `.LBB1_2`), which are
branches inside a function rather than calls.
"""

import argparse
import os
import re
import subprocess
import sys
from collections import defaultdict

NM = os.environ.get("NM", "arm-none-eabi-nm")
OBJDUMP = os.environ.get("OBJDUMP", "arm-none-eabi-objdump")


def symbols(path):
    """(defined, undefined) for one object file."""
    defined, undefined = set(), set()
    for line in subprocess.run([NM, "-A", path], capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        # `path:symbol type value` for definitions, `path: U symbol` for references.
        if parts[1] == "U":
            undefined.add(parts[2])
        else:
            defined.add(parts[2])
    return defined, undefined


def calls(path):
    """{function: [(address, target), ...]} in address order, per object."""
    out = subprocess.run([OBJDUMP, "-dr", path], capture_output=True, text=True).stdout
    sym = None
    per = defaultdict(list)
    for line in out.splitlines():
        m = re.match(r"^([0-9a-f]{8}) <(\S+)>:$", line)
        if m:
            sym = m.group(2)
            continue
        # `bl` is a call; a bare `b` to a named symbol is a call the compiler made a tail call.
        # The target address is 0 for a symbol this object does not define and the resolved local
        # address for one it does - both are edges, and the second kind is the 305 blind spot the
        # module docstring describes.
        # Conditional branches (`bne`, `beq`, ...) are not followed: that is a path this tool does
        # not model. A target that is a local label (`foo+0x10`, `.LBB1_2`) is a branch inside a
        # function, not a cross-function edge.
        m = re.match(r"^\s+([0-9a-f]+):\s+[0-9a-f ]+\s+b(?:l)?\s+(?:0|[0-9a-f]+) <(\S+)>", line)
        if m and sym and "+" not in m.group(2) and not m.group(2).startswith("."):
            per[sym].append((int(m.group(1), 16), m.group(2)))
    return per


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("objects", nargs="+")
    ap.add_argument("--from", dest="root", default="_start")
    ap.add_argument("--list", type=int, default=8, help="how many stops to print")
    ap.add_argument("--objects-only", action="store_true")
    args = ap.parse_args()

    defined, edges, undef = set(), {}, set()
    for obj in args.objects:
        # The generated stub file is deliberately not part of the walk: it defines every symbol the
        # image is missing, so including it would make the frontier set empty. Pass the objects the
        # build links *before* the stubs, and the walk reports what pass 1 will report.
        if os.path.basename(obj).endswith("_realstubs.o"):
            continue
        if not os.path.exists(obj):
            sys.exit("entry_frontier: no such object: %s" % obj)
        d, u = symbols(obj)
        defined |= d
        undef |= u
        for fn, lst in calls(obj).items():
            edges.setdefault(fn, []).extend(lst)
    undef -= defined

    for fn in edges:
        edges[fn].sort()

    stops, seen = [], set()

    def walk(fn, chain):
        if fn not in defined:
            if fn not in stops:
                stops.append((fn, list(chain)))
            return
        if fn in seen or len(chain) > 64:
            return
        seen.add(fn)
        for _, tgt in edges.get(fn, []):
            walk(tgt, chain + [(fn, tgt)])

    walk(args.root, [])

    print("root            %s" % args.root)
    print("%d function(s) with calls, %d defined symbol(s), %d undefined" %
          (len(edges), len(defined), len(undef)))
    print("stops (source order):")
    for i, (name, chain) in enumerate(stops[:args.list]):
        print("  %2d. %s" % (i + 1, name))
        if chain:
            print("      via " + " -> ".join("%s" % c[0] for c in chain[-6:]) + " -> " + name)
    if len(stops) > args.list:
        print("  ... %d more" % (len(stops) - args.list))
    print("TOTAL %d stop(s) reachable by direct call from %s" % (len(stops), args.root))
    return 0


if __name__ == "__main__":
    sys.exit(main())
