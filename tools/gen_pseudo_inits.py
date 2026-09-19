#!/usr/bin/env python3
"""
Generate `pseudo_inits[]` — the array `bsd/kern/bsd_init.c` walks in `bsd_autoconf()`.

    ./tools/gen_pseudo_inits.py --write          # write the generated source
    ./tools/gen_pseudo_inits.py --check          # fail if the file on disk is stale
    ./tools/gen_pseudo_inits.py                  # the source, to stdout

Why this exists. Experiment 438's run ended like this:

    real XNU entry: exception: prefetch abort
    xnu_entry_prefetch_abort_ifar = 0xE52DE004        <- the ARM encoding of `push {lr}`

`bsd_init.c:861` is `bsd_autoconf();` - *inside* `bsd_init` - and `bsd_autoconf` walks

    for (pi = pseudo_inits; pi->ps_func; pi++)
        (*pi->ps_func) (pi->ps_count);

over `struct pseudo_init { int ps_count; int (*ps_func)(int count); }`, which
`bsd/dev/busvar.h:46` declares **`extern struct pseudo_init pseudo_inits[]`**. The image supplied
`pseudo_inits` as a *function* stub, so the second word of the array's first entry - the stand-in's
own prologue - read as `ps_func`, was non-NULL, and `blx r1` at `bsd_autoconf+0x28` jumped to it.
The undefined list gives names and never *kinds*, which is why nothing caught it.

So this file supplies the array, and it **derives** its contents rather than typing them, because
`pseudo_inits` is generated in Apple's build too: `SETUP/config/mkioconf.c:79-100` writes

    extern int %s(int);                      one per pseudo-device with a d_init
    struct pseudo_init pseudo_inits[] = {
        {%d, %s},                            count = d_slave, and 1 when d_slave <= 0
        ...
        {0, 0},
    };

and the entries come from `dtab`, i.e. from the **configuration file's** `pseudo-device` lines in
the order they appear. Which lines survive is decided by doconf's `<feature>` filtering, and this
project already reproduces that pipeline in bash: `tools/xnu_config/expand.sh CONFIG` prints exactly
the lines the configuration keeps. So the derivation is:

    expand.sh <CONFIG> | the `pseudo-device` lines | those with an `init` word | in order

and the count rule (`count <= 0 -> 1`) and the `{0, 0}` terminator are `mkioconf.c`'s, not a choice
made here.

**The `init` functions are declared and not included, and that is Apple's arrangement, not a
shortcut.** `mkioconf.c` writes `extern int %s(int);` for each of them and its generated file
includes no header that declares them, so the declaration never meets the real prototype. It would
not agree if it did: `pty_init` is `int pty_init(int n_ptys)` and `ptmx_init` is
`int ptmx_init(int config_count)`, but `random_init` is `void random_init(void)`, `fsevents_init` is
`void fsevents_init(void)`, `mdevinit` is `void mdevinit(int the_cnt)` and `bpf_init` is
`void bpf_init(void *unused)`. They are called as `(*pi->ps_func)(pi->ps_count)` through
`int (*)(int)` regardless; on the ARM AAPCS the count arrives in `r0` and the return value is
ignored, so the call is the one Apple's kernel makes. Reproducing the declaration rather than the
signature is what keeps this file compiling under the same `-Werror` set as the rest of the build -
and the mismatch is recorded here because a reader who notices it deserves the reason.

The one thing that is *ours*: the configuration this array is generated for must be the
configuration whose objects are linked, for the same reason the option headers are per
configuration. `--check` is what makes that structural - the build regenerates and refuses when the
file on disk disagrees.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

CONFIG = os.environ.get("XNU_KERNEL_CONFIG", "RELEASE")
# Per configuration, in its own directory, and for the reason `tools/gen_option_headers.py` records:
# RELEASE and STAGE90_BOOT do not agree about this array (RELEASE keeps `bpfilter` and `fsevents`,
# STAGE90_BOOT does not), and one shared path means whichever generation ran last wins for both
# builds - silently, since a wrong entry set is six words rather than an error.
OUT_ROOT = os.environ.get("XNU_PSEUDO_INITS_OUT", os.path.join(REPO_ROOT, "out", "xnu_pseudo_inits"))


def out_file(config):
    """Where this configuration's generated source lives.

    A function and not a module constant, because `--config` and the environment can name different
    configurations and the path has to follow the one actually in use - the first version of this
    file computed it from `CONFIG` alone, so `--config STAGE90_BOOT` would have written RELEASE's
    path. `--print-path` is the same function, so the build script and the writer cannot disagree.
    """
    return os.path.join(OUT_ROOT, config, "stage90_pseudo_inits.c")

# `pseudo-device NAME [NUMBER] [init FUNC]` - the four alternatives `parser.y:207-229` accepts.
# `d_init` is what `mkioconf.c` filters on, so a line with no `init` word contributes nothing:
# `pseudo-device ether` and `pseudo-device loop` are in MASTER and are not in the array.
DEVICE_RE = re.compile(r"^pseudo-device\s+(\S+)\s*(.*)$")
COUNT_RE = re.compile(r"^(\d+)\s*(.*)$")
INIT_RE = re.compile(r"^init\s+(\S+)\s*$")


def configuration_lines(config):
    """The expanded configuration's lines, from the same pipeline `make_defines.sh` reads."""
    script = os.path.join(HERE, "xnu_config", "expand.sh")
    proc = subprocess.run([script, config], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"expand.sh {config} failed:\n{proc.stderr}")
    return proc.stdout.split("\n")


def pseudo_devices(config):
    """[(name, count, init_or_None)] in configuration order - `mkioconf.c`'s own ordering."""
    out = []
    for line in configuration_lines(config):
        m = DEVICE_RE.match(line.strip())
        if not m:
            continue
        name, rest = m.group(1), m.group(2).strip()
        count = 0
        cm = COUNT_RE.match(rest)
        if cm:
            count = int(cm.group(1))
            rest = cm.group(2).strip()
        init = None
        if rest:
            im = INIT_RE.match(rest)
            if not im:
                sys.exit(f"pseudo-device line this generator does not understand: {line!r}")
            init = im.group(1)
        out.append((name, count, init))
    return out


def array_entries(config):
    """The entries `mkioconf.c` would emit - one per pseudo-device with a `d_init`."""
    entries = []
    for name, count, init in pseudo_devices(config):
        if init is None:
            continue                      # `d_init == 0`: not in the array, and not in Apple's either
        entries.append((init, count if count > 0 else 1))
    return entries


def render(config, entries):
    functions = [fn for fn, _ in entries]
    lines = []
    lines.append("/*")
    lines.append(" * Generated by tools/gen_pseudo_inits.py for the %s configuration." % config)
    lines.append(" *")
    lines.append(" * This is `pseudo_inits`, the array `bsd_autoconf()` walks (`bsd/kern/bsd_init.c:1083`):")
    lines.append(" *")
    lines.append(" *     for (pi = pseudo_inits; pi->ps_func; pi++)")
    lines.append(" *         (*pi->ps_func) (pi->ps_count);")
    lines.append(" *")
    lines.append(" * `SETUP/config/mkioconf.c:79-100` generates it in Apple's build, from the")
    lines.append(" * configuration file's `pseudo-device` lines that carry an `init` word, in")
    lines.append(" * `dtab` order, with `count <= 0` becoming 1 and a `{0, 0}` terminator. This")
    lines.append(" * file reproduces exactly that, from `tools/xnu_config/expand.sh %s`." % config)
    lines.append(" *")
    lines.append(" * **Why it is supplied rather than stubbed, which is experiment 438:** the array's")
    lines.append(" * second word is the walk's own termination test, so a *function* stand-in for this")
    lines.append(" * symbol puts its own prologue there, `ps_func` reads as non-NULL, and the first")
    lines.append(" * `blx` jumps to the value of `push {lr}` - `0xE52DE004`, an unmapped address, and")
    lines.append(" * an instruction prefetch abort. The kind of the stand-in is the whole of the bug.")
    lines.append(" *")
    lines.append(" * The `extern int NAME(int);` declarations are `mkioconf.c:84-88`'s, and they do")
    lines.append(" * not agree with every real definition - `random_init` and `fsevents_init` take no")
    lines.append(" * argument, `mdevinit` and `bpf_init` return `void`. Apple's generated file declares")
    lines.append(" * them this way and includes nothing that would notice, and the call is")
    lines.append(" * `(*pi->ps_func)(pi->ps_count)` either way: on the AAPCS the count arrives in `r0`.")
    lines.append(" * Reproducing the declaration and not the signature is what keeps this file")
    lines.append(" * compiling under the build's own `-Werror` set.")
    lines.append(" */")
    lines.append("")
    lines.append("#include <dev/busvar.h>          /* struct pseudo_init, and the prototype of this array */")
    lines.append("")
    for fn in functions:
        lines.append("extern int %s(int);" % fn)
    lines.append("")
    lines.append("struct pseudo_init pseudo_inits[] = {")
    for fn, count in entries:
        lines.append("\t{%d,\t%s}," % (count, fn))
    lines.append("\t{0,\t0},")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--write", action="store_true", help="write the generated source")
    ap.add_argument("--check", action="store_true", help="fail when the file on disk is stale")
    ap.add_argument("--print-path", action="store_true",
                    help="print the file this configuration generates, and nothing else")
    ap.add_argument("--config", default=CONFIG, help=f"configuration (default {CONFIG})")
    args = ap.parse_args()
    path = out_file(args.config)

    # `--print-path` exists so the build script does not have to reconstruct this path a second
    # time. A `$REPO_ROOT/out/...` spelled twice is the defect class this project keeps meeting:
    # two definitions of one value, and nothing comparing them.
    if args.print_path:
        print(path)
        return 0

    entries = array_entries(args.config)
    text = render(args.config, entries)

    if args.check:
        try:
            on_disk = open(path).read()
        except FileNotFoundError:
            sys.exit(f"no {path} - run: ./tools/gen_pseudo_inits.py --write")
        if on_disk != text:
            sys.exit(f"{path} is not what the {args.config} configuration derives - run:\n"
                     f"  XNU_KERNEL_CONFIG={args.config} ./tools/gen_pseudo_inits.py --write")
        names = ", ".join(fn for fn, _ in entries) or "none"
        print(f"ok: {args.config} pseudo_inits carries {len(entries)} entr"
              f"{'y' if len(entries) == 1 else 'ies'} + the {{0, 0}} terminator ({names})")
        return 0

    if args.write:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(text)
        print(f"wrote {path}")
    else:
        sys.stdout.write(text)

    for fn, count in entries:
        print(f"  {{{count},\t{fn}}}", file=sys.stderr)
    print(f"  {len(entries)} entr{'y' if len(entries) == 1 else 'ies'} + {{0, 0}}, "
          f"from the {args.config} configuration", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
