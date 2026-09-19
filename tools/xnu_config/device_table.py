#!/usr/bin/env python3
"""
The conditions the manifest decides, derived from the configuration that declares them.

    ./tools/xnu_config/device_table.py                # the table, to stdout
    ./tools/xnu_config/device_table.py --write PATH
    ./tools/xnu_config/device_table.py --unknown      # conditions with no entry, and what they gate

Why this exists. A `*/conf/files` line is `<path> optional <cond>`, and in Apple's build `cond` is
answered by two separate mechanisms:

1. an **option** — an `options <NAME>` line in the configuration, which `config/MASTER` expanded by
   doconf carries. `list_sources.py` reads these already, via `make_defines.sh`.
2. a **device** — `device` / `pseudo-device` in the configuration. `parser.y:207-229` puts it in
   `dtab`, and `condition_met`'s `optional <cond>` matches against dtab **as well as** the option
   list. `bsd/net/if_loop.c` is `optional loop` and `loop` is a device; there is no `options LOOP`.

**This file used to say the second half was absent, and that was wrong.** It recorded

    grep -c '^pseudo-device' */conf/files      # 0 in every component

and concluded "4570 publishes no `device` or `pseudo-device` lines at all ... so this half of the
configuration is simply absent". The grep is right; the conclusion is not. `config/MASTER` publishes
**24** of them, `expand.sh` already expands them — experiment 439 derived `pseudo_inits[]` from
exactly those lines — and looking for the declaration in `*/conf/files` (where a condition is
*tested*) instead of in `config/MASTER` (where it is *declared*) is
[[mi4-not-absent-its-build-output]] once more: not absent, in the other file.

So the table is **derived** now: every device the configuration declares is in it at 1, with the
value it needs for the manifest. What is left for a human is `OVERRIDES` — the places where this
configuration deliberately does *not* follow the declaration — and each of those carries its reason
and is checked, so an override that is no longer doing anything is reported rather than rotting.

**This table is the manifest's half of one decision that has three halves**, and the other two are
`tools/gen_device_headers.py` (`#define N<COND> <count>`) and `tools/gen_pseudo_inits.py` (the
`{count, func}` array). All three read `tools/xnu_config/devices.py`. `tools/check_device_conditions.py`
compares the three and stops the build on a disagreement, because for several stages they disagreed
silently: the manifest said `bpfilter` was off while the array this image walks contained
`{4, bpf_init}`, and the hand-written `bpfilter.h` said `NBPFILTER 0`.
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

sys.path.insert(0, HERE)
import devices as devices_mod                                                     # noqa: E402

COMPONENTS = ["osfmk", "bsd", "libkern", "iokit", "pexpert", "libsa", "security", "san"]

# `path/to/file.c	optional <cond> [<cond> ...]`
LINE = re.compile(r"^(\S+)\s+optional\s+(.*)$")

# The places this configuration does not follow its own device declarations. An entry is a decision
# with a reason, and the check in `main()` reports one that has stopped being needed, so this cannot
# become a list of things that used to be true.
#
#   * `monotonic` 1 -- not a device at all. It is the per-SoC value experiment-112 identified as the
#                      one genuinely absent from MASTER, and `build_xnu_arm_kernel.sh` defines
#                      `-DMONOTONIC=1` by hand. It is here rather than derived because
#                      `osfmk/kern/kern_monotonic.c` is `optional monotonic` and the build script is
#                      what turns it on; the entry is what makes the two halves agree.
#   * `xpr_debug` 0 -- also not a device. `-DXPR_DEBUG=0` means the condition is *off*, and the file
#                      behind it (`osfmk/kern/xpr.c`) is not in the configuration. Consistent, and
#                      stated rather than implied.
#
# There are no device overrides left, and that is the point of the step: `pty`, `ptmx`, `ether`,
# `loop` and `bpfilter` were hand-written entries with hand-written reasons, and every one of them is
# now what `config/MASTER` says it is. A device override belongs here only with a cause that is
# measured, because the honest default is the declaration.
OVERRIDES = {
    "monotonic": (1, "the build script defines -DMONOTONIC=1; not a device"),
    "xpr_debug": (0, "the build script defines -DXPR_DEBUG=0; not a device"),
}


def derived(config):
    """condition -> 1, for every device the configuration declares — `dtab` membership."""
    return {name.lower(): 1 for name, _number, _init, _kind in devices_mod.devices(config)}


def table(config):
    """The whole table: the derived devices plus the overrides."""
    out = dict(derived(config))
    for name, (value, _reason) in OVERRIDES.items():
        out[name] = value
    return out


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
    ap.add_argument("--config", default=os.environ.get("XNU_KERNEL_CONFIG", "RELEASE"))
    ap.add_argument("--unknown", action="store_true",
                    help="conditions with no entry, and the files that want them")
    args = ap.parse_args()

    conds = conditions()
    opts = config_options()
    hand = hand_set_defines()
    decl = devices_mod.devices(args.config)
    decl_names = {name.lower() for name, _n, _i, _k in decl}
    TABLE = table(args.config)

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

    # An override that names a device of the configuration is the derived value and an opinion about
    # it, and the opinion would win silently. The configuration is the specification, so an override
    # of a declared device is a disagreement unless it says 1, and a `1` override is redundant.
    for name, (value, reason) in sorted(OVERRIDES.items()):
        if name in decl_names:
            disagreements.append(
                f"{name}: this file overrides a device the {args.config} configuration declares "
                f"(value {value}). The declaration is the answer; delete the override "
                f"({reason}) or record why the declaration is not.")

    # The same defect in the other direction: an override for a name no configuration declares and
    # no file list tests. `xpr_debug`'s *file* list line is what keeps it alive, so a name with
    # neither is dead weight that reads as a decision.
    for name in sorted(OVERRIDES):
        if name not in decl_names and name not in conds:
            disagreements.append(f"{name}: an override for a name that is not a device of "
                                 f"{args.config} and is not tested by any conf/files line - it "
                                 f"decides nothing")

    # A condition the manifest builds (table 1) whose name the build does define is fine; one it
    # does not define is only fine when it is a device, because a device's value lives in the
    # generated `<cond>.h` and not in the command line. **A device's macro is its name upper-cased
    # with an `N`** — the condition is `pty` and the header is `pty.h` containing `#define NPTY 16`
    # — so the check accepts either spelling. Comparing the two names directly reported a
    # disagreement for a pair that agreed, which is the same one-value-two-definitions shape this
    # file exists to catch, one level up.
    for name in sorted(TABLE):
        if not TABLE[name] or name not in conds:
            continue
        if name in decl_names or hand.get(name) or hand.get("n" + name):
            continue
        disagreements.append(
            f"{name}: table says 1, but it is not a device of {args.config} and the build defines "
            f"neither -D{name.upper()}=1 nor -DN{name.upper()}=1")

    lines = ["# condition\tvalue\treason"]
    for name in sorted(TABLE):
        if name in decl_names and name not in OVERRIDES:
            reason = f"device of {args.config} (config/MASTER)"
        elif name in OVERRIDES:
            reason = OVERRIDES[name][1]
        else:
            reason = "the build script defines it" if name in hand else "chosen here"
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
        # Two different reports, and the first is the one that used to crash: this mode raised
        # `NameError: UNRESOLVED is not defined` from the day experiment-132 removed that set, so
        # the mode whose whole purpose is to make an omission **visible** was the one thing nobody
        # could run. Found while 440 scoped the device gap.
        rest = sorted(k for k in conds if k not in opts and k not in TABLE)
        print(f"\n== {len(rest)} condition(s) tested by a conf/files line with no entry ==")
        for name in rest:
            print(f"  {name:32s} {len(conds[name])} file list(s)")
        untested = sorted(decl_names - {k for k in conds})
        print(f"\n== {len(untested)} device(s) the configuration declares that no conf/files line "
              f"tests ==")
        for name in untested:
            print(f"  {name:32s} its sources are `standard`, so it is built unconditionally - "
                  f"and N{name.upper()} is read by nothing")
    return 0


if __name__ == "__main__":
    sys.exit(main())
