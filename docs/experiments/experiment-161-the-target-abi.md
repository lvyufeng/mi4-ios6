# Experiment 161 — the target ABI: an ELF triple that carries Darwin's type widths

Date: 2026-09-18
Host only — nothing here runs on the device.
Artifacts: `tools/xnu_config/arm_target.sh` (new), four build scripts

| `RELEASE` | before (`armv7-none-eabi`) | after (`armv7-unknown-netbsd-eabi`) |
| --- | --- | --- |
| C | 609 of 615 | **610 of 615** |
| C++, unchanged | 80 of 83 | **82 of 83** |
| undefined after a whole-kernel link | 132 | **97** |
| boot-path stubs, from `arm_init` | 15 of 132 | **12 of 97** |
| `.text` in the measurement image | 4942544 bytes | 4896304 bytes |

No line of XNU's code was changed, and no flag was added. The value that moved is the **target
triple** — and it moved because experiment-160 left one file in the way whose failure turned out not
to be about that file at all.

## The failure that is not about the file

`bsd/vm/vnode_pager.c` was the last failure in `osfmk/vm` after experiment-160, and its error is a
type conflict:

```
vnode_pager.c:211:11: error: conflicting types for 'vnode_trim'
vm_protos.h:224:17: note: previous declaration is here
```

The two declarations are

```c
extern uint32_t vnode_trim (struct vnode *, int64_t offset, unsigned long len);   /* vm_protos.h */
        u_int32_t vnode_trim (struct vnode *, off_t offset, size_t length)        /* vnode_pager.c */
```

Preprocessing the file with its own flags shows the return types agree — `u_int32_t`, `uint32_t` and
`__uint32_t` all resolve to `unsigned int`. The **third parameter** does not: `unsigned long`
against `size_t`. And `size_t` here is `unsigned int`, because `bsd/arm/_types.h:67-71` is

```c
#if defined(__SIZE_TYPE__)
typedef __SIZE_TYPE__		__darwin_size_t;	/* sizeof() */
#else
typedef unsigned long		__darwin_size_t;	/* sizeof() */
#endif
```

Apple's own header takes `__SIZE_TYPE__`, and **clang defines that per target**: `long unsigned int`
for `armv7-apple-ios`, `unsigned int` for `armv7-none-eabi`. The source is not at fault; the
compiler's idea of the target is.

This is the same finding the project had already recorded from the other end, without acting on it:
the C++ failures include "`operator new[]`'s `size_t` (the target triple, same as `vnode_pager.c`)".

## Why not just define the macro

`-D__SIZE_TYPE__='long unsigned int'` on the command line works for C — `vnode_pager.c` compiles,
610 of 615. On C++ it is a **disaster**: 80 of 83 becomes **3 of 83**, every file failing the same way,

```
libkern/libkern/c++/OSMetaClass.h:922:19: error: 'operator new' takes type size_t ('unsigned int') as first parameter
```

because clang checks `operator new`'s first parameter against the *target's* built-in size type, not
against the macro. The macro redefines `size_t` in every header and changes nothing about what the
front end requires. So the choice is between a C-only macro — which leaves the C++ half of the kernel
compiling against an ABI the source was not written for — and moving the target itself.

## The target

Four predefined macros differ between the two triples:

| | `armv7-none-eabi` | `armv7-apple-ios` | `armv7-unknown-netbsd-eabi` |
| --- | --- | --- | --- |
| `__SIZE_TYPE__` | `unsigned int` | `long unsigned int` | `long unsigned int` |
| `__UINTPTR_TYPE__` | `unsigned int` | `long unsigned int` | `long unsigned int` |
| `__INTPTR_TYPE__` | `int` | `long int` | `long int` |
| `__WCHAR_TYPE__` | `unsigned int` | `int` | `int` |

`armv7-apple-ios` and `armv7-apple-darwin` carry all four and produce **Mach-O**, which
experiment-150 closed: `ld64.lld` cannot link 32-bit ARM Mach-O, so the dialect translation to ELF
is the only linkable object format this project has. The question was whether an ELF triple exists
with the same widths, and there are two:

```bash
for t in armv7-unknown-freebsd armv7-unknown-netbsd-eabi armv7-unknown-openbsd \
         armv7-unknown-linux-gnueabihf armv7-none-eabi; do
    printf '%-32s ' "$t"
    clang -target $t -dM -E -x c /dev/null | grep __SIZE_TYPE__
done
#  armv7-unknown-freebsd            #define __SIZE_TYPE__ unsigned int
#  armv7-unknown-netbsd-eabi        #define __SIZE_TYPE__ long unsigned int
#  armv7-unknown-openbsd            #define __SIZE_TYPE__ long unsigned int
#  armv7-unknown-linux-gnueabihf    #define __SIZE_TYPE__ unsigned int
#  armv7-none-eabi                  #define __SIZE_TYPE__ unsigned int
```

**`armv7-unknown-netbsd-eabi` is chosen**, over OpenBSD, because its macro set is the smaller
deviation. With this project's own flags the two differ by exactly this:

```
< #define __ARM_DWARF_EH__ 1                       < #define __NetBSD__ 1
> #define __PIC__ 1                                 > #define __OpenBSD__ 1
> #define __PIE__ 1  __pic__ 1  __pie__ 1           > #define unix 1  __unix 1
> #define __SSP_STRONG__ 2                          > #define __STDC_NO_THREADS__ 1
```

Not one of those is read anywhere in this tree — `grep -rn "defined(unix)"` over the whole of
`xnu-4570.1.46` is empty, `__PIC__` appears only under `defined(__i386__)` in `libsyscall/os/tsd.h`,
and `__ARM_FEATURE_UNALIGNED` is unreferenced — and `-fno-pic` was verified to win over OpenBSD's PIE
default by compiling `int *f(void){return &g;}` under both triples and getting byte-identical code.
So OpenBSD is not *wrong*; NetBSD is the smaller delta, and the choice is recorded as a preference
rather than as a measurement.

The one branch in the tree that reads either name is `bsd/netinet/ip_compat.h:122`, and the
`__NetBSD__` path gives `typedef u_int32_t u_32_t` — the same typedef as the `#else` path it replaces.

## One value, one place

The triple was spelled out in four scripts — `build_xnu_arm_kernel.sh` (twice, C and C++),
`build_xnu_arm_layer.sh`, `sweep_xnu_osfmk.sh` and `gen_assym.sh` — which is the "one value, two
definitions" shape this project keeps meeting, with four definitions instead of two. It now lives in
`tools/xnu_config/arm_target.sh`, beside `component_defines.sh` and `make_defines.sh`, which are the
same kind of thing: the values the build configuration would have supplied and the tarball does not.
The script also honours `XNU_ARM_TARGET`, so the before case above is one environment variable rather
than a second copy of the build script:

```bash
XNU_ARM_TARGET=armv7-none-eabi ./tools/build_xnu_arm_kernel.sh     # the previous behaviour
```

## What it bought, precisely

40 symbols closed and 5 opened, and both lists are what they should be. The 40 are the definitions of
the four files that now compile — 9 `vnode_pager_*`, 31 from `libkern/c++/OSKext.cpp` (27 mangled
`OSKext::` methods plus `gLoadedKextSummaries`, `gLoadedKextSummariesTimestamp`,
`OSKextGetAllocationSiteForCaller`, `OSKextGetKmodIDForSite`), the 3 `kern_os_*` from
`OSRuntime.cpp`, and `__cxa_pure_virtual`, which `OSRuntime.cpp` defines. The 5 opened are all
referenced by `libkern_c++_OSKext.o`, which is what "a file entering the link brings its references
with it" means: `firehose_trace_metadata`, `kext_get_vm_map`, `OSKextLoadedKextSummariesUpdated` and
the two `__llvm_profile_*` names.

## What it cost — three things, all measured

- **`__NetBSD__` is now defined.** One branch in the tree reads it and it does the same thing as the
  path it replaces, above.
- **`__PTRDIFF_TYPE__` became `long int`, where Darwin has `int`.** So `ptrdiff_t` is now a different
  type from Apple's, in the opposite direction from `size_t`. Both are 32 bits; nothing in the tree
  was observed to care, and the undefined count is the measurement that says so.
- **The exception model changed.** `__ARM_DWARF_EH__` replaces ARM EHABI, so **no `.ARM.exidx`
  sections are emitted**. The old build emitted one per function — 113 orphan `.ARM.exidx.*`
  sections in the measurement link, all zero-sized once linked, plus a real one. Nothing in XNU, in
  `xnu_measure_link.ld` (whose `/DISCARD/` list names `.comment`, `.note`, `.ARM.attributes` and
  `.llvm_addrsig`, and never `.ARM.exidx`) or in the entry image reads them, and no unwind helper
  (`__aeabi_unwind_cpp_pr*`, `_Unwind_*`) appears in either undefined list. This is also where the
  46 KB of `.text` went.

## Verified, not assumed

- **`build_xnu_arm_layer.sh` is unchanged where it should be**: 32 of 32, 445 undefined symbols — the
  same two numbers it reported before, on the same flags.
- **`gen_assym.sh` produces a byte-identical `assym.s`**: 266 defines, same values. Its intermediate
  `genassym.s` differs only in `.eabi_attribute` comments and in `Tag_CPU_unaligned_access` becoming
  1 — no constant moves, which is the check that matters for the assembly that consumes them.
- **Both configurations improved.** `STAGE90_BOOT`, measured the same way — same manifest, same
  script, same tool, `XNU_ARM_TARGET` the only difference:

  | `STAGE90_BOOT` | before | after |
  | --- | --- | --- |
  | C | 415 of 426 | **416 of 426** |
  | C++ | 80 of 83 | **82 of 83** |
  | undefined, merged relocatable link | 273 | **238** |
  | boot-path stubs (`stub_blockers.py --min`) | not re-measured | 24 |

  (experiment-157 recorded 330 for the minimal configuration's undefined count; experiment-160's
  `vm_object.c` accounts for most of the difference between 330 and the 273 above.)
- **The two link routes still agree**: 97 by `nm -u` on the merged relocatable link and 97 by
  `measure_link.sh`.

## Reproduce

```bash
./tools/build_xnu_arm_kernel.sh                  # C 610 of 615, C++ 82 of 83
./tools/measure_link.sh --keep-stubs             # 97 undefined, 97 stubs
./tools/stub_reach.py --from arm_init            # 12 of 97
./tools/build_xnu_arm_layer.sh                   # 32 of 32, 445 undefined
./tools/gen_assym.sh                             # 266 defines, unchanged

# the controlled comparison, one variable
XNU_ARM_TARGET=armv7-none-eabi ./tools/build_xnu_arm_kernel.sh
```

and the negative control, which is why the target moved rather than the macro:

```bash
XNU_KERNEL_EXTRA_DEFINES='-D__SIZE_TYPE__=long unsigned int' ./tools/build_xnu_arm_kernel.sh
# C 610 of 615, C++ 3 of 83 - "operator new takes type size_t ('unsigned int')"
```
