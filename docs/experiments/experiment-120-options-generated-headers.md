# Experiment 120 — the third kind of generated header: `OPTIONS/`

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/gen_option_headers.py` (new), `tools/gen_libkern_version.sh` (new),
`tools/build_xnu_arm_kernel.sh`, `tools/build_xnu_arm_layer.sh`, ten files deleted from
`stages/stage90/shims_arm/`

| Configuration | before (experiment-119) | after |
| --- | --- | --- |
| `STAGE90_BOOT` | 315 of 405 | **328 of 405** |
| `RELEASE` | 356 of 573 | **368 of 573** |

No line of XNU's code was changed, and ten hand-written shim headers were **deleted**.

## The mechanism, read off the source

`*/conf/files` contains 124 lines of the shape

```
OPTIONS/mach_ipc_debug		optional mach_ipc_debug
```

and `SETUP/config/mkheaders.c` turns each into a one-line header. Every detail matters and each
was read rather than guessed, because the naming is not what it looks like:

- `headers()` (`mkheaders.c:71-79`) walks the file table and, for each entry with a first need,
  calls `do_count(need, need, 1)`.
- `nextopt:` (`mkmakefile.c:366-372`) sets `needs = ns(wd)` from the **first word after
  `optional`** — so the generated file is named after the *option word*, not after the `OPTIONS/`
  name. They differ for `OPTIONS/bridgestp optional bridgestp if_bridge`: the file is
  `bridgestp.h`.
- the pseudo-device for that word is created with `d_slave = 0` and set to `1` if `allCaps(word)` is
  among the configuration's options (`mkmakefile.c:379-400`).
- `do_count` takes `count = d_slave` and, because `d_flags` is set, passes `dev = NULL`;
  `do_header` (`mkheaders.c:114-135`) then writes

  ```c
  fprintf(outf, "#define %s %d\n", name, count);
  ```

  into `path(hname) + ".h"`, with `name` the header name upper-cased. `path()`
  (`main.c:206-216`) is the object directory, which is **flat** — and the include sites confirm it:
  `osfmk/ipc/ipc_hash.h:128` says `#include <mach_ipc_debug.h>`, not `<mach/mach_ipc_debug.h>`.
- the same function appends `#include <name.h>` to `meta_features.h`, which every component
  force-includes (`osfmk/conf/Makefile.template:19` and its seven siblings).

So the generator is 91 one-line headers plus the list, and `tools/gen_option_headers.py` is that
plus the parsing. For `STAGE90_BOOT`: **91 headers, 11 on, 80 off**.

## The tell, and why it was findable earlier

`osfmk/ipc/ipc_hash.h:128` includes `<mach_ipc_debug.h>` with **no `#if` around it**, and the real
content is on the very next line under `#if MACH_IPC_DEBUG`. A header that is included
unconditionally, exists nowhere in the tree, and whose content is guarded on the next line, is a
header whose only job is to define a macro. That shape is the whole diagnostic, and it applies to
nine more names this project had been shimming by hand.

## Ten shims deleted, and the measurement that says it was safe

Nine of the ten OPTIONS-named shims hard-coded a value the configuration already states
(`MACH_ASSERT 0`, `ZONE_DEBUG 0`, `CONFIG_DTRACE 0`, …) and the tenth (`mach_pagemap.h`) was empty.
They were removed and both configurations rebuilt:

```
STAGE90_BOOT   328 of 405 with them, 328 of 405 without   <- identical failure sets, diffed
RELEASE        368 of 573 with them, 368 of 573 without   <- identical failure sets, diffed
```

The diff was of the file lists, not the counts: `comm -3` on `failed.txt` produced no lines in
either configuration. So they were dead the moment the generator existed, and what they still
asserted — *absent from the tarball* — had become false. A shim that says a file is missing when
the build generates it is worse than no file: it is a wrong statement in the repository.

Deleting them took `build_xnu_arm_layer.sh` to **0 of 32** until the generated roots were added to
its include line. That is the same fact from the other side and it is the reason it is worth doing
rather than leaving them: the dependency was real, it was just not this directory's to supply.

## One measurement that did not move, recorded rather than claimed

`meta_features.h` is force-included now, because Apple force-includes it in every component and its
absence is a real deviation: without it, a file that tests `#if KDEBUG` without including
`<kdebug.h>` silently sees *undefined* rather than *0*. **It changed no count** — 325 either way at
the point it was tested. It is kept anyway, on the grounds that a macro with the wrong value is
silent while a missing header is loud, and this project's costliest defects have all been silent
ones. Stated plainly here so it is not read as having bought something.

## `libkern/version.h` is the same idea with a different generator

`osfmk/kern/startup.c:115` includes `<libkern/version.h>`, which does not exist. It is not MIG
output and not an `OPTIONS/` header — it is built from two files that *are* in the tarball, by a
tool that is also in the tarball:

```make
# libkern/libkern/Makefile:79-87
EXPORT_MI_GEN_LIST = version.h
version.h: version.h.template $(SRCROOT)/config/MasterVersion
	install $(DATA_INSTALL_FLAGS) $< $@
	$(NEWVERS) $@ > /dev/null
```

with `NEWVERS = $(SRCROOT)/config/newvers.pl` (`MakeInc.cmd:146`). `tools/gen_libkern_version.sh`
does exactly that: copy the template, run Apple's own `perl` script on it, and fail if any
`###KERNEL_VERSION_*###` placeholder survives. It stamps **7 replacements** from
`config/MasterVersion` (first line `17.0.0`).

One property worth knowing before anyone hashes it: `newvers.pl` also substitutes
`###KERNEL_BUILDER###` and `###KERNEL_BUILD_DATE###`, so **the output is not reproducible**. That is
Apple's behaviour, not something introduced here.

## What is left

77 failures in the minimal configuration, 205 in `RELEASE`. The header list is now short and is
mostly *genuinely* absent rather than generated:

```
libkern/version.h            gone (generated)
mach_ipc_debug.h, mach_vm_debug.h, mach_cluster_stats.h, mach_ipc_test.h,
kdebug.h, vm_cpm.h           gone (generated)
os/firehose_buffer_private.h, sys/modctl.h, pty.h, loop.h, compat_43.h   <- still absent
```

`loop.h`, `pty.h`, `compat_43.h`, `sys/modctl.h` and `os/firehose_buffer_private.h` match no
`OPTIONS/` line and are in no component's `EXPORT_MI_LIST`, so they are a real gap rather than a
build step.

The layer build is unchanged at **32 of 32**, 118,970 bytes of text, with **445** distinct undefined
symbols against 443 before. The two were not identified; they are consistent with the option macros
now carrying their configured values (`MACH_DEBUG=1`, `MACH_IPC_DEBUG=1`, `MACH_KDP=1`) instead of
being undefined, but that is a hypothesis and it is recorded as one.

## How to reproduce

```bash
XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  ./tools/gen_option_headers.py          # 91 headers, 11 on, 80 off -> out/xnu_options/
./tools/gen_libkern_version.sh           # 17.0.0, 7 replacements -> out/xnu_generated/libkern/

MANIFEST=out/xnu_arm_manifest_min.txt
XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  ./tools/xnu_config/list_sources.py STAGE90_BOOT --write $PWD/$MANIFEST

XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$PWD/$MANIFEST XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
  ./tools/build_xnu_arm_kernel.sh        # 328 of 405
```

`tools/gen_option_headers.py` and `tools/build_xnu_arm_layer.sh` must be regenerated and rebuilt
together with `build_xnu_arm_kernel.sh`: the layer build reads the same two generated roots, so a
stale `out/xnu_options/` compiles one configuration against another's macros.
