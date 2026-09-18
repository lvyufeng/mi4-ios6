# Experiment 156 — the MIG run was missing `-DKERNEL`, and that was 13 symbols *and* every routine name

Date: 2026-09-18
Host only — nothing here runs on the device.
Artifacts: `tools/gen_mach_headers.sh` (one Apple define list instead of four hand-copied ones)

| | before | after |
| --- | --- | --- |
| generated MIG files that differ | — | **32 of 260** (8 preprocessed inputs, 13 headers, 1 generated `.c`, 10 build-dir files) |
| `RELEASE`, C | 608 of 615 | **608 of 615** |
| `RELEASE`, C++ | 75 of 83 | **78 of 83** |
| `STAGE90_BOOT`, C | 414 of 426 | **414 of 426** |
| `STAGE90_BOOT`, C++ | 75 of 83 | **78 of 83** |
| undefined symbols in the image, `RELEASE` | 448 | **189** |
| undefined symbols in the image, `STAGE90_BOOT` | 586 | **330** |
| boot-path stubs, `RELEASE` | 46 | **42** |
| boot-path stubs, `STAGE90_BOOT` | 53 | **50** |
| objects, `RELEASE` | 683 + 17 | **686 + 17** |
| objects, `STAGE90_BOOT` | 489 + 17 | **492 + 17** |

Nothing in XNU's source changed, both configurations still build to exit 0, the payload build is
byte-identical (`stage90.bin` sha256 `7994f0ce…`), and the tree is clean after every run.

## The fix is one word

`gen_mach_headers.sh` preprocesses each `.defs` before handing it to MIG, and it did so with a
hand-written flag list. Apple's list is `$(DEFINES)`, and `MIGFLAGS` starts with it
(`makedefs/MakeInc.def:470`):

```make
DEFINES = -DAPPLE -DKERNEL -DKERNEL_PRIVATE -DXNU_KERNEL_PRIVATE \
          -DPRIVATE -D__MACHO__=1 -Dvolatile=__volatile $(CONFIG_DEFINES) $(SEED_DEFINES)
```

**`-DKERNEL` was not in the script.** `osfmk/mach/vm_map.defs:75-79` is where that matters:

```c
#define CONCAT(a,b) a ## b
#if !KERNEL && !LIBSYSCALL_INTERFACE
#define PREFIX(NAME) CONCAT(_kernelrpc_, NAME)
#else
#define PREFIX(NAME) NAME
#endif
```

and then, three times in the same file (`:116`, `:132`, `:153`):

```c
#if !KERNEL && !LIBSYSCALL_INTERFACE
skip;
#else
routine PREFIX(vm_deallocate)(target_task : vm_task_entry_t; ...);
#endif
```

Without `-DKERNEL`, both branches were taking the **userspace** side. So the kernel's own
`<mach/vm_map.h>` skipped `vm_allocate`, `vm_deallocate` and `vm_protect` entirely, and every routine
that is written `PREFIX(...)` in a `.defs` was generated with the `_kernelrpc_` prefix — the name
libsystem calls it by, not the name the kernel defines it under.

`grep -c deallocate out/mach_headers/mach/vm_map.h` was **0**. It is 7 now.

The `.defs` side of that is not subtle once seen: `PREFIX` is spelled in the same `#if` as the
routines, in a file the project had been reading for thirty stages.

## What it bought, in two parts

### 1. Three C++ files, and they carry 250 undefined symbols with them

`iokit/Kernel/IOService.cpp` (11 boot-path stubs), `IOUserClient.cpp` (5) and
`IOMemoryDescriptor.cpp` failed on names their own headers should have declared:

```
IOUserClient.cpp:2134:9: error: use of undeclared identifier 'vm_deallocate'
IOMemoryDescriptor.cpp:1020:16: error: use of undeclared identifier 'mach_vm_deallocate'
IOService.cpp:4068:10: error: use of undeclared identifier 'thread_policy_set';
                                  did you mean 'thread_policy_state'?
```

`vm_deallocate` and `mach_vm_deallocate` have **no other declaration in the tree** — the only one is
the generated `<mach/vm_map.h>`. `thread_policy_set` looks declared: `osfmk/mach/thread_policy.h:54-66`
has the prototype — **inside a `/* … */` block**, which is why clang's suggestion was the next
identifier down. All three are defined by the kernel (`osfmk/vm/vm_user.c:336`, `osfmk/kern/
thread_policy.c`), so nothing was missing from the source; only the declaration was.

That is the three files, and it is not the 259 symbols. The rest of the drop is the same mechanism
one step out: those three files compile now, so their **829 defined symbols** enter the image, and
**250 of the 829 were undefined before**. The arithmetic is exact:

| | count |
| --- | --- |
| undefined, before | 448 |
| supplied by `IOService.o` + `IOUserClient.o` + `IOMemoryDescriptor.o` | −250 |
| resolved by the `PREFIX` change (`_kernelrpc_*` ×12, `_host_page_size`) | −13 |
| newly referenced by the three new objects (`upl_get_internal_*`, `OSKext::isWaitingKextd`) | +4 |
| undefined, after | **189** |

### 2. Thirteen symbols the C side never noticed

The 12 `_kernelrpc_*` names and `_host_page_size` were undefined in the previous image and are gone
from this one — because the calls in the tree now resolve to the declarations that match the
definitions. The C side was **unchanged at 608 of 615** either way, which is the interesting half:
those names were being stubbed silently in a C build that reported no problem at all. A stub is not
a diagnostic, and the count of them is a count of what is missing, not of what is wrong.

## What changed in the 32 files

Two constructs, and both follow from the same `#if`:

* **routine names lost the `_kernelrpc_` prefix** — `_kernelrpc_vm_read` → `vm_read`,
  `_kernelrpc_mach_port_allocate` → `mach_port_allocate`, `_kernelrpc_mach_vm_deallocate` →
  `mach_vm_deallocate`, `_kernelrpc_thread_policy_set` → `thread_policy_set`, `_host_page_size` →
  `host_page_size`, and the matching `__Request__`/`__Reply__` struct names;
* **three routines and one PRIVATE one appeared** — `vm_allocate`, `vm_deallocate`, `vm_protect`, and
  `mach_zone_force_gc` (`mach_host.defs:252-261`, behind `#ifdef PRIVATE`, which is also in
  `$(DEFINES)` and was also missing).

`KERNEL_SERVER_SUFFIX`'s `_external` naming is untouched — `KERNEL_SERVER` was already passed — so
the build-dir `.c` files still define `vm_allocate_external` and friends, and the export/`kserver`
split that `experiment-144` depends on still holds. Checked rather than assumed, because the change
touches both variants:

```
mach/exc_server.h                   ipc_kobject:0
kserver/mach/exc_server.h           ipc_kobject:1
mach/vm_map_server.h                ipc_kobject:0
kserver/mach/vm_map_server.h        ipc_kobject:1
```

## The four hand-written copies

The script had four `cc -E` invocations, each with its own copy of the flag list:

| line | run | flags |
| --- | --- | --- |
| 157 | the `subsystem` test | `KERNEL_PRIVATE XNU_KERNEL_PRIVATE MACH_KERNEL_PRIVATE KERNEL_SERVER` |
| 259 | export `_server.h` (`MIG_USHDRS`) | same, no `KERNEL_SERVER` |
| 278 | build-dir `_server.h` (`MIG_KSHDRS`) | same, `KERNEL_SERVER` |
| 292 | user side (`MIGKUFLAGS`) | same, `KERNEL_USER` |

Three of them agree with each other and **none of them with Apple** — which is exactly how a missing
`-DKERNEL` survives four copies of one list. This is the eleventh instance of the project's
most-repeated defect, and the seventh where the value was Apple's and the copy was the project's.
They are now one array:

```bash
DEFS_DEFINES=(-DKERNEL=1 -DKERNEL_PRIVATE=1 -DXNU_KERNEL_PRIVATE=1 -DPRIVATE=1)
```

`-DAPPLE` is left out because it appears in no `.defs` outside license comments; `-D__MACHO__=1` and
`-Dvolatile=__volatile` are referenced by no `.defs` in the tree. `-DMACH_KERNEL_PRIVATE` is *not* in
`$(DEFINES)` — it comes from MIGKUFLAGS/MIGKSFLAGS per rule — and it is still passed on all four
runs, unchanged, because `experiment-144`'s design depends on the current arrangement. Both
exclusions are recorded in the script rather than silently matched, since "matches Apple" and "matches
the part of Apple that is load-bearing" are different claims.

## What is left of the C++ block

Five files, and the three that closed were three *different* underlying problems that had been
counted as one "vm_deallocate/MIG" item:

| file | now fails on | the shape |
| --- | --- | --- |
| `iokit/Kernel/IODataQueue.cpp` | out-of-line `enqueue` signature | C++ |
| `iokit/Kernel/IOSharedDataQueue.cpp` | out-of-line `enqueue` signature | C++ |
| `libkern/c++/OSKext.cpp` | `kxld_create_context` no matching function | C++ |
| `libkern/OSKextLib.cpp` | `kext_request` has a different language linkage | C++ |
| `libkern/c++/OSRuntime.cpp` | `operator new[]` takes `size_t` = `unsigned int` | the target triple |

and the seven C failures are unchanged. The next host-side lever is the two `enqueue` definitions,
which are one signature disagreement in two files.

## Reproduce

```bash
./tools/gen_mach_headers.sh                                  # 40 generated, 0 failed
./tools/build_xnu_arm_kernel.sh                              # C 608/615, C++ 78/83
XNU_KERNEL_CONFIG=STAGE90_BOOT \
XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
MANIFEST=$PWD/out/xnu_arm_manifest_min.txt ./tools/build_xnu_arm_kernel.sh
./tools/measure_link.sh --keep-stubs                         # 686 objects, 189 undefined
./tools/measure_link.sh --min --keep-stubs                   # 492 objects, 330 undefined
./tools/stub_blockers.py                                     # 42 boot-path stubs
./tools/stub_blockers.py --min                               # 50
```

The before numbers above were re-measured rather than remembered: `out/mach_headers` was restored
from a copy taken before the change, rebuilt into a scratch object directory (`XNU_KERNEL_OBJ_OUT`),
linked with `LINK_OUT` pointing elsewhere, and the new headers put back. Both configurations give
683/489 objects and 448/586 undefined on the old headers, which reproduces the previously recorded
448 exactly.
