#!/usr/bin/env python3
"""
The devices a configuration declares — one parse, read by every consumer.

    ./tools/xnu_config/devices.py RELEASE

Why this exists as its own file. Three things in this build have to agree about the same decision:

  1. **the manifest** — `list_sources.py` matches `optional <cond>` against the condition set, and
     `<cond>` can be a *device* (`bsd/net/if_loop.c optional loop`);
  2. **the device headers** — `mkheaders.c:85-101` writes `#define N<COND> <count>` into `<cond>.h`,
     and the sources test those as counts and as flags (`#if NLOOP > 0`, `#if NBPFILTER > 0`);
  3. **the generated `pseudo_inits[]`** — `mkioconf.c:79-100` writes one `{count, func}` per
     pseudo-device that has a `d_init`.

All three read the **same lines** — the `device` / `pseudo-device` declarations of the
configuration, which reach us as the output of `expand.sh <CONFIG>` — and until now they did not
read them in the same place. `device_table.py` said the devices were "simply absent" from 4570 and
hand-wrote a table; `gen_device_headers.sh` hand-wrote four headers with values chosen to match that
table; and `gen_pseudo_inits.py` (experiment 439) derived the array correctly from the configuration
and so disagreed with both.

**The premise of the old table was wrong, and the measurement is one grep.** It read

    grep -c '^pseudo-device' */conf/files      # 0 in every component

and concluded that 4570 publishes no device tables. The grep is right. The conclusion is not:
`config/MASTER` publishes **24** `pseudo-device` lines, `expand.sh` already expands them (that is
what 439's array was derived from), and the eight that survive RELEASE's feature filtering are

    ether  loop  pty 16  ptmx 1  mdevdevice 1  bpfilter 4  fsevents 1  random 1

`*/conf/files` is where an `optional` *condition* is tested; `config/MASTER` is where the device set
that answers it is declared. Looking for the latter in the former is
[[mi4-not-absent-its-build-output]] again: not absent, in the other file.

**The two counts in Apple's source, and they are different rules on purpose.** Both are `d_slave`,
and the two writers disagree about what `d_slave == UNKNOWN` means:

    mkheaders.c:85-101   count = dp->d_slave != UNKNOWN ? dp->d_slave : 1     <- the header
    mkioconf.c:79-100    count = d_slave; if (count <= 0) count = 1;          <- the array entry

A bare `pseudo-device loop` has `d_slave = UNKNOWN` (parser.y:207-210) and becomes `NLOOP 1` in the
header and `{1, loopattach...}` in the array — the same 1 by two routes. An explicit
`pseudo-device bpfilter 4` keeps its 4 in both. So this module returns the **raw** declaration and
each consumer applies its own rule, **with the citation**, rather than one rule being applied twice
and being wrong once.
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))

# `device NAME [NUMBER] [init FUNC]` and `pseudo-device NAME [NUMBER] [init FUNC]` — the four
# alternatives `parser.y:207-229` accepts for `pseudo-device`. `device` shares the Dev/NUMBER/INIT
# shape and no configuration in the tarball uses it, but a configuration that did would be silently
# dropped by a `pseudo-device`-only pattern, so both are matched and the kind is kept.
DEVICE_LINE = re.compile(r"^(pseudo-device|device)\s+(\S+)\s*(.*)$")
NUMBER_RE = re.compile(r"^(\d+)\s*(.*)$")
INIT_RE = re.compile(r"^init\s+(\S+)\s*$")


def configuration_lines(config, expand=None):
    """The expanded configuration's lines, from the pipeline the manifest and the array both read."""
    script = expand or os.path.join(HERE, "expand.sh")
    proc = subprocess.run([script, config], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"expand.sh {config} failed:\n{proc.stderr}")
    return proc.stdout.split("\n")


def devices(config, expand=None):
    """[(name, number_or_None, init_or_None, kind)] in configuration order.

    `number` is `None` when the line carries no NUMBER — which is `d_slave == UNKNOWN`, and is *not*
    the same as a written `0`. The distinction is the whole reason this returns the raw value:
    `mkheaders.c` maps `None` to 1 and passes a written `0` through.
    """
    out = []
    for line in configuration_lines(config, expand):
        m = DEVICE_LINE.match(line.strip())
        if not m:
            continue
        kind, name, rest = m.group(1), m.group(2), m.group(3).strip()
        number = None
        nm = NUMBER_RE.match(rest)
        if nm:
            number = int(nm.group(1))
            rest = nm.group(2).strip()
        init = None
        if rest:
            im = INIT_RE.match(rest)
            if not im:
                sys.exit(f"{kind} line this parser does not understand: {line!r}")
            init = im.group(1)
        out.append((name, number, init, kind))
    return out


def header_count(number):
    """`mkheaders.c:85-101`: `count = d_slave != UNKNOWN ? d_slave : 1`."""
    return 1 if number is None else number


def array_count(number):
    """`mkioconf.c:79-100`: `count <= 0 -> 1`, which is a different rule on the same value."""
    return number if (number is not None and number > 0) else 1


def main():
    config = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("XNU_KERNEL_CONFIG", "RELEASE")
    rows = devices(config)
    if not rows:
        sys.exit(f"the {config} configuration declares no devices at all - that is not a "
                 f"configuration this project has ever built, so it is an error and not an empty set")
    print(f"{config}: {len(rows)} device(s)")
    for name, number, init, kind in rows:
        raw = "UNKNOWN" if number is None else str(number)
        print(f"  {name:14s} {kind:14s} d_slave={raw:8s} "
              f"header N{name.upper()}={header_count(number)}  "
              f"init={init or '-'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
