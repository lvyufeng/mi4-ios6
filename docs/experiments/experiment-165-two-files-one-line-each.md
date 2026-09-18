# Experiment 165 — two files, one line each: the C++ block is complete

Date: 2026-09-18
Host only — nothing here runs on the device.
Artifacts: `tools/build_xnu_arm_kernel.sh` (two new per-file switches),
`stages/stage90/shims/kext_request_c.h` (new)

Two files were failing to compile for one cause each, and both causes are things Apple's build
supplies and a two-configuration rebuild of 4570 does not. Between them they close thirteen symbols
and open one, and they take the C++ block from *almost* complete to complete in both configurations.

| `STAGE90_BOOT` | before | after |
| --- | --- | --- |
| C | 419 of 426 | **420 of 426** |
| C++ | 82 of 83 | **83 of 83** |
| objects in the measurement link | 501 compiled + 17 assembled | 503 + 17 |
| undefined, measurement link | 114 | **102** |
| boot-path stubs, from `arm_init` | 11 of 114 | **9 of 102** |
| — `stub_blockers.py`: a file that fails to compile | 2 | **0** |
| — : a file not in the manifest | 5 | 5 |
| — : no source in the tree | 4 | 4 |
| `.text` in the measurement image | 2835728 bytes | 2840256 bytes |
| `.data` / `.bss` | 125400 / 242680 | 125408 / 242744 |

| `RELEASE` | before | after |
| --- | --- | --- |
| C | 612 of 615 | 612 of 615 |
| C++ | 82 of 83 | **83 of 83** |
| undefined, measurement link | 63 | **56** |
| boot-path stubs, from `arm_init` | 9 of 63 | **8 of 56** |
| `.text` in the measurement image | (not re-measured) | 4911440 bytes |

No line of XNU's source was changed, no configuration option was turned on, and no compiler flag
was added to every file: each of the two changes is scoped to the one file that needs it.

## The last `.cpp` was a declaration that disagreed with itself

`libkern/OSKextLib.cpp` was the one C++ file of the 83 that failed, with three errors:

```
libkern/OSKextLib.cpp:190:15: error: declaration of 'kext_request' has a different language linkage
libkern/OSKextLib.cpp:280:30: error: 'loadFromMkext' is a private member of 'OSKext'
libkern/OSKextLib.cpp:291:30: error: 'handleRequest' is a private member of 'OSKext'
```

`kext_request` is a `friend` of `OSKext` — `libkern/libkern/c++/OSKext.h:189`, inside
`#ifdef XNU_KERNEL_PRIVATE` — and `OSKextLib.cpp:190` defines it inside the `extern "C" {` block that
opens at `:39`. Those are the same function. But a function's linkage is fixed by its **first**
declaration ([dcl.link]), and in the file the friend declaration is read first, so clang gives the
friend C++ linkage and the definition C linkage. The second two errors are the consequence, not
separate defects: a `friend` declaration grants access to *that function*, so a `kext_request` clang
does not believe is the friend has no access to `OSKext`'s private statics either.

The fix is `stages/stage90/shims/kext_request_c.h`, force-included into that file and no other:

```c
extern "C" {
kern_return_t kext_request(
    host_priv_t                             hostPriv,
    uint32_t                                clientLogSpec,
    ...);
}
```

It declares nothing new — it is Apple's own declaration, spelled out in full rather than abbreviated
through a `typedef`, so that if either copy of the signature moves the result is a "conflicting
types" error and not a second silent disagreement. That is the whole point: this defect is a member
of the family in [[mi4-one-value-two-definitions]] — one function with two spellings — and the shim
exists to make the two spellings meet *at compile time*.

Closed by this fix, 7 symbols: `kext_request`, `kext_dump_panic_lists`, `kext_get_vm_map`,
`kmod_dump_log`, `OSKextKextForAddress`, `OSKextLoadedKextSummariesUpdated`,
`OSKextRemoveKextBootstrap`.

## `in_pcb.h` is not self-contained, and 4570 gets away with it

`bsd/dev/unix_startup.c` includes `<netinet/tcp_var.h>`, which includes `<netinet/in_pcb.h>`, which
uses `struct in_addr`, `struct in6_addr`, `struct route` and `struct sockaddr_in`
at `:114,176,185,295` and includes `<netinet/in.h>`, `<netinet6/in6.h>` and `<net/route.h>` nowhere.
Measured error, one line:

```
netinet/in_pcb.h:114:17: error: field has incomplete type 'struct in_addr'
```

It does not have to be self-contained, because Apple's configurations all set `IPSEC=1`, and
`in_pcb.h:84`'s `#if IPSEC` then pulls `<netinet6/ipsec.h>` → `<net/if.h>` → `<net/if_var.h>` →
**`<net/route.h>`** → `<net/radix.h>` → `<net/if_llatbl.h>` → `<netinet/in.h>`. `STAGE90_BOOT` is
the configuration with `IPSEC` off, so the chain is cut at its first link.

Apple later fixed this in the header rather than in the configuration — upstream `main`'s `in_pcb.h`
opens with `#include <netinet/in.h>` and `#include <sys/socketvar.h>` (lines 74–75) — which is the
same fix in the other file, and not one this project can make.

So `-include net/route.h`, for `bsd/dev/unix_startup.c` only. The narrowness is deliberate: three
other files in the same manifest also reach `in_pcb.h` (`kern_malloc.c`, `sys_generic.c`,
`audit_syscalls.c`) and all three compile today because their own closure reaches `net/route.h`
anyway (`kern_malloc.c` through `<sys/kpi_mbuf.h>`). Those three are the control that says the
header is the missing piece rather than a symptom — and that a global `-include net/route.h` would
be the broad-include-path mistake this build has already paid for four times (see the comment at
`tools/build_xnu_arm_kernel.sh`, `INCLUDES`).

Closed by this fix, 6 symbols: `bsd_scale_setup`, `max_nbuf_headers`, `nbuf_headers`,
`nbuf_hashelements`, `niobuf_headers`, `serverperfmode`.

## Thirteen closed, one opened

The two fixes together take `STAGE90_BOOT` from 114 to 102, and the arithmetic is worth stating
because it is not 114 − 13:

```
  13 closed   kext_request kext_dump_panic_lists kext_get_vm_map kmod_dump_log
              OSKextKextForAddress OSKextLoadedKextSummariesUpdated OSKextRemoveKextBootstrap
              bsd_scale_setup max_nbuf_headers nbuf_headers nbuf_hashelements
              niobuf_headers serverperfmode
   1 opened   bsd_exec_setup
```

`bsd_exec_setup` is opened by the *first* fix, not broken by it: `unix_startup.c` now compiles
further and its object defines more, and one of the functions it now contains calls `bsd_exec_setup`,
which is defined in `bsd/kern/bsd_init.c` — a file that still fails to compile. Closing a file can
open a symbol, and the direction of that is always "into a file that does not compile yet".

## What this does not settle

**The boot path no longer hits a compile failure.** `stub_blockers.py --min` now reports two
categories and no third:

```
   5  a file not in the manifest     MD5Init/MD5Update/MD5Final (7 edges), __nosan_bzero (7),
                                     chudxnu_thread_get_callstack64_kperf (9)
   4  no source in the tree          __firehose_buffer_create (4), ..._tracepoint_flush (5),
                                     ..._tracepoint_reserve (5), mach_msg_destroy_from_kernel (10)
```

That is a change of kind, not only of size: the nearest remaining blocker is four edges in and is a
missing component, not a broken file. And the `__nosan_bzero` row is a misattribution worth
recording — the symbol is *not* missing from the tarball, and the file that declares it is not
really "not in the manifest":

- `osfmk/kern/zalloc.c:561` calls it with **no declaration visible at all** — 0 occurrences of
  `san/memintrinsics` or `libsa/string` in the preprocessed translation unit — so clang emits an
  implicit-declaration external reference. Its only definition in the tree is `static inline` at
  `san/memintrinsics.h:40`.
- That header is reached through `osfmk/libsa/string.h:97-99`, `#ifdef PRIVATE → #include
  <san/memintrinsics.h>`. Nothing in `osfmk`, `bsd`, `libkern`, `iokit`, `pexpert` or `security`
  includes `osfmk/libsa/string.h` — because in Apple's build *it is* the kernel's `<string.h>`.
- In this build `<string.h>` resolves to `stages/stage90/shims_arm/string.h`, our own replacement,
  which declares the same functions and silently does not carry the `memintrinsics` block.

So this one is the project's own shim, and the fix is to give the shim back the block the header it
replaces had — not to add `san` to the manifest.

**Four of the six remaining `STAGE90_BOOT` failures are also one cause**, and it is on this side of
the line: `bsd/dev/arm/conf.c`, `bsd/kern/tty_ptmx.c`, `bsd/kern/tty_pty.c` and `bsd/kern/bsd_init.c`
all include a device header that config(8) generates, and the generated directory exists for only
one of the two configurations:

```
$ ls out/xnu_device/
RELEASE
$ ls out/xnu_device/RELEASE/
bpfilter.h  loop.h  ptmx.h  pty.h
```

`conf.c:111` is `#include <pty.h>`, and `tty_ptmx.c:67` and `tty_pty.c:67` are the same line; with no
`pty.h` in the include path clang falls through to the **host's** `/usr/include/pty.h`, and the file
dies in glibc rather than in XNU:

```
In file included from bsd/dev/arm/conf.c:111:
In file included from /usr/include/pty.h:22:
In file included from /usr/include/features.h:392:
/usr/include/features-time64.h:20:10: fatal error: 'bits/wordsize.h' file not found
```

`bsd_init.c:875` is the same shape with `loop.h`. The other two failures are unrelated and are the
ones the two configurations share: `bsd/kern/subr_prof.c` (`error: unknown type name 'STATIC'`,
line 160 — `STATIC` comes from `GPROF`) and `osfmk/kperf/kperfbsd.c` (`conflicting types for 'ffs'`,
the `MACH_KERNEL_PRIVATE`-per-component set). `RELEASE` fails on those two plus `bsd/net/if_bridge.c`
(`use of undeclared identifier 'DLT_EN10MB'`).

`bsd_init.c` also has a second, independent error that only `STAGE90_BOOT` sees —
`bsd/netinet/mptcp_var.h:465: no member named 't_mptcb' in 'struct tcpcb'` — which is the
experiment-164 shape again (an option off in one configuration, its field guarded by a different
spelling at the definition and the use), and is not addressed here.

## Reproduce

```bash
# the two changes
grep -n 'ROUTE_FORCE\|KEXT_FORCE' tools/build_xnu_arm_kernel.sh
sed -n '1,46p' stages/stage90/shims/kext_request_c.h

# the measurement, minimal
XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$PWD/out/xnu_arm_manifest_min.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
  ./tools/build_xnu_arm_kernel.sh          # C 420 of 426, C++ 83 of 83
./tools/measure_link.sh --min --keep-stubs # 102 undefined
./tools/stub_reach.py --min --from arm_init --list 40   # 9 of 102
./tools/stub_blockers.py --min                          # 5 / 4, no compile-failure row

# the measurement, RELEASE
./tools/build_xnu_arm_kernel.sh            # C 612 of 615, C++ 83 of 83
./tools/measure_link.sh --keep-stubs       # 56 undefined
./tools/stub_reach.py --from arm_init --list 20         # 8 of 56

# the chain behind the 6 closed symbols, and the one that is still latent upstream
sed -n '84,86p' external/xnu-4570.1.46/bsd/netinet/in_pcb.h
curl -sSfL https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/netinet/in_pcb.h \
  | sed -n '72,76p'

# why __nosan_bzero is undefined although the header exists
sed -n '96,100p' external/xnu-4570.1.46/osfmk/libsa/string.h
```
