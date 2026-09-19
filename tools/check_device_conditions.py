#!/usr/bin/env python3
"""
One device decision, three consumers, and this compares all three.

    ./tools/check_device_conditions.py

Why this exists. "Is `bpfilter` on?" is answered in **three** places in this build, and for several
stages they gave three different answers with nothing comparing them:

    out/device_table.txt                  bpfilter  -> absent  -> the manifest omits bsd/net/bpf.c
    out/xnu_device/<CONFIG>/bpfilter.h    #define NBPFILTER 0     -> hand-written
    out/xnu_pseudo_inits/.../*.c          {4, bpf_init}           -> the array the image walks

The third is the one that gave it away: experiment 439 generated the array from the configuration and
the image then walked it, so the run stopped on `stub_hit=bpf_init` — a symbol this image was
simultaneously claiming (in the array's own count, 4, taken from `config/MASTER`) and refusing to
build (no `bpfilter` in the table). **A count written into a table this image executes is a
declaration that the device exists**, and the manifest disagreed with it silently.

What is checked, and each of these is a cross-file comparison rather than a restatement:

  1. **the configuration vs the table** — every device the configuration declares is in the table at
     1, unless `device_table.py`'s `OVERRIDES` says otherwise with a reason;
  2. **the table vs the headers** — a condition the table turns on that a `conf/files` line tests must
     have a header, and the header's count must be the configuration's `d_slave`
     (`mkheaders.c:85-101`: `d_slave != UNKNOWN ? d_slave : 1`), not a number someone chose;
  3. **the headers vs the sources** — every device macro the tree actually reads (`NETHER` in
     `bsd_init.c`, `NLOOP` in four files, `NBPFILTER` in eighteen) must have a header, because an
     undefined identifier in `#if` is **0** and that is a silent "off" rather than an error;
  4. **the headers vs the array** — for a device with an `d_init`, the array's count
     (`mkioconf.c:79-100`, `count <= 0 -> 1`) and the header's count (`mkheaders.c:85-101`) come from
     the same `d_slave` by two different rules, and where they differ the difference is reported
     rather than assumed away.

Both directions, in all four: a table entry of 1 with no header fails, a header for a condition the
configuration does not have fails, a macro the tree reads with no header fails, and an array entry
for a device whose header says something else fails.
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
XNU = os.environ.get("XNU_TREE", os.path.join(REPO_ROOT, "external", "xnu-4570.1.46"))

sys.path.insert(0, os.path.join(HERE, "xnu_config"))
import devices as devices_mod                                                     # noqa: E402
import device_table as table_mod                                                  # noqa: E402
import gen_device_headers as headers_mod                                          # noqa: E402

sys.path.insert(0, HERE)
import gen_pseudo_inits as inits_mod                                              # noqa: E402

CONFIG = os.environ.get("XNU_KERNEL_CONFIG", "RELEASE")
DEVICE_TABLE = os.environ.get("XNU_DEVICE_TABLE", os.path.join(REPO_ROOT, "out", "device_table.txt"))

# `#define NFOO 3` in a generated device header.
DEFINE_RE = re.compile(r"^#define\s+(N[A-Z0-9_]+)\s+(\d+)\s*$")
# Every `#include <x.h>` in the tree, for the third comparison.
INCLUDE_RE = re.compile(r"#\s*include\s*<([^>]+)>")
# `{4,\tbpf_init},` in the generated array.
ENTRY_RE = re.compile(r"^\s*\{\s*(\d+)\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\}\s*,\s*$")


def read_table(path):
    out = {}
    if not os.path.isfile(path):
        sys.exit(f"no {path} - run: XNU_KERNEL_CONFIG={CONFIG} ./tools/xnu_config/device_table.py "
                 f"--write {path}")
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        parts = line.split()
        if len(parts) >= 2:
            out[parts[0].lower()] = parts[1] == "1"
    return out


def read_headers(config):
    """macro -> (count, filename), for every generated device header."""
    out = {}
    directory = headers_mod.out_dir(config)
    if not os.path.isdir(directory):
        sys.exit(f"no device headers at {directory} - run:\n"
                 f"  XNU_KERNEL_CONFIG={config} ./tools/gen_device_headers.py")
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".h"):
            continue
        for line in open(os.path.join(directory, name)):
            m = DEFINE_RE.match(line.strip())
            if m:
                out[m.group(1)] = (int(m.group(2)), name)
    return out


def tree_sources():
    """Every source and header file the tree contains, for the include and macro scans."""
    for component in ("osfmk", "bsd", "libkern", "iokit", "pexpert", "libsa", "security"):
        root = os.path.join(XNU, component)
        for dirpath, _dirs, names in os.walk(root):
            for name in names:
                if name.endswith((".c", ".h", ".cpp", ".s")):
                    yield os.path.join(dirpath, name)


def macro_readers(macros):
    """macro -> ['file:line', ...] for every read of one of these device macros."""
    pattern = re.compile(r"\b(%s)\b" % "|".join(re.escape(m) for m in macros))
    found = {}
    for path in tree_sources():
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        if not pattern.search(text):
            continue
        rel = os.path.relpath(path, REPO_ROOT)
        for lineno, line in enumerate(text.split("\n"), 1):
            stripped = line.strip()
            # A `#define NFOO 1` or a comment mentioning the macro is not a read of it.
            if stripped.startswith(("/*", "*", "//")) or stripped.startswith("#define"):
                continue
            for m in pattern.findall(line):
                found.setdefault(m, []).append(f"{rel}:{lineno}")
    return found


def header_includers(headers):
    """header filename -> ['file:line', ...], and the set of headers included by name anywhere."""
    included = set()
    for path in tree_sources():
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for m in INCLUDE_RE.finditer(text):
            if m.group(1) in headers:
                included.add(m.group(1))
    return included


def array_entries(config):
    """[(init, count)] from the generated array source, and the path it was read from."""
    path = inits_mod.out_file(config)
    if not os.path.isfile(path):
        sys.exit(f"no {path} - run: XNU_KERNEL_CONFIG={config} ./tools/gen_pseudo_inits.py --write")
    out = []
    for line in open(path):
        m = ENTRY_RE.match(line)
        if m:
            out.append((m.group(2), int(m.group(1))))
    return out, path


def main():
    table = read_table(DEVICE_TABLE)
    declared = devices_mod.devices(CONFIG)
    decl = {name.lower(): number for name, number, _init, _kind in declared}
    headers = read_headers(CONFIG)
    headers_as_files = {name for _count, name in headers.values()}
    tested = table_mod.conditions()
    emitted = dict(headers_mod.emitted(CONFIG))

    problems = []
    notes = []

    # (1) the configuration vs the table.
    for cond, number in sorted(decl.items()):
        want = table_mod.OVERRIDES.get(cond)
        if want is not None:
            if want[0] != 1:
                notes.append(f"{cond}: the configuration declares it and OVERRIDES turns it "
                             f"{want[0]} ({want[1]})")
            continue
        if not table.get(cond):
            problems.append(
                f"the {CONFIG} configuration declares `{cond}` (d_slave="
                f"{'UNKNOWN' if number is None else number}) and the table says "
                f"{'absent' if cond not in table else '0'} - so every file behind "
                f"`optional {cond}` is left out of the manifest while the configuration says the "
                f"device exists")

    # (2) the table vs the headers.
    for cond in sorted(tested):
        if cond not in declared:
            continue
        want = devices_mod.header_count(decl[cond])
        macro = "N" + cond.upper()
        if not table.get(cond):
            if macro in headers and headers[macro][0] != 0:
                problems.append(
                    f"{cond}: the table says off, so `optional {cond}` never matches, but "
                    f"{headers[macro][1]} says `#define {macro} {headers[macro][0]}` - the sources "
                    f"that read it compile the device in while the manifest leaves its file out")
            continue
        if macro not in headers:
            problems.append(
                f"{cond}: the table says on and `optional {cond}` matches, but no generated header "
                f"defines {macro} - an undefined identifier in `#if` is 0, so every source that "
                f"tests it silently takes the off branch while the manifest builds the device's "
                f"files")
        elif headers[macro][0] != want:
            problems.append(
                f"{cond}: {headers[macro][1]} says `#define {macro} {headers[macro][0]}` but the "
                f"configuration's d_slave is {want} - the value is hand-written where the "
                f"declaration has one")

    # A header for a condition the configuration does not have: it would be found on the include
    # path and believed, which is the same defect in the loud direction.
    #
    # `decl` and not `declared`: `declared` is the list of tuples and `cond` is a string, so
    # `cond not in declared` is true for every condition and the check reported all five headers as
    # stale on its own first run. `in` against the wrong container of the right values is the
    # quietest way to write a check that passes - or fails - for the wrong reason.
    for macro, (count, name) in sorted(headers.items()):
        cond = macro[1:].lower()
        if macro.startswith("N") and cond not in decl:
            problems.append(f"{name} defines {macro} {count}, but `{cond}` is not a device of "
                            f"{CONFIG} - a stale header is found on the include path and believed")

    # (3) the headers vs the sources.
    readers = macro_readers(["N" + c.upper() for c in decl])
    for macro, sites in sorted(readers.items()):
        if macro not in headers:
            problems.append(
                f"{macro} is read by the tree ({len(sites)} site(s), first {sites[0]}) and no "
                f"generated header defines it - `#if {macro} > 0` is then 0 and the guard is off, "
                f"silently")
    included = header_includers({f"{c}.h" for c in decl})
    for name in sorted(included):
        cond = name[:-2]
        if cond not in emitted and cond in decl:
            problems.append(f"<{name}> is included by the tree but is not a condition any "
                            f"conf/files line tests, so it has no generated header to include")

    # (4) the headers vs the array: two rules on one d_slave.
    entries, array_path = array_entries(CONFIG)
    for init, count in entries:
        owner = [(name, number) for name, number, i, _k in declared if i == init]
        if not owner:
            continue
        name, number = owner[0]
        macro = "N" + name.upper()
        header_count = devices_mod.header_count(number)
        if macro in headers and headers[macro][0] != count and header_count != count:
            problems.append(
                f"{init}: the array at {os.path.relpath(array_path, REPO_ROOT)} carries count "
                f"{count}, header {headers[macro][1]} says {macro} {headers[macro][0]}, and the "
                f"declaration is d_slave={'UNKNOWN' if number is None else number} - three "
                f"answers to one count")

    # (5) the two generators must be disjoint, and every device header must be reachable.
    #
    # Both halves are on-disk comparisons rather than source-level ones, because what bites is a
    # *file*, and a file survives a change to the code that wrote it. Before this step both
    # generators wrote `ether.h` and `bpfilter.h` — `out/xnu_options/` said `#define ETHER 0` and
    # `out/xnu_device/` said `#define NETHER 1` — and `-I$OPTION_HEADERS` comes first, so the device
    # header was shadowed and `#if NETHER > 0` in `bsd/kern/bsd_init.c:890` stayed a silent zero
    # while the manifest built `bsd/net/ether_if_module.c`.
    option_dir = os.path.join(REPO_ROOT, "out", "xnu_options", CONFIG)
    device_dir = headers_mod.out_dir(CONFIG)
    if os.path.isdir(option_dir):
        both = sorted(set(os.listdir(option_dir)) & set(os.listdir(device_dir)))
        for name in both:
            problems.append(
                f"{name} exists in both {os.path.relpath(option_dir, REPO_ROOT)} and "
                f"{os.path.relpath(device_dir, REPO_ROOT)} - two headers, one name, one include "
                f"path, and `-I` order decides which a source gets")

    # `do_header` ends by appending `#include <hname.h>` to the component's `meta_features.h`, so a
    # device header nothing force-includes is a macro the sources cannot see. That is not a
    # hypothetical: `NETHER` was read by `bsd_init.c:890` and `nfs_boot.c:128` with no `ether.h`
    # anywhere, and `#if NETHER > 0` is `0` rather than an error.
    force_included = set()
    for component in ("osfmk", "bsd", "libkern", "iokit", "pexpert", "libsa", "security"):
        path = os.path.join(option_dir, component, "meta_features.h")
        if not os.path.isfile(path):
            continue
        for line in open(path):
            m = INCLUDE_RE.search(line)
            if m:
                force_included.add(m.group(1))
    for name in sorted(set(headers_as_files) - force_included):
        problems.append(
            f"{name} is generated but no component's meta_features.h force-includes it, so no "
            f"source can read its macro without an explicit #include - and an unread guard is a "
            f"silent 0")

    # The *inert* case, and it is worth printing rather than ignoring: a header no source includes
    # and a macro no source reads changes nothing, so its value cannot be wrong in a way anything
    # would notice. `NPTMX` is exactly this - `ptmx.h` is generated faithfully (config(8) emits it
    # because `tty_dev.c` and `tty_ptmx.c` are `optional ptmx`) and `NPTMX` appears in no source in
    # the tree, while in Apple's build it would at least be *defined* everywhere through
    # `meta_features.h`'s appended include.
    #
    # The first version of this compared `name in s` against the site strings - a substring test
    # against `'file:line'` - which is true for nothing, so every macro was reported inert including
    # `NBPFILTER`, read in eighteen places. The container is `readers`, keyed by macro.
    emitted_but_unused = sorted(
        name for name in headers
        if name not in readers and (name[1:].lower() + ".h") not in included)
    if problems:
        print(f"the device conditions disagree for {len(problems)} reason(s):", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1

    print(f"ok: {len(decl)} device(s) declared by {CONFIG}; "
          f"{sum(1 for c in decl if table.get(c))} on in {os.path.relpath(DEVICE_TABLE, REPO_ROOT)}, "
          f"{len(headers)} header(s) "
          f"({', '.join(f'{m}={headers[m][0]}' for m in sorted(headers))}); "
          f"{len(readers)} macro(s) read by the tree, all defined; "
          f"{len(entries)} array entr{'y' if len(entries) == 1 else 'ies'} "
          f"{'agrees' if len(entries) == 1 else 'agree'} with the device's header")
    # Printed whether or not anything failed, for the reason the other checks in this project print
    # their negative half: a header nothing includes and a macro nothing reads is the *inert* case,
    # and silence about it is indistinguishable from agreement.
    if emitted_but_unused:
        print(f"    emitted but read by nothing: {', '.join(emitted_but_unused)}")
    for note in notes:
        print(f"    note: {note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
