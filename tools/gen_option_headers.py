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

# The options that every component sees, because a component that does not declare one reads it.
#
# Membership alone is not enough and this is the measurement that says so. Two option macros are read
# by a component other than the one that declares them:
#
#   GPROF      declared by libkern, read by bsd/kern/{bsd_init,kern_clock,subr_prof,subr_xxx}.c and
#              bsd/sys/gmon.h. Every one of those reads is *dead when it is off* - the blocks it
#              guards are the profiler, `kmstartup` is unreferenced once `bsd_init`'s call goes, and
#              `nm` over the 680-object pool finds no reference to `mcount`, `_gmonparam` or
#              `cfreemem`. So this one is NOT shared: bsd simply does not see it, which is also the
#              only reading under which Apple's own `bsd/kern/subr_prof.c` compiles.
#
#   CONFIG_MACF declared by bsd and security, read by osfmk - `osfmk/kern/task.h:241` guards a
#              `struct task` field and a parameter with `#ifdef CONFIG_MACF` while
#              `osfmk/kern/task.c` guards the same parameter with `#if CONFIG_MACF`. Sharing is the
#              *safe* answer and it is worth saying why rather than waving at compatibility: with the
#              macro invisible to osfmk, `struct task`'s layout would differ between osfmk
#              translation units and the bsd/security ones that allocate and read it, which is an ABI
#              mismatch the compiler cannot see. Keeping one value across components is what this
#              build has always done and what the measurement supports; narrowing it would be a
#              change to `struct task` disguised as an option-header change.
#
# So the rule is: a read of an option a component does not declare must be either *shared* here with
# a reason, or *inert* and listed in tools/check_option_headers.py with a reason. Anything else stops
# the build. See experiment-438.
SHARED_DEFAULT = "config_macf"
SHARED = [m for m in os.environ.get("XNU_OPTION_SHARED", SHARED_DEFAULT).split(",") if m]


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


DEVICE_SET = None
# The `OPTIONS/<word>` rows whose word is a device, collected so the run can report them.
skipped_devices = set()


def declared_devices():
    """The device names this configuration declares, from the shared parse.

    Used to keep the two header generators **disjoint**, which is not tidiness either. `do_count`
    (mkheaders.c:85-101) is called for every `conf/files` need word, and when that word names a
    device in `dtab` it writes `#define N<WORD> <d_slave>` into `<word>.h` - the `N` is kept because
    `dev` is non-NULL. So for `OPTIONS/bpfilter optional bpfilter` in a configuration that declares
    `pseudo-device bpfilter 4 init bpf_init`, Apple's build writes **one** `bpfilter.h` containing
    `#define NBPFILTER 4`.

    This generator also saw the line and wrote `<word>.h` containing `#define <WORD> 0`, because
    `BPFILTER` is not among the configuration's *options* - it is a device, and `allCaps(word)` is
    looked up in the option list only. Two files with one name in two directories, both on the
    include path, and `-I` order decided which one a source got; `-I$OPTION_HEADERS` comes first in
    this build, so the device header would have been shadowed and `#if NETHER > 0` would have stayed
    silent zero while the manifest built `bsd/net/ether_if_module.c`. Nothing reads the bare macros
    (measured: the only hits for `ETHER` and `BPFILTER` outside their `N` forms in the whole tree are
    `bsd/kern/bsd_init.c:894`'s `#endif /* ETHER */` comment - which names the wrong macro for its
    own guard - and prose in `ethernet.h`), so the device generator's file is the one to keep, and
    these rows are dropped here with the count printed.
    """
    global DEVICE_SET
    if DEVICE_SET is None:
        sys.path.insert(0, os.path.join(HERE, "xnu_config"))
        import devices as _devices
        DEVICE_SET = {name.lower() for name, _n, _i, _k in _devices.devices(CONFIG)}
    return DEVICE_SET


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
            # The header takes its name from the first option word, and so does the macro -
            # unless that word is a device, in which case the device generator owns the header.
            option = words[0]
            if option.lower() in declared_devices():
                skipped_devices.add(option + ".h")
                continue
            header = option + ".h"
            macro = option.upper()
            # First writer wins, and the value is the same whichever component declared it: what
            # `d_slave` holds is a property of the configuration, not of the conf/files line.
            found.setdefault(header, (macro, option))
    return found


def device_headers_by_component():
    """{component: [header, ...]} — the device headers that component's `conf/files` tests.

    The same walk `scan_options_by_component()` does, over the non-`OPTIONS/` lines: for every
    `<path> optional <cond> ...` whose first word is a **device of this configuration**, config(8)
    writes `<cond>.h` and appends its include here. `tools/gen_device_headers.py` writes the headers
    themselves (configuration-wide, because `d_slave` is), and this decides which component sees
    which — Apple's own membership rule, per component, as in experiment 438.

    A condition that is an *option* is not in here even when it is also a device: `do_count` is
    called once per need word and the `OPTIONS/` line already produced its header, so the device
    list would only add a duplicate include of the same file.
    """
    try:
        sys.path.insert(0, os.path.join(HERE, "xnu_config"))
        import devices as _devices
        declared = {name.lower() for name, _n, _i, _k in _devices.devices(CONFIG)}
    except Exception as exc:                                              # pragma: no cover
        print(f"cannot read the configuration's devices: {exc}", file=sys.stderr)
        return {}
    out = {}
    for component in COMPONENTS:
        path = os.path.join(XNU, component, "conf", "files")
        if not os.path.isfile(path):
            continue
        rows = []
        for line in open(path, encoding="utf-8", errors="replace"):
            line = line.split("#", 1)[0].strip()
            if not line or line.startswith("OPTIONS/"):
                continue
            parts = line.split()
            if len(parts) < 3 or parts[1] not in ("optional", "standard"):
                continue
            words = parts[2:]
            if words and words[0] == "not":
                words = words[1:]
            if not words:
                continue
            cond = words[0].lower()
            if cond in declared and f"{cond}.h" not in rows:
                rows.append(f"{cond}.h")
        if rows:
            out[component] = rows
    return out


def scan_options_by_component():
    """The same lines, kept per component: `{component: [(header, macro), ...]}` in file order.

    This is the half `scan_options()` above cannot answer, and the comment in it says why it thought
    the answer did not matter: the *value* of a macro is a property of the configuration, and the
    *membership* - which files see it at all - is a property of the component. Apple's build makes
    that explicit in two places: `headers()` (mkheaders.c:71-79) walks **the file table it was
    given**, and each component runs the config tool over its own `conf/files` from its own object
    directory (`makedefs/MakeInc.dir`), which is why `MakeInc.def:466` can say `INCFLAGS_LOCAL = -I.`
    and have that find *this component's* `meta_features.h`.

    Getting this wrong is not cosmetic. `libkern/conf/files:5` is `OPTIONS/gprof optional gprof`, so
    a flat `meta_features.h` defines `GPROF 0` for every translation unit - and
    `bsd/kern/bsd_init.c:852` is `#ifdef GPROF`, where `#define GPROF 0` is *defined*. The call it
    guards is `kmstartup()`, whose only definer (`bsd/kern/subr_prof.c`, `standard` in
    `bsd/conf/files:430`) has its whole body inside the same `#ifdef` and does not compile. See
    experiment-438.
    """
    by_component = {}
    for component in COMPONENTS:
        path = os.path.join(XNU, component, "conf", "files")
        if not os.path.isfile(path):
            continue
        rows = []
        for line in open(path, encoding="utf-8", errors="replace"):
            m = OPTION_RE.match(line.strip())
            if not m:
                continue
            words = m.group(3).split()
            if words and words[0] == "not":
                words = words[1:]
            if not words:
                continue
            if words[0].lower() in declared_devices():
                skipped_devices.add(words[0] + ".h")
                continue
            rows.append((words[0] + ".h", words[0].upper()))
        if rows:
            by_component[component] = rows
    return by_component


PER_COMPONENT_NOTE = """\
/*
 * This component's slice of the option headers, which is what Apple's build gives each component:
 * `mkheaders.c` writes the OPTIONS headers into the object directory of the `conf/files` that
 * declared them, and `MakeInc.def:466` puts that directory first (`INCFLAGS_LOCAL = -I.`). A
 * component therefore sees the options *its* file list declares and no others, which is the whole
 * reason `bsd/kern/bsd_init.c`'s `#ifdef GPROF` is false in Apple's kernel even though
 * `libkern/conf/files` declares `gprof`.
 *
 * The flat `meta_features.h` one directory up still exists and is still what this project's own
 * out-of-manifest translation units (the platform expert, the pthread and crypto tables, the EABI
 * runtime, firehose) are compiled with - they belong to no component, and are compiled with the
 * whole option set as before.
 *
 * Generated by tools/gen_option_headers.py; the check that it matches `conf/files` is
 * tools/check_option_headers.py, which the build runs.
 */
"""


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

    # A header left over from an earlier generation is found on the include path and believed, and
    # this generator now writes *fewer* headers than it used to - `ether.h` and `bpfilter.h` moved to
    # the device generator. Nothing else removes them.
    for name in sorted(os.listdir(OUT)):
        if name.endswith(".h") and name != "meta_features.h" and name not in written:
            os.unlink(os.path.join(OUT, name))
            print(f"  removed stale option header {name}")

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

    # And the same accumulate side per component, which is the one Apple's build actually force
    # includes: `MakeInc.def:466`'s `-I.` is the component's own object directory. The membership
    # here is the point - `scan_options_by_component()` has the reason.
    per_component = scan_options_by_component()
    device_headers = device_headers_by_component()
    # The shared options are the ones a component reads without declaring them; see SHARED above.
    # A component only gets a shared header if the header exists at all, so a configuration without
    # the option still gets `#define X 0` (and its `#undef`, if OFF_UNDEF says so) rather than an
    # unresolvable include.
    all_headers = {}
    for rows in per_component.values():
        for header, macro in rows:
            all_headers.setdefault(macro, header)
    # `SHARED` names options the way `conf/files` does (`optional config_macf`); a header is found by
    # the macro the line produces. The two spellings differ only in case here, and taking that for
    # granted is how a shared option silently becomes no shared options.
    shared_macros = {m.upper() for m in SHARED}
    shared_headers = [all_headers[m] for m in sorted(shared_macros) if m in all_headers]
    for component in COMPONENTS:
        rows = per_component.get(component)
        if not rows:
            continue
        directory = os.path.join(OUT, component)
        os.makedirs(directory, exist_ok=True)
        own = {macro for _, macro in rows}
        macros = own | {m for m in shared_macros if m in all_headers}
        with open(os.path.join(directory, "meta_features.h"), "w") as f:
            f.write(PER_COMPONENT_NOTE)
            seen = set()
            for header, _ in rows:
                if header in seen:
                    continue
                seen.add(header)
                f.write(f"#include <{header}>\n")
            for header in shared_headers:
                if header in seen:
                    continue
                seen.add(header)
                f.write(f"#include <{header}>   /* shared: read by a component that does not declare it */\n")
            # **And the device headers, which is the second half of `mkheaders.c`'s `do_header`.**
            # `headers()` walks the file table and calls `do_count(fl->f_needs, ...)` for every entry
            # with a need - not only for the `OPTIONS/` entries - so a line like
            # `bsd/net/if_loop.c optional loop` produces `loop.h` *and* appends `#include <loop.h>` to
            # **bsd's** `meta_features.h`. This generator wrote only the option half, so
            # `bsd/kern/bsd_init.c:890`'s `#if NETHER > 0` had no `NETHER` in scope at all: an
            # undefined identifier in `#if` is **0**, so the guard was off while the manifest built
            # `bsd/net/ether_if_module.c`. Silent, and in the direction that looks like a decision.
            for header in device_headers.get(component, []):
                if header in seen:
                    continue
                seen.add(header)
                f.write(f"#include <{header}>   /* device: this component's conf/files tests it */\n")
            if off_macros:
                off_here = [m for m in off_macros if m in macros]
                undef = off_here if OFF_UNDEF == ["all"] else [m for m in off_here if m in OFF_UNDEF]
                if undef:
                    f.write(OFF_UNDEF_NOTE)
                    for macro in undef:
                        f.write(f"#undef {macro}\n")

    print(f"{CONFIG}: {len(written)} option headers, {on} on, {len(written) - on} off"
          f" ({len(OFF_UNDEF) if OFF_UNDEF != ['all'] else 'all'} of the off ones undefined)")
    print(f"  in {OUT}")
    print(f"  {len(options)} options in the configuration")
    print(f"  shared with every component: {', '.join(shared_headers) if shared_headers else 'none'}")
    if skipped_devices:
        print(f"  {len(skipped_devices)} OPTIONS row(s) belong to the device generator, not this one: "
              + ", ".join(sorted(skipped_devices)))
    print(f"  device headers, per component: "
          + ", ".join(f"{c}: {len(v)}" for c, v in sorted(device_headers.items()))
          if device_headers else "  device headers, per component: none")
    for component in COMPONENTS:
        rows = per_component.get(component)
        if rows:
            unique = len({h for h, _ in rows})
            extra = len([h for h in shared_headers if h not in {h2 for h2, _ in rows}])
            print(f"  {component}: {unique} option header(s) of its own"
                  + (f" + {extra} shared" if extra else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
