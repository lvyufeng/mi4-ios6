#!/usr/bin/env python3
"""
Generate the per-option headers Apple's build generates from the `OPTIONS/` lines.

    ./tools/gen_option_headers.py                     # for the default configuration
    XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=... ./tools/gen_option_headers.py

What this is. `*/conf/files` contains 124 lines of the shape

    OPTIONS/mach_ipc_debug          optional mach_ipc_debug

and `SETUP/config/mkheaders.c` turns each of them into a one-line header. The semantics below are
read off that source rather than guessed:

  * `headers()` (mkheaders.c:71-79) walks the file table and, for every entry with a first need,
    calls `do_count(need, need, 1)`.
  * `nextopt:` (mkmakefile.c:366-372) sets `needs = ns(wd)` from the first word after `optional` —
    so **the generated file is named after the first option word, not after the `OPTIONS/` name.**
    The two differ for `OPTIONS/bridgestp optional bridgestp if_bridge`: the file is `bridgestp.h`.
  * the pseudo-device for that word is created with `d_slave = 0`, set to 1 if `allCaps(word)` is
    among the configuration's options (mkmakefile.c:379-400).
  * `do_count` (mkheaders.c:85-101) takes `count = d_slave` and, because `d_flags` is set, passes
    `dev = NULL`; `do_header` (mkheaders.c:114-135) then writes

        fprintf(outf, "#define %s %d\n", name, count);

    into `path(hname) + ".h"`, where `name = tomacro(hname)` with the leading `N` dropped — i.e. the
    header name upper-cased. `path()` (main.c:206-216) is the object directory, which is a **flat**
    directory, and the include sites confirm it: `osfmk/ipc/ipc_hash.h:128` says
    `#include <mach_ipc_debug.h>`, not `<mach/mach_ipc_debug.h>`.
  * the same function appends `#include <name.h>` to `meta_features.h`, the header every component
    force-includes (`osfmk/conf/Makefile.template:19` and its seven siblings).

So the whole generator is: one `#define <MACRO> <0|1>` per option, plus the list.

Why this matters here. Six of the headers in the last measurement's "missing header" list are
OPTIONS output — `mach_ipc_debug.h` (9 files), `mach_vm_debug.h`, `mach_cluster_stats.h`,
`mach_ipc_test.h`, `kdebug.h`, `vm_cpm.h` — and this project had written hand shims for nine more
(`mach_assert.h`, `mach_ldebug.h`, `mach_counters.h`, `mach_pagemap.h`, `zone_debug.h`,
`config_dtrace.h`, `xpr_debug.h`, `task_swapper.h`, `mach_kdp.h`). All nine hard-code a value the
configuration already states.

The unguarded include is the tell and it is worth recording: `osfmk/ipc/ipc_hash.h:128` includes
`<mach_ipc_debug.h>` with no `#if` around it, and the real content is on the next line under
`#if MACH_IPC_DEBUG`. A header included unconditionally, that exists nowhere in the tree and
contains nothing but a macro, is a generated header.
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
XNU = os.environ.get("XNU_TREE", os.path.join(REPO_ROOT, "external", "xnu-4570.1.46"))
# Per-configuration, and that is not tidiness. RELEASE and STAGE90_BOOT disagree on **20** of these
# macros - CRYPTO, NETWORKING, SOCKETS, DEVFS, FIFO, MACF, DUMMYNET and more - and a single shared
# directory means whichever run was last wins for both builds. It did: the RELEASE build was
# compiled with STAGE90_BOOT's values for all 20, because that generation happened to be the most
# recent. Nothing said so, and the failure it produced was 400 lines into the link ("multiple
# definition of `ctl_register`") rather than anywhere near the cause - `bsd/net/net_stubs.c:31` is
# `#if !NETWORKING` and NETWORKING had been silently forced to 0 by a generated header that was not
# this configuration's.
OUT_ROOT = os.environ.get("XNU_OPTION_HEADERS_OUT", os.path.join(REPO_ROOT, "out", "xnu_options"))
CONFIG = os.environ.get("XNU_KERNEL_CONFIG", "RELEASE")

# How the macros of options this configuration does NOT select are spelled. `mkheaders.c` writes
# `#define X 0`, and that is what every header above keeps. The macros named here get an `#undef`
# appended to meta_features.h on top of it, which changes nothing for `#if X` (an undefined `X`
# reads as 0) and everything for `#ifdef X`, `#ifndef X` and `defined(X)` (defined-as-0 is
# *defined*). A tree that only ever tested these with `#if X` would not care which spelling is used.
#
# One macro, and the reason is measured rather than stylistic. `osfmk/kern/task.c:1600` guards its
# definition of `task_collect_crash_info`'s `crash_label` argument with `#ifdef CONFIG_MACF`, while
# `osfmk/kern/task.h:635` guards the prototype of the same function's same argument with
# `#if CONFIG_MACF` and `task.h:241` guards its `struct task` field with `#ifdef CONFIG_MACF`. With
# the option on, every spelling agrees and Apple's build never sees a difference - upstream `main`
# still has the `#ifdef` a decade later. With the option off there is no value that satisfies both:
# the prototype and the definition disagree by one argument, and `osfmk/kern/task.c` does not
# compile. Undefined is the one spelling under which the whole file is self-consistent - the field,
# the prototype and the definition all drop `crash_label` together, in every translation unit.
#
# The obvious generalisation is wrong and was measured: `XNU_OPTION_OFF_UNDEF=all` also undefines
# `NFSCLIENT`, and that breaks `bsd/vfs/vfs_syscalls.c` **in the other direction** -
# `bsd/sys/mount_internal.h:243` guards the *definition* of `MNTK_TYPENAME_OVERRIDE` with
# `#ifdef NFSCLIENT` while nine uses of it in `vfs_syscalls.c` are not guarded at all. Apple's tree
# compiles with NFS off only because `#define NFSCLIENT 0` makes that `#ifdef` true. Two macros,
# two opposite dependencies on the same spelling; there is no rule, only measurements. See
# experiment-164.
OFF_UNDEF_DEFAULT = "CONFIG_MACF"
OFF_UNDEF = [m for m in os.environ.get("XNU_OPTION_OFF_UNDEF", OFF_UNDEF_DEFAULT).split(",") if m]


OFF_UNDEF_NOTE = """\
/*
 * The options this configuration does not select that have to be undefined rather than 0, because
 * this tree tests the same declaration with both spellings and `#define X 0` can only satisfy one.
 * The `#define X 0` each header above wrote stays - those files are byte-for-byte Apple's.
 * Generated by tools/gen_option_headers.py; the reason, and the counter-example that rules out
 * doing this to every off option, are in experiment-164.
 */
"""

# The components whose conf/files carry OPTIONS lines, in Apple's own order (MakeInc.def:46).
COMPONENTS = ["osfmk", "bsd", "libkern", "iokit", "pexpert", "libsa", "security", "san"]

# `OPTIONS/<name>   optional <word> [<word> ...]`. A `not` may precede the first word and inverts
# the *file* condition, not the macro value (mkmakefile.c:366-372 consumes it before setting
# `needs`), so it is skipped for naming but recorded.
OPTION_RE = re.compile(r"^OPTIONS/(\S+)\s+(optional|standard)\s+(.*)$")


def configured_options():
    """The configuration's option names, which is what `opt` holds when mkmakefile.c runs."""
    defines = subprocess.run([os.path.join(HERE, "xnu_config", "make_defines.sh"), CONFIG],
                             capture_output=True, text=True, check=True).stdout
    names = set()
    for line in defines.split():
        if line.startswith("-D"):
            names.add(line[2:].split("=")[0])
    return names


def scan_options():
    """(header, macro, option) for every OPTIONS line, in a stable order."""
    found = {}
    for component in COMPONENTS:
        path = os.path.join(XNU, component, "conf", "files")
        if not os.path.isfile(path):
            continue
        for line in open(path, encoding="utf-8", errors="replace"):
            m = OPTION_RE.match(line.strip())
            if not m:
                continue
            words = m.group(3).split()
            if words and words[0] == "not":
                words = words[1:]
            if not words:
                continue
            # The header takes its name from the first option word, and so does the macro.
            option = words[0]
            header = option + ".h"
            macro = option.upper()
            # First writer wins, and the value is the same whichever component declared it: what
            # `d_slave` holds is a property of the configuration, not of the conf/files line.
            found.setdefault(header, (macro, option))
    return found


def main():
    if not os.path.isdir(XNU):
        print(f"no XNU tree at {XNU}", file=sys.stderr)
        return 2

    options = configured_options()
    found = scan_options()

    OUT = os.path.join(OUT_ROOT, CONFIG)
    os.makedirs(OUT, exist_ok=True)

    on = 0
    written = []
    off_macros = []
    for header in sorted(found):
        macro, option = found[header]
        value = 1 if macro in options else 0
        on += value
        if not value:
            off_macros.append(macro)
        path = os.path.join(OUT, header)
        # Exactly what mkheaders.c writes, including the absence of a guard: Apple's file is one
        # line and is regenerated per build, so re-including it is not a hazard it guards against.
        with open(path, "w") as f:
            f.write(f"#define {macro} {value}\n")
        written.append(header)

    # meta_features.h is the accumulate side: every component force-includes it, and it is what
    # makes the option macros visible without an explicit include. Written from the same list so the
    # two cannot disagree.
    with open(os.path.join(OUT, "meta_features.h"), "w") as f:
        f.write("/* generated by tools/gen_option_headers.py - see that file for the semantics */\n")
        for header in written:
            f.write(f"#include <{header}>\n")
        if off_macros:
            undef = [m for m in off_macros if m in OFF_UNDEF] if OFF_UNDEF != ["all"] else off_macros
            if undef:
                f.write(OFF_UNDEF_NOTE)
                for macro in undef:
                    f.write(f"#undef {macro}\n")

    print(f"{CONFIG}: {len(written)} option headers, {on} on, {len(written) - on} off"
          f" ({len(OFF_UNDEF) if OFF_UNDEF != ['all'] else 'all'} of the off ones undefined)")
    print(f"  in {OUT}")
    print(f"  {len(options)} options in the configuration")
    return 0


if __name__ == "__main__":
    sys.exit(main())
