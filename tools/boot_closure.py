#!/usr/bin/env python3
"""
Which of the files that do not compile are actually on the boot path.

    ./tools/boot_closure.py                 # from the osfmk/arm entry layer
    ./tools/boot_closure.py --roots osfmk/arm osfmk/kern
    ./tools/boot_closure.py --why vm_pageout

Why this and not a failure count. `link_gap.sh` says 1187 symbols are missing, and
`build_xnu_arm_kernel.sh` says 80 files do not compile. Neither says which of those files a
*bootable* kernel needs. Most of the 80 are `bsd/netinet6`, `bsd/nfs` and `bsd/vfs` - subsystem
code that a kernel reaching a first scheduler tick does not touch.

This computes the closure instead of guessing: start at the objects in the ARM entry layer, take
their undefined symbols, find which object defines each, and repeat. An object is on the boot path
only if something reachable from the entry references it. Two outputs matter:

  * **closure size** - how many objects a boot actually pulls in, out of everything compiled.
  * **the work list** - every file in the closure that has no object, because it fails to compile
    or is not in the manifest. Sorted by how many symbols it would close.

The second is the answer to "which failure do I fix next", and it is not the same as "which file
has the most errors".

One honest limit, stated rather than implied: this walks the *symbol* graph from the ARM layer,
and it cannot see a dependency that only exists through data the linker never names - a function
pointer stored in a table, a `__attribute__((constructor))`, or a call through an assembly entry
that is not in the object set. So the closure is a lower bound on the boot path, and the work list
is in dependency order rather than complete.
"""

import argparse
import collections
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
XNU = os.environ.get("XNU_TREE", os.path.join(REPO_ROOT, "external", "xnu-4570.1.46"))
KERNEL_OBJ = os.path.join(REPO_ROOT, "out", "xnu_kernel_obj")
MIN_OBJ = os.path.join(REPO_ROOT, "out", "xnu_min_obj")
ARM_OBJ = os.path.join(REPO_ROOT, "out", "xnu_arm_obj")
NM = os.environ.get("NM", "arm-none-eabi-nm")

# Symbols the compiler runtime provides at link time. Not source, so never a work item.
COMPILER_RT = re.compile(r"^(__aeabi_|__div|__udiv|__mod|__umod|__mul|__clz|__float|__fix|__trunc"
                         r"|__extend|_Unwind|__stack_chk|__bswap|__ASH|__ash)")


def nm_sets(obj):
    """(defined, undefined) symbol sets for one object."""
    try:
        out = subprocess.run([NM, obj], capture_output=True, text=True, check=True).stdout
    except subprocess.CalledProcessError:
        return set(), set()
    defined, undefined = set(), set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and len(parts[1]) == 1 and parts[1].isalpha():
            # `nm` prints `addr TYPE name` for defined, `         U name` for undefined.
            if parts[1].upper() == "U":
                undefined.add(parts[2])
            else:
                defined.add(parts[2])
        elif len(parts) == 2 and parts[0].upper() == "U":
            undefined.add(parts[1])
    return defined, undefined


def load(object_dirs):
    """(sym -> [objs defining it], obj -> undefined set, obj -> defined set)."""
    providers = collections.defaultdict(list)
    undef = {}
    defs = {}
    for d in object_dirs:
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if not name.endswith(".o"):
                continue
            path = os.path.join(d, name)
            dset, uset = nm_sets(path)
            if not dset and not uset:
                continue
            defs[path] = dset
            undef[path] = uset
            for sym in dset:
                providers[sym].append(path)
    return providers, undef, defs


def failing_files():
    """Files that have no object, from the build's own failure lists."""
    out = {}
    for d in (KERNEL_OBJ, MIN_OBJ):
        p = os.path.join(d, "failed.txt")
        if not os.path.isfile(p):
            continue
        for line in open(p):
            line = line.strip()
            if line:
                out[line] = line
    return sorted(out)


def symbols_defined_in_file(path, candidates):
    """Which of `candidates` this source file defines. Definition-shaped lines at column 0.

    Deliberately conservative: XNU puts the return type on its own line often enough that this
    undercounts. The ordering it produces is still the useful part.
    """
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return set()
    pat = re.compile(r"^(?:[A-Za-z_][A-Za-z0-9_ \t\*]*?[\s\*])([A-Za-z_][A-Za-z0-9_]*)\s*(?:\(|\[|=)")
    found = set()
    for line in text.splitlines():
        if not line or line[0] in " \t#/*}":
            continue
        m = pat.match(line)
        if m and m.group(1) in candidates:
            found.add(m.group(1))
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--roots", nargs="*", default=["arm"],
                    help="substrings of the object paths to start from (default: the ARM layer)")
    ap.add_argument("--why", metavar="SUBSTR",
                    help="explain why a file or symbol is in the closure")
    ap.add_argument("--show", type=int, default=25, help="how many work-list entries to print")
    args = ap.parse_args()

    providers, undef, defs = load([KERNEL_OBJ, MIN_OBJ, ARM_OBJ])
    if not defs:
        print("no objects - run the builds first", file=sys.stderr)
        return 2

    roots = [o for o in defs if any(r in o for r in args.roots)]
    if not roots:
        print(f"no objects match roots {args.roots}", file=sys.stderr)
        return 2

    # BFS over the symbol graph.
    in_closure = set(roots)
    queue = list(roots)
    needed = set()
    unresolved = {}
    while queue:
        obj = queue.pop()
        for sym in undef[obj]:
            needed.add(sym)
            for prov in providers.get(sym, ()):
                if prov not in in_closure:
                    in_closure.add(prov)
                    queue.append(prov)
            if sym not in providers:
                unresolved.setdefault(sym, set()).add(obj)

    missing_source = {s: o for s, o in unresolved.items() if not COMPILER_RT.match(s)}
    runtime = {s for s in unresolved if COMPILER_RT.match(s)}

    print("== the boot closure ==")
    print(f"  objects available:        {len(defs)}")
    print(f"  objects on the boot path: {len(in_closure)}")
    print(f"  symbols needed:           {len(needed)}")
    print(f"  needed but undefined everywhere: {len(missing_source)}"
          f"  (+{len(runtime)} compiler-runtime)")
    print()

    # Which missing symbols are defined by a file that merely fails to compile?
    absent = set()
    for o in in_closure:
        absent |= undef[o]
    absent -= set(providers)
    absent = {s for s in absent if not COMPILER_RT.match(s)}

    failing = failing_files()
    by_file = collections.Counter()
    for path in failing:
        got = symbols_defined_in_file(path, absent)
        if got:
            by_file[path] = len(got)

    print("== the work list: files that fail to compile and are on the boot path ==")
    if not by_file:
        print("  (none - every symbol the closure needs that no object defines is defined nowhere)")
    total = 0
    for path, n in by_file.most_common(args.show):
        rel = path.replace(XNU + "/", "")
        print(f"  {n:5d}  {rel}")
        total += n
    print()
    print(f"  {len(by_file)} file(s), {total} symbol(s) - a lower bound, see the module docstring")
    print()

    if args.why:
        needle = args.why
        print(f"== why '{needle}' ==")
        for obj in sorted(in_closure):
            if needle in obj:
                print(f"  in closure: {obj}")
        for sym in sorted(s for s in missing_source if needle in s):
            print(f"  missing symbol: {sym}  (needed by "
                  f"{', '.join(sorted(os.path.basename(o) for o in sorted(missing_source[sym]))[:4])})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
