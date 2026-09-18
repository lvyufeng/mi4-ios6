# Experiment 163 — the nearest blocker was not XNU's: the EABI runtime

Date: 2026-09-18
Host only — nothing here runs on the device.
Artifacts: `stages/stage90/xnu_aeabi_runtime.c` (new), `tools/build_xnu_arm_kernel.sh`,
`tools/measure_link.sh`, `tools/link_xnu_arm.sh`

| `RELEASE` | before | after |
| --- | --- | --- |
| C, unchanged | 612 of 615 | 612 of 615 |
| C++, unchanged | 82 of 83 | 82 of 83 |
| undefined after a whole-kernel link | 72 | **63** |
| — of which compiler runtime | 9 | **0** |
| boot-path stubs, from `arm_init` | 14 of 72 | **9 of 63** |
| `.text` in the measurement image | 4908480 bytes | 4911088 bytes |

| `STAGE90_BOOT` | before | after |
| --- | --- | --- |
| C, unchanged | 418 of 426 | 418 of 426 |
| C++, unchanged | 82 of 83 | 82 of 83 |
| undefined, measurement link | 229 | **220** |
| boot-path stubs (`stub_reach.py --min --list 40`) | 27 of 229 | **23 of 220** |
| `stub_blockers.py`'s "compiler runtime, not source" | 4 | **0** |

9 symbols closed, 0 opened, in both configurations. No line of XNU's source was changed. One new
file of this project's own, plus the host's compiler runtime, which is linked rather than copied.

## The nearest thing a boot hits was not in the tarball either

`stub_reach.py --from arm_init` on the image experiment-162 left put `__aeabi_memcpy4` at
**distance 1** — the closest missing symbol in the whole image, and the first thing `arm_init`'s
first aggregate copy would call. Five of the fourteen boot-path stubs were `__aeabi_*`
(`__aeabi_memcpy4` at 1, `__aeabi_memcpy8` at 2, `__aeabi_uldivmod` at 3, `__aeabi_ldivmod` and
`__aeabi_memclr8` at 6), all five "compiler runtime, not source" in `stub_blockers.py`'s own
grouping, a category the project had been naming and not acting on since the layer build first
reported it.

They are not a defect in anything. They are the price of the ELF path, and the shape is exactly the
one this project keeps meeting from the other side: **the same source lowers to different calls
under different ABIs.**

```
armv7-apple-ios / armv7-apple-darwin   an aggregate copy becomes  bl memcpy
armv7-unknown-netbsd-eabi (EABI)       an aggregate copy becomes  bl __aeabi_memcpy4
```

Apple's kernel never mentions `__aeabi_anything` because Apple's target is not EABI —
`grep -rn "__aeabi" external/xnu-4570.1.46` over the whole tree is **empty**. The Darwin target was
closed by experiment-150 (`ld64.lld` cannot link armv7 Mach-O), so EABI is the only linkable
dialect this project has, and these nine symbols are what it costs. The cost was not in
experiment-161's table of three; it belongs to experiment-150's decision, and it has been visible
in every undefined list since the first ELF link.

## The four that are ours

`__aeabi_memcpy`, `__aeabi_memcpy4`, `__aeabi_memcpy8` and `__aeabi_memclr8` — plus the five
siblings written for symmetry — are `stages/stage90/xnu_aeabi_runtime.c`: twelve tail calls to
`memcpy`, `memmove` and `memset`, which the kernel already defines in `libkern`.

- **The whole file is 108 bytes of `.text`** (measured: `arm-none-eabi-size -A` on the object), and
  every body compiles to a single `b memcpy` / `b memset` / `b memmove` — the point being that a
  runtime must not become a second implementation of anything.
- **`__aeabi_memset`'s argument order is `(dst, n, c)`** — size before character — and it is the
  one signature here that does not match the routine it forwards to. It is written as the EABI
  specifies it and not as it looks like it should be, which is the same class as the `size_t`
  widening of experiment-161: a value read in one spelling and used in another.
- It is compiled **by `build_xnu_arm_kernel.sh`, with the loop's own `CC_ARGS` and `INCLUDES`**,
  into its own `out/xnu_rt_obj/` — not by a new script with a second copy of the flags. The project
  had four copies of the target triple once. Setting this up produced the same error that comment
  records: `sys/_types/_u_int.h not found`, from a force-include, in a file that had been given
  `-I$XNU/osfmk` but not the other components. `COMPONENT_LIST` is now named once at the top of the
  script and read in both places.

## The five that are the compiler's

`__aeabi_uldivmod`, `__aeabi_ldivmod`, `__aeabi_d2ulz`, `__aeabi_l2d`, `__aeabi_ul2d` are the
compiler runtime: clang emits them for 64-bit division and for `int64 ↔ double` conversions, and
they are **`libgcc.a`**, which every kernel links rather than reimplements. Measured, not assumed:

- **`arm-none-eabi-compiler-rt` is not installed on this host** — `/usr/lib/llvm-14/lib/clang/14.0.0/lib/`
  holds `linux/` and `clang_linux` only, and there is no `libclang_rt.builtins-arm*` anywhere — so
  `libgcc.a` from `arm-none-eabi-gcc 10.3.1` is the only compiler runtime available.
- **The ARM-state multilib is selected deliberately**, by `arm-none-eabi-gcc -marm -mfloat-abi=soft
  -print-libgcc-file-name` → `/usr/lib/gcc/arm-none-eabi/10.3.1/libgcc.a`. The multilib that matches
  this build's own `-mfpu=neon-vfpv4 -mfloat-abi=softfp` is `thumb/v7ve+simd/softfp/libgcc.a`, and it
  is **Thumb**, where the whole image is ARM (`-marm`, `.arm` in the stub assembly, every translated
  `.s`). It pulls 34 symbols against 40 — and the check that says the ARM one is safe is worth more
  than 6 symbols.
- **That check is the one this stage could have got silently wrong.** A `softfp` caller and a
  `soft` callee disagree about where a `double` is passed: VFP register against core register pair.
  Disassembling both sides settles it, and they agree — the `__aeabi_*` helpers take their `double`
  arguments in **r0:r1** under both ABIs:

  ```
  our code (bsd_vfs_vfs_disk_conditioner.o, -mfloat-abi=softfp)
      160:  vmov    d16, r0, r1        <- __aeabi_ul2d returned the double in r0:r1
      168:  vmov    r0, r1, d16        <- and __aeabi_d2ulz is called with it in r0:r1
      16c:  bl      __aeabi_d2ulz

  libgcc's _fixunsdfdi.o, ARM-state soft-float
      0:  mov  r6, r0  /  mov r7, r1   <- reads the double from r0:r1

  libgcc's _fixunsdfdi.o, thumb/v7ve+simd/softfp
      4:  vmov  d16, r0, r1            <- also r0:r1, then vmul.f64 / vcvt.u32.f64 in hardware
  ```

- **What it costs**: 40 symbols enter the image — this file's 12 plus 28 from `libgcc.a`, of which
  five are the ones asked for and the other 23 are their closure. The closure is soft-float double
  arithmetic (`__adddf3`, `__muldf3`, `__subdf3`, `__extendsfdf2`, …), because the ARM-state libgcc
  builds `_fixunsdfdi.o` in terms of `__aeabi_dmul`. In an image that has VFP these are dead weight
  — **+2608 bytes of `.text`** for the lot — and they are *correct* weight: nothing calls them but
  the routines that were asked for.
- **One linker warning, twice**, and it is inert: `_fixunsdfdi.o` and `_udivmoddi4.o` "use
  variable-size enums yet the output is to use 32-bit enums". The members' interfaces are
  `double`, `unsigned long long` and `unsigned int`; not one of them is an enum. It is recorded
  rather than suppressed.

## What it bought

The five nearest boot-path stubs, all of them, and nothing new:

```
RELEASE        14 → 9     __aeabi_memcpy4 at 1, __aeabi_memcpy8 at 2, __aeabi_uldivmod at 3,
                          __aeabi_ldivmod and __aeabi_memclr8 at 6   -- all five, all of them
STAGE90_BOOT   27 → 23    __aeabi_memcpy4 at 1, __aeabi_memcpy8 at 2, __aeabi_uldivmod at 3,
                          __aeabi_memclr8 at 4  -- four, not five: `__aeabi_ldivmod` is not on
                          the minimal configuration's walk at all, which `stub_blockers.py --min`'s
                          own four-item compiler-runtime list confirms
```

Nothing opened, in either configuration, and no reachable stub was added by the closure — which is
the thing that made experiment-162's count go *up* and did not happen here, because libgcc's
routines call nothing that the image does not already have.

## A figure in experiment-162 that was wrong

The `RELEASE` boot-path row in experiment-162's table said **7 of 72**, and 7 was the number of rows
in a `tail -20` of `stub_reach.py`'s output. The tool prints the count in a header line and then up
to `--list` (15) rows **nearest the entry first**, so cutting the output from the top hides the
smallest distances and leaves the count unread — and the seven rows a `tail` showed were the seven
*furthest* ones. The true figure is **14**, re-measured here by rebuilding that exact image
(`XNU_RT_OBJ_OUT` pointed at an empty directory and `arm-none-eabi-gcc` shadowed by a stub on
`$PATH`, which is the only way to reconstruct a link that no longer exists), and experiment-162's
table now says so.

It is the eighth instance of the class the project keeps a note about: the tool was right, the
reading was not. `--list 40` is the fix, and the header line is the number.

## Verified, not assumed

- **Both link routes agree**: 63 by `nm -u` on the merged relocatable link and 63 by
  `measure_link.sh`; 220 and 220 for `STAGE90_BOOT`.
- **The runtime is config-independent and shared**: one object in `out/xnu_rt_obj/`, used by both
  configurations' links, because the ABI it implements is a property of the target triple and not
  of the manifest.
- **`build_xnu_arm_layer.sh` unchanged**: 32 of 32, 445 undefined.
- **`link_xnu_arm.sh` still works**: 0 duplicate definitions, and its own `compiler runtime` line —
  which has been computing this class since before the class was closed — now reads **0**.
- **The compile counts did not move**: 612 of 615 and 82 of 83, and 418 of 426 and 82 of 83.

## Reproduce

```bash
./tools/build_xnu_arm_kernel.sh                  # C 612 of 615, C++ 82 of 83, + 1 runtime object
./tools/measure_link.sh --keep-stubs             # 63 undefined, compiler runtime 0
./tools/stub_reach.py --from arm_init --list 40  # 9 of 63
./tools/link_xnu_arm.sh                          # 0 duplicates, compiler runtime 0
./tools/build_xnu_arm_layer.sh                   # 32 of 32, 445 undefined

XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$PWD/out/xnu_arm_manifest_min.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
  ./tools/build_xnu_arm_kernel.sh                # C 418 of 426, C++ 82 of 83
./tools/measure_link.sh --min --keep-stubs       # 220 undefined
./tools/stub_reach.py --min --from arm_init --list 40
./tools/stub_blockers.py --min                   # 23 boot-path stubs, and no compiler-runtime row

# what libgcc contributes
arm-none-eabi-ld -r -o /tmp/full.o out/xnu_kernel_obj/*.o out/xnu_asm_obj/*.o \
    out/xnu_rt_obj/*.o -L"$(dirname "$(arm-none-eabi-gcc -marm -mfloat-abi=soft \
    -print-libgcc-file-name)")" -lgcc
```

## What this does not settle

The runtime is a **link input**, not a fix for anything in XNU: an image linked without
`out/xnu_rt_obj/` and without `-lgcc` is exactly the image experiment-162 measured, and both link
scripts say so rather than silently producing a different number — a count that depends on whether a
directory exists is the "one value, two definitions" defect wearing its other hat.

The `STAGE90_BOOT` boot path's nearest stub is now `bsd_scale_setup` and `task_init`, both at
distance 3, both from files that do not compile — and the largest single category is still
`stub_blockers.py`'s 14 "a file that fails to compile". The four `__firehose_*` are behind it, at
distance 4 and 5, and they are now the top of the "no source in the tree" group with a known source
and a known obstacle: `libdispatch/src/firehose/firehose_buffer.c` needs `os/internal/atomic.h`,
which is not in the tarball either — the `os_atomic_*` layer, absent from xnu-4570.1.46 by
measurement (`grep -rln "os_atomic_load\|os_atomic_cmpxchg"` over the tree is empty, and the only
three `os_atomic_*` macros it has are the ones `osfmk/arm/atomic.h` defines for
`chunk_private.h`). The files to fetch, none of them vendored here because a stage that does not use
a file should not commit it:

```bash
for f in firehose_buffer.c firehose_buffer_internal.h firehose_inline_internal.h \
         firehose_internal.h; do
    curl -sSfLO "https://raw.githubusercontent.com/apple-oss-distributions/libdispatch/\
libdispatch-913.30.4/src/firehose/$f"
done
curl -sSfLO "https://raw.githubusercontent.com/apple-oss-distributions/libdispatch/\
libdispatch-913.30.4/os/firehose_buffer_private.h"
```

and one thing to settle before writing any of it: **the kernel's buffer is 16 chunks, not 64.**
`firehose_buffer_create`'s `#ifdef KERNEL` path does
`size = FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT * FIREHOSE_CHUNK_SIZE` for the allocation `oslog_init`
has already made, while the published `firehose_buffer_internal.h:33` says
`FIREHOSE_BUFFER_CHUNK_COUNT 64ul`. Its own `#ifdef KERNEL` half resolves it:
`FIREHOSE_BUFFER_CHUNK_PREALLOCATED_COUNT 15`, which is 16 − 1 — the identity the next generation
writes out as `(__firehose_buffer_kernel_chunk_count - 1)`. So the *kernel's* copy of that header is
not the published one, and 16 is the value `FIREHOSE_BUFFER_CHUNK_COUNT` must take in the port.
