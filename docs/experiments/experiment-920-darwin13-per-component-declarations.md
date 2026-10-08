# 920 — Darwin-13's per-component declarations: the device table, the option set, the comma option, and `confdep.h` (2026-10-08)

After 919 the Darwin-13 whole-kernel compile stood at **359 of ~367** and every remaining failure
was the same shape as 917/918/919's: **a value the harness read from the wrong place because the
2013 tree declares per component what 4570 declares once.** This rung closes four of them, and with
them `bsd/conf/param.c` — the file that had been the single non-firehose failure — compiles.

Objects: **354 (918) → 359 (919) → 372 (920)**, manifest unchanged at 409. The only remaining
failure is the pre-existing firehose block (`firehose_buffer.c`, `firehose_kernel_config.c`), which
is part of the build's `EXIT=5` on both trees and is not this rung's.

## The one premise, four instances

4570 keeps **one** `config/MASTER`; Darwin-13 keeps `<component>/conf/MASTER` (seven of them). Every
place the harness opened "the MASTER" and read a declaration was therefore reading *one component's
slice* of a set Apple's `config` builds **globally** — `dtab` (devices), `opt[]` (options), and
`maxusers` are each single, tree-wide values in `mkmakefile.c`, assembled from every component's
MASTER. Four consumers read one slice each:

| # | Consumer | Read | Darwin-13 consequence |
|---|---|---|---|
| 1 | `devices.py::configuration_lines` | `osfmk/conf/MASTER` only | `loop`/`pty`/`ether`/… invisible — `bsd_init.c`'s `#include <loop.h>` had no header (917's wall) |
| 2 | `check_device_conditions.py` rule (3) | "declared" for "emitted" | `vol` visible but driverless — demanded a header that `mkheaders.c` never writes |
| 3 | `gen_option_headers.py::configured_options` | `osfmk/conf/MASTER` only | `INET` invisible → `inet.h` said `#define INET 0` → **shadowed `-DINET=1`** → `if_loop.c`'s `apple_hwcksum_tx` undeclared |
| 4 | `make_defines.sh` | — (grammar) | see §3 |

The fix for 1–3 is one function, `devices._master_dirs()` (§1), and making 2 and 3 read through it.
Instance 4 is a different kind: not *where* is read, but *how the line is split* (§3).

## 1. `devices._master_dirs()` — one parse, every consumer

`devices.py` already held the tree's device parse; the change gives it the **layout rule** for where
the MASTERs live, by the same shape `select_master.sh` uses (913/918): a tree with `<root>/config/
MASTER` has one dir (4570 → the union is the identity); otherwise the `<component>/conf/MASTER` set,
in `COMPONENTS` order.

```python
def _master_dirs():
    root = os.environ.get("XNU_TREE") or ...
    if os.path.isfile(os.path.join(root, "config", "MASTER")):
        return [os.path.join(root, "config")]
    dirs = []
    for comp in COMPONENTS:                       # osfmk, bsd, libkern, iokit, pexpert, libsa, security, san
        d = os.path.join(root, comp, "conf")
        if os.path.isfile(os.path.join(d, "MASTER")) and d not in seen:
            dirs.append(d)
    return dirs
```

`configuration_lines()` expands each dir with `XNU_MASTER_DIR=d` set and concatenates — the same
`expand.sh` the manifest and the array read, so the union is Apple's "one global table" and not a
second parse. First declaration wins, so a name in two components resolves once.

`gen_option_headers.py::configured_options()` now iterates the same list; that single change is why
`inet.h` reads `INET 1` on Darwin-13 and the option-header set is the union Apple's `opt[]` holds.
On 4570 the list has one entry, so both are the identity (measured: `out/xnu_options/RELEASE` is
**byte-identical** to a regen against the stashed script).

## 2. `check_device_conditions.py` rule (3): emitted ≠ declared

The union made `vol` **visible** — `pseudo-device vol` is in `bsd/conf/MASTER` and in RELEASE's
`BASE` — and the check then demanded a `vol.h`, which `mkheaders.c` does not write. The rule was
reading "declared" where the emitter reads "**emitted**": `headers()` walks the *file table* and
writes `<cond>.h` only for a `conf/files` need word that names a device in `dtab` — an
**intersection**. Darwin-13's `vol` has no driver and no need word (`bsd/dev/arm/conf.c`'s bdevsw row
already takes the `eno_opcl` branch at `:287`), so `NVOL` is 0 **by design**. Rule (3) now requires a
header only when the condition is in the emitted set.

This dissolves (and supersedes) the `pty_init` note from the option/device-header overlap work: the
array's `8` and `pty.h`'s `NPTY 16` are `mkioconf.c`'s `count <= 0 → 1`-on-`d_slave` vs
`mkheaders.c`'s raw `d_slave`, and the disagreement is *reported*, not asserted away — the check's own
design (its §4), unchanged here.

## 3. `make_defines.sh`: a top-level comma is two options

Apple's grammar is `Opt_list: Opt_list COMMA Option` (`parser.y:437-438`), so

```
bsd/conf/MASTER:81   options  TIMEZONE=0, PST=0
```

is **two** entries in `opt[]`, and `mkmakefile.c:269-272` writes each as its own flag —
`-DTIMEZONE=0 -DPST=0`. Our awk produced one define, `-DTIMEZONE=0, PST=0`, so `param.c:85`'s

```c
struct timezone tz = { TIMEZONE, PST };     // expands to { 0, PST=0 }, PST undeclared
```

died — and it died **silently on 4570**, which has no such line.

The fix splits on commas **outside double quotes**: the state machine tracks `"` so `KAUTH_CRED_PRIMES=
"{5, 17, 97}"` (one option, commas in its value) stays whole while `TIMEZONE=0, PST=0` becomes two.
4570's expansion is **byte-identical** (108 defines), because it has no comma option at all.

## 4. `confdep.h`: an Apple-generated header, reproduced and tree-selected

`bsd/conf/param.c:69` includes `<confdep.h>`, which is in **no** tarball — the `config` binary writes
it:

```
mkmakefile.c:274-276   if (maxusers) { do_build("confdep.h", build_confdep); }
mkmakefile.c:813-816   build_confdep() { fprintf(fp, "#define MAXUSERS %d\n", maxusers); }
```

and `maxusers` is the configuration's selected `maxusers` line (`parser.y:267`). Those lines are
**tagged by scale** (`64 <xlarge>` … `2 <bsmall>`), and Darwin-13's ARM `RELEASE` carries the tag
**`bsmall`** (`osfmk/conf/MASTER.arm:12`), so exactly one survives: `maxusers 2`. That is Apple's own
iOS-7 ARM value — 4570's `param.c` hardcodes the equivalent of `32`, so the two trees genuinely
differ and the value must follow the tree.

`tools/gen_confdep.py` derives `MAXUSERS` from `devices.configuration_lines()` — the same expansion
every other define reads — and writes `$XNU_GENERATED/confdep.h` (the per-tree generated root the
build force-includes via `-I"$GENERATED"`), mirroring `gen_libkern_version.sh`. On 4570 the expansion
yields no `maxusers` line (nothing in that tree includes `<confdep.h>`; its `param.c` hardcodes
`NPROC`), so it writes **nothing** — the same inertness `gen_libkern_version.sh` gets from its marker
test. Two surviving `maxusers` lines would be two values for one number, so the generator **refuses**
rather than taking the last.

## Verification

- **Darwin-13**: 372 objects / 409 manifest; `param.c` compiles; only the firehose block fails.
  `check_device_conditions.py` (D13) → `ok: 16 device(s) … 8 macro(s) read, all defined; 7 array
  entries agree`.
- **4570 neutrality** (build-side only, per the two-branch split — no object rebuild):
  - `make_defines.sh RELEASE` → **byte-identical** (108 defines);
  - `out/xnu_options/RELEASE` regen → **byte-identical**;
  - `make check` → **0**;
  - `check_device_conditions.py` → 0 new (the single `pty_init` note is pre-existing, unchanged).
- **New file**: `tools/gen_confdep.py`; documented as a build prerequisite in
  `build_xnu_arm_kernel.sh`'s header.

## What this does not do

The firehose block (`firehose_buffer.c`, `firehose_kernel_config.c`) still fails — pre-existing,
independent of the tree. `bsdthread_*_args` (28 sites, generated `sysproto` detail) is still owed.