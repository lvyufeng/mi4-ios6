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
