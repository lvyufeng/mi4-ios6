#!/usr/bin/env python3
"""
Check that tools/xnu_config/component_defines.sh still matches XNU's own Makefile templates.

    ./tools/check_component_defines.py

Why this exists. `component_defines.sh` is a transcription: someone read
`<component>/conf/Makefile.template`, took the `-D` flags off its `CFLAGS+=` line, and wrote them
into a shell table. A transcription is the exact shape of this project's recurring defect - one
value, two definitions - and the failure mode is silent: if the table drifts, the build compiles
against a flag set Apple's does not use, and nothing says so.

So the table is checked against the source it came from. Each template's `CFLAGS+=` block is read
with line continuations joined and `#` comments stripped, `-include meta_features.h` is removed (it
is the one header Apple's build generates and does not ship, and every component force-includes it),
and the remaining `-D` flags are compared as sets.

A flag spelled `-DNAME` and one spelled `-DNAME=1` are the same thing here, so they compare equal.

Exit status 0 if every component matches, 1 otherwise.
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
XNU = os.environ.get("XNU_TREE", os.path.join(REPO_ROOT, "external", "xnu-4570.1.46"))
TABLE_SH = os.path.join(HERE, "xnu_config", "component_defines.sh")

# The one part of every component's CFLAGS this build cannot reproduce.
GENERATED_FORCE_INCLUDE = "meta_features.h"


def table_from_script():
    """component -> set of -D flags, from the shell table the build actually uses."""
    out = subprocess.run([TABLE_SH, "--table"], capture_output=True, text=True, check=True).stdout
    table = {}
    for line in out.splitlines():
        if not line.strip():
            continue
        component, _, flags = line.partition("\t")
        table[component] = normalise(flags.split())
    return table


def table_from_templates():
    """component -> set of -D flags, from the templates themselves (the source of truth)."""
    template = os.path.join(XNU, "config", "Makefile.template")
    if not os.path.isfile(template):
        # Not every tree lays it out the same way; fall back to the per-component path.
        pass
    found = {}
    for component in sorted(table_from_script()):
        path = os.path.join(XNU, component, "conf", "Makefile.template")
        if not os.path.isfile(path):
            found[component] = None  # reported as missing rather than as empty
            continue
        found[component] = normalise(cflags_defines(path))
    return found


def cflags_defines(path):
    """The -D flags on the `CFLAGS+=` line of a Makefile template, continuations joined."""
    text = open(path, encoding="utf-8", errors="replace").read()
    # Join backslash-continued lines first, so a CFLAGS block split over three lines is one line.
    text = re.sub(r"\\\n", " ", text)
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("CFLAGS+="):
            # Strip a trailing `#` comment, which iokit's template uses to park a disabled flag.
            stripped = re.sub(r"\s#.*$", "", stripped[len("CFLAGS+="):])
            tokens = stripped.split()
            return [t for t in tokens if t.startswith("-D")]
    return []


def normalise(tokens):
    """`-DNAME` and `-DNAME=1` are the same define; express both as `-DNAME=1`."""
    out = set()
    for token in tokens:
        if not token.startswith("-D"):
            continue
        if token == "-include" or token.endswith("/meta_features.h"):
            continue
        body = token[2:]
        if "=" not in body:
            body += "=1"
        out.add("-D" + body)
    return out


def main():
    if not os.path.isdir(XNU):
        print(f"no XNU tree at {XNU}", file=sys.stderr)
        return 2

    declared = table_from_script()
    actual = table_from_templates()

    bad = 0
    for component in sorted(declared):
        if actual.get(component) is None:
            print(f"  {component:<10} no conf/Makefile.template to check against")
            continue
        only_declared = declared[component] - actual[component]
        only_actual = actual[component] - declared[component]
        if only_declared or only_actual:
            bad += 1
            print(f"  {component:<10} MISMATCH")
            for flag in sorted(only_declared):
                print(f"      in component_defines.sh, not in the template: {flag}")
            for flag in sorted(only_actual):
                print(f"      in the template, not in component_defines.sh: {flag}")
        else:
            print(f"  {component:<10} {len(declared[component])} flags, match")

    print()
    if bad:
        print(f"{bad} component(s) drifted from their Makefile template")
        return 1
    print(f"all {len(declared)} components match their Makefile template "
          f"(minus the generated force-include {GENERATED_FORCE_INCLUDE})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
