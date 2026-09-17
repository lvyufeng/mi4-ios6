# Experiment 115 — a minimal-boot configuration, and one define that unlocked twelve files

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/xnu_config/minimal/STAGE90_BOOT.local`, `tools/build_xnu_arm_kernel.sh`

## Why a second configuration

`experiment-114` measured Apple's `RELEASE` — the full iOS kernel — and found **254 of the 397
failures in the network stack** (`bsd/net`, `bsd/netinet`, `bsd/netinet6`). A kernel whose job is to
reach `machine_startup` and print a line does not need those.

The configuration names are Apple's, but the *mechanism* is general: the declarations live in
`#  NAME = [ attribute ... ]` comment lines in `config/MASTER` and `config/MASTER.arm`, and
`doconf` documents a `MASTER.local` for adding more. So
`tools/xnu_config/minimal/STAGE90_BOOT.local` adds **one line**:

```
#  STAGE90_BOOT = [ KERNEL_RELEASE BSD_BASE IOKIT_BASE LIBKERN_BASE PERF_DBG_BASE MACH_RELEASE
#                   SCHED_RELEASE VM_BASE MONOTONIC_BASE ]
```

Every attribute in it is Apple's own, drawn from `MASTER.arm`'s definitions. What is left out:
`NETWORKING`, `PF`, `VPN`, `MULTIPATH`, `SKYWALK_*`, `FILESYS_*`, `SECURITY` (macf), and the
dtrace/zleaks debug sets. `MONOTONIC_BASE` is named in `PERF_DBG_BASE` but defined nowhere —
it is the per-SoC attribute from `experiment-112`, and naming it here *is* the decision that
per-SoC file would have made.

The manifest drops from 694 files to **526**, and from 569 C files to 401.

## The define that unlocked twelve files

`bsd/sys/types.h:162` includes `sys/_types/_clock_t.h` **unconditionally** — the `#ifdef KERNEL`
branch in that file comes sixty lines later — and that header typedefs

```c
typedef __darwin_clock_t clock_t;      /* unsigned long */
```

while `osfmk/kern/kern_types.h:193` typedefs

```c
typedef struct clock *clock_t;         /* a Mach clock object */
```

Both visible in one translation unit is a redefinition. `_clock_t.h` is guarded by `_CLOCK_T`, and
`kern_types.h` does not define it, so the BSD userland meaning wins by default — and the kernel's
Mach clock object fails to exist.

**`-D_CLOCK_T=1`** makes the kernel's definition the surviving one. One flag, and:

| | before | after |
| --- | --- | --- |
| `iokit` failing files | 1 of 62 | **0** |
| total compiling (STAGE90_BOOT) | 179 | **191** |

This is the fourth instance of the pattern this project keeps meeting — one name, two definitions,
and the build has to say which wins. The device-tree child count, the pmap descriptor literals, the
cache-policy `0u` and now `clock_t`. What is different here is that the *source* provides the
switch, and finding it was a matter of reading which of the two definitions had a guard.

## Running the minimal build

```bash
MANIFEST=out/xnu_arm_manifest_min.txt
XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  ./tools/xnu_config/list_sources.py STAGE90_BOOT --write $MANIFEST

XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$MANIFEST XNU_KERNEL_OBJ_OUT=out/xnu_min_obj ./tools/build_xnu_arm_kernel.sh
```

`build_xnu_arm_kernel.sh` also gained the configuration's own options — it now expands `MASTER`
through the doconf pipeline and compiles with those, where before it used only the hand-worked
flags. Fixing that alone moved the minimal run from 178 to 191, which is the correction to
experiment-114's numbers too: those were measured with a handful of guessed values in place of the
104 Apple's configuration selects.

## What is left, honestly

The remaining blockers are no longer headers and no longer the network stack:

- **`uthread_t`** (146 occurrences) — `bsd/sys/user.h:290`, undeclared in files that need it.
- **`copyinstr`** (129) — genuinely conflicting declarations: `osfmk/kern/misc_protos.h:107` says
  `(const user_addr_t, char *, vm_size_t, vm_size_t *)` and `bsd/libkern/libkern.h:183` says
  `(const user_addr_t, void *, size_t, size_t *)`. Two headers, one name, different types.
- **`ORDINARY`**, **`struct tty`** (225) — `bsd/kern/tty.c` and its neighbours.
- **`M_NAMEI`, `BYTE`, `UINT`** — mbuf and sysctl internals.

These are the Mach-view-versus-BSD-view collisions, and Apple's build resolves them through the
**exported-header set**: `makedefs/MakeInc.def:463-469` shows `INCFLAGS_IMPORT` pointing at
`$(OBJROOT)/EXPORT_HDRS/$(COMPONENT)`, populated from the `config/*.exports` files by the
`installfile` tool in `SETUP/`. That is a build step this project has not reproduced, and it is now
the named next thing rather than a guess.

## What this does not mean

**XNU still does not run**, no OS is entered, no driver runs. 191 of 401 files compiling in a
minimal-boot configuration is progress on the kernel half and nothing at all on the driver half,
which remains as I described it: not started, and not reachable by continuing this work.
