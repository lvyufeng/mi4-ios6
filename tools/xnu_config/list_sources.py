#!/usr/bin/env python3
"""Resolve XNU's own per-component file lists against a configuration.

    ./tools/xnu_config/list_sources.py RELEASE          # the ARM RELEASE kernel's source files
    ./tools/xnu_config/list_sources.py RELEASE --why    # ...with the condition each one met

XNU ships its kernel's file manifest in `*/conf/files` and `*/conf/files.<arch>`. The format is the
classic BSD one:

    path/to/file.c          standard
    path/to/other.c         optional <flag> [<flag> ...]

and the semantics are in the reference implementation, `SETUP/config/mkmakefile.c` — not guessed:

  * every listed flag must be **defined** (AND, not OR). Reading `mkmakefile.c:416-422`, each word
    is checked and, when it is *not* defined, added to the file's `needs`; the file is emitted only
    if `needs` ends up empty.
  * `optional not <flag>` inverts: emit when `<flag>` is **absent**.

This is the piece that turns "I have a configuration" into "I have a build". Combined with the
option set from `make_defines.sh`, it says exactly which files an ARM kernel is made of — which is
what the 32-of-32 ARM measurement was missing: it compiled every `.c` in `osfmk/arm`, including
ones a real kernel would not use.

Paths in `files.<arch>` are relative to the component root (`osfmk/`), and paths beginning with
`./` are relative to a *generated-object* directory — which is where a MIG-generated `*_server.c`
and a makesyscalls-generated `init_sysent.c` belong. There is more than one such directory and they
are searched in order; see `resolve_path`.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))

# Component -> the directories its file lists live in. `conf` is the component's own conf/ dir.
COMPONENTS = ["osfmk", "bsd", "libkern", "iokit", "pexpert", "libsa", "security", "san"]

# Apple's own COMPONENT_LIST (MakeInc.def:46), less `libsa`:
#
#   COMPONENT_LIST = osfmk bsd libkern iokit pexpert libsa security san
#
# `libsa` is excluded because its files are bootloader-context code the kernel does not link (its
# `<types.h>` is reached as a header, which is what tools/gen_libsa_export.sh is for). `security` and
# `san` were excluded here too, and **that was a defect with a measurable cost**: `CONFIG_MACF=1` is
# set in RELEASE, `security/mac_*.c` are `optional config_macf`, and the kernel's own sources call
# them - so the manifest was selecting a configuration whose files could never define the symbols
# the rest of the kernel referenced. `security/` alone accounted for **~180 of the 699 undefined
# symbols** in the link, and no amount of fixing compile errors would have touched them, because the
# files were never compiled at all.
#
# `san` is excluded as well, and that is a measurement rather than an omission: its five files are
# `standard` in san/conf/files, but they are KASAN build machinery — `san/kasan.c:54` includes
# `<kasan.h>`, which is not in the tarball, and `san/kasan_internal.h:52` is `#error KASAN undefined`
# — and a non-KASAN kernel compiles none of them. Measured: including `san` adds 5 failing files and
# contributes **0** of the link's undefined symbols.
DEFAULT_COMPONENTS = ["osfmk", "bsd", "libkern", "iokit", "pexpert", "security"]


def expand_options(xnu, config):
    """The option names a configuration selects, via the ported doconf pipeline.

    XNU_MASTER_LOCAL, if set, names a fragment that can declare extra configurations - the same
    role Apple's doconf gives MASTER.local. See tools/xnu_config/minimal/.
    """
    env = {**os.environ, "XNU_TREE": xnu}
    if os.environ.get("XNU_MASTER_LOCAL"):
        env["XNU_MASTER_LOCAL"] = os.environ["XNU_MASTER_LOCAL"]
    out = subprocess.run(
        [os.path.join(HERE, "make_defines.sh"), config],
        capture_output=True, text=True, check=True,
        env=env,
    ).stdout
    names = set()
    for line in out.splitlines():
        if not line.startswith("-D"):
            continue
        name = line[2:].split("=", 1)[0]
        names.add(name)
    return names


def option_aliases(options):
    """Options are matched case-insensitively by XNU's config tool (`opteq`), so `mach_bsd` in a
    files list matches `MACH_BSD` in MASTER. Build the lookup the same way."""
    return {o.lower() for o in options}


def parse_file_list(path, component):
    """Yield (relative_path, condition, comment) for every non-comment line."""
    if not os.path.isfile(path):
        return
    with open(path) as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            rel, kind = parts[0], parts[1]
            if kind not in ("standard", "optional"):
                continue
            # `OPTIONS/foo` is not a source file. It is how the old config tool is told to emit
            # `opt_foo.h`, the header a source includes to know whether an option is on. Those
            # headers are ours to generate (see the shims), so the entries are dropped here rather
            # than reported as missing files.
            if rel.startswith("OPTIONS/"):
                continue
            flags = parts[2:]
            yield rel, kind, flags


def device_table(path):
    """condition -> 0|1, from tools/xnu_config/device_table.py.

    A `*/conf/files` condition can come from three places in Apple's build, and only the first is in
    `config/MASTER`: an option, an `OPTIONS/` line's header value, and a `device`/`pseudo-device`
    declaration. **4570 publishes no device lines at all**, so `optional loop` and `optional pty` can
    never match - and `optional monotonic` could not either, while the build script defines
    `-DMONOTONIC=1` by hand. That meant `osfmk/kern/kern_monotonic.c`, the only file implementing
    what the macro turns on, was excluded from the manifest while every user of it was compiled
    against `MONOTONIC 1`. This table is where those choices live, so the two cannot disagree again.
    """
    table = {}
    if not path or not os.path.isfile(path):
        return table
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) >= 2:
            table[parts[0].lower()] = parts[1] == "1"
    return table


def condition_met(kind, flags, options_lc, extras=None):
    if kind == "standard":
        return True, ""
    if not flags:
        return False, "no condition given"
    extras = extras or {}
    if flags[0] == "not":
        present = [f for f in flags[1:] if f.lower() in options_lc or extras.get(f.lower())]
        return (not present), "not " + " ".join(flags[1:])
    met = [f for f in flags if f.lower() in options_lc or extras.get(f.lower())]
    return (len(met) == len(flags)), " ".join(flags)


def resolve_path(xnu, component, rel, generated_dirs):
    """`./x` is relative to a generated-object directory; anything else to the tree root.

    Verified against the lists rather than assumed: `osfmk/conf/files.arm` writes
    `osfmk/arm/pmap.c` (already component-prefixed) and `osfmk/conf/files` writes both
    `osfmk/kern/sched_prim.c` and `./gssd/gssd_mach.c`, so the prefix form is root-relative and
    the `./` form is a generated directory.

    **Which** generated directory is not one thing, and treating it as one was a defect. The 42
    `./` entries across the components' `files` lists come from three different generators:

        ./mach/task_server.c, ./device/device_server.c, ...     MIG          -> out/mach_headers
        ./init_sysent.c, ./syscalls.c, ./audit_kevents.c, ...   makesyscalls -> out/xnu_generated/bsd
        ./ioconf.c                                              config(8)    -> not generated here

    so the roots are searched in order and the first that exists wins. Before this, every one of
    them was resolved against the MIG root, which is why `init_sysent.c` and `syscalls.c` were
    reported as "listed but absent" while sitting in a directory nobody had generated them into.
    """
    if rel.startswith("./"):
        for root in generated_dirs:
            candidate = os.path.join(root, rel[2:])
            if os.path.exists(candidate):
                return candidate, True
        # Nothing generated it; report the path the first root would have used, so the message
        # names a directory someone can go and look in.
        return os.path.join(generated_dirs[0], rel[2:]), True
    return os.path.join(xnu, rel), False


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("config", help="configuration name, e.g. RELEASE or DEVELOPMENT")
    ap.add_argument("--xnu", default=os.path.join(REPO_ROOT, "external", "xnu-4570.1.46"))
    ap.add_argument("--arch", default="arm")
    ap.add_argument("--component", action="append", dest="components",
                    help="limit to a component (repeatable); default is all kernel components")
    ap.add_argument("--why", action="store_true", help="print the condition each file met")
    ap.add_argument("--missing", action="store_true",
                    help="report listed files that are not present on disk")
    ap.add_argument("--generated-dir",
                    default=":".join([os.path.join(REPO_ROOT, "out", "mach_headers"),
                                      os.path.join(REPO_ROOT, "out", "xnu_generated", "bsd")]),
                    help="directories holding build-generated sources, colon-separated and "
                         "searched in order; `./x` entries are resolved against them")
    ap.add_argument("--device-table",
                    default=os.path.join(REPO_ROOT, "out", "device_table.txt"),
                    help="conditions this project chooses beyond config/MASTER; see "
                         "tools/xnu_config/device_table.py")
    ap.add_argument("--write", metavar="PATH",
                    help="write the selected file list here, one path per line, instead of "
                         "printing it - so the manifest is a build input rather than a report")
    args = ap.parse_args()

    options = expand_options(args.xnu, args.config)
    options_lc = option_aliases(options)
    extras = device_table(args.device_table)
    components = args.components or DEFAULT_COMPONENTS

    total = 0
    present = 0
    absent = []
    generated = []
    selected = []

    for component in components:
        conf = os.path.join(args.xnu, component, "conf")
        if not os.path.isdir(conf):
            continue
        entries = []
        for name in ("files", f"files.{args.arch}"):
            for rel, kind, flags in parse_file_list(os.path.join(conf, name), component):
                entries.append((rel, kind, flags))

        chosen = []
        for rel, kind, flags in entries:
            ok, why = condition_met(kind, flags, options_lc, extras)
            if ok:
                chosen.append((rel, why))

        if not chosen:
            continue

        if not args.write:
            print(f"== {component}: {len(chosen)} file(s) ==")
        for rel, why in chosen:
            path, is_generated = resolve_path(args.xnu, component, rel,
                                              args.generated_dir.split(":"))
            total += 1
            selected.append(path)
            if is_generated:
                generated.append(path)
                mark = "gen" if os.path.isfile(path) else "GEN?"
            elif os.path.isfile(path):
                present += 1
                mark = "   "
            else:
                absent.append(path)
                mark = "MISS"
            if not args.write:
                suffix = f"   # {why}" if args.why else ""
                print(f"  {mark} {path}{suffix}")
        if not args.write:
            print()

    if args.write:
        # Sorted and de-duplicated: a file can be listed by both `files` and `files.<arch>`, and a
        # manifest with a duplicate is a manifest that compiles the same object twice.
        with open(args.write, "w") as fh:
            for path in sorted(set(selected)):
                fh.write(path + "\n")
        print(f"wrote {len(set(selected))} path(s) to {args.write}")

    print(f"{args.config}: {total} file(s) selected for {args.arch}")
    print(f"  present on disk:      {present}")
    print(f"  MIG-generated:        {len(generated)}")
    print(f"  listed but absent:    {len(absent)}")
    if args.missing and absent:
        print()
        for path in absent:
            print(f"  MISSING {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
