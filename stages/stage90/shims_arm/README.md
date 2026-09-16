# `shims_arm/` — Phase 4 ARM header stubs (work in progress, not wired in)

Four headers that let some of `external/xnu-4570.1.46/osfmk/arm/` be parsed standalone. They
are **not** part of the Stage90 payload, are **not** linked into anything, and no build script
uses them yet. They exist because Phase 4 needs them and because writing them produced a
measurement that corrected the plan.

| File | Why | Status |
| --- | --- | --- |
| `sys/_symbol_aliasing.h` | build-generated, not shipped | empty; **verified by iteration** — adding it moved the sweep's dominant blocker from 17 files to 16 and produced no errors of its own |
| `sys/_posix_availability.h` | build-generated, not shipped | minimal; **verified the same way** |
| `sys/_pthread/_pthread_types.h` | `bsd/sys/_pthread/` is absent entirely, and so are the `__darwin_pthread_*` base types | opaque handles; nothing in `osfmk/` takes `sizeof()` of them or dereferences them (only two occurrences in the tree, both comments in `mach/thread_policy.h`) |
| `mach_assert.h` | absent entirely; `kern/assert.h:68` includes it | `MACH_ASSERT 0` — the released-kernel configuration, which exercises *less* code, so a clean result here does not overstate anything |

## What using them showed

With these four plus `-DKERNEL=1` (which `EXTERNAL_HEADERS/stdint.h` documents and needs —
without it, the compiler's `stdint.h` and Darwin's `_int32_t.h` both define `int32_t`),
the `osfmk/arm` sweep goes from **2 of 32 to 3 of 32**.

That is the finding: four headers and the correct configuration flags bought one file. And the
remaining errors are no longer *missing files* — they are **undefined build-configuration
symbols**:

```
AST_NONE undeclared
INTSTACK_SIZE undeclared
gPhysBase undeclared
expected specifier-qualifier-list before 'decl_simple_lock_data'
#error unsupported compiler
```

That last one is the clearest: `EXTERNAL_HEADERS/stdatomic.h:24` is

```c
#ifndef __clang__
#error unsupported compiler
#endif
```

XNU's atomic layer is clang-only, and this project's ARM toolchain is `arm-none-eabi-gcc`.

## So the conclusion changed

An earlier estimate in `docs/status/roadmap.md` said the ARM layer's gap was **8 missing
headers, bounded and enumerable, the same order of magnitude as the 16 shims already written
for five pexpert objects**. That was wrong, and this directory is the evidence: the 8 were only
the first level, and clearing the first level does not approach compilation.

The accurate statement is that the source tree is complete and what is absent is the
**build configuration** — the `.def` files, the arch/machine target definitions, a symbol set
that `osfmk/kern/*` expects to be defined by the build, and a clang toolchain. That is the
same conclusion the build-system investigation reached from the other direction, now confirmed
by trying to compile.
