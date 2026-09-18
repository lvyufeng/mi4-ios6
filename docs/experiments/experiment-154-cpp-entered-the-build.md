# Experiment 154 — the C++ block entered the build, and the line that was blocking it was mine

Date: 2026-09-18
Host only — nothing here runs on the device.
Artifacts: `tools/build_xnu_arm_kernel.sh` (a C++ path and an `XNU_KERNEL_EXTRA_DEFINES` hook),
`stages/stage90/shims_arm/string.h` (five declarations)

| | before | after |
| --- | --- | --- |
| `RELEASE`, C | 608 of 615 | **608 of 615** |
| `RELEASE`, C++ | **not attempted** | **75 of 83** |
| `STAGE90_BOOT`, C | 414 of 426 | **414 of 426** |
| `STAGE90_BOOT`, C++ | **not attempted** | **75 of 83** |
| C++ objects in the build | 0 | 75 in each configuration |
| `.s` still skipped | 17 | 17 |
| boot-path stubs, `RELEASE` | 51 | **46** |
| boot-path stubs, `STAGE90_BOOT` | not previously recorded | 53 (the minimal configuration fails 12 C files to RELEASE's 7) |
| C-side undefined symbols | 224 | **209** |
| undefined symbols in the image | 224 | 448 — **239 of them C++** (see below) |

Both configurations give the same 75 of 83, which is the expected result and a small confirmation in
itself: the eight C++ failures depend on the language and on two headers, not on which options the
configuration turns on.

## The build was skipping 83 files, and the reason it gave was half wrong

`build_xnu_arm_kernel.sh` had this:

```bash
    # Only C. The .s files need the assembler flags from xnu_arm_assemble.sh, and the .cpp files
    # need libkern's C++ runtime, which is a separate and larger problem than this measures.
    case "$src" in
        *.cpp|*.s|*.S) skipped=$((skipped + 1)); continue ;;
    esac
```

The second clause is true of the **link** and false of the **compile**. A `.cpp` in `libkern` is a
`libkern` translation unit; it needs the same per-component defines, the same generated roots, the
same include order and the same shims as the `.c` file next to it, and `clang++` instead of `clang`.
Skipping them meant the project's headline number — "608 of 615" — silently meant "of the 615 that
are C". It now attempts all 698 and reports the two halves separately.

And the skip is how the project got experiment-152's answer wrong, because the number that filled
the gap came from a hand-written loop rather than from this script.

## The correction: the one dead line was reached only because of a flag I passed

experiment-152 says, as its headline: *all 83 `.cpp` fail on `osfmk/kern/misc_protos.h:254`,
`struct kmod_info_t;` — valid C, invalid C++ — and no flag can fix it*, and calls that line "the
single highest-value line in the repository". The measurement it came from is still in the
transcript; the command passes:

```
-DKERNEL=1 -DMACH_KERNEL=1 -DMACH_KERNEL_PRIVATE=1 -DXNU_KERNEL_PRIVATE=1 -DKERNEL_PRIVATE=1
```

for **every** file. `MACH_KERNEL_PRIVATE` is not a global flag. It is a *component* flag
(`xnu_config/component_defines.sh`), it belongs to `osfmk` and `bsd`, and **no `libkern` or `iokit`
file gets it.** The line is reached through

```c
osfmk/kern/kern_types.h:190   #ifdef	MACH_KERNEL_PRIVATE
osfmk/kern/kern_types.h:192   #include <kern/misc_protos.h>
```

and `vm/vm_object.h:84`, which is the only header that includes `misc_protos.h` unconditionally —
and neither is on any `.cpp`'s include path.

Reproduced with the real build, one command, now that the script has a hook for it:

```
$ XNU_KERNEL_EXTRA_DEFINES=-DMACH_KERNEL_PRIVATE=1 ./tools/build_xnu_arm_kernel.sh --dir libkern
  C files tried:        46      compile: 23   fail: 23
  C++ files tried:      22      C++ compile: 0    C++ fail: 22
```

and every one of the 22:

```
osfmk/kern/misc_protos.h:254:8: error: definition of type 'kmod_info_t' conflicts with typedef
```

**The identical error, from the identical flag, and it is the flag.** The same command without it
is the first row of the table below. This is the eighth instance of the project's measurement
defect (memory: `mi4-measurement-defects.md`) — and the first where the wrong measurement produced
a *finding* rather than just a number, because the finding then acquired a recommendation (edit
Apple's source) that the corrected measurement does not support.

The counterfactual in experiment-152 — "measured, by removing it in a scratch copy: 0 of 83 → 4 of
83" — is not wrong as arithmetic. It is a measurement of the wrong thing: at 4 of 83 the errors
become `IMIGObjectVtbl`, `kqueue_id_t`, `clock_t`, which is the C block's error chain, and the
reason it looked like "one line away" is that the run it was compared against had a global define
in it. **Removing that line would have bought exactly nothing**, and the source edit this project
does not make is not needed.

## A second flag in the same loop, with the same shape

The same loop passes `-DMACH_KERNEL=1` for every file too. `component_defines.sh` gives it to
`osfmk` and `bsd` (`osfmk/conf/Makefile.template:19`, `bsd/conf/Makefile.template:41-43`) and to
nobody else. `iokit/IOKit/IOTypes.h:148` is

```c
#ifndef MACH_KERNEL
...
typedef OSObject * io_object_t;      /* the C++ spelling, for a kernel C++ file */
#include <device/device_types.h>
typedef io_object_t io_connect_t; ... io_service_t;
#endif /* MACH_KERNEL */
```

so defining it for an `iokit` file removes `io_object_t`'s C++ spelling, removes `io_connect_t`,
`io_iterator_t` and `io_service_t` (they are defined only there and in `device_types.h`, which is
inside that block), and replaces `io_object_t` with `ipc_port *`. That is exactly the error list the
hand sweep reported — 48 `cannot initialize a parameter of type 'const OSMetaClassBase *' with an
lvalue of type 'io_object_t' (aka 'ipc_port *')`, then `io_connect_t` (8), `io_service_t` (4),
`io_iterator_t` (2).

Reproduced the same way:

```
$ XNU_KERNEL_EXTRA_DEFINES=-DMACH_KERNEL=1 ./tools/build_xnu_arm_kernel.sh --dir iokit
  C++ files tried:      61      C++ compile: 51    C++ fail: 10
  distinct missing names:
       12 io_buf_ptr_t     8 io_connect_t     6 vm_deallocate
        4 io_service_t     2 mach_vm_deallocate     2 io_iterator_t
```

against **56 of 61** with the flag removed — every name in that list a type `IOTypes.h:148` defines
or includes, and `io_buf_ptr_t` among them because `device_types.h` is reached from inside the same
`#ifndef`.

Two global component flags, one loop, and both of the C++ block's headline blockers were the flags.

## What the real build gives, and what five declarations were worth

Run with the script's own flags, per-component table, nothing added:

| | C++ compiling |
| --- | --- |
| as the hand sweep measured it (two global component defines) | 42 of 83 |
| **as the build measures it** | **46 of 83** |
| **+ five declarations in `shims_arm/string.h`** | **75 of 83** |

The five are `bcopy`, `bzero`, `bcmp`, `strlcpy`, `strlcat` — and `strchr` for the same reason. They
are not missing from the tree: `osfmk/libsa/string.h:72,73,93,94,95` declares all of them. They were
missing from *this build*, because `libkern/c++/*.cpp` includes `<string.h>` for them and
`EXTERNAL_HEADERS/` ships stdarg, stdatomic, stdbool, stddef and stdint and **no `string.h` at
all** — so `<string.h>` resolves to the shim, and the shim was five functions short of what the
block uses.

`libkern` alone, which is where they were measured first: **7 of 22 → 19 of 22**, with `bzero` (33
errors), `bcopy` (22), `strlcpy` (8), `strlcat` (5) and `bcmp` (1) gone from the blocker list.

## What is left of the C++ block

Eight files, no two alike:

| file | why |
| --- | --- |
| `OSKext.cpp`, `IOUserClient.cpp`, `IOMemoryDescriptor.cpp` | `vm_deallocate` / `mach_vm_deallocate` — **a MIG input, not a source problem** (below) |
| `IOService.cpp` | `thread_policy_set` undeclared |
| `IODataQueue.cpp`, `IOSharedDataQueue.cpp` | out-of-line `enqueue` definition disagrees with the declaration |
| `OSKextLib.cpp` | `kext_request` declared with a different language linkage than its friend declaration |
| `OSRuntime.cpp` | `operator new[]` takes `unsigned int`, the definition says `unsigned long` — the target triple, the same thing `vnode_pager.c` fails on |

`vm_deallocate` is the interesting one and it is **not** a C++ problem at all. It is a MIG routine:

```
osfmk/mach/vm_map.defs:132   #if !KERNEL && !LIBSYSCALL_INTERFACE
osfmk/mach/vm_map.defs:135   routine PREFIX(vm_deallocate)(...)
```

Apple's `MIGFLAGS` begins `$(DEFINES)` (`makedefs/MakeInc.def:474`), and `DEFINES` is
`-DAPPLE -DKERNEL -DKERNEL_PRIVATE -DXNU_KERNEL_PRIVATE -DPRIVATE ...` (`:78-79`). This project's
`gen_mach_headers.sh` preprocesses each `.defs` with `-DKERNEL_PRIVATE -DXNU_KERNEL_PRIVATE
-DMACH_KERNEL_PRIVATE` and **not `-DKERNEL`** — so the guard reads true, `skip;` is taken, and the
routine is not generated. Measured:

```
$ grep -c deallocate out/mach_headers/mach/vm_map.h
0
```

That is a missing define in a generator input, it explains three of the eight, and it is the next
stage rather than this one, because it changes every generated header at once and needs its own
before/after.

## The C++ objects exposed three tools, and each was wrong in the same direction

Putting the C++ objects into the image turned up three defects, all of the same kind: **a name read
in one spelling and used in another**, which is the project's `one value, two definitions` class
arriving in the tooling rather than in the build.

**1. The shim's declarations had C++ linkage.** `shims_arm/string.h` declared `bzero`, `bcopy`,
`memcpy` and the rest without `extern "C"`, so every `.cpp` that included it emitted a reference to
`_Z5bzeropvj` while the kernel's `bzero` — the C symbol from `osfmk/arm/bzero.s` — stayed `bzero`.
Fourteen undefined symbols whose *names are signatures*:

```
bcmp(void const*, void const*, unsigned int)   bcopy(...)   bzero(...)   memcpy(...)   memmove(...)
memset(...)   memcmp(...)   strlen(...)   strnlen(...)   strcmp(...)   strncmp(...)   strncpy(...)
strcpy(...)   strchr(...)   strlcpy(...)   strlcat(...)
```

Every function below the shim's `#ifdef __cplusplus` line, and nothing else. Fixed by wrapping them
in `extern "C"`, which is what Apple's kernel `<string.h>` does through `__BEGIN_DECLS`. **Mangled
count 462 → 448, and the 14 are gone.**

The one other shim that declares a function and *is* reached — `chud/arm/chud_xnu_private.h` — was
deliberately **left alone**: its real counterpart `osfmk/chud/i386/chud_xnu_private.h:60` declares
`chudxnu_cpu_signal_handler` with no `extern "C"` either, and Apple never includes it from C++. Put
in this build, it is still referenced unmangled (`grep chudxnu` in the undefined list), so there is
nothing to fix and matching the original is the right answer.

**2. The measurement link demangled its own input.** `measure_link.sh` collects the undefined set
from the linker's diagnostics and writes one weak stub per name — but GNU ld **demangles C++ names
when it prints them**, so the list held `IOUserClient::getService()` and `bzero(void*, unsigned int)`
instead of `_ZN14IOUserClient10getServiceEv`. `emit_stubs` refuses any name that is not a bare
identifier, correctly — it cannot assemble `a::b()` — so it silently skipped **every C++ symbol**,
pass 2 failed, and the report became

```
the linker refused even with every undefined symbol stubbed. Diagnostics that are not
undefined references:
```

with nothing under it. That message is a contradiction on its face, and the reason is that the
diagnostics *were* all undefined references: the tool could stub none of them. Fixed with
`--no-demangle` on both passes, so the names are printed and consumed in the spelling the objects
contain. Exit 0, and the image is back:

```
.text    addr 0x80000000  size 0x48c1b0 (4768176 bytes)     _start       80427074
.data    addr 0x80490000  size 0x029988                      arm_init     8031b59c
.bss     addr 0x804ba000  size 0x04a5bc                      arm_vm_init  8031d4f0
```

**3. The census could not read a mangled name.** `stub_blockers.py` finds a symbol's defining file
by scanning sources for the name, so `_ZN9IOService15getPMRootDomainEv` matched nothing and landed
in **"no source in the tree"** — a category that means *the tarball does not have this; write a
driver*. Nineteen boot-path stubs were being sent there by a lookup that could not read what it was
given. It now demangles and matches out-of-line definitions (`Class::method`), and the census reads:

| | before (51 stubs) | after (46 stubs) |
| --- | --- | --- |
| a file that fails to compile | 32 | **37** |
| C++ the build never attempts | 10 | **0** |
| assembly the build never attempts | 0 | 0 |
| no source in the tree | 4 | 2 |
| compiler runtime | 4 | 4 |
| a file not in the manifest | 0 | 2 |
| anomalous | 1 | 1 |

**And the C++ entries are on the boot path now, behind two files.** `IOService.cpp` accounts for 11
of the 46, `IOUserClient.cpp` for 5 and `OSKext.cpp` for 5 — and each of the three fails on **one
thing**: `thread_policy_set`, `vm_deallocate`, `vm_deallocate`. So two of the seven files behind the
boot path's C++ stubs are waiting on the same missing MIG routine, which is 10 stubs and one define.

## Why this is worth a stage

Three things, and the third is the one that matters:

1. 83 files moved from "not measured" to 75 of 83, in the build, with the build's flags.
2. The corrected number is *higher* than the hand measurement's, because the hand measurement was
   depressing it — one global `-DMACH_KERNEL_PRIVATE` and one global `-DMACH_KERNEL`.
3. **A recorded finding was wrong and it had a recommendation attached.** experiment-152 ended by
   putting a one-line change to Apple's published source in front of the reader as the highest-value
   line in the repository. There was nothing there. The build was measuring a define it had given
   itself, and no flag, shim or edit was ever going to remove it, because it was never in the tree.

The defence against this is now in the script: the C++ files are compiled by
`build_xnu_arm_kernel.sh` with that script's own flags, so the next claim about them comes from the
same command as the C count and cannot be produced by a private flag list. `XNU_KERNEL_EXTRA_DEFINES`
exists so that the comparison above is one command rather than a hand-copied loop — which is the
difference between the mistake being reproducible and the mistake being repeatable.

## The one thing experiment-152 got right, carried forward

Its headline and its recommendation are retracted, but one real fix came out of the same work and it
is still in `stages/stage90/shims_arm/string.h`, so it is recorded here rather than only in the
retracted log: **the shim defined `NULL` as `((void *)0)`**, which is not a null pointer constant in
C++ — every `return NULL;` in a `.cpp` was an error. It is `0` under `#ifdef __cplusplus` now. It
moves no count on its own, and it would not have been found without attempting the C++ block (the
first file it surfaced in was `osfmk/kern/cdata.h:1119`, returning `NULL` from a `char *` function).

The general shape is worth keeping next to the retraction: experiment-152 was wrong about the *cause*
of the C++ failures and right about one *defect* it walked past on the way — because the two came from
different places. The cause came from the private flag list; the `NULL` defect came from a `.cpp`
that the flag list could not stop from being compiled. A measurement can be an artifact and still have
something real inside it.
