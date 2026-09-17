# Experiment 138 — the assembly convention, and the work list completed

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/assemble_arm_layer.sh` (new), `tools/stub_blockers.py` (new)

| | before | after |
| --- | --- | --- |
| stub symbols in the measurement image | 427 | **416** |
| boot-path stubs | 136 | **130** |

## 1. Ten assembly files define the wrong names, and the objects were fine

`osfmk/arm/asm.h:88-98` gives two conventions behind one flag:

```c
#ifndef __NO_UNDERSCORES__
#define EXT(x)  _ ## x        /* Darwin: a C symbol `bcopy` is `_bcopy` in assembly */
#else
#define EXT(x)  x             /* ELF:    it is `bcopy` in both */
#endif
```

This project assembles with `-D__NO_UNDERSCORES__=1`, because the toolchain is `armv7-none-eabi`
(ELF) and clang emits `bcopy` for `void bcopy(void)`. **But ten of the manifest's assembly files do
not use `EXT()`** — they write their labels literally, with the underscore Apple's convention
expects:

```
osfmk/arm/bcopy.s:40      _bcopy:      /* void bcopy(const void *src, void *dest, size_t len); */
osfmk/arm/bcopy.s:38      .globl _memcpy
```

So those objects assemble **cleanly** and define the wrong names. The failure appears at link time
as `undefined reference to 'bcopy'` from files that plainly have an implementation — the same
one-value-two-definitions shape this project keeps meeting, in the assembler's own convention, and
it had been invisible because nothing had ever looked at what the assembled objects *define* rather
than whether they assemble.

`tools/assemble_arm_layer.sh` applies the flag's own intent to the files that do not use the macro:
for each assembled object it renames `_x` to `x` **when `x` is a symbol the link cannot otherwise
resolve**. That condition is what makes it safe — `start.s` defines `_start`, `start` is not
undefined, so the entry point is left alone. Stripping unconditionally, which is what was tried
first, turned the entry into `start` and the link lost `_start`.

**22 symbols renamed across 9 objects.** XNU's source is untouched; the rename is a property of the
object, which is what `EXT()` does in the first place.

## 2. The work list, completed

`tools/stub_blockers.py` attributes each boot-path stub to its blocker, and the four outcomes need
different work:

| count | category |
| --- | --- |
| **88** | a file that fails to compile |
| **23** | assembly the build never attempts |
| **10** | C++ the build never attempts |
| 4 | compiler runtime (`__aeabi_*`) — libgcc at link time, not source |
| 4 | no source in the tree |
| 1 | defined by a compiled file — should not be a stub |

and the 88 are concentrated:

| stubs | file | what blocks it |
| --- | --- | --- |
| 27 | `osfmk/vm/vm_object.c` | the `const` member — **not fixable here** (experiment-127) |
| 24 | `bsd/kern/uipc_mbuf.c` | `sync_qos_count_t`, the include-order question (experiment-121) |
| 15 | `libkern/gen/OSAtomicOperations.c` | `false`/`true` are macros by then (experiment-130) |
| 10 | `bsd/kern/kern_event.c` | `AST_KEVENT_REDRIVE_THREADREQ` |
| 4 | `bsd/kern/kern_ktrace.c` | — |
| 3 | `osfmk/kern/btlog.c` | `u_char` |

**That is the plan, and it is four files for 76 of the 130.** Three of the four have a known,
named cause; `vm_object.c`'s is the one this host cannot fix without changing XNU's source.

## 3. Two defects in this stage's own tools, both the same shape

Both were found by running a checker against a case that should have passed — the only way a
checker gets tested — and both are instances of the project's recurring defect:

- **`stub_blockers.py` classified `.s` and `.cpp` files as "compiled"**, because it asked
  `failed.txt` and the build *skips* those extensions. An assembly file that cannot assemble is
  neither compiled nor failed, so it looked fine. It reported **30 boot-path symbols as "defined by a
  compiled file — should not be a stub"**, including `machine_routines_asm.s`. The test is now
  **whether an object exists for the file**, which is the question the build answers.
- **its definition regex missed names at column 0.** `_vm_object_allocate(` has no return type on
  its own line, so nothing matched, and **85 symbols were reported as "no source in the tree" when
  the source was right there**. A measurement that cannot see a definition looks exactly like a
  definition that is absent.

## What this does not change

The Mach-O-versus-ELF question is untouched, and it still owns the two deepest blockers: all 23
"assembly the build never attempts" are `machine_routines_asm.s` and `data.s`
(experiment-137). What this stage adds is that **the other 107 boot-path stubs are ordinary work
with named causes**, and that the assembly convention was a third thing standing on the same
question — `_bcopy` is what Apple's convention produces, and the ELF side has to be told otherwise
either by the flag (which it is, for the files that use the macro) or by the rename (for the ten
that do not).

## How to reproduce

```bash
./tools/gen_assym.sh                  # 266 defines
./tools/assemble_arm_layer.sh         # 13 ok, 4 failed; 22 symbols de-underscored
./tools/measure_link.sh --keep-stubs  # 416 stubs
./tools/stub_reach.py                 # 130 on the boot path
./tools/stub_blockers.py              # why each one is missing
```
