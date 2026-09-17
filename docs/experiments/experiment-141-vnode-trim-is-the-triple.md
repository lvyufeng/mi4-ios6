# Experiment 141 — `vnode_trim` is a target-triple conflict, and it cannot be fixed by a macro

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/build_xnu_arm_kernel.sh`

| Configuration | before (experiment-140) | after |
| --- | --- | --- |
| `RELEASE` | 602 of 615 | 602 of 615 |
| `STAGE90_BOOT` | 409 of 426 | 409 of 426 |
| image stubs | 362 | 362 |
| boot path | 113 | 113 |

**No count moved.** This stage is one investigation that produced a definite negative result, and it
is the fourth independent thing pointing at the same decision, so it is worth more than the count
suggests.

## The conflict is `size_t`

```
osfmk/vm/vm_protos.h:224   extern uint32_t vnode_trim (struct vnode *, int64_t offset, unsigned long len);
bsd/vm/vnode_pager.c:211   u_int32_t vnode_trim (struct vnode *vp, off_t offset, size_t length)
```

The two declarations differ in one parameter: `unsigned long` against `size_t`. Reduced to a minimal
case, clang calls that a hard error on this target:

```c
extern unsigned int f(struct vnode *, long long, unsigned long);
unsigned int f(struct vnode *vp, long long offset, unsigned int length) { return 0; }
→ error: conflicting types for 'f'
```

And the reason `size_t` is `unsigned int` here is the triple, not the code — checked with
`_Static_assert(__builtin_types_compatible_p(...))`, which works as a discriminator:

| target | `__SIZE_TYPE__` |
| --- | --- |
| `armv7-none-eabi` | `unsigned int` |
| `armv7-apple-darwin` | `unsigned long` |

XNU's own `bsd/arm/_types.h:67-71` says `typedef __SIZE_TYPE__ __darwin_size_t` **when
`__SIZE_TYPE__` is defined, and `unsigned long` otherwise** — so Apple's build and this one agree
with their respective compilers, and XNU's `vm_protos.h` was written for the compiler where
`size_t` is `unsigned long`. **The two headers disagree with each other only under the ELF triple.**

## And the command line cannot change it

The obvious fix — `-D__SIZE_TYPE__=long` — **does not work**, and this was measured rather than
assumed:

```
$ clang --target=armv7-none-eabi -D__SIZE_TYPE__=long -c probe.c
<command line>:1:9: warning: '__SIZE_TYPE__' macro redefined
<built-in>:91:9: note: previous definition is here
```

clang's builtin is applied **after** the command line, so the builtin wins. Confirmed with a
`_Static_assert` on `__builtin_types_compatible_p(size_t, unsigned long)`: it fails **both with and
without** the `-D`. (A first probe compared `sizeof`, which is 4 either way on ARMv7 and settled
nothing — a measurement that cannot distinguish the two cases looks exactly like a measurement that
says they are the same.)

And adding the flag to the whole build changes nothing at all: **602 of 615, no regressions, no new
passes** — which is the same negative result from the other direction.

## What that leaves, and why it matters beyond one file

`vnode_trim` cannot be fixed by a flag, an include, or a header. It is option A (the Mach-O triple)
or a source edit — and this project does not edit XNU's source.

It is also **the fourth independent thing this session has found pointing at the same choice**:

| | |
| --- | --- |
| experiment-123 | the triple compiles one more file and produces objects nothing here can link |
| experiment-137 | 23 of the boot path's stubs are `data.s` and `machine_routines_asm.s`: Mach-O section directives and a positional-macro dialect no assembler here accepts |
| experiment-138 | `_bcopy` — the underscore convention Apple's side expects and the ELF side has to be told otherwise |
| experiment-141 | `size_t`, where XNU's own two headers disagree only under ELF |

## One thing that did change, and it moves no count

`osfmk/kperf/kperfbsd.c` fails on `clock_t`: it is an **osfmk** file that includes
`bsd/libkern/libkern.h` → `bsd/sys/types.h` → `_clock_t.h`, so it sees both definitions at once.
experiment-126 removed `-D_CLOCK_T` because the per-component defines made the two halves agree —
and they do, for files that stay on one side. This one does not.

`-D_CLOCK_T=1` is now given to **osfmk files only**, which takes `_clock_t.h`'s guard so
`kern_types.h`'s definition survives — the same restriction, for the same reason, as the BSD-only
`-include sys/types.h` from experiment-140. The conflict disappears and the file moves on to its
next error, `ffs`/`fls` from `kern/misc_protos.h` against `bsd/libkern/libkern.h` — which is
experiment-118's defect arriving from the other side, in a file that genuinely straddles the
Mach/BSD boundary. **One file, and no flag set satisfies both views of it.**

## Where it stands

`RELEASE` 602 of 615, `STAGE90_BOOT` 409 of 426, image 362 stubs with `_start` at `0x803b7074`,
boot path 113 stubs. The 13 remaining failures are now individually distinct, and the four biggest
groups are: 4 files on `sync_qos_count_t`, 2 on the firehose chunk count, 2 conflicting-types cases
(this one and `task_collect_crash_info`), and the rest single files with their own causes.

## How to reproduce

```bash
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh          # 602 of 615
clang --target=armv7-none-eabi -D__SIZE_TYPE__=long -c probe.c    # the builtin wins
clang --target=armv7-apple-darwin ...                                # size_t is unsigned long
```
