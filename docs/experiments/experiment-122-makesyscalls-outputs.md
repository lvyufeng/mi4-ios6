# Experiment 122 — `makesyscalls.sh` has six output kinds and this project asked for one

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/gen_bsd_headers.sh`, `tools/xnu_config/list_sources.py`,
`tools/build_xnu_arm_kernel.sh`, `stages/stage90/shims_arm/sys/syscall.h` (deleted)

| Configuration | before (experiment-121) | after |
| --- | --- | --- |
| `STAGE90_BOOT` | 375 of 417 | **381 of 419** |
| `RELEASE` | 496 of 585 | **499 of 587** |

## The finding, which is the third instance of one shape

`bsd/kern/makesyscalls.sh:75` states its own interface:

```
usage: makesyscalls.sh input-file [<names|proto|header|table|audit|trace>]
```

and Apple's Makefiles ask for all of them, into named destinations:

| Kind | Output | Where Apple's rule is |
| --- | --- | --- |
| `header` | `syscall.h` — the `#define SYS_*` numbers | `bsd/sys/Makefile:216` |
| `proto` (default) | `sysproto.h` — the syscall argument structs | `bsd/conf/Makefile.template` |
| `table` | `init_sysent.c` | `bsd/conf/Makefile.template:291` |
| `names` | `syscalls.c` | `bsd/conf/Makefile.template:295` |
| `audit` | `audit_kevents.c` | `bsd/conf/Makefile.template:299` |
| `systrace` | `systrace_args.c` | `bsd/conf/Makefile.template:303` |

`tools/gen_bsd_headers.sh` ran **only `proto`** — because `sysproto.h` was the file the failure
list named, and 55 files include it. That is the same mistake as `gen_mach_headers.sh` asking MIG
for one of its two outputs (experiment-121) and `mkheaders.c`'s `OPTIONS/` headers being written by
hand (experiment-120): **a tool with several output kinds has to be asked for all of them, and the
one named in an error message is not a guide to the set.**

`sys/syscall.h` is where the syscall *numbers* live, and without it:

```
bsd/kern/kern_mman.c:663       use of undeclared identifier 'SYS_mmap'
bsd/kern/sys_generic.c:263     use of undeclared identifier 'SYS_pread'
bsd/kern/kern_guarded.c:801    use of undeclared identifier 'SYS_guarded_pwrite_np'
```

— each in a file that *defines* the routine whose number it is looking up. The script now generates
all six, checks that `syscall.h` actually contains `SYS_*` definitions (415 of them) and that
`sysproto.h` contains argument structs, and fails if either is empty: a header that is well-formed
and defines nothing would otherwise look like a successful run.

## A second defect, in how the manifest resolves build output

The 42 `./` entries in the components' `files` lists come from **three** generators, and
`list_sources.py` resolved all of them against one directory:

```
./mach/task_server.c, ./device/device_server.c, ...    MIG           -> out/mach_headers
./init_sysent.c, ./syscalls.c, ./audit_kevents.c, ...  makesyscalls  -> out/xnu_generated/bsd
./ioconf.c                                             config(8)     -> not generated here
```

`resolve_path` now searches a list of generated roots in order (`--generated-dir`, colon-separated)
instead of one. Before this, `init_sysent.c` and `syscalls.c` were reported as *listed but absent*
while the first of them sat in a directory nobody had generated them into — and after they were
generated, they were still looked for in the MIG root.

**And that fix immediately produced a third.** `build_xnu_arm_kernel.sh`'s `component_of()` maps a
source path to the component whose `Makefile.template` flags it gets, and it defaulted anything
outside the XNU tree to `osfmk`. `out/xnu_generated/bsd/init_sysent.c` compiled as osfmk therefore
picked up `-DMACH_KERNEL_PRIVATE`, which reaches `kern/misc_protos.h`, and died on the
`ffs`/`fls`/`copyinstr` collision that experiment-118 exists to record. The mapping is now explicit
for all three generated roots, and the comment says what the failure looked like.

## A shim deleted, and it had been doing harm

`stages/stage90/shims_arm/sys/syscall.h` declared `unix_syscall` and `mach_syscall` on the premise
that `sys/syscall.h` "does not exist anywhere in the tarball". It exists as the `header` output of a
tool that is in the tarball. Measured with and without it in both configurations: **byte-identical
failure sets**, so it was already dead.

It was not harmless while it was live, though. `bsd/dev/arm/systemcalls.c:82` *defines*
`unix_syscall`, and the shim's prototype was the cause of that file's
`error: conflicting types for 'unix_syscall'` — a hand-written declaration of a function the tree
already defines, in a file that had been on the failure list since before the generated header
existed. `systemcalls.c` compiles now.

## What is left

38 failures in the minimal configuration, 88 in `RELEASE`. By first error they are now small and
scattered: `clock_t`/`uid_t` in `bsd/sys/times.h` and `bsd/sys/kauth.h`, `u_char`/`caddr_t` in
`osfmk/device/subrs.c` and `osfmk/kern/btlog.c`, the `_CLOCK_T` collision in `osfmk/mach/memory_object.h`
(7 files), `z_off_t` in `libkern/zlib/zutil.h` (8 files), and the three BSD-include-order regressions
experiment-121 recorded. No single fix is worth 30 files any more, which is itself the state change:
the big structural gaps are gone.

## How to reproduce

```bash
./tools/gen_bsd_headers.sh                        # 6 generated from syscalls.master (415 SYS_* numbers)

MANIFEST=out/xnu_arm_manifest_min.txt
XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  ./tools/xnu_config/list_sources.py STAGE90_BOOT --write $PWD/$MANIFEST

XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$PWD/$MANIFEST XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
  ./tools/build_xnu_arm_kernel.sh                  # 381 of 419
```
