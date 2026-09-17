# Experiment 126 — `CLOCK_T` was a workaround for the old defect, and `<types.h>` is the kernel's

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/gen_libsa_export.sh` (new), `tools/build_xnu_arm_kernel.sh`

| Configuration | before (experiment-125) | after |
| --- | --- | --- |
| `STAGE90_BOOT` | 395 of 419 | **400 of 419** |
| `RELEASE` | 560 of 587 | **565 of 587** |
| undefined after linking | 898 | **777** |

No line of XNU's code was changed.

## 1. `-D_CLOCK_T=1` was a workaround for a defect that is now fixed

`experiment-115` introduced it, with a correct diagnosis at the time: `bsd/sys/_types/_clock_t.h`
guards itself with `_CLOCK_T` and defines `clock_t` as `__darwin_clock_t`, while
`osfmk/kern/kern_types.h:193` defines `typedef struct clock *clock_t` under `MACH_KERNEL_PRIVATE`.
Both were reaching the same translation unit, so one had to be told which wins, and the flag said
the kernel's does.

**That was a symptom of `MACH_KERNEL_PRIVATE` being global**, which experiment-118 removed. With the
per-component defines, `kern_types.h`'s definition is reachable from osfmk and iokit files only, and
a BSD file gets `_clock_t.h`'s — the two no longer meet, and the flag has nothing left to resolve.
Removing it is **560 → 564**, no regressions, four files newly passing, all of them the
`bsd/sys/times.h:83: unknown type name 'clock_t'` cluster.

Worth recording because it is a pattern rather than an incident: **a workaround outlives the defect
it worked around, and then reads as a fact about the source.** The comment that introduced it is
preserved in the script next to the removal, so the next person finds both.

## 2. `<types.h>` means `osfmk/libsa/types.h`, and the directory cannot simply be exposed

`osfmk/device/subrs.c:138` includes `<libsa/stdlib.h>`, which at `:63` includes `<types.h>` — the
kernel's own, with `u_char`, `u_short`, `u_int`, `u_long`, `caddr_t` and `daddr_t`. Three files fail
without it (`subrs.c`, `kern/btlog.c`, `vm/vm_compressor.c`).

The obvious fix — `-I$XNU/osfmk/libsa` — is what `experiment-117` measured as costing four files, and
it still does: the same directory holds `string.h`, whose `strncat`/`strcpy` are
`__builtin___*_chk` macros for a kernel build. It shadows the real `<string.h>` and takes
`iokit/Kernel/IOStringFuncs.c` from passing to failing. Placing the directory *last* instead is
inert, because `<types.h>` then loses to `bsd/arm/types.h` — Darwin's machine types, no `u_char`.

So `tools/gen_libsa_export.sh` copies the three headers `<types.h>` needs and nothing else, into
`out/xnu_libsa_export/`, and that root goes immediately before `-I$XNU/bsd/arm`. It is Apple's own
`EXPORT_HDRS` principle — **a selected list per component** — applied to the one component that
needed it, and it is the third time this project has had to reach for it (experiment-117 for
`osfmk/libsa`, experiment-124 for MIG's output set).

| Arrangement | `RELEASE` | Regressions |
| --- | --- | --- |
| no libsa root | 564 | — |
| the whole directory, before `bsd/arm` | 567 | `iokit/Kernel/IOStringFuncs.c` |
| the whole directory, at the end | 564 | none — `<types.h>` resolves to `bsd/arm/types.h` |
| **the three filtered headers, before `bsd/arm`** | **565** | **none** |

The minimal configuration gains more (395 → 400) than `RELEASE` does, which is the expected shape:
`STAGE90_BOOT` does not build the networking files that the whole-directory arrangement also fixed.

## 3. And the link is 121 symbols closer

```
objects:                  565
duplicate definitions:    0
image:                    6 409 916 bytes, 23 348 symbols defined
undefined after linking:  777   (9 compiler runtime, 768 a source file must provide)
```

777 against 898 before. The remaining 22 `RELEASE` failures are unchanged in kind and are now the
whole list: `loop.h` (4 files), the `sync_qos_count_t` include-order question experiment-121 left
open (3), `os/firehose_buffer_private.h` (2), `pty.h`, `uid_t`, `AST_KEVENT_REDRIVE_THREADREQ`,
`STATIC` in `subr_prof.c`, `DLT_EN10MB` in `if_bridge.c`, `OSAtomicOperations.c` and
`vm_object.c`'s const-member error, `mptcp_var.h`, `in_pcb.h`, `kperfbsd`'s `ffs`, `kern_sysctl.c`'s
negative-size array, `vnode_trim`, and `task_collect_crash_info`.

**`u_char` and `caddr_t` are gone from the list**, and so is every `clock_t`.

## How to reproduce

```bash
./tools/gen_libsa_export.sh                       # 3 headers -> out/xnu_libsa_export/
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh                 # 565 of 587
./tools/link_xnu_arm.sh                           # 0 duplicates, 777 undefined
```
