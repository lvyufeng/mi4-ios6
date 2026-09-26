#!/usr/bin/env python3
"""Expand a link's object set until nothing more can be resolved, and report what is left.

    ./tools/entry_closure.py --seed out/xnu_kernel_obj/osfmk_arm_arm_init.o \
                             --preset out/stage90/xnu_arm_start.o \
                             --out  out/stage90/xnu_arm_entry_closure.txt

`build_entry.sh` can link XNU's own compiled `arm_init.o` into the entry image, but one object is a
poor measurement: everything it calls has to be stubbed, so the device would report the first name
in `arm_init`'s *prologue* rather than the first thing that genuinely does not exist yet. This walks
the closure instead - link, read the undefined set, add whatever object in the pool defines those
symbols, link again - so the names that survive are the real ones.

Rules, all of which matter for the answer to be a measurement rather than a guess:

  * an object is added only if it defines something *currently undefined*, so nothing joins the
    image "because it looks relevant";
  * `--preset` objects are in the image from the start and their definitions win. The entry image
    has its own `panic`, its own stacks, its own `gPhysBase`; a real kernel object that defines one
    of those is not a better answer, it is a duplicate definition;
  * a candidate that would duplicate a provided symbol is skipped, and the skip is printed, because
    two objects claiming one name is exactly the failure this project keeps recording;
  * where several objects could satisfy one symbol, the one closing the most of the undefined set
    wins, so the number of rounds stays proportional to the closure rather than to the symbol count.

The output answers "what does the compile graph have to grow by" from the objects this project
already builds.
"""

import argparse
import os
import re
import subprocess
import sys
from collections import defaultdict

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NM = os.environ.get("NM", "arm-none-eabi-nm")
LD = os.environ.get("LD", "arm-none-eabi-ld")

# nm's type letters that mean storage rather than code. A reference to one of these has to be
# writable memory in the image: `arm_init` copies 320 bytes of boot_args into `BootCpuData` before
# it calls anything.
STORAGE = set("DBRSGC")
NM_LINE = re.compile(r"^(?:([0-9a-fA-F]+)\s+)?([A-Za-z])\s+(\S+)$")


def nm_defs(paths):
    """({symbol: set(object)}, {symbol: type}) for the definitions in `paths`.

    `-A -P` because the file name has to be on every line: GNU nm only prints its `path:` header
    line when it is given *more than one* object, so a parse that relies on the header silently
    returns nothing for a single object - which is how the first version of this tool reported no
    conflict while linking two objects that both define `gPhysBase`.

    Upper-case nm types only: a lower-case type is a local symbol, which cannot satisfy a reference
    from another object and is therefore not a definition here.
    """
    out = subprocess.run([NM, "-A", "-P", "--defined-only", *paths],
                         capture_output=True, text=True).stdout
    defs = defaultdict(set)
    types = {}
    for line in out.splitlines():
        obj, _, rest = line.partition(":")
        parts = rest.split()
        if len(parts) < 2:
            continue
        name, typ = parts[0], parts[1]
        if typ.isupper():
            defs[name].add(obj)
            types.setdefault(name, typ)
    return defs, types


def link(objs, script, out_elf):
    """Link; return (undefined symbols, duplicate-definition diagnostics)."""
    # entry.ld takes its base as a `--defsym` symbol rather than defining one, so that the base is
    # written down in exactly one place (build_entry.sh's ENTRY_BASE). This tool discovers which
    # symbols a set of objects leaves undefined, and that answer does not depend on where the image
    # lands - so it links at zero, deliberately a value no build uses.
    r = subprocess.run([LD, "-T", script, "-nostdlib", "--no-demangle",
                        "--defsym=ENTRY_BASE=0",
                        "-o", out_elf, *objs],
                       capture_output=True, text=True)
    undef = set(re.findall(r"undefined reference to `([^']+)'", r.stderr))
    dupes = [l.strip() for l in r.stderr.splitlines() if "multiple definition" in l]
    return undef, dupes


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed", action="append", required=True,
                    help="object to start the closure from (repeatable)")
    ap.add_argument("--preset", action="append", default=[],
                    help="object already in the image whose definitions must not be duplicated")
    ap.add_argument("--script", default=os.path.join(
        REPO_ROOT, "src/entry/entry.ld"))
    ap.add_argument("--pool", action="append", default=None,
                    help="directory of candidate objects (default: the kernel and asm object dirs)")
    ap.add_argument("--max", type=int, default=400, help="give up after this many added objects")
    ap.add_argument("--out", help="write the object list and the remaining symbols here")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    pools = args.pool or [os.path.join(REPO_ROOT, "out/xnu_kernel_obj"),
                          os.path.join(REPO_ROOT, "out/xnu_asm_obj")]
    pool = []
    for d in pools:
        if not os.path.isdir(d):
            print(f"no such object pool: {d}", file=sys.stderr)
            return 2
        pool += sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith(".o"))
    if not pool:
        print("the object pool is empty - run ./tools/build_xnu_arm_kernel.sh first", file=sys.stderr)
        return 2

    if not args.quiet:
        print(f"pool: {len(pool)} objects from {', '.join(pools)}")
    pool_defs, pool_types = nm_defs(pool)
    seed_defs, _seed_types = nm_defs(list(args.seed) + list(args.preset))
    provided = set(seed_defs)

    tmp = os.path.join(REPO_ROOT, "out/stage90/xnu_arm_entry_closure.elf")
    os.makedirs(os.path.dirname(tmp), exist_ok=True)

    def log(*a):
        if not args.quiet:
            print(*a, flush=True)

    objs = list(args.seed)
    rounds = 0
    while True:
        rounds += 1
        undef, dupes = link(list(args.preset) + objs, args.script, tmp)
        if dupes:
            print("duplicate definitions in the image - two providers were linked:",
                  file=sys.stderr)
            print("\n".join(dupes[:5]), file=sys.stderr)
            return 3
        if not undef:
            log(f"round {rounds}: the link closes with {len(objs)} closure object(s)")
            break
        if len(objs) >= args.max:
            log(f"stopping at {args.max} objects; {len(undef)} symbol(s) still undefined")
            break

        # Candidates: pool objects defining a currently-undefined symbol, minus what is already in.
        candidates = defaultdict(set)
        for sym in undef:
            for obj in pool_defs.get(sym, ()):
                if obj not in objs:
                    candidates[obj].add(sym)
        if not candidates:
            log(f"round {rounds}: nothing in the pool defines any of the {len(undef)} "
                f"remaining symbol(s) - they are the real stubs")
            break

        # Greedy by symbols closed. Ties broken by path so the walk is deterministic and a rerun
        # of the same tree produces the same closure.
        ranked = sorted(candidates.items(), key=lambda kv: (-len(kv[1]), kv[0]))
        chosen = None
        for obj, closes in ranked:
            obj_defs, _ = nm_defs([obj])
            clash = sorted(set(obj_defs) & provided)
            if clash:
                log(f"  skip {os.path.basename(obj)}: defines {', '.join(clash[:3])}"
                    f"{' ...' if len(clash) > 3 else ''}, already provided by the image")
                continue
            chosen = (obj, closes)
            break
        if chosen is None:
            log(f"round {rounds}: every candidate conflicts with something already provided")
            break

        obj, closes = chosen
        log(f"  + {os.path.basename(obj)}  closes {len(closes)}: "
            f"{', '.join(sorted(closes)[:4])}{' ...' if len(closes) > 4 else ''}")
        objs.append(obj)
        provided |= set(nm_defs([obj])[0])

    undef, dupes = link(list(args.preset) + objs, args.script, tmp)
    by_kind = defaultdict(list)
    for sym in sorted(undef):
        if sym in provided:
            kind = "provided by the image"
        elif sym.startswith("__aeabi") or sym.startswith("__gnu"):
            kind = "compiler runtime"
        elif sym not in pool_defs:
            kind = "not defined anywhere in the pool"
        elif pool_types.get(sym) in STORAGE:
            kind = "storage"
        else:
            kind = "code"
        by_kind[kind].append(sym)

    print()
    print(f"closure: {len(objs)} object(s), {len(undef)} symbol(s) still undefined")
    for kind in sorted(by_kind):
        names = by_kind[kind]
        print(f"  {kind} ({len(names)}): {' '.join(names)}")

    if args.out:
        with open(args.out, "w") as f:
            f.write("# objects in the closure, in the order they were added\n")
            for o in objs:
                f.write(f"obj {o}\n")
            for kind in sorted(by_kind):
                for sym in by_kind[kind]:
                    f.write(f"{kind.split()[0]} {sym}\n")
        print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
