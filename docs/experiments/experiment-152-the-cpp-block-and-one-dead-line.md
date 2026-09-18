# Experiment 152 — the whole C++ block is behind one dead line, and no flag can fix it

> **CORRECTED 2026-09-18 by [experiment-154](experiment-154-cpp-entered-the-build.md).** The
> headline is wrong and so is the recommendation it leads to. The "83 of 83 fail on
> `osfmk/kern/misc_protos.h:254`" measurement came from a hand-written compile loop that passed
> `-DMACH_KERNEL_PRIVATE` and `-DMACH_KERNEL` for **every** file; both are *component* defines, and
> no `libkern` or `iokit` file gets either, so no `.cpp` in the manifest reaches that line at all.
> Compiling them in the build, with the build's flags, gives **46 of 83** — and **75 of 83** once
> five missing declarations are supplied. **The source edit this page puts in front of the reader is
> not needed and would have bought nothing.** Everything below about the line itself (it is valid C
> and invalid C++, it is used nowhere, it is the only one of its kind in the tree) is correct; what
> is wrong is which translation units reach it, and that is the whole finding. Kept unedited
> underneath as the record of how the mistake was made.

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `stages/stage90/shims_arm/string.h` (the C++ `NULL`)

The C++ side was the last unmeasured piece: 83 manifest `.cpp` files, **none of them attempted** by
`build_xnu_arm_kernel.sh`, accounting for 10 of the boot path's 51 stubs. It is now measured, and the
answer is unusually sharp.

## All 83 fail on the same line

```
83  osfmk/kern/misc_protos.h:254:8: error: definition of type 'kmod_info_t' conflicts with
    typedef of the same name
```

`osfmk/kern/misc_protos.h:253-254`:

```c
/* symbol lookup */
struct kmod_info_t;
```

and `osfmk/mach/kmod.h:100` ends `} kmod_info_t;` — a typedef. **`struct kmod_info_t;` after a
typedef of that name is valid C and invalid C++**, reduced to four lines:

```c
typedef struct { int a; } foo_t;
struct foo_t;                  /* C: fine.  C++: error: definition of type 'foo_t'
                                  conflicts with typedef of the same name */
```

And **the declaration is used nowhere**: `misc_protos.h` mentions `kmod_info` exactly once — in that
line. It is a dead forward declaration under a `/* symbol lookup */` comment.

**No flag fixes it.** Tested: `-fms-extensions`, `-fpermissive`, `-fno-ms-compatibility`,
`-fdelayed-template-parsing`, and g++ — all reject. It is a language difference, not a diagnostic.

## And it is the *only* thing in the tree of its kind

```
$ grep -rhn "^struct [a-z_]*_t;" --include=*.h osfmk/ bsd/ libkern/ iokit/ | wc -l
1
```

One line. **One dead line in one header blocks 83 files, 13% of the kernel, and none of them can
report anything else** — a fatal error in a header ends the translation unit, so every C++ file's
first error is this one.

## Measured, by removing it in a scratch copy

Not a fix — a counterfactual, to put a number on what the line costs. A copy of `misc_protos.h` with
that line deleted, placed first on the include path for the sweep only:

| | C++ files compiling |
| --- | --- |
| as shipped | **0 of 83** |
| that one line removed | **4 of 83** |
| ...and with the per-component `-D_CLOCK_T` the C side already uses | **4 of 83** |

and the errors immediately become the *same chain the C side already worked through* —
`kern_types.h`'s `clock_t` (experiment-126), then `IMIGObjectVtbl`, then `kqueue_id_t`. **The C++
block is not a different problem; it is the C block's problem, entered through one line that no flag
can remove.**

## The one thing this stage did fix

`stages/stage90/shims_arm/string.h` defined `NULL` as `((void *)0)`. **That is not a null pointer
constant in C++** — `void *` does not implicitly convert to another pointer type — so every
`return NULL;` in a `.cpp` was an error. Found because the first thing the sweep revealed after the
`kmod_info_t` line was `osfmk/kern/cdata.h:1119` returning `NULL` from a `char *` function. Standard
`<stddef.h>` uses `0` in C++ for exactly this reason; the shim now does too, under `#ifdef __cplusplus`.

It changes no count — the C build is 608 of 615 before and after, `osfmk/arm` is 32 of 32, the payload
rebuilds and passes its gate — and it is recorded because it is a real defect that the C++ attempt
surfaced, and the C++ attempt is the only reason it would ever have been found.

## What this means for the plan

**The C++ block is now characterized**, and it belongs to the same category as `vm_object.c` and
`subr_prof.c`: **the fix is a source edit this project does not make.** The difference is leverage —
one line, 83 files, 10 boot-path stubs — which makes it the single highest-value line in the
repository and the clearest instance of the project's rule meeting its cost.

Stated plainly for whoever decides: deleting `osfmk/kern/misc_protos.h:254` is a one-line change to a
file Apple published, in a declaration used nowhere in it. It would make the C++ block *attemptable*
for the first time, and it would not be the last thing that block needs.
