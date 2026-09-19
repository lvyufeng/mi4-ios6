#!/usr/bin/env python3
"""
Refuse a *function* stand-in for a symbol the tree declares as an object.

    ./tools/check_stub_kinds.py                 # uses the paths build_entry.sh writes
    ./tools/check_stub_kinds.py --undef P --kernsyms Q

Why this exists, and it is the only place it could have been caught. `build_entry.sh` generates one
stand-in per undefined symbol, and it decides function-or-storage from `nm` over the **object pool**.
When the pool defines the symbol, that decision is measured. **When the pool defines nothing, the
generator has no type information at all and falls through to a function stub** - which is how
`pseudo_inits` became one, and why experiment 438's run ended with an instruction prefetch abort at
`ifar=0xE52DE004`, the ARM encoding of `push {lr}`:

    bsd/kern/bsd_init.c:861        bsd_autoconf();
    bsd/kern/bsd_init.c:1095       for (pi = pseudo_inits; pi->ps_func; pi++) (*pi->ps_func)(pi->ps_count);
    bsd/dev/busvar.h:46            extern struct pseudo_init pseudo_inits[];

The stand-in's own prologue *was* the array's first `ps_func`, so the walk's termination test read
non-NULL and the first `bx`/`blx` went to an instruction word. A function stand-in for an array is
not a slightly wrong stand-in; it is a valid little table whose second word points into itself.

**The undefined list gives names and never kinds, so the kind has to come from the tree's own
headers.** This check reads them: for every name the pool does not define, it looks for a
declaration and classifies it. A name declared as an object fails the build, because the right
answer for such a name is to *supply* the object (439 supplies `pseudo_inits` from
`tools/gen_pseudo_inits.py`) rather than to stand in for it.

**What it will not do, stated rather than implied:** it reads declarations with two patterns over
`*.h`, so it can miss an object declared in a way those patterns do not match, and it reports those
as *unclassified* with a count rather than passing them silently. A negative result from a
fixed-shape scan is a statement about the scan as much as about the tree - the rule this project's
own defect ledger keeps arriving at - so the count is printed on every run, including the runs where
nothing fails.
"""

import argparse
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
XNU = os.environ.get("XNU_TREE", os.path.join(REPO_ROOT, "external", "xnu-4570.1.46"))

DEFAULT_UNDEF = os.path.join(REPO_ROOT, "out", "stage90", "xnu_arm_entry_undef.txt")
DEFAULT_KERNSYMS = os.path.join(REPO_ROOT, "out", "stage90", "xnu_arm_entry_kernsyms.txt")

# The wrong-kind stand-ins this build still ships, each with the *measured* cause and the step that
# will remove it. The table is checked in both directions - an entry whose name is no longer an
# object-declared stub fails the check - so it cannot rot into a list of things that used to be true,
# and it is the same shape as `KNOWN_READS` in tools/check_option_headers.py.
#
# All four are latent: nothing in this boot reaches them, which is why they are recorded rather than
# fixed here. `pseudo_inits` is *not* in the table, because 439 supplies it - and if it were to come
# back as an object-declared stub, the both-direction check is what would say so.
KNOWN_KINDS = {
    "etherbroadcastaddr": "bsd/net/ether_if_module.c is `optional ether` and `ether` IS a "
                          "pseudo-device of this configuration, but out/device_table.txt does not "
                          "list it - so the file is not in the manifest and the array has no "
                          "definer. The condition table has to come from config/MASTER, not from a "
                          "hand-written list (see experiment 439's measurements).",
    "lo_ifp": "the same cause: bsd/net/if_loop.c is `optional loop`, and `loop` is a pseudo-device "
              "of this configuration that the hand-written condition table omits.",
    "osrelease": "defined by config/version.c, a **tree-root** source that no */conf/files lists, so "
                 "the manifest has never contained it.",
    "ostype": "the same file as osrelease - config/version.c declares all four of osrelease, ostype, "
              "version_major and version_minor together.",
}

# `nm`'s letters for a symbol the pool defines. The generator's own split, repeated here so this
# check agrees with it about which names it is even allowed to have an opinion on.
STORAGE = set("DBRSGC")


def undefined_names(path):
    if not os.path.isfile(path):
        sys.exit(f"no {path} - run stages/stage90/xnu_arm_boot/build_entry.sh first")
    return [l.strip() for l in open(path) if l.strip()]


def pool_types(path):
    """name -> nm type letter, from the `nm -A -S -P` dump the generator also reads."""
    types = {}
    if os.path.isfile(path):
        for line in open(path):
            parts = line.split()
            if len(parts) >= 2:
                types.setdefault(parts[0], parts[1])
    return types


def headers():
    for component in ("osfmk", "bsd", "libkern", "iokit", "pexpert", "libsa", "security", "san"):
        root = os.path.join(XNU, component)
        for dirpath, _dirs, names in os.walk(root):
            for name in names:
                if name.endswith(".h"):
                    yield os.path.join(dirpath, name)


def classify(name, text):
    """('function'|'object'|None, 'file:line') for the first declaration of `name` in one header.

    Two shapes, and both are needed. A prototype is the name followed by `(`; an object is an
    `extern` declaration with the name not followed by `(` - `extern struct pseudo_init
    pseudo_inits[];` is the one that matters here. The `extern` is required for the object case so
    that an ordinary use of the name inside a header cannot be read as its declaration.
    """
    word = re.compile(r"\b%s\b" % re.escape(name))
    for lineno, line in enumerate(text.split("\n"), 1):
        m = word.search(line)
        if not m:
            continue
        stripped = line.strip()
        # Function-like: the name is followed by an opening paren (possibly with whitespace).
        if re.match(r".*\b%s\s*\(" % re.escape(name), line) and "(" in line[m.end():m.end() + 4]:
            return "function", lineno
        if "extern" in stripped and "(" not in line[m.end():]:
            return "object", lineno
    return None, None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--undef", default=DEFAULT_UNDEF)
    ap.add_argument("--kernsyms", default=DEFAULT_KERNSYMS)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    names = undefined_names(args.undef)
    types = pool_types(args.kernsyms)
    # Only the names the pool does not define: for the others the generator's decision is measured
    # from `nm`, which is a better answer than a declaration scan can give.
    unknown = [n for n in names if n not in types]
    known_storage = [n for n in names if types.get(n) in STORAGE]

    declared = defaultdict(list)
    wanted = set(unknown)
    header_files = list(headers())
    for path in header_files:
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for name in wanted:
            if name not in text:
                continue
            kind, lineno = classify(name, text)
            if kind:
                rel = os.path.relpath(path, REPO_ROOT)
                declared[name].append((kind, f"{rel}:{lineno}"))

    objects = {n: [w for k, w in declared.get(n, []) if k == "object"] for n in unknown}
    object_names = sorted(n for n in unknown if objects.get(n))
    unclassified = sorted(n for n in unknown if not declared.get(n))

    problems = []
    # Both directions, so the exception table cannot become a record of things that used to be true.
    for name in sorted(set(object_names) - set(KNOWN_KINDS)):
        problems.append(
            f"'{name}' is declared as an OBJECT ({objects[name][0]}) and this image has no definition "
            f"of it, so the generator would emit a FUNCTION stand-in. A function stand-in for an "
            f"object is a valid little table whose words are its own instructions - reachable through "
            f"a byte that skips the stub, and not through a call. Supply the object (see "
            f"tools/gen_pseudo_inits.py and experiment 439), link the object that defines it, or - if "
            f"it is latent and the cause is measured - add it to KNOWN_KINDS with that cause.")
    for name in sorted(set(KNOWN_KINDS) - set(object_names)):
        problems.append(
            f"KNOWN_KINDS lists '{name}' as a wrong-kind stand-in, but it is not one in this build. "
            f"An exception that outlives its cause is worse than no exception: remove it, or say in "
            f"the table why it is still right. (recorded cause: {KNOWN_KINDS[name]})")

    if problems:
        print(f"stub kinds are not safe for {len(problems)} name(s):", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1

    if not args.quiet:
        # The counts are printed whether or not anything failed, because the useful half of this
        # check is what it could *not* classify: a scan that finds nothing and says nothing is the
        # failure mode `mi4-measurement-defects` keeps recording.
        functions = sorted(n for n in unknown if declared.get(n)
                           and any(k == "function" for k, _ in declared[n]))
        print(f"ok: {len(names)} undefined; {len(known_storage)} storage by nm, "
              f"{len(names) - len(unknown)} defined by the pool; "
              f"{len(unknown)} with no pool definition - "
              f"{len(functions)} declared in a header as functions, "
              f"{len(object_names)} declared as objects and all of them in KNOWN_KINDS "
              f"({', '.join(object_names) or 'none'}), "
              f"{len(unclassified)} unclassified")
        for name in object_names:
            print(f"    object-declared stand-in, recorded: {name} ({objects[name][0]}) - "
                  f"{KNOWN_KINDS[name].split('.')[0]}.")
        # The unclassified names are printed for the same reason the count is: the check cannot say
        # what kind they are, so the honest output is the list and not a pass.
        if unclassified:
            print(f"    unclassified (no declaration found, function stand-in unverified): "
                  f"{', '.join(unclassified)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
