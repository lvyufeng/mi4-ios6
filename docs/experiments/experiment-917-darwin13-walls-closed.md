# 917 — Closing 916's two walls: MIG headers per tree, and the BSD <sys/types.h> shadow (2026-10-08)

916 pointed the whole-kernel build at Darwin-13 and named two structural walls: **MIG headers pinned
to 4570** and **BSD legacy typedefs** (`gid_t`, `uid_t`, `off_t`, … ~1,800 `unknown type name` sites).
This step closes both, and finds a third defect *inside* the second fix. Result: **195 → 288 of 367**
manifest sources compile, with **4570 provably unchanged** (703/703 objects byte-identical).

## 1. The MIG wall was a value pinned to the wrong tree

`mach_host.h:353` cites `ipc_voucher_t`, but Darwin-13 has **no voucher subsystem at all** — no
`ipc_voucher.h`, no `ipc_voucher_t` anywhere, and D13's `mach_host.defs` does not mention it. That is
the tell: the build was reading **4570's generated headers**. Three things pinned it:

| where | pin | fix | 4570 effect |
|---|---|---|---|
| `build_xnu_arm_kernel.sh:62` | `MIG_KSERVER` defaulted to `$REPO_ROOT/out/mach_headers/kserver`, fixed and independent of `MIG_HEADERS` | derive it as `$MIG_HEADERS/kserver` (they are two outputs of one `.defs` set) | none (same path) |
| `build_xnu_arm_kernel.sh:213` | the manifest ran `list_sources.py` with **no** `--generated-dir`, so every MIG `_server.c` resolved into 4570's root | pass `--generated-dir "$MIG_KSERVER:$MIG_HEADERS:…"` | none (the explicit value equals the default) |
| same call | no `--device-table`, defaulting to 4570's `out/device_table.txt` | pass `--device-table "$DEVICE_TABLE"` | none (default path) |

`gen_mach_headers.sh`, `list_sources.py` and `device_table.py` **already** honored `XNU_TREE`; the
build just wasn't using those knobs. Generating D13's set is 36 `.defs` → **36 generated, 0 failed**
(4570's spec is 40 — the four voucher/shared-memory `.defs` are simply gone from D13). Verified:
D13's `mach_host.h` has **0** `ipc_voucher` references, 4570's still has 3.

## 2. The BSD typedef wall was a `<sys/types.h>` *shadow*, not absent types

The ~1,800 missing names were not missing. Darwin-13 **ships** them in `bsd/sys/types.h` (and its
`_types/` fragments). The failure is that the build resolved `<sys/types.h>` to a **different file**:

> Darwin-13 ships a **legacy private `osfmk/sys/types.h`** whose guard is `_SYS_TYPES_H_` — the **same
> guard** the modern `bsd/sys/types.h` uses. 4570 ships **no** `osfmk/sys/types.h`.

Our import chain put a component's raw source root ahead of the others, so for a `libkern` or `libsa`
file `-I$XNU/osfmk` preceded `-I$XNU/bsd`: `#include <sys/types.h>` found the legacy header first,
which defines `_SYS_TYPES_H_` and the old `time_t`/`dev_t`/`daddr_t`; `bsd/sys/types.h`'s body is
then skipped entirely by its own guard, and `gid_t`/`uid_t`/`off_t`/`mode_t` never appear.

**Apple never sees this** because its import is by **curation**, not by raw tree: `INCFLAGS_IMPORT =
-I$(OBJROOT)/EXPORT_HDRS/%` (`makedefs/MakeInc.def:463`), and `EXPORT_HDRS/<component>/` holds only
the headers a component *exports* (`EXPORT_MI_LIST`/`EXPORT_MD_LIST`). That private header is not
exported. `tools/gen_export_headers.sh` reproduces the mechanism but was **measured worse** as an
include path (incomplete roots), so the build imports raw trees.

The fix reproduces Apple's *resolution* without the curated roots: **put `bsd` ahead of the others in
the import chain.** Derived from the single `COMPONENT_LIST` (so it cannot drift from it) and gated on
the tree shipping `osfmk/sys/types.h` — a tree that does not (4570) gets Apple's exact
`COMPONENT_LIST` order, so the change is the identity there.

## 3. The fix found a second definition of "which component"

`SRC_COMPONENT` was computed as a naive first-path-segment cut, `${src#$XNU/} | cut -d/ -f1`. For a
MIG-generated source — which lives **outside** `$XNU` — the path does not start with `$XNU/`, so the
cut hands back the **empty string**. With `bsd` now first, an empty component meant the generated
server's `-I` chain started at `bsd`, so `<arm/locks.h>` resolved to `bsd/arm/locks.h`, which dies on
its `#include <ARM/hw_lock_types.h>` (a macOS case-insensitive artifact; Linux is case-sensitive).
`component_of()` — the function the *defines* table already uses — answers `osfmk` for those files
correctly. Reusing it (one definition, not two) fixed 62 files at once and is inert on 4570.

## 4. The number, and neutrality

**Darwin-13 RELEASE: 288 of 367 manifest sources compile** (was 195), exit 0. Typedef sites went
3,778 → 0; the `ARM/*` case wall 62 files → 0.

**4570 neutrality, measured, not argued:** baseline build with the committed script (703 objects) vs
the modified script (703 objects) → **703/703 byte-identical**. `make check` 0,
`check_device_conditions` 0, `device_table` 0.

## 5. What is NOT done (the next rungs)

- **`default_pager_object_server.h`** (12 files): D13's `default_pager_object.defs` declares
  `server` (MIGKSFLAGS `-DKERNEL_SERVER=1`) so the kernel needs the `_server.h`; the generator emitted
  the `_server.c` but not the `.h`. A generation-list detail, not a source defect.
- **BSD legacy design issues** surfacing per file now that types resolve: `CONFIG_MAX_CLUSTERS`
  undefined, `io_object_t` defined twice (`osfmk/device/device_types.h` vs `security/mac_framework.h`),
  a stray `#include <loop.h>`.
- **Host-header leakage**: `bsd/dev/arm/conf.c:142` `#include <pty.h>` resolves to the host's
  `/usr/include/pty.h` → `bits/wordsize.h`. Needs the build's own pty surface.

*Provenance: `out/{d13_kernel_build.log,4570_base_build.log}`, `out/xnu_kernel_obj_d13/`,
`out/mach_headers_d13/`. Host-side, reversible, no press. Related: experiment-916,
[[mi4-913-ios7-rebase-decision]].*

## 6. The next wall, measured and NOT crossed: Darwin-13's per-component doconf

91 of the manifest's 367 sources remain. The largest single cluster is `CONFIG_*` tunables
(`CONFIG_MAX_CLUSTERS` 50 sites, `CONFIG_VNODE_FREE_MIN`, `CONFIG_NC_HASH`, …). They are declared in
Darwin-13's `bsd/conf/MASTER`, but `select_master.sh` reads only `osfmk/conf/MASTER` — the modern
tree (4570) keeps **one** consolidated `config/MASTER`; the 2013 tree keeps a **per-component**
`<component>/conf/MASTER` (7 of them, 395 `options` lines total). The layout guess reads 41 `-D`
flags where the modern tree's 108 is the scale.

A union of the seven was implemented and **reverted**, because it does not reproduce Apple's pipeline:

- **Each component's `MASTER.arm` declares its own `RELEASE`** with different size attributes
  (`osfmk`: `bsmall`; `libkern`: `medium`), so the union selects *both* `CONFIG_MAX_CLUSTERS=4` and
  `=8` — 20 macros end up with two values. The modern tree's single declaration excludes one.
- It surfaced a device condition the single-file read never saw: `vol` is a `pseudo-device` in
  `bsd/conf/MASTER.arm` and an attribute of `osfmk`'s `RELEASE`, but has no `conf/files` need-word, so
  the device-condition check refuses the build (`NVOL ... no generated header defines it`).

Both are the same question — **how Apple's doconf composes per-component MASTERs into one
configuration** — and answering it is a sub-project, not a line. Reverting kept the measured landmark
(325/367) and the 4570 control intact (4570's `make_defines`/`expand`/`select_master` are provably
byte-identical under the union too, so the work is preserved as a documented direction, not lost).

### Rung 918 (next)

1. Reproduce Apple's per-component doconf: per-component configurations, OR attribute-name scoping so
   the size attributes cannot cross components.
2. Decide `vol`: a `conf/files` need-word, or an explicit `NVOL` from the device table.
3. Remaining after that: `bsdthread_*_args` incomplete (28 sites, a generated sysproto detail), the HD2
   tree's malformed `# HTC HD2` marker in `version.h.template`/`Makefile.template`, and host-header
   leakage (`bsd/dev/arm/conf.c` `<pty.h>`).