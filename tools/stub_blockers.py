#!/usr/bin/env python3
"""
Attribute every boot-path stub to the thing that blocks it.

    ./tools/stub_blockers.py                 # RELEASE
    ./tools/stub_blockers.py --min
    ./tools/stub_blockers.py --list 30

`stub_reach.py` says which stubs a boot reaches. This says **why each one is missing**, which is
the difference between a list and a plan. Four outcomes, and they need different work:

  * **in the manifest but never compiled** — the build's `failed.txt` is not enough to decide this.
    It holds only the files that *failed*, and `build_xnu_arm_kernel.sh` **skips every `.s`**, so an
    assembly file that cannot assemble is neither compiled nor failed and looks "fine" to anything
    keyed on that list. A first version of this tool made exactly that mistake and reported 30
    boot-path symbols as "defined by a compiled file - should not be a stub", including
    `machine_routines_asm.s`, which does not assemble. The test here is **whether an object exists
    for the file**, which is the same question the build answers.
    (`.cpp` was in that skip list until experiment-154 and is not any more; the distinction this
    paragraph draws is why the rule is "does an object exist" and not "is it in failed.txt" — it
    survived the `.cpp` half of the skip being removed without a line changing here.)
  * **a file that fails to compile** — on the failure list. Fix the file, the stub goes away.
  * **a file that is not in the manifest** — the definition exists but the build never asks for it
    (`optional <cond>` nothing satisfies, or a component the manifest omits). Fix the selection.
  * **nothing in the tree** — no source file defines it at all: a driver the kernel calls and the
    tarball does not contain, or a name only Apple's build defines.

The counts by category are the plan. The per-symbol detail says what to do about each.
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
OBJDUMP = os.environ.get("OBJDUMP", "arm-none-eabi-objdump")

LABEL = re.compile(r"^([0-9a-f]{8}) <([^>]+)>:")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{8}\s+)?(bl|blx)\s+([0-9a-f]+)\s+<([^>]+)>")

# A definition-shaped line: the name at the start of a definition, C or assembly.
# Two forms, and the second was missing from the first version: a definition whose name is at
# column 0 with the return type on a previous line — `_vm_object_allocate(` in vm_object.c — matched
# nothing, so 85 symbols were reported as "no source in the tree" when the source was right there.
# A measurement that cannot see a definition looks exactly like a definition that is absent.
DEF_C = re.compile(
    r"^(?:[A-Za-z_][A-Za-z0-9_ \t\*]*?[\s\*])([A-Za-z_][A-Za-z0-9_]*)\s*(?:\(|\[|=)"
    r"|^([A-Za-z_][A-Za-z0-9_]*)\s*\(")
DEF_ASM = re.compile(r"^\s*(?:LEXT\((\w+)\)|EXT\((\w+)\)|(\w+):)")
# An out-of-line C++ definition: `IOUserClient::copyClientEntitlement(` at the start of a line,
# with the return type either before it or on the line above. Used only for looking up the symbols
# the linker reports mangled - see `defines_cpp`.
DEF_CPP = re.compile(
    r"^[A-Za-z_][A-Za-z0-9_:<>, \t\*&]*?\b([A-Za-z_][A-Za-z0-9_]*)::(~?[A-Za-z_][A-Za-z0-9_]*)\s*\(")

COMPONENTS = ["osfmk", "bsd", "libkern", "iokit", "pexpert", "libsa", "security", "san"]


def boot_stubs(config, entry):
    out = os.path.join(REPO_ROOT, "out", "link")
    elf = os.path.join(out, f"{config}-measure.elf")
    stub_file = os.path.join(out, f"{config}-stubs.s")
    for path in (elf, stub_file):
        if not os.path.isfile(path):
            print(f"no {path} - run ./tools/measure_link.sh --keep-stubs first", file=sys.stderr)
            return None

    stubs = set()
    for line in open(stub_file):
        m = re.match(r"\s*\.weak\s+(\S+)", line)
        if m:
            stubs.add(m.group(1))

    dis = subprocess.run([OBJDUMP, "-d", elf], capture_output=True, text=True).stdout
    names, calls = set(), collections.defaultdict(set)
    cur = None
    for line in dis.splitlines():
        m = LABEL.match(line)
        if m:
            cur = m.group(2)
            names.add(cur)
            continue
        if cur is None:
            continue
        m = INSN.match(line)
        if m:
            calls[cur].add(m.group(4))
    if entry not in names:
        print(f"entry {entry} not in {elf}", file=sys.stderr)
        return None
    calls.setdefault(entry, set())

    dist, seen = {entry: 0}, {entry}
    queue = collections.deque([entry])
    reached = []
    while queue:
        fn = queue.popleft()
        for callee in sorted(calls.get(fn, ())):
            if callee in seen:
                continue
            seen.add(callee)
            dist[callee] = dist[fn] + 1
            if callee in stubs:
                reached.append(callee)
                continue
            queue.append(callee)
    reached.sort(key=lambda s: (dist[s], s))
    return reached, dist


def failing_files(config):
    d = "xnu_min_obj" if config == "STAGE90_BOOT" else "xnu_kernel_obj"
    p = os.path.join(REPO_ROOT, "out", d, "failed.txt")
    return [l.strip() for l in open(p)] if os.path.isfile(p) else []


def manifest(config):
    name = "xnu_arm_manifest_min.txt" if config == "STAGE90_BOOT" else "xnu_arm_manifest.txt"
    p = os.path.join(REPO_ROOT, "out", name)
    return set(l.strip() for l in open(p)) if os.path.isfile(p) else set()


def defines(path, want):
    """Which of `want` this file defines, by its definition-shaped lines."""
    got = set()
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return got
    if path.endswith((".s", ".S")):
        for line in text.splitlines():
            m = DEF_ASM.match(line)
            if m:
                n = m.group(1) or m.group(2) or m.group(3)
                if n in want:
                    got.add(n)
    else:
        for line in text.splitlines():
            if not line or line[0] in " \t#/*}":
                continue
            m = DEF_C.match(line)
            if m:
                n = m.group(1) or m.group(2)
                if n in want:
                    got.add(n)
    return got


def defines_cpp(path, want_q):
    """Which `Class::method` names of `want_q` this file defines, by its definition-shaped lines.

    This exists because of the mangling. A `.cpp` object's undefined references come back from the
    linker as `_ZN9IOService15getPMRootDomainEv`, and **no file in the tree contains that string**,
    so every C++ symbol landed in "no source in the tree" - a category that means "the tarball does
    not have this; write a driver". Nineteen boot-path stubs were being sent there by a lookup that
    could not read the name it was given. Demangled to `IOService::getPMRootDomain()` and matched
    against out-of-line definitions, they land where they belong, which is behind the two `.cpp`
    files that fail to compile.
    """
    got = set()
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return got
    for line in text.splitlines():
        if not line or line[0] in " \t#/*}":
            continue
        m = DEF_CPP.match(line)
        if m:
            q = m.group(1) + "::" + m.group(2)
            if q in want_q:
                got.add(q)
    return got


def demangle(syms):
    """`_ZN...` -> `Class::method(args)`. Empty if nothing could be demangled."""
    syms = sorted(syms)
    if not syms:
        return {}
    for tool in ("c++filt", "arm-none-eabi-c++filt", "llvm-cxxfilt"):
        try:
            out = subprocess.run([tool], input="\n".join(syms), capture_output=True,
                                 text=True, check=True).stdout.splitlines()
        except (OSError, subprocess.CalledProcessError):
            continue
        if len(out) == len(syms):
            return dict(zip(syms, out))
    return {}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--min", action="store_true")
    ap.add_argument("--from", dest="entry", default="arm_init")
    ap.add_argument("--list", type=int, default=20)
    args = ap.parse_args()

    config = "STAGE90_BOOT" if args.min else "RELEASE"
    res = boot_stubs(config, args.entry)
    if res is None:
        return 2
    boot, dist = res

    want = set(boot)
    failed = failing_files(config)
    inman = manifest(config)

    # One pass over the tree: which file defines which wanted symbol.
    bysym = collections.defaultdict(set)
    bysym_q = collections.defaultdict(set)
    # The mangled ones, and their demangled spelling with the parameter list stripped, which is
    # what a definition line can be matched against.
    demangled = demangle({s for s in want if s.startswith("_Z")})
    want_q = {d.split("(")[0].strip(): s for s, d in demangled.items() if "::" in d}
    inventory = []
    for comp in COMPONENTS:
        for p in (os.path.join(XNU, comp),):
            for root, _d, files in os.walk(p):
                if any(x in root for x in ("/i386", "/x86_64", "/arm64")):
                    continue
                for name in files:
                    if not name.endswith((".c", ".s", ".S", ".cpp", ".h")):
                        continue
                    f = os.path.join(root, name)
                    inventory.append(f)
                    got = defines(f, want)
                    for s in got:
                        bysym[s].add(f)
                    if want_q and name.endswith((".cpp", ".h")):
                        for q in defines_cpp(f, want_q):
                            bysym_q[q].add(f)

    failed_set = set(failed)
    obj_dir = os.path.join(REPO_ROOT, "out",
                           "xnu_min_obj" if config == "STAGE90_BOOT" else "xnu_kernel_obj")

    def has_object(f):
        """Does the build produce an object for this file?

        The build names objects `<path with / -> _>.o`, so the test is the same one the linker
        would make. It is asked instead of trusting `failed.txt`, which cannot answer for the
        files the build skips.
        """
        key = os.path.relpath(f, XNU).replace("/", "_")
        key = re.sub(r"\.(c|s|S|cpp)$", "", key)
        return os.path.exists(os.path.join(obj_dir, key + ".o"))

    cats = collections.Counter()
    detail = collections.defaultdict(list)

    rt = re.compile(r"^(__aeabi_|__div|__udiv|__mod|__umod|__mul|__clz|__float|__fix|__trunc|_Unwind|__stack_chk)")

    def classify(sym, files):
        if rt.match(sym):
            return "compiler runtime, not source", "libgcc / compiler-rt"
        if not files and sym in demangled:
            # The C-name scan cannot see a mangled name; the qualified scan can. Only used when the
            # first lookup found nothing, so a C name keeps whatever the first pass said.
            q = demangled[sym].split("(")[0].strip()
            files = bysym_q.get(q, set())
        if not files:
            return "no source in the tree", "-"
        blocked = [f for f in files if f in failed_set]
        if blocked:
            return "a file that fails to compile", blocked[0].replace(XNU + "/", "")
        # Assembled and never-compiled files come through here: in the manifest, not failed,
        # and with no object.
        no_obj = [f for f in files if f in inman and not has_object(f)]
        if no_obj:
            f = sorted(no_obj)[0]
            kind = "assembly the build never attempts" if f.endswith((".s", ".S")) \
                else "C++ the build never attempts" if f.endswith(".cpp") \
                else "in the manifest, no object produced"
            return kind, f.replace(XNU + "/", "")
        outside = [f for f in files if f not in inman]
        if outside:
            return "a file not in the manifest", sorted(outside)[0].replace(XNU + "/", "")
        return "defined by a compiled file - should not be a stub", sorted(files)[0].replace(XNU + "/", "")

    for sym in boot:
        cat, f = classify(sym, bysym.get(sym, set()))
        cats[cat] += 1
        detail[cat].append((sym, dist[sym], f))

    print(f"== {config}: why the {len(boot)} boot-path stubs are missing ==")
    for cat, n in cats.most_common():
        print(f"  {n:4d}  {cat}")
    print()

    for cat, _n in cats.most_common():
        rows = sorted(detail[cat], key=lambda r: (r[1], r[0]))
        print(f"== {cat} ({len(rows)}) ==")
        for sym, d, f in rows[:args.list]:
            print(f"  {d:4d}  {sym:34s} {f}")
        if len(rows) > args.list:
            print(f"        ... and {len(rows) - args.list} more")
        print()

    print("  Each category needs different work: a compile fix, a manifest fix, or a driver that")
    print("  the tarball does not contain. The distances are from stub_reach.py and are lower bounds.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
