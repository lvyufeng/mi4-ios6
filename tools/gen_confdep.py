#!/usr/bin/env python3
"""
Generate `confdep.h`, the one-line header `param.c` includes and Apple's `config` writes.

    ./tools/gen_confdep.py                       # for the default tree/configuration
    XNU_TREE=.../xnu-hd2-darwin13/xnu XNU_GENERATED=out/xnu_generated_d13 \
        ./tools/gen_confdep.py                   # the Darwin-13 (iOS 7) tree

What this is. `bsd/conf/param.c` does not hardcode its process limit the way 4570's copy does; it
reads it from `MAXUSERS`:

    bsd/conf/param.c:69    #include <confdep.h>
    bsd/conf/param.c:87    #define NPROC (20 + 16 * MAXUSERS)

and `confdep.h` is **generated, not shipped** — it appears in no tarball of either tree. Its
producer is the `config` binary itself:

    SETUP/config/mkmakefile.c:274-276   if (maxusers) { do_build("confdep.h", build_confdep); }
    SETUP/config/mkmakefile.c:813-816   build_confdep() { fprintf(fp, "#define MAXUSERS %d\n", maxusers); }

and `maxusers` is the value of the configuration's selected `maxusers` line (`parser.y:267`). Its
generator is in the tree, so this reproduces it rather than choosing a number: the `maxusers` lines
of `bsd/conf/MASTER` are **tagged by the scale attribute** (`64 <xlarge>`, `50 <large>`, `32
<medium>`, `16 <small>`, `8 <xsmall>`, `2 <bsmall>`), and the selection is `expand.sh`'s — the same
expansion every other define in this build reads. For the ARM `RELEASE` configuration the tag is
`bsmall` (`osfmk/conf/MASTER.arm:12`), so exactly one line survives: `maxusers 2`.

**Why it lives in the `GENERATED` root, and inert on 4570.** The value has to belong to the selected
tree, like `libkern/version.h` (`tools/gen_libkern_version.sh`) and `bsd/sys/sysproto.h`
(`tools/gen_bsd_headers.sh`) — so it is written under `$XNU_GENERATED`, which is the per-tree output
root the build force-includes from (`-I"$GENERATED"`). The modern tree needs none of this: 4570's
`param.c` hardcodes `#define NPROC (20 + 16 * 32)` and **nothing in that tree includes `<confdep.h>`**,
so on 4570 the expansion yields no `maxusers` line and this writes nothing at all. That is the same
inertness `gen_libkern_version.sh` gets from the `HTC HD2` marker test — the file is a Darwin-13-only
input, and the generator is a no-op where it is not needed.

**The one thing it refuses.** Two surviving `maxusers` lines would be two values for `MAXUSERS`, and
`NPROC` is a single number — so the ambiguity is an error rather than a last-one-wins. A tagged file
that selected two would be a defect in the tree or the expansion, and silently picking one would be
[[mi4-one-value-two-definitions]] at the top of the process table.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

sys.path.insert(0, os.path.join(HERE, "xnu_config"))
import devices as devices_mod                                                       # noqa: E402

CONFIG = os.environ.get("XNU_KERNEL_CONFIG", "RELEASE")
# The per-tree generated root the build settles at `GENERATED=${XNU_GENERATED:-...}` and
# force-includes. Read from the same variable so a build pointed at another tree's root
# (`XNU_GENERATED`, 913/917) writes there and not into the default tree's.
OUT = os.environ.get("XNU_GENERATED", os.path.join(REPO_ROOT, "out", "xnu_generated"))


def maxusers(lines):
    """The single `maxusers N` value the configuration expands to, or None if it has none."""
    found = []
    for line in lines:
        parts = line.strip().split()
        if len(parts) == 2 and parts[0] == "maxusers" and parts[1].isdigit():
            found.append(int(parts[1]))
    values = sorted(set(found))
    if not values:
        return None
    if len(values) > 1:
        sys.exit(f"the {CONFIG} configuration selects {len(values)} distinct maxusers values "
                 f"({', '.join(str(v) for v in values)}) - the scale attribute should leave exactly "
                 f"one. `MAXUSERS` is one number (bsd/conf/param.c:87), so this is an error rather "
                 f"than a choice of the last line read")
    return values[0]


def main():
    value = maxusers(devices_mod.configuration_lines(CONFIG))
    if value is None:
        # Not a tree that generates it (4570: param.c hardcodes NPROC, nothing includes <confdep.h>).
        # Writing nothing is the correct no-op, not a made-up value.
        print(f"{CONFIG}: no maxusers line in the configuration - not writing confdep.h "
              f"(this tree does not generate it)")
        return 0
    path = os.path.join(OUT, "confdep.h")
    os.makedirs(OUT, exist_ok=True)
    with open(path, "w") as fp:
        fp.write(f"#define MAXUSERS {value}\n")
    print(f"confdep.h: #define MAXUSERS {value} -> {os.path.relpath(path, REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())