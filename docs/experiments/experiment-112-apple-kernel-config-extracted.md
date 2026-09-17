# Experiment 112 — Apple's own kernel configuration, extracted and used

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/xnu_config/{select_master,expand,make_defines}.sh`

## The correction

`experiment-111` closed with a correction: the build configuration is not absent. This is the
follow-through — the configuration *extracted and used*, which is what makes the correction
concrete rather than a note.

`config/MASTER` (737 lines) and `config/MASTER.arm` (86) are in the tarball, and so is the tool's
source under `SETUP/config/`. `osfmk/conf/MASTER.XXX` — the path `sched_prim.h:574`'s `#error`
names, reported missing for several sessions — is where `doconf` **writes its output**, and the
search that concluded "absent" looked there and not at the repository root.

## What it gives: the whole ARM kernel's configuration, from Apple

`doconf` is a `csh` script and this host has no `csh`, so its two stages are ported to bash:
`select_master.sh` is its sed pipeline and `expand.sh` its awk expansion. Running Apple's own
attribute sets through them produces, for `RELEASE`, **108 option lines**:

```
options   CONFIG_ZONE_MAP_MIN=1048576
options   CONFIG_TASK_MAX=512
options   CONFIG_THREAD_MAX=1536
options   CONFIG_IPC_TABLE_ENTRIES_STEPS=64
options   CONFIG_MAX_CLUSTERS=4
options   CONFIG_MAXVIFS=16
options   CONFIG_KN_HASHSIZE=48
options   CONFIG_AIO_MAX=20
options   CONFIG_VNODES=1024
options   CONFIG_MACH_APPROXIMATE_TIME
options		CONFIG_SCHED_TRADITIONAL
options		CONFIG_SCHED_MULTIQ
options		CONFIG_SCHED_TIMESHARE_CORE
options		KPC
options		MACH_BSD
options		CONFIG_MACF
options		SERIAL_CONSOLE
```

Three of those — `CONFIG_ZONE_MAP_MIN`, `CONFIG_TASK_MAX`, `CONFIG_IPC_TABLE_ENTRIES_STEPS` —
appeared in this project's own osfmk sweep as **undeclared identifiers**. They were never missing.
And `CONFIG_SCHED_MULTIQ` is **on**, alongside `CONFIG_SCHED_TIMESHARE_CORE` and
`CONFIG_SCHED_TRADITIONAL` — which resolves the earlier reasoning that "`MULTIQ` wants a
`sched_multiq.h` the tarball does not ship, so it must be off". Three schedulers are on together
because `TIMESHARE_CORE` is the queue core the others build on, not a competing choice.

`make_defines.sh` turns the option lines into a compiler's `-D` list, dropping the `pseudo-device`
and `machine` lines that configure the old BSD device tables.

## The test: Apple's config, rebuilt on the ARM layer

```
osfmk/arm with APPLE'S OWN CONFIG: 31 of 32
```

versus 32 of 32 with the hand-guessed set. **The one difference is the interesting part.**

`monotonic_arm.c` fails: *no member named `cpu_monotonic` in `struct cpu_data`*. That member is
guarded by `MONOTONIC`, and **`MONOTONIC` is not in `config/MASTER` in any form** — not as an
option, not as an attribute, under any configuration name. It is a per-SoC decision, and the
per-SoC configuration (`ARCH_CONFIGS_EMBEDDED`, `DEVICEMAP_PRODUCTS_*`) is the piece that genuinely
does not ship. So the hand-guessed set was right about `MONOTONIC=1` and Apple's RELEASE is simply
silent on it.

That is the sharpest statement of what is and is not available:

| Piece | Available |
| --- | --- |
| The option catalogue and its attribute tags | ✅ `config/MASTER`, `config/MASTER.arm` |
| The configuration names and their attribute sets | ✅ same files |
| The tool that turns them into `MASTER.XXX` and headers | ✅ `SETUP/config/` |
| The per-SoC product definitions | ❌ not published — and they decide things like `MONOTONIC` |

So the configuration is *nearly* complete. What is missing is a small, specific layer: the choices
that vary by SoC, of which this layer needs exactly one.

## What this does and does not mean

**Does:** the project no longer guesses at configuration values. Apple's own catalogue is extracted,
reproducible, in the tree, and its values are used in the measurement. Three values that looked like
missing code turned out to be written down.

**Does not:** build XNU. Compiling the ARM layer is `experiment-110`'s result and it is unchanged;
linking is still 443+ undefined symbols; and none of this touches the device. **XNU does not run.**

The value is in what it removes: a class of "unknown" that was really "unlooked-for". The next
concrete step is building `SETUP/config` (bison, flex and eight C files; it links on this host) and
running doconf to generate the real `MASTER.XXX`, replacing the ported pipeline with the actual
tool.
