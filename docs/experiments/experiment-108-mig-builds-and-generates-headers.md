# Experiment 108 — MIG builds on this host, and 23 Mach interface headers are generated

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/build_mig.sh`, `tools/gen_mach_headers.sh`, `tools/mig_host/`, one more shim

## The result

Phase 4's wall was recorded in `experiment-107` as **MIG-generated headers** — 40 `.defs` files
ship under `osfmk/mach/` and their generated headers do not, because Apple's build makes them. The
note said MIG "is not in the tarball and not in this host's package repository", which is true, and
then drew the wrong conclusion from it.

**MIG is published and fetchable: `apple-oss-distributions/bootstrap_cmds`, directory
`migcom.tproj`** — the real thing, `parser.y` + `lexxer.l` + ~550 KB of C, not a reimplementation.
It now builds and runs on this host:

```
$ ./tools/build_mig.sh
== fetching bootstrap_cmds (migcom.tproj) ==
== generating the parser and lexer ==
== compiling ==
migcom: /mnt/data/mi4-ios6/out/mig/build/migcom

$ ./tools/gen_mach_headers.sh
  mach_host                      29621 bytes
  mach_port                      41536 bytes
  mach_vm, task, thread_act, vm_map, host_priv, host_security, lock_set, clock, clock_priv,
  exc, mach_exc, mach_notify, processor, processor_set, semaphore, memory_object_default,
  notify, mach_voucher_attr_control, audit_triggers, coalition_notification, ktrace_background

generated 23, failed 2, absent-from-tarball 0
```

`mach_host.h` and `mach_port.h` are the two that were blocking `arm_init.c`, and they are the two
that exist now. The output is recognisably a Darwin MIG header — guard, `__MigTypeCheck` blocks,
the voucher boilerplate — generated from 4570's *own* `.defs`, so the content is 4570's.

## What building MIG actually took

Three things, each of which would have looked like a dead end on its own:

1. **A host include set for the mach headers.** MIG is a host program that reads Darwin headers, and
   compiling those against glibc collides: Darwin's `sys/cdefs.h` and glibc's disagree about
   `__THROW` and a dozen neighbouring macros, and `bsd/sys/types.h` drags in a `_pthread` directory
   that is not in the tarball. `tools/mig_host/` carries eleven small headers with every constant
   copied from the 4570 tree rather than invented.
2. **`#ifdef linux` in `type.h`.** MIG's own `type.h` does
   `#ifdef linux / #include <linux/types.h> / #else / <sys/types.h>`, so on this host it takes a
   branch the Darwin build never takes, and glibc's `linux/types.h` does not define the BSD `u_int`
   that the next line uses. The failure is `u_int unknown` immediately after an include that looks
   like it should provide it — which is why it is worth recording. `tools/mig_host/linux/types.h`.
3. **MIG reads its input on stdin, preprocessed.** It takes no filename; a bare path is
   `fatal: bad argument`. Apple's `mig.sh` does `cat file | cpp | migcom flags`, and that pipeline
   is what `gen_mach_headers.sh` reproduces.

`handler.c` is excluded from the build, deliberately and with a comment: it carries a second, older
`WriteIncludes(FILE *)` conflicting with `write.h`'s three-argument one — legacy code in Apple's
tree that nothing references. Excluding the file removes the conflict without editing Apple's
source.

## The measurement is now complete for the first time

With `mach_pagemap.h` shimmed (see below), the error count stopped being a floor:

```
  arm_init.c                   35 error(s)
  arm_vm_init.c                29 error(s)
```

No `FATAL` marker — `-ferror-limit=0` plus no missing headers means these are **complete counts**,
and they are the first ones this project has had. The number is *higher* than `experiment-107`'s 20
for the same reason: that 20 was truncated by a fatal missing include.

`mach_pagemap.h` is the one header here that could not be generated: no `mach_pagemap.defs` exists
anywhere in the tarball, so unlike `mach_host.h` there is nothing to run MIG on. It is shimmed
empty, after checking rather than assuming — `pagemap` appears exactly once in `vm_object.h`, in
the include line itself, and nothing on the entry path uses a symbol from it.

## What this changes for Phase 4

The wall as described was "40 `.defs` files ship and their generated headers do not". That is now
**23 generated, 2 failed, 0 absent** — and the two failures are `upl` and `memory_object_control`,
which need `upl_t`/`upl_size_t` declared before MIG sees them (a `.defs` import-order problem, not a
missing generator).

So the honest position: **the largest named component of Phase 4's wall is no longer a wall.** What
remains of it is 35 real errors in `arm_init.c` — include ordering, `CONFIG_*` values, and
`MASTER.XXX` — all of which are *choosing values*, not obtaining tools.

That does not make XNU run. `arm_init.c` compiling is the first step of a much longer path, and the
35 is a floor for the *file*, not for the kernel. But the difference between "the generator does not
exist here" and "the generator is a command" is the difference between a wall and a hill.

## What was added

- `tools/build_mig.sh` — fetches `bootstrap_cmds`, generates the parser and lexer, builds `migcom`.
  Reproducible from a clean `out/`; every flag and exclusion carries its reason.
- `tools/gen_mach_headers.sh` — runs MIG over the `.defs` set the entry path reaches, and checks
  each header is non-empty (a MIG that exits 0 without writing anything would look like success).
- `tools/mig_host/` — the host-side mach and libc header set MIG needs.
- `shims_arm/mach_pagemap.h` — empty, with the check that justifies it recorded.
