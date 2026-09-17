# Experiment 139 — two headers named `kern/ast.h`, and Apple's per-component include order

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/build_xnu_arm_kernel.sh`

| Configuration | before (experiment-138) | after |
| --- | --- | --- |
| `RELEASE` | 599 of 615 | **600 of 615** |
| `STAGE90_BOOT` | 406 of 426 | **407 of 426** |
| stub symbols in the image | 416 | **377** |
| boot-path stubs | 130 | **120** |

## The defect: two files, one include guard

```
external/xnu-4570.1.46/osfmk/kern/ast.h:63   #ifndef _KERN_AST_H_
external/xnu-4570.1.46/bsd/kern/ast.h:34     #ifndef _KERN_AST_H_
```

**XNU has two `kern/ast.h`** — the Mach side's and the BSD side's — and they guard themselves with
the same macro. They are not variations of each other: `AST_KEVENT_REDRIVE_THREADREQ`,
`AST_KEVENT_RETURN_TO_KERNEL`, `act_set_astbsd` and `kevent_ast` are in the BSD one and nowhere else
(`bsd/kern/ast.h:43-48`).

`bsd/kern/kern_event.c:101` includes `<kern/ast.h>` and uses `AST_KEVENT_REDRIVE_THREADREQ` at
`:6566`. With `osfmk` ahead of `bsd` on the include path, the Mach header is found, its guard is
taken, and **the BSD one is skipped** — so the error is `use of undeclared identifier` for a macro
in the file the line above asked for.

Thirteen files newly pass in `RELEASE` when this is fixed, `kern_event.c` among them, and the
measurement image loses **39 stub symbols**.

## Why a single flat include list cannot express it

Apple's order is per component, and the file's own component comes **first**
(`makedefs/MakeInc.def:463-469`):

```make
INCFLAGS_IMPORT = $(patsubst %, -I$(OBJROOT)/EXPORT_HDRS/%, $(COMPONENT_IMPORT_LIST))
INCFLAGS_GEN    = -I$(SRCROOT)/$(COMPONENT) -I$(OBJROOT)/EXPORT_HDRS/$(COMPONENT)
INCFLAGS        = $(INCFLAGS_LOCAL) $(INCFLAGS_GEN) $(INCFLAGS_IMPORT) ...
```

so a BSD file sees `bsd/` before `osfmk/`, and an osfmk file the reverse. `COMPONENT_IMPORT_LIST` is
`$(filter-out $(COMPONENT),$(COMPONENT_LIST))`, which is exactly "own component first, then the
others in order" — the thing experiment-117 noted a single build cannot reproduce, and this is the
first case where it *matters* rather than being an approximation.

The two component roots now move out of the fixed list and into a per-file placement, after the
generated roots and before everything else. That position is not free choice: **putting the
component ahead of MIG's output re-breaks the six `osfmk/vm` files experiment-124 fixed** — measured,
6 regressions — because `<mach/memory_object.h>` must come from the generated root for those.

## Two defects in the change itself, both caught by measuring

Both are the same shape as the defects this stage is about, which is why they are recorded rather
than quietly fixed:

- **The first version appended the component roots instead of prepending them.** `-I` order is left
  to right, so the array at the end of the command line made the component *last* and changed
  nothing — the compile count was identical and only reading the command showed why.
- **The second version put them before everything**, including the generated roots, which fixed
  `kern_event.c` and broke six `osfmk/vm` files. The build reported 594 of 615, down from 599, with
  six regressions and one newly-passing file — a net loss presented by a single number. Both halves
  had to be looked at before the fix was the fix.

## Where it stands

| | |
| --- | --- |
| `RELEASE` | 600 of 615 |
| image | 377 stubs, `_start` at `0x803b6074` |
| boot path | 120 stubs: 78 from failing files, 23 the assembly the build never attempts, 10 C++, 4 compiler runtime, 4 no source |

**Every number moved in the right direction and the goal is still not met** — XNU does not run. The
two deepest blockers are unchanged and both are the Mach-O-versus-ELF question: all 23
"assembly the build never attempts" are `machine_routines_asm.s` and `data.s` (experiments 137-138).

## How to reproduce

```bash
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh          # 600 of 615
./tools/measure_link.sh --keep-stubs       # 377 stubs
./tools/stub_reach.py                      # 120 on the boot path
```
