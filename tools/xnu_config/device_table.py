#!/usr/bin/env python3
"""
The conditions this build turns on that Apple's `MASTER` configuration does not carry.

    ./tools/xnu_config/device_table.py                # the table, to stdout
    ./tools/xnu_config/device_table.py --write PATH
    ./tools/xnu_config/device_table.py --unknown      # the conditions with no entry, and what needs them

Why this exists. A `*/conf/files` line is `<path> optional <cond>`, and `cond` can come from three
places in Apple's build:

1. an **option** — `config/MASTER`, expanded by doconf. `list_sources.py` reads these already.
2. an **`OPTIONS/` line** — `SETUP/config/mkheaders.c` writes `<COND>.h` with a `0` or `1`, and the
   same name is what `optional` matches against. `tools/gen_option_headers.py` reproduces it. These
   agree with (1), so they need no separate handling.
3. a **device** — `device` / `pseudo-device` in a kernel configuration file, which becomes
   `#define NLOOP 1` in a generated header. **4570 publishes no `device` or `pseudo-device` lines at
   all** (`grep -c '^pseudo-device' */conf/files` is 0 in every component), so this half of the
   configuration is simply absent — the same gap `MONOTONIC` was (experiment-112) and the per-SoC
   build definitions are (experiment-113).

Two consequences, and the second is a defect this table exists to fix:

  * `optional loop` and `optional pty` can never match, so `bsd/net/if_loop.c` and
    `bsd/kern/tty_pty.c` are never compiled, and the `NLOOP`/`NPTY` values in the headers have to be
    chosen to agree with that.
  * **`optional monotonic` can never match either — and the build script defines `-DMONOTONIC=1` by
    hand.** So `osfmk/kern/kern_monotonic.c`, the only file that implements what the macro turns on,
    was excluded from the manifest while every user of it was compiled against `MONOTONIC 1`. That
    is the project's recurring defect with a new mechanism: one value, two definitions, and the
    build never compared them.

So the table is the place where "conditions this project chooses" live, and **both** the manifest and
the compile flags read it, which is what stops them disagreeing again.

`1` means the condition is on; `0` means off. A condition not in the table at all is reported by
`--unknown` with the files that want it, so an omission is visible rather than silent.
"""

import argparse
import os
import re
import subprocess
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
XNU = os.environ.get("XNU_TREE", os.path.join(REPO_ROOT, "external", "xnu-4570.1.46"))

COMPONENTS = ["osfmk", "bsd", "libkern", "iokit", "pexpert", "libsa", "security", "san"]

# `path/to/file.c	optional <cond> [<cond> ...]`
LINE = re.compile(r"^(\S+)\s+optional\s+(.*)$")

# The table. Every entry is a decision with its reason, and each is checked against the build:
#
#   * `monotonic` 1 -- the build defines `-DMONOTONIC=1`, so the file must be built. Not a choice;
#                      it is what makes the two halves agree.
#   * `xpr_debug` 0 -- the build defines `-DXPR_DEBUG=0`; the file behind it is `kern/xpr.c`, which
#                      the configuration does not build. Consistent, and stated rather than implied.
#   * the rest     0 -- devices the tarball does not declare. `loop` is safe at 0 because
#                      `gen_device_headers.sh` writes `NLOOP 0` to match; `pty` is NOT safe at 0
#                      (measured: `conf.c` then fails on `ptsselect`, which exists nowhere in the
#                      tree) and is left out of the table entirely so `--unknown` keeps reporting it.
#   * `pty`, `ptmx` 1 -- measured, and the measurement is what decides it. `NPTY 0` does **not**
#                      compile in 4570: `bsd/dev/arm/conf.c:112` has `#if NPTY > 0` and its `#else`
#                      branch defines `ptcselect` but not `ptsselect`, which exists nowhere in the
#                      tree (experiment-130). So the device has to be on, and that means all three
#                      files behind it — `tty_dev.c optional ptmx pty`, `tty_ptmx.c optional ptmx`,
#                      `tty_pty.c optional pty` — and `gen_device_headers.sh` writing `NPTY 1` and
#                      `NPTMX 1` to match. `tty_pty.c:89-92` promotes NPTY 1 to 32 with a #warning,
#                      which is why 1 is the value that both compiles the file and satisfies the
#                      macro's own expectation.
TABLE = {
    "monotonic": 1,
    "xpr_debug": 0,
    "pty": 1,
    "ptmx": 1,
}


def conditions():
    """cond -> set of file lists that name it."""
    found = defaultdict(set)
    for comp in COMPONENTS:
        for name in ("files", "files.arm"):
            path = os.path.join(XNU, comp, "conf", name)
            if not os.path.isfile(path):
                continue
            for line in open(path, encoding="utf-8", errors="replace"):
                line = line.split("#", 1)[0].strip()
                m = LINE.match(line)
                if not m:
                    continue
                for word in m.group(2).split():
                    if word != "not":
                        found[word.lower()].add(f"{comp}/conf/{name}")
    return found


def config_options():
    out = subprocess.run([os.path.join(HERE, "make_defines.sh"),
                          os.environ.get("XNU_KERNEL_CONFIG", "RELEASE")],
                         capture_output=True, text=True, check=True).stdout
    names = set()
    for token in out.split():
        if token.startswith("-D"):
            names.add(token[2:].split("=")[0].lower())
    return names


def hand_set_defines():
    """The conditions the kernel build script decides for itself: name -> value.

    The value matters and a first version of this ignored it. `-DXPR_DEBUG=0` mentions the name but
    turns the feature *off*, and `optional xpr_debug` is about the feature, not about the token
    appearing on the command line. Reading only the name reported a disagreement where there was
    agreement, and would have missed the real one — `-DMONOTONIC=1` — if the table had said 0.

    A bare `-DNAME` counts as 1, and `-DNAME=0` as 0, which is what the preprocessor means.
    """
    path = os.path.join(REPO_ROOT, "tools", "build_xnu_arm_kernel.sh")
    body = open(path).read().split("DEFINES=(", 1)[1].split("\n)", 1)[0]
    out = {}
    # `-DNPTY=1` is a valid C identifier; `NPTY/NPTMX` in a comment is not, which is why the name
    # pattern rejects a `/`. A first version matched `NPTY` inside a comment line and reported the
    # flag as present when it was not.
    for m in re.finditer(r"-D([A-Za-z_][A-Za-z0-9_]*)(?:=([0-9]+))?", body):
        name, value = m.group(1).lower(), m.group(2)
        out[name] = int(value) if value is not None else 1
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", metavar="PATH")
    ap.add_argument("--unknown", action="store_true",
                    help="conditions with no entry, and the files that want them")
    args = ap.parse_args()

    conds = conditions()
    opts = config_options()
    hand = hand_set_defines()

    # The check that would have caught `monotonic`: any condition the build defines by hand must be
    # in the table, and agree with it.
    disagreements = []
    for name in sorted(set(hand) & set(conds) - opts):
        if hand[name] == 0:
            continue  # `-DXPR_DEBUG=0` mentions the name and means the condition is off
        if name not in TABLE:
            disagreements.append(f"{name}: the build script defines it =1, absent from this table")
        elif not TABLE[name]:
            disagreements.append(f"{name}: the build script defines it =1, table says 0")
    # A condition the manifest builds (table 1) whose *file list* condition the compiler will see as
    # off is the same defect in the other direction, so a table entry of 1 that the build does not
    # define is reported. **A device's macro is its name upper-cased with an `N`** - the
    # condition is `pty` and the header is `pty.h` containing `#define NPTY 1` - so the check accepts
    # either spelling. Comparing the two names directly reported a disagreement for a pair that
    # agreed, which is the same one-value-two-definitions shape this table exists to catch, one
    # level up.
    for name in sorted(TABLE):
        if TABLE[name] and name in conds and not (hand.get(name) or hand.get("n" + name)):
            disagreements.append(
                f"{name}: table says 1, the build defines neither -D{name.upper()}=1 "
                f"nor -DN{name.upper()}=1")

    lines = ["# condition\tvalue\treason"]
    for name in sorted(TABLE):
        reason = ("the build script defines it" if name in hand else "chosen here")
        lines.append(f"{name}\t{TABLE[name]}\t{reason}")
    text = "\n".join(lines) + "\n"

    if args.write:
        with open(args.write, "w") as fh:
            fh.write(text)
        print(f"wrote {len(TABLE)} conditions to {args.write}")
    else:
        sys.stdout.write(text)

    if disagreements:
        print(file=sys.stderr)
        for d in disagreements:
            print(f"  DISAGREEMENT {d}", file=sys.stderr)
        return 1

    if args.unknown:
        rest = sorted(k for k in conds if k not in opts and k not in TABLE)
        print(f"\n== {len(rest)} conditions with no entry ==")
        for name in rest:
            mark = " (UNRESOLVED - needs a device table)" if name in UNRESOLVED else ""
            print(f"  {name:32s} {len(conds[name])} file list(s){mark}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
