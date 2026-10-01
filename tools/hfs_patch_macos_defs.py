#!/usr/bin/env python3
"""Apply the one in-place substitution the HFS+ port needs to `hfs_macos_defs.h`.

    tools/hfs_patch_macos_defs.py PATH/TO/hfs_macos_defs.h

4570 reaches `<stdbool.h>`, which `#define`s `false` and `true`; 2050's `hfs_macos_defs.h` declares
them as enum members under `#if !TYPE_BOOL` (`TYPE_BOOL` is 0 for C).  The enum cannot be declared
under those macros, and it is the only thing supplying the values, so it becomes the definitions it
used to make.  See experiment 869.

This is ONE definition of that edit, called by BOTH the host-only probe (`tools/hfs_port_probe.sh`,
which compiles in a /tmp sandbox) and the staging script (`tools/stage_hfs.sh`, which edits a copy of
the untracked 4570 tree).  A second copy of the substitution would be this project's "one value, two
definitions" defect - the probe would then be measuring a file the build does not produce.

Idempotent: a file that already carries the marker is left alone.  Refuses (exit 2) if neither the
marker nor the original block is found, because that means the tree moved under it.
"""
import sys

MARK = "hfs_macos_defs.h port patch (tools/hfs_patch_macos_defs.py)"
MARKER = "/* PORT SHIM (%s)" % MARK

OLD = """#if !TYPE_BOOL

enum {
\tfalse\t\t\t\t\t\t= 0,
\ttrue\t\t\t\t\t\t= 1
};

#endif  /*  !TYPE_BOOL */"""

NEW = """/* PORT SHIM (%s): 4570 reaches <stdbool.h>, which #defines false and true, so this enum cannot be
 * declared under those macros.  The enum is the only thing supplying the values, so it becomes the
 * definitions it used to make.  See experiment 869. */
#if !TYPE_BOOL
#ifndef false
#define false\t0
#endif
#ifndef true
#define true\t1
#endif
#endif  /*  !TYPE_BOOL */""" % MARK


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    path = argv[1]
    s = open(path).read()
    if MARKER in s:
        print("hfs_patch_macos_defs: already patched")
        return 0
    if OLD not in s:
        print("hfs_patch_macos_defs: the false/true enum is not in %s - the tree moved" % path,
              file=sys.stderr)
        return 2
    open(path, "w").write(s.replace(OLD, NEW))
    print("hfs_patch_macos_defs: false/true enum -> macro definitions")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))