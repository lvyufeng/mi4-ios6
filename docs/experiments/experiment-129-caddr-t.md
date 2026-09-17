# Experiment 129 — `caddr_t`, and the biggest boot-path file

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/build_xnu_arm_kernel.sh`

| Configuration | before (experiment-128) | after |
| --- | --- | --- |
| `STAGE90_BOOT` | 401 of 420 | **402 of 420** |
| `RELEASE` | 591 of 609 | **592 of 609** |
| undefined after linking | 499 | **454** |
| …on the boot path | 460 | **421** |

## The finding

`osfmk/vm/vm_compressor.c` was the largest single item on the boot path — 17 symbols — and its
failure is one line:

```
osfmk/vm/vm_compressor.c:226:1: error: unknown type name 'caddr_t'
caddr_t         c_segments_next_page;
```

`caddr_t` is defined in exactly one place in the tree, `bsd/sys/_types/_caddr_t.h:30`, and
`vm_compressor.c` reaches **no header that includes it**. Verified by preprocessing: the file's
include closure contains zero occurrences of `caddr_t`'s typedef, while `osfmk/device/subrs.c`'s
contains one — the difference being that `subrs.c` reaches `osfmk/libsa/types.h` through
`<libsa/stdlib.h>` and `vm_compressor.c` reaches nothing.

That is the same shape as `u_int`, which this build has force-included since the ARM layer first
compiled (`kern/sched.h` uses it and includes no header that defines it). The narrow header —
`_caddr_t.h` defines `caddr_t` and nothing else — takes `RELEASE` from 591 to 592 with **no
regressions**, and the minimal configuration from 401 to 402.

What is *not* established, and is recorded rather than implied: **how Apple's build reached it.**
The plausible answer is the BSD `<sys/types.h>` chain, but forcing that header in would reintroduce
the `clock_t` collision experiment-118 fixed, so it is not the mechanism to copy. The measurement
that justifies the change is the narrow one; the mechanism question is left open.

## And the link is 45 symbols closer

```
objects:                  592
duplicate definitions:    0
undefined after linking:  454   (9 compiler runtime, 445 a source file must provide)
```

The boot closure is now **11 files / 42 symbols**, from 12 / 60. `vm_compressor.c` has left it
entirely. What remains, in order:

| symbols | file | first error |
| --- | --- | --- |
| 15 | `libkern/gen/OSAtomicOperations.c` | `expected identifier` at the `enum { false = 0, true = 1 };` |
| 7 | `bsd/dev/arm/conf.c` | `pty.h` not found |
| 6 | `bsd/kern/bsd_init.c` | `loop.h` — now generated, so a different error |
| 4 | `bsd/kern/subr_log.c` | `os/firehose_buffer_private.h` not found |
| 3 | `libkern/os/log.c` | the same header |
| 3 | `osfmk/kern/task.c` | `task_collect_crash_info` conflicting types |
| 2 | `bsd/kern/uipc_mbuf.c` | `sync_qos_count_t` include order |
| 2 | `osfmk/vm/vm_object.c` | the `const` member — **not fixable here** (experiment-127) |

The first two are worth a stage each and both are identified: `OSAtomicOperations.c` defines
`enum { false = 0, true = 1 }` and something in its closure now defines them first (C11 `_Bool`'s
`<stdbool.h>`, or a MIG header), and `pty.h` is a header Apple's kernel configuration generates for
a `pseudo-device pty` the tarball does not publish — the same shape as `loop.h`, which
`tools/gen_device_headers.sh` already handles.

## How to reproduce

```bash
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh                 # 592 of 609
./tools/link_xnu_arm.sh                           # 0 duplicates, 454 undefined
./tools/boot_closure.py                           # 11 files / 42 symbols on the boot path
```
