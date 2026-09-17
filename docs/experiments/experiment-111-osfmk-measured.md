# Experiment 111 — the rest of osfmk measured, and 18 missing headers down to 3

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/sweep_xnu_osfmk.sh`, server headers from `tools/gen_mach_headers.sh`

## The result

With `osfmk/arm` at 32 of 32 (`experiment-110`), the next question is the kernel proper. Measured
the same way — by compiling — and with the same flag set:

```
osfmk (excluding arm): 81 of 170 parse
```

for the directories that matter to an ARM kernel: `kern` 41 of 83, `vm` 9 of 27, `ipc` 2 of 20,
`kdp` 3 of 5, `console` 6 of 6, `chud` 5 of 5, `prng` 2 of 3, `device` 1 of 3.

And the **missing-header list collapsed from 18 names to 3**:

```
'atm/atm_notification.h'    'device/device_server.h'    'libkern/version.h'
```

## What moved it: the server headers

`tools/gen_mach_headers.sh` was emitting only `-header` output. The kernel's own sources include
**three** MIG outputs per `.defs`, not one:

| Output | Included as | Was it generated |
| --- | --- | --- |
| `X.h` | `<mach/mach_host.h>` | yes |
| `X_server.h` | `<mach/mach_host_server.h>` | **no — this was the gap** |
| `X_client.c` | not part of a kernel | no, correctly |

`osfmk/kern/*.c` includes the server headers by name, and nothing else provides them. Adding
`-sheader` to the MIG invocation produced them for all 23 `.defs`, which is the whole difference
between 75 of 170 and 81 — and between 18 missing headers and 3.

That is worth recording as a small lesson about the earlier measurement: **"MIG is built and 23
headers are generated" was true and incomplete.** MIG has three outputs and the first run asked for
one of them. A tool being present is not the same as the tool being used fully.

## Two wrong-architecture measurements caught

The first run of the sweep included `osfmk/i386` and `osfmk/x86_64`, which produced 400-odd
`_STRUCT_X86_*` "blockers" for a kernel that will never contain them. The second included
`osfmk/arm64`. Both are now excluded with the reason written in the script, because a measurement
that counts another architecture's failures is worse than no measurement — it looks like a result.

`u_long` was the single largest real blocker (223 occurrences) and is now supplied: `bsd/sys/types.h`
defines it under a `_U_LONG` guard, and the BSD type fragments the entry path can include do not
carry it. Copied verbatim, guard and all, into `mi4ios6_build_config.h`.

## How far the remainder is — the measure that matters

A count of failing files is not actionable. The distribution is, so the sweep now reports it, one
log per file:

```
== how far each failing file is ==
  1-2 errors    55 file(s)
  3-10          27 file(s)
  11-50          5 file(s)
  51+            2 file(s)
```

**55 of the 89 failing files are one or two errors from parsing**, and only 2 are more than fifty
away. That is a long tail of config values and includes, not a wall of subsystems — and it is a
materially different picture from "89 files fail", which is what a bare count said.

Getting that number required fixing the measurement twice. The first version reused the outer
loop's `name` variable, so every failing file in a directory wrote to one log and the histogram
counted directories; the second still mixed per-file logs with per-directory aggregates. Both are
recorded in the script, because a histogram that silently counts the wrong thing is exactly the
kind of result this project has had to correct before.

## The bound on "basic drivers running", stated plainly

`grep -rli "msm8974\|snapdragon\|qualcomm" --include=*.c --include=*.h --include=*.cpp` over the
whole xnu-4570.1.46 tree returns **nothing.** XNU has no Qualcomm code of any kind. What it has for
ARM is `pexpert/arm/` (5 files) and Apple's own platform conventions.

That matters for what the goal asks for, because it splits it:

- **Reaching a first scheduler tick** — the roadmap's T2 — needs a timer, an interrupt controller
  and a console. The first two are what Phase 3's shim provides, and it is verified on hardware
  (`experiment-104`: the timer armed through XNU's own `tbd_ops`, fired, was serviced, disarmed).
  The third does not need a driver: `ram_console` is a legitimate console and the entry image
  already writes through it (`experiment-106`). **T2 does not require any driver XNU lacks.**
- **"把基础驱动跑起来"** — display, eMMC, USB, power — needs drivers that do not exist here and
  are not a build-configuration problem. They would have to be written, against a SoC with no
  vendor documentation, for a kernel that expects Apple's platform. That is a different and much
  larger project than the one that produced everything above.

So the honest split: the *kernel* half of the goal is a large but ordinary build-and-port problem,
partly solved already. The *driver* half is not started and is not reachable by continuing this
work.

## The build configuration was never absent

The same turn's measurement sent me looking for where `CONFIG_ZONE_MAP_MIN` and friends come from —
they were showing up as *undeclared identifiers* in this sweep. They are in the tarball:

| What | Where |
| --- | --- |
| Master configuration | `config/MASTER` (737 lines) |
| ARM master | `config/MASTER.arm` |
| The tool's source | `SETUP/config/{main.c,parser.y,lexer.c,mkheaders.c,mkmakefile.c,…}` |
| The driver | `SETUP/config/doconf` |

`osfmk/conf/MASTER.XXX` — the path `sched_prim.h:574`'s `#error` names, and the one this project
reported as missing for several sessions — is where doconf **writes its output**. The source is at
`config/`. Running doconf's own pipeline gives Apple's ARM kernel attribute sets and, expanded,
the option lines:

```
KERNEL_BASE = [ arm xsmall config_embedded ]
SCHED_BASE  = [ config_sched_traditional config_sched_multiq ]
options   CONFIG_ZONE_MAP_MIN=1048576
options   CONFIG_TASK_MAX=512
options   CONFIG_IPC_TABLE_ENTRIES_STEPS=64
options   CONFIG_MAX_CLUSTERS=4
```

Three of those were on this sweep's "undeclared" list. And `sched_group_t` — the largest single
blocker at 25 occurrences — is gated on `CONFIG_SCHED_MULTIQ`, which `SCHED_BASE` says is on.

That is written up in [`tools/xnu_config/README.md`](../../tools/xnu_config/README.md) and it is a
correction to the roadmap, not just an addition: **"the source tree is complete and the build
configuration is absent" was wrong**, and it had been repeated in commit messages and used to bound
the plan for several turns.

## What is left, and what kind of thing it is

The remaining blockers are no longer headers. They are names gated behind configuration this
project has not chosen — `fmsg`, `mnl_msg_t`, `mach_node_t` (all from `osfmk/kern/mach_node.h` and
`osfmk/ipc/flipc.h`, the multi-node Mach IPC machinery), `sched_group_t`, `UINT`,
`SEMAPHORE_OPTION_NONE`. Each is either a `CONFIG_*` to set or a file to add to the build, and the
method for deciding between those is the one that took `osfmk/arm` from 3 of 32 to 32 of 32.

**What this does not mean.** 81 of 170 parsing is not a kernel. The remaining 89 files include
`ipc` (2 of 20), where the Mach IPC layer lives, and every one of the 170 would still have to link
— the 443 undefined symbols from `experiment-110` are a floor, not a total, until the rest of the
tree is compiled and counted. And none of this changes the device: **XNU does not run**, no OS is
entered, no driver runs.

What has changed is that the measurement now covers the kernel proper rather than one layer of it,
and the shape of the remainder is known: configuration values and files to add, plus a link.
