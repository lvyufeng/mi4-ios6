# Experiment 125 — the first real link, and the 47 files that were compiled as the wrong configuration

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/link_xnu_arm.sh` (new), `tools/gen_option_headers.py`, `tools/build_xnu_arm_kernel.sh`,
`tools/build_xnu_arm_layer.sh`

| Configuration | before (experiment-124) | after |
| --- | --- | --- |
| `STAGE90_BOOT` | 395 of 419 | 395 of 419 |
| `RELEASE` | 513 of 587 | **560 of 587** |

And the first link of the compiled kernel:

```
560 objects → 6 338 748 bytes, 23 043 symbols defined, 0 duplicate definitions
undefined after linking: 898   (9 compiler runtime, 889 a source file must provide)
```

No line of XNU's code was changed.

## 1. The linker found what `nm` cannot: 690 duplicate definitions

Everything reported so far comes from `nm` and from clang, and neither can see two objects defining
the same symbol — both compile fine, and only a link fails. The first `ld -r` over the compiled set
said:

```
bsd_kern_subr_xxx.o: multiple definition of `rc4_init'; bsd_crypto_rc4_rc4.o: first defined here
bsd_net_net_stubs.o: multiple definition of `ctl_register'; bsd_kern_kern_control.o: first defined here
bsd_net_net_stubs.o: multiple definition of `fifo_open'; bsd_miscfs_fifofs_fifo_vnops.o: first defined here
bsd_netinet6_in6_cksum.o: multiple definition of `inet6_cksum'; bsd_kern_kpi_mbuf.o: first defined here
```

`bsd/net/net_stubs.c:31` is `#if !NETWORKING` — it exists to provide panicking stubs in a kernel
built with no networking. `bsd/kern/kern_control.c` is `optional networking`. Both were being
compiled, so `NETWORKING` was false where it should have been true. **And it was false because the
wrong configuration's option headers were on the include path** — see §2.

The same shape three more times: `subr_xxx.c`'s rc4 stubs under `#if !CRYPTO`, `in6_cksum.c` under
`optional inet6`, `fifo_*` beside `net_stubs.c`'s `#if !NETWORKING` copies.

`tools/link_xnu_arm.sh` is the step, and the reason it is a step rather than a one-off: **a compiler
and `nm` answer "does this file parse" and "what does this object want"; only a linker answers "do
these objects fit together".** After the fix the same command reports `0 multiple definition` and
exit 0 — 560 objects that link into one relocatable image.

## 2. The option headers were one directory for two configurations

`out/xnu_options/` held one set of generated `OPTIONS/` headers for every build. They are
**per-configuration**, and RELEASE and STAGE90_BOOT disagree on **20 of them**:

```
CRYPTO  NETWORKING  SOCKETS  DEVFS  FIFO  DUMMYNET  IF_BRIDGE  IF_FAKE  FS_COMPRESSION
CONFIG_FSE  CONFIG_MACF  CONFIG_MACF_SOCKET_SUBSET  ...  (31 on for RELEASE, 11 for STAGE90_BOOT)
```

Whichever generation ran last won for both builds. That was `STAGE90_BOOT` — so the RELEASE build
was compiled with `NETWORKING 0`, `CRYPTO 0`, `SOCKETS 0` and seventeen others. `meta_features.h`
force-includes them, so a generated `#define NETWORKING 0` overrode the command line's
`-DNETWORKING=1` without a word.

`gen_option_headers.py` now writes `out/xnu_options/<CONFIG>/`, and the build fails with a named
error rather than silently using the other one. **`RELEASE` 513 → 560 of 587. No regressions.**

This is the sixth instance of this project's recurring defect and the first where the *mechanism*
was a directory rather than a duplicated literal: one value, two definitions, and the build had to
say which one wins. What made it expensive is where the failure appeared — 400 lines into a link,
naming `bsd/net/net_stubs.c`, a file whose connection to the option set is a single `#if` 30 lines
in.

## 3. And the link reports 889 symbols a source file must provide

`ld -T stages/stage90/xnu_link.ld --no-undefined -e _start` fails, correctly, on:

```
bsd_dev_arm_cons.o: in function `cnopen': undefined reference to `cdevsw'
```

That is the same number the `nm`-based `link_gap.sh` gives within rounding (988 across both
configurations, 889 for RELEASE alone), which is a useful cross-check: two independent methods, one
walking symbols and one being the actual linker.

`boot_closure.py` narrows it further — **19 of the 27 remaining failures are on the boot path**, and
they account for 78 of the missing symbols:

| symbols | file |
| --- | --- |
| 17 | `osfmk/vm/vm_compressor.c` |
| 15 | `libkern/gen/OSAtomicOperations.c` |
| 11 | `bsd/netinet6/nd6.c` |
| 8 | `bsd/kern/bsd_init.c` |
| 7 | `bsd/dev/arm/conf.c` |
| 4 | `osfmk/kern/task.c` |

## What is left

24 failures in the minimal configuration, 27 in `RELEASE` — and the two sets are now nearly the
same files, which is itself a result: the configurations have converged because most of what
separated them was the option-macro defect.

The remaining clusters are small and specific: `clock_t`/`uid_t` in `bsd/sys/times.h` and
`kauth.h`, `u_char`/`caddr_t` in `osfmk/device/subrs.c`, `kern/btlog.c` and `vm_compressor.c`, the
`sync_qos_count_t` include-order question experiment-121 left open, `libkern/gen/OSAtomicOperations.c`
and `bsd/kern/subr_prof.c` (both preprocessor `expected identifier`), and four genuinely absent
headers (`pty.h`, `loop.h`, `compat_43.h`, `sys/modctl.h`).

## How to reproduce

```bash
XNU_KERNEL_CONFIG=RELEASE ./tools/gen_option_headers.py            # out/xnu_options/RELEASE/
XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=... ./tools/gen_option_headers.py
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh          # 560 of 587
./tools/link_xnu_arm.sh                    # 0 duplicate definitions, 898 undefined
./tools/link_xnu_arm.sh --final            # the full link: fails on 889, as it should
./tools/boot_closure.py                    # 19 files / 78 symbols on the boot path
```
