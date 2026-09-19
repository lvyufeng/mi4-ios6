#!/usr/bin/env python3
"""
Check that each component's generated `meta_features.h` is that component's own slice.

    ./tools/check_option_headers.py            # exits 1 with the offending entries

Why this exists. `tools/gen_option_headers.py` writes one `meta_features.h` per component, because
that is what a per-component build does - the OPTIONS headers belong to the `conf/files` that
declared them and are reached through the component's own include root. Which options a translation
unit sees is therefore a property of the *component*, and until experiment-438 this project's
generator emitted one flat `meta_features.h` for the whole kernel.

That was not cosmetic. `libkern/conf/files:5` declares `gprof`, so the flat header gave every
translation unit `#define GPROF 0` - and `bsd/kern/bsd_init.c:852` is `#ifdef GPROF`, for which
`#define GPROF 0` is *defined*. The call it guards is `kmstartup()`, whose only definer
(`bsd/kern/subr_prof.c`, `standard` in `bsd/conf/files`) cannot compile: its whole body is inside
the same `#ifdef` and it uses the macro `STATIC`, which no header it includes defines. The result
was a boot that stopped at `bsd_init + 0x844` on a symbol no configuration that ships can define.

Two checks, and the second is the one with teeth:

  1. **Membership.** Each `<component>/meta_features.h` must list exactly that component's own
     headers, then the shared ones, in that order.

  2. **Reads.** Every `#ifdef`/`#ifndef`/`defined()` read of an option macro by a component that
     does not declare it must be either *shared* (see `SHARED` in the generator, with the reason) or
     listed in `KNOWN_READS` below with the reason it is inert. A new one stops the build, because
     the decision it forces - is this read intentional? - is exactly the one that was never made
     for `GPROF`. The table is checked in both directions: a `KNOWN_READS` entry with no
     corresponding read fails too, so it cannot rot into a list of things that used to be true.

Structural, which is the point: it compares the generated files against `conf/files` and needs no
compiler, so it fails in a second rather than after a 680-file build. See
`mi4-a-claim-in-a-comment-is-not-a-check`.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
REPO_ROOT = os.path.dirname(HERE)

import gen_option_headers as gen  # noqa: E402  (same directory, same tables)

# The cross-component reads that are deliberate and inert, with the reason. One macro, five files,
# all of them the profiler: `bsd/kern/bsd_init.c`'s `kmstartup()` call, `kern_clock.c`'s and
# `subr_xxx.c`'s includes and `cfreemem`, `subr_prof.c`'s whole body, and `bsd/sys/gmon.h`'s
# declarations of the same. All of it is dead when GPROF is off - which is the configuration every
# shipping kernel is - and nothing in the pool references what it defines: `nm` over all 680
# objects finds no reference to `mcount`, `_gmonparam` or `cfreemem`, and `kmstartup` disappears with
# the call. So these reads are correct to leave undefined, and this entry is what says the
# difference between "inert" and "nobody looked" was looked at.
KNOWN_READS = {
    ("bsd", "GPROF"): "the profiler: dead when off, and nothing references what it defines",
}

READ_RE = re.compile(r"#\s*(?:ifdef|ifndef)\s+([A-Za-z_][A-Za-z0-9_]*)|defined\s*\(?\s*([A-Za-z_][A-Za-z0-9_]*)")


INCLUDE_RE = re.compile(r"^#include\s*<([^>]+)>")


def includes_of(path):
    """The `#include <...>` names a generated meta_features.h lists, in order.

    A regex and not `line.endswith(">")`: the generator marks the shared entries with a trailing
    comment (`#include <config_macf.h>   /* shared: ... */`), and the first version of this function
    read that as *not an include* - so the check reported five components missing the very header it
    had just been given. A parser that assumes nothing follows the `>` is a claim about the writer.
    """
    if not os.path.isfile(path):
        return None
    out = []
    for line in open(path, encoding="utf-8"):
        m = INCLUDE_RE.match(line.strip())
        if m:
            out.append(m.group(1))
    return out


def order_preserving_unique(names):
    seen, ordered = set(), []
    for name in names:
        if name not in seen:
            seen.add(name)
            ordered.append(name)
    return ordered


def reads_of(path):
    """The option macros a source or header tests with `#ifdef`/`#ifndef`/`defined()`."""
    src = open(path, encoding="utf-8", errors="replace").read()
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    found = set()
    for line in src.split("\n"):
        if line.lstrip().startswith("*"):
            continue
        for m in READ_RE.finditer(line):
            found.add(m.group(1) or m.group(2))
    return found


def measured_reads(declared_by, shared_macros, sources, headers):
    """{(component, macro): [file, ...]} for reads of an option the component does not declare.

    `declared_by` is macro -> the SET of components whose conf/files declare it, and the set is the
    point: `MACH_ASSERT` is declared by osfmk, bsd AND iokit, so a bsd file reading it is reading its
    own. A macro -> component map answers "who declared it first", which is a different question and
    gets `bsd/kern/kern_credential.c` wrong.
    """
    measured = {}
    for path, component in sources + headers:
        for macro in reads_of(path):
            declarers = declared_by.get(macro)
            if declarers is None or component in declarers or macro in shared_macros:
                continue              # not an option macro, its own, or shared on purpose
            measured.setdefault((component, macro), []).append(os.path.relpath(path, REPO_ROOT))
    return measured


def main():
    config = os.environ.get("XNU_KERNEL_CONFIG", "RELEASE")
    out = os.path.join(gen.OUT_ROOT, config)
    by_component = gen.scan_options_by_component()
    declared_by = {}
    all_headers = {}
    for component, rows in by_component.items():
        for header, macro in rows:
            declared_by.setdefault(macro, set()).add(component)
            all_headers.setdefault(macro, header)

    # The shared list names *options*, the way conf/files does (`optional config_macf`); the macros
    # are their upper-cased forms. Both spellings are needed and neither is derived from the other
    # in the generator, which is why this is written out rather than assumed.
    shared_macros = {m.upper() for m in gen.SHARED}
    shared_headers = [all_headers[m] for m in sorted(shared_macros) if m in all_headers]

    problems = []
    for component, rows in sorted(by_component.items()):
        path = os.path.join(out, component, "meta_features.h")
        own = order_preserving_unique([h for h, _ in rows])
        want = order_preserving_unique(own + shared_headers)
        got = includes_of(path)
        if got is None:
            problems.append(f"{component}: no {path} - run ./tools/gen_option_headers.py")
            continue
        if got != want:
            missing = [h for h in want if h not in got]
            extra = [h for h in got if h not in want]
            detail = []
            if missing:
                detail.append("missing " + ", ".join(missing))
            if extra:
                detail.append("not this component's: " + ", ".join(extra))
            if not detail:
                detail.append("same names in a different order")
            problems.append(f"{component}: " + "; ".join(detail))

    # The flat file, which is what the out-of-manifest translation units get: it must still be the
    # union, or a file that belongs to no component loses an option silently.
    flat = includes_of(os.path.join(out, "meta_features.h"))
    union_expected = []
    for component in gen.COMPONENTS:
        union_expected += order_preserving_unique([h for h, _ in by_component.get(component, [])])
    union_expected = order_preserving_unique(union_expected)
    if flat is None:
        problems.append(f"no {os.path.join(out, 'meta_features.h')}")
    elif sorted(set(flat)) != sorted(set(union_expected)):
        problems.append("the flat meta_features.h is not the union of the components': "
                        + ", ".join(sorted(set(union_expected) - set(flat))) + " missing")

    # Check 2: the reads. The sources are the manifest's, and the headers are the component trees'
    # (a header is read by whichever component includes it, and this tree's headers are read across
    # components - `osfmk/kern/task.h` is, which is why CONFIG_MACF is shared).
    xnu = gen.XNU
    files = []
    manifest = os.path.join(REPO_ROOT, "out", "xnu_arm_manifest.txt")
    if os.path.isfile(manifest):
        for line in open(manifest, encoding="utf-8"):
            path = line.strip()
            if not path.startswith(xnu + "/") or not os.path.isfile(path):
                continue
            component = os.path.relpath(path, xnu).split("/")[0]
            if component in gen.COMPONENTS:
                files.append((path, component))
    headers = []
    for component in gen.COMPONENTS:
        for root, _dirs, names in os.walk(os.path.join(xnu, component)):
            for name in names:
                if name.endswith(".h"):
                    headers.append((os.path.join(root, name), component))

    measured = measured_reads(declared_by, shared_macros, files, headers)
    for (component, macro), where in sorted(measured.items()):
        if (component, macro) in KNOWN_READS:
            continue
        problems.append(f"{component} tests {macro} (declared by "
                        f"{'/'.join(sorted(declared_by[macro]))}) without declaring it, and it is "
                        f"neither shared nor known: " + ", ".join(sorted(where)[:3])
                        + " - decide: add it to SHARED, or to KNOWN_READS with the reason")
    for key in sorted(KNOWN_READS):
        if key not in measured:
            problems.append(f"KNOWN_READS lists {key[1]} read by {key[0]}, and nothing reads it any "
                            "more - remove the entry rather than leaving it to be believed")

    if problems:
        print(f"option headers do not match conf/files for {config}:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        print("run: ./tools/gen_option_headers.py", file=sys.stderr)
        return 1
    counts = ", ".join(f"{c} {len(order_preserving_unique([h for h, _ in by_component[c]]))}"
                       for c in gen.COMPONENTS if c in by_component)
    inert = ", ".join(f"{c}/{m}" for c, m in sorted(KNOWN_READS))
    print(f"ok: {config} option headers carry their component "
          f"({counts}; + {len(shared_headers)} shared; flat {len(flat)} = the union; "
          f"inert cross-component read(s): {inert})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
