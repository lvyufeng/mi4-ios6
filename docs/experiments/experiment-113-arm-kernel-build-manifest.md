# Experiment 113 — Apple's own build manifest for an ARM kernel, resolved

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/xnu_config/list_sources.py`

## The result

```
$ ./tools/xnu_config/list_sources.py RELEASE --write out/xnu_arm_manifest.txt
wrote 694 path(s) to out/xnu_arm_manifest.txt

RELEASE: 694 file(s) selected for arm
  present on disk:      652
  MIG-generated:        39
  listed but absent:    3
```

**That is the object list of an ARM kernel, from Apple's own manifests** — not from globbing
directories. 591 `.c`, 83 `.cpp`, 20 `.s`, and the three absent files are hand-written ARM assembly
optimisations Apple did not publish (`corecrypto/ccn/src/arm/ccn_set.s`,
`libkern/zlib/arm/{inffastS,adler32vec}.s`).

## Why this is different from the earlier counts

`experiment-110` reported "32 of 32 compile" for `osfmk/arm`. That was every `.c` in the directory.
A real kernel does not build that set: it builds what `osfmk/conf/files.arm` says, filtered by the
configuration. The difference matters in both directions — the manifest excludes files a kernel
would not use, and includes files from outside the directory (the `commpage` subdirectory, the
`kdp/ml/arm` helpers, `kperf/arm`).

So the honest count for "how big is this kernel" is **694 files**, not 32, and the earlier
measurement was of a directory rather than of a build.

## The file lists were in the tarball too

`osfmk/conf/files.arm`, `bsd/conf/files.arm`, `libkern/conf/files.arm`, `iokit/conf/files.arm`,
`pexpert/conf/files.arm` — 80, 17, 2, 2 and 7 entries plus a shared `files` per component. Same
format as the 1980s Berkeley `config`: a path, then `standard` or `optional <flag>…`.

The condition semantics are taken from the reference implementation rather than guessed.
`SETUP/config/mkmakefile.c:416-422` checks each flag in turn and, when one is *not* defined, adds it
to the file's `needs`; the file is emitted only if `needs` ends up empty. So **multiple flags are
AND**, and `optional not <flag>` inverts. Both are implemented in the tool with that line cited.

Two details that would have produced a wrong manifest if assumed:

- **Paths are root-relative, not component-relative.** `osfmk/conf/files.arm` writes
  `osfmk/arm/pmap.c` while `osfmk/conf/files` writes `./gssd/gssd_mach.c`. The first form joins the
  tree root; the `./` form joins the generated-object directory — which is where a MIG
  `*_server.c` belongs. Getting this wrong gave a first run that reported **0 of 272 files
  present**.
- **`OPTIONS/foo` is not a file.** Those entries tell the old config tool to emit `opt_foo.h`, the
  header a source includes to learn whether an option is on. They are dropped rather than reported
  as missing.

## What this closes and what it opens

**Closes:** "what is an ARM XNU kernel made of" is no longer an estimate. It is a list, generated
from Apple's files by Apple's rules under Apple's configuration, and it is reproducible with one
command into `out/xnu_arm_manifest.txt`.

**Opens:** the manifest is the input a build needs, and it makes the next measurement the right one —
not "how many of `osfmk/arm` compile", but **how many of these 694 do**. That is a bigger and more
honest denominator, and it is now one command away.

**Does not:** build or run anything. The device is untouched by all of this, XNU does not run, and
the driver half of the goal remains where it was. What changed is that the kernel half now has a
file list instead of a directory listing, which is the difference between "some of this might
build" and "here is what has to build".
