#!/usr/bin/env python3
"""
The MIG outputs Apple's own Makefiles declare, as a spec for tools/gen_mach_headers.sh.

    ./tools/xnu_config/mig_outputs.py                # the spec, to stdout
    ./tools/xnu_config/mig_outputs.py --write PATH   # ...to a file

Why this exists. `gen_mach_headers.sh` used to run MIG over *every* `.defs` in `osfmk` and ask for
all four outputs each time. That over-generates, and the over-generation is not harmless: Apple's
`osfmk/mach/Makefile` lists `notify_server.h` (from `notify.defs`) but **not** `notify.h`, because
`mach/notify.h` is hand-written in the tree and carries `MACH_NOTIFY_NO_SENDERS` and the
notification structs. Generating a `notify.h` from the `.defs` produces a user-side stub with none
of that, and putting the generated root where Apple puts its own (`-I.`, ahead of the component
source tree) then hides the real header from `osfmk/ipc/ipc_voucher.c` and
`osfmk/kern/ipc_kobject.c`.

The same is true in the other direction: `memory_object.h` IS listed, in `MIG_KUHDRS`, so Apple's
kernel sees the generated one and never hits the collision between the hand-written
`osfmk/mach/memory_object.h` (user-side `typedef mach_port_t memory_object_t`) and
`osfmk/mach/memory_object_types.h` (kernel-side `struct memory_object *`). That collision is what
fails six `osfmk/vm` files here.

So the rule is not "generate everything" and not "generate nothing" - it is "generate what the
Makefiles say", which is this file's whole job.

How the lists are read. Each is a Make variable of file names, possibly continued over several
lines. A name ending `_server.h` / `Server.h` means MIG was asked for `-sheader`; `_user.c` means
`-user`; `_server.c` / `Server.c` means `-server`; a plain `X.h` means `-header`. The `.defs` a name
comes from is the same name with that suffix removed, in the same directory - which is exactly the
`%_server.h : %.defs` pattern the rules use.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
XNU = os.environ.get("XNU_TREE", os.path.join(REPO_ROOT, "external", "xnu-4570.1.46"))

# Any Make variable assignment; the filter on which ones matter is applied afterwards, because the
# name is not enough - `MIG_KUHDRS` and `DEVICE_FILES` are the same kind of thing under two
# conventions. (A first version required a `MIG_` prefix here, and silently skipped
# `osfmk/device/Makefile` entirely: `device_server.h` is included by three files and was reported as
# missing by the build rather than by this tool.)
ASSIGN = re.compile(r"^\s*([A-Za-z][A-Za-z0-9_]*)\s*[:+]?=\s*(.*)$")

# Output suffix -> the MIG flag that produces it. Order matters: `_server.h` must be tested before
# `.h`, or `foo_server.h` would be read as a plain header whose base is `foo_server`.
OUTPUT_KINDS = [
    (re.compile(r"^(.*)_server\.h$"), "sheader"),
    (re.compile(r"^(.*)Server\.h$"), "sheader"),
    (re.compile(r"^(.*)_user\.c$"), "user"),
    (re.compile(r"^(.*)_server\.c$"), "server"),
    (re.compile(r"^(.*)Server\.c$"), "server"),
    (re.compile(r"^(.*)\.h$"), "header"),
]


def makefiles():
    """Every Makefile that actually runs MIG.

    The filter is `$(MIG)` appearing in the file, not the directory name: `osfmk/device/Makefile`
    hard-codes its outputs in the rule (`-sheader device_server.h`) and lists them in a variable
    called `DEVICE_FILES`, which no name-based rule would find, while `osfmk/mach/Makefile` uses
    `MIG_*HDRS`. Both run MIG; only the second is guessable from the variable name.
    """
    for root, _dirs, files in os.walk(XNU):
        if os.sep + "conf" + os.sep in root + os.sep:
            continue
        if "libsyscall" in root:
            continue
        for name in files:
            if name != "Makefile":
                continue
            path = os.path.join(root, name)
            try:
                text = open(path, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            if "$(MIG)" in text or "${MIG}" in text:
                yield path


def spec():
    """base -> (relative dir, {kind: output name}), from every Makefile in the tree."""
    found = {}
    for path in sorted(makefiles()):
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        text = re.sub(r"\\\n", " ", text)
        rel_dir = os.path.relpath(os.path.dirname(path), XNU)
        for line in text.splitlines():
            m = ASSIGN.match(line)
            if not m:
                continue
            var, value = m.group(1), m.group(2)
            # `_FILES` as well as `HDRS`/`SRC`: osfmk/device calls its outputs DEVICE_FILES.
            if not (var.endswith("HDRS") or var.endswith("SRC") or var.endswith("_FILES")):
                continue
            if var == "COMP_FILES":
                continue   # an alias for another variable, not a list of its own
            for token in value.split():
                if not token.endswith((".h", ".c")):
                    continue
                for pattern, kind in OUTPUT_KINDS:
                    hit = pattern.match(token)
                    if hit:
                        base = hit.group(1)
                        entry = found.setdefault(base, (rel_dir, {}))
                        entry[1][kind] = token
                        break
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", metavar="PATH")
    args = ap.parse_args()

    found = spec()
    # A `user` run is always invoked as `-user $*_user.c -header $*.h` (osfmk/mach/Makefile:361-365),
    # so a base with a `user` output also has a header - even when no HDRS list names it.
    # `sysdiagnose_notification` is exactly that case: `sysdiagnose_notification.h` is included by
    # osfmk/kern/sysdiagnose.c:33 and appears in no list at all.
    for _base, (_d, kinds) in found.items():
        if "user" in kinds:
            kinds.setdefault("header", None)
    lines = ["# base\tdir\toutputs   -- from the MIG_* lists in XNU's Makefiles"]
    on_disk = 0
    for base in sorted(found):
        rel_dir, kinds = found[base]
        defs = os.path.join(XNU, rel_dir, base + ".defs")
        if not os.path.isfile(defs):
            continue
        on_disk += 1
        kinds = {k for k in kinds if kinds[k] is not None} | ({"header"} if "user" in kinds else set())
        lines.append(f"{base}\t{rel_dir}\t{','.join(sorted(kinds))}")

    text = "\n".join(lines) + "\n"
    if args.write:
        with open(args.write, "w") as fh:
            fh.write(text)
        print(f"wrote {on_disk} MIG outputs to {args.write}")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
