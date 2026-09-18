# Experiment 256 — the firehose port opens, and its first missing header is from a newer XNU

Date: 2026-09-18
Host only — nothing here runs on the device. No image changed.
Artifacts: `stages/stage90/firehose/` (5 files + a README, all new)

## What was tried

Experiment 255's stop is `__firehose_buffer_create`, called from `oslog_init`, and experiment 162 had
already established that its implementation is not published under `xnu` in any release while
`libdispatch/src/firehose/firehose_buffer.c` contains a `#ifdef KERNEL` half written for exactly this.
So the next measurement is the one experiment 162 named as next:

> Whether it compiles for `armv7-unknown-netbsd-eabi` freestanding is the next measurement, and the
> answer is allowed to be no.

The source (46930 bytes) and its four headers are now in `stages/stage90/firehose/`, unmodified, with
Apple's Apache-2.0 header intact. The first compile is:

```bash
clang -target armv7-unknown-netbsd-eabi -mcpu=cortex-a15 -marm -ffreestanding -fno-builtin \
  -fno-common -fno-pic -O2 -std=gnu11 \
  -DARMA7=1 -D__APPLE__=1 -DKERNEL=1 -DKERNEL_PRIVATE=1 -DMACH_KERNEL=1 -DMACH_KERNEL_PRIVATE=1 \
  -DXNU_KERNEL_PRIVATE=1 -DMACH_BSD=1 -DPRIVATE=1 -D__arm__=1 -DCONFIG_EMBEDDED=1 \
  -DDISPATCH_USE_DTRACE=0 -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 \
  -I out/xnu_generated -I out/xnu_generated/bsd -I out/mach_headers -I out/xnu_options/STAGE90_BOOT \
  -I external/xnu-4570.1.46/{osfmk,iokit,bsd,libkern,pexpert,EXTERNAL_HEADERS} \
  -I external/xnu-4570.1.46/osfmk/arm -I external/xnu-4570.1.46/bsd/arm -I external/xnu-4570.1.46 \
  -I stages/stage90/shims -I stages/stage90/shims/kern -I stages/stage90/shims/mach \
  -I stages/stage90/shims_arm -I stages/stage90/shims_arm/{kern,mach,sys,sys/_pthread} \
  -I external/xnu-4570.1.46/libkern/firehose -I stages/stage90/firehose \
  -c stages/stage90/firehose/firehose_buffer.c -o /tmp/fh.o
```

## The result: one error, and it is decisive

```
stages/stage90/firehose/firehose_buffer.c:21:10: fatal error: 'os/atomic_private.h' file not found
#include <os/atomic_private.h>
         ^~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

`os/atomic_private.h` is the file the whole port turns on. Its provenance is worth recording exactly,
because "looked in one directory" is this project's most repeated mistake (memory:
`mi4-not-absent-its-build-output`) and this is the second time the firehose has produced it:

| where it was looked for | answer |
| --- | --- |
| `external/xnu-4570.1.46/libkern/os/` (the tree this project builds) | **no** — it ships `base.h`, `firehose.h`, `log*.h`, `log_encode*`, `object.c`, `internal.c`, `Makefile` |
| `apple-oss-distributions/libplatform`, `private/os/` | **no** — `alloc_once_impl.h`, `apt_private.h`, `crashlog_private.h`, `lock_private.h`, `log_simple_private*`, `once_private.h`, `script_config_private.h`, `security_config_private.h`, `semaphore_private.h`; the repo has no top-level `os/` at all |
| `apple-oss-distributions/libdispatch` at `rel/libdispatch-913`, `os/` | **no** — `firehose_buffer_private.h`, `firehose_server_private.h`, `object*.h`, `voucher*.h`, `linux_base.h` |
| `apple-oss-distributions/xnu` at `main`, `libkern/os/` | **yes** — `atomic.h`, `atomic_private.h`, `atomic_private_arch.h`, `atomic_private_impl.h`, `base.h`, `base_private.h`, `cpp_util.h`, `hash.h`, … |

So the port's dependency is a header that **exists in a much newer XNU than the one this project
builds** (4570.1.46 is the 10.13-era tree; `libkern/os/atomic_private.h` appears in the modern tree
alongside `log_queue.c`, `log_mem.c` and `cpp_util.h`, none of which our tree has). The port is
therefore not "compile one file" but "supply the newer tree's small `os/` atomics surface to a
10.13-era kernel".

That is a bounded, checkable thing — the previous experiment's own build recipe names the rest of the
dependency set, because the real firehose target links against them:

```
target_link_libraries(libfirehose_kernel PRIVATE xnu_private_headers xnu_kernel_headers
                      libplatform_headers libplatform_private_headers
                      pthread_common_headers pthread_common_private_headers AvailabilityHeaders)
target_compile_definitions(libfirehose_kernel PRIVATE
    KERNEL=1 DISPATCH_USE_DTRACE=0 OS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1
    OS_ATOMIC_CONFIG_STARVATION_FREE_ONLY=0)
```

so the surface is: `os/atomic*.h` (+ `os/base*.h`, `os/cpp_util.h` as they pull in), the libplatform
`os/` set, `pthread` private headers, and `Availability` — all of which this project already has shims
for in `stages/stage90/shims_arm/` (`sys/_pthread/` is on the include path in every build here).

## What is next, and what the answer may be

The next measurement is to bring that `os/` atomics surface in — from the newer tree's
`libkern/os/` at a pinned release, as port material under `stages/stage90/firehose/os/` — and compile
again. Two outcomes, both informative:

- **It compiles.** Then the object goes into `build_xnu_arm_kernel.sh` the way `RUNTIME_SOURCES`
  (`xnu_aeabi_runtime.c`) already does, and experiment 257 links it into the entry image and runs.
  That is a *port*, which is the same kind of step `xnu_aeabi_runtime.c` was: four symbols Apple's
  tree never mentions, compiled with the loop's own flags.
- **It does not.** Then the number of missing headers is itself the measurement, and the decision
  becomes whether the kernel needs a firehose at all to reach the OS — `oslog_init`'s caller only
  fails to boot if the return value is used as a pointer it cannot write to, and experiment 162
  already recorded that `__firehose_allocate` (`libkern/os/log.c:580`) is the only consumer and has
  no caller in the tree.

The stop at 255 is unchanged either way: the entry image still stops at
`stub_hit=__firehose_buffer_create`, `oslog_init+0x70`.

## Reproduce

```bash
ls stages/stage90/firehose/
head -3 stages/stage90/firehose/firehose_buffer.c          # Apache-2.0, Apple 2015
grep -n 'include <' stages/stage90/firehose/firehose_buffer.c | head -12

# the one error
clang -target armv7-unknown-netbsd-eabi -mcpu=cortex-a15 -marm -ffreestanding -O2 -std=gnu11 \
  -DKERNEL=1 -DKERNEL_PRIVATE=1 -DMACH_KERNEL=1 -DXNU_KERNEL_PRIVATE=1 -D__APPLE__=1 -DARMA7=1 \
  -I external/xnu-4570.1.46/libkern/firehose -I stages/stage90/firehose \
  -c stages/stage90/firehose/firehose_buffer.c -o /tmp/fh.o

# where the header is NOT, and where it is
find external/xnu-4570.1.46 -name 'atomic_private.h'
curl -sS https://api.github.com/repos/apple-oss-distributions/libplatform/contents/private/os | grep '"name"'
curl -sS https://api.github.com/repos/apple-oss-distributions/libdispatch/contents/os?ref=rel/libdispatch-913 | grep '"name"'
curl -sS https://api.github.com/repos/apple-oss-distributions/xnu/contents/libkern/os?ref=main | grep '"name"'

# the real target's dependency set
find /tmp/fh -name CMakeLists.txt -exec cat {} \;

# the seam this port is for
grep -n '__firehose_buffer_create' external/xnu-4570.1.46/bsd/kern/subr_log.c
grep -n 'KERNELFILES' external/xnu-4570.1.46/libkern/firehose/Makefile
```
