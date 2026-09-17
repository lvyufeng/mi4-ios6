# Experiment 114 — the kernel's own file list, compiled: 172 of 569

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/build_xnu_arm_kernel.sh`, `tools/gen_bsd_headers.sh`

## The result

```
$ ./tools/build_xnu_arm_kernel.sh
== Apple's ARM RELEASE manifest, compiled ==
  C files tried:        569
  compile:              172
  fail:                 397
  absent from tarball:  25
  skipped (.s, .cpp):   100
```

**172 of 569 (30%) of the C files in Apple's own ARM RELEASE manifest compile.** This is the first
measurement against the *right* denominator — `experiment-113`'s manifest of the 694 files
`*/conf/files` selects — rather than against a directory listing.

## Where it succeeds and where it does not

| Component | Failed | Note |
| --- | --- | --- |
| `osfmk/arm` | **0** | every file in the ARM bring-up layer compiles (confirms `experiment-110`) |
| `iokit` | 1 of 62 | essentially clean |
| `pexpert` | 4 of 9 | |
| `libkern` | 32 of 70 | |
| `osfmk` | 77 of 230 | the pmap, IPC and scheduler-adjacent files |
| `bsd` | **288 of 293** | almost nothing |

That split is the finding. **The Mach side is largely working and the BSD side is almost entirely
blocked** — `bsd/kern`, `bsd/net`, `bsd/netinet`, `bsd/netinet6` and `bsd/vfs` account for 236, 90,
86, 78 and 42 of the failing files.

Two things about that:

- **It is not simply "more of the same work".** The one header that dominated the BSD failures,
  `<sys/sysproto.h>`, turned out to be *generatable*: `bsd/kern/makesyscalls.sh` produces it from
  `bsd/kern/syscalls.master`, and `bsd/conf/Makefile.template:289` shows how it is invoked.
  `tools/gen_bsd_headers.sh` now runs Apple's own generator unmodified. Its 55 missing-header
  errors are gone — and the pass count did not move, because those files have other problems
  behind it. Worth stating plainly: a fix that removes the *reported* blocker and changes nothing
  is exactly the kind of progress that has to be measured rather than assumed.
- **The remaining blockers are `fq`, `pf_status`, `tcp_now`, `rnh_lock`, `sysctl__net_inet_tcp_children`,
  `M_SECA`, `PF_DEBUG_MISC`** — the packet-filter, TCP and MAC-network internals. Those are
  configuration and subsystem depth, not missing files.

## What this means for the goal, and it is a decision rather than a status

The kernel half of the goal now has a concrete shape:

- **The bring-up path compiles.** `osfmk/arm` is 32 of 32, and that is `arm_init`, `arm_vm_init`,
  `pmap`, `machine_routines`, `locks_arm`, `trap` — the files between `_start` and a first
  scheduler tick.
- **A boot to that tick does not need the network stack.** `bsd/net`, `bsd/netinet` and
  `bsd/netinet6` are 254 of the 397 failures. Whether a kernel that only needs to reach
  `machine_startup` and print should be built with `RELEASE`'s networking attributes at all is an
  open question this project has not asked — the configuration names are Apple's, and
  `config/MASTER.arm` may well support a smaller attribute set.
- **The BSD layer is where the remaining kernel work is concentrated**, and it is the largest single
  piece by file count.

So the honest position is unchanged in substance and sharper in shape: **XNU does not run**, no OS
is entered, no driver runs — and the *kernel* half is now 172-of-569 measurable with the bring-up
path complete, while the *driver* half remains what I said two turns ago: not started, and not
reachable by continuing this work.
