# Experiment 157 — `-include kern/queue.h` was rewriting declarations before the compiler saw them

Date: 2026-09-18
Host only — nothing here runs on the device.
Artifacts: `tools/build_xnu_arm_kernel.sh` and `tools/build_xnu_arm_layer.sh` (one line each)

| | before | after |
| --- | --- | --- |
| `RELEASE`, C | 608 of 615 | **608 of 615** |
| `RELEASE`, C++ | 78 of 83 | **80 of 83** |
| `STAGE90_BOOT`, C | 414 of 426 | **414 of 426** |
| `STAGE90_BOOT`, C++ | 78 of 83 | **80 of 83** |
| objects, `RELEASE` | 686 + 17 | **688 + 17** |
| objects, `STAGE90_BOOT` | 492 + 17 | **494 + 17** |
| undefined symbols | 189 / 330 | **189 / 330** — byte-identical lists |
| boot-path stubs | 42 / 50 | **42 / 50** |
| the 686 shared objects | — | **byte-identical**, all of them |

Nothing in XNU's source changed. The payload is unchanged (`stage90.bin` sha256 `7994f0ce…`) and the
tree is clean after every run.

## The error, and what it does not say

```
iokit/Kernel/IODataQueue.cpp:157:22: error: out-of-line definition of 'enqueue' does not match any
declaration in 'IODataQueue'
Boolean IODataQueue::enqueue(void * data, UInt32 dataSize)
                     ^~~~~~~
```

Nothing is wrong with that line. `iokit/IOKit/IODataQueue.h:131` declares exactly it:

```c
virtual Boolean enqueue(void *data, UInt32 dataSize);
```

The two are the same declaration and the same definition, in the same translation unit. The
preprocessed output is what settles it — the text at the header's line 131 is not what the header
says:

```
# 131 "/mnt/data/mi4-ios6/external/xnu-4570.1.46/iokit/IOKit/IODataQueue.h"
    virtual Boolean enqueue_tail(void *data, UInt32 dataSize);
```

`enqueue(` had become `enqueue_tail(` on the way through the preprocessor, because
`osfmk/kern/queue.h:224-225` defines **function-like macros** of exactly those names:

```c
#define enqueue(queue,elt)	enqueue_tail(queue, elt)
#define	dequeue(queue)		dequeue_head(queue)
```

and the build force-includes `kern/queue.h` into every translation unit. A function-like macro fires
on `name(` — it does not care that the argument list is a parameter list, that the name is qualified,
or that it is inside a class body. The declaration was rewritten; the definition at `.cpp:157` was
not, because `IODataQueue.cpp:47` does `#undef enqueue` — **after** including the header at `:31`.
The file knows about this macro. It undefines it four lines too late to save its own declaration,
which in Apple's build is irrelevant because Apple force-includes nothing at all.

The same file pattern is in `IOSharedDataQueue.cpp:205` (it declares both `enqueue` and `dequeue`
overrides), which is why the two failures were one failure.

## Why the force-include was there, and what removing it costs

`kern/queue.h` was in the list for `mpqueue_head_t`, which `osfmk/arm/cpu_data_internal.h` uses
without including it. The measurement says that reason was already obsolete: **removing the
force-include leaves all 686 objects byte-identical** — same `.text`, same symbols, same relocations —
and adds only the two objects that had been failing to compile. `cpu_data_internal.h` reaches
`mpqueue_head_t` another way.

That comparison was made object-by-object, and it matters that it was: the way to check a
force-include's blast radius is not to read the includes it pulls in but to build with it and without
it and compare. The macro users are the ones to worry about — `IOService.cpp:1845,1866`,
`osfmk/kern/host_notify.c:177`, `osfmk/kern/mach_node.c:547`, `bsd/kern/sys_ulock.c:326` all call
`enqueue(q, e)` and `dequeue(q)` as macros, and three of those five do not include `<kern/queue.h>`
themselves. They all still expand: no object contains an undefined `enqueue` or `dequeue`, in either
build. They reach the header transitively, so their expansions were identical either way.

`tools/build_xnu_arm_layer.sh` had the same line for the same reason and gets the same result —
**32 of 32 compile, all 32 objects byte-identical, the 445-symbol undefined list identical** — so it
is gone there too, and the two scripts still describe one configuration. Its comment claimed
`kern/queue.h, kern/ast.h` were needed for `mpqueue_head_t` and `ast_t`; half of that is now measured
false and the comment says so.

## The third force-include to cost more than it bought

The list is this project's, not Apple's, and this is the third time an entry in it has been the
problem rather than the fix:

| entry | what it did |
| --- | --- |
| `-include stdatomic.h` | reached `<stddef.h>` → `ptrdiff_t` → `libkern/zlib/zutil.h:193`'s `#if KERNEL` typedef: eight zlib files, removed (experiment-151) |
| a `stdbool.h` shim | fixed `OSAtomicOperations.c`'s `enum { false, true }` and broke **78** other files with the same shape; narrowed to one file (experiment-151) |
| `-include kern/queue.h` | two function-like macros rewriting a C++ declaration in a header the file includes before it undefines them: two IOKit files, removed |

The pattern in all three: a force-include is a **global** edit to the input of every translation unit,
and its cost is invisible because it lands in a place that reports something else. `stdatomic.h`
reported a `typedef` redefinition in zlib; `kern/queue.h` reported a C++ out-of-line definition that
did not match its own declaration. Neither message contains the name of the force-include, the macro,
or the header that was rewritten.

**How to apply:** an entry in a force-include list should carry the *measurement* that justifies it,
not the observation that motivated it — and the measurement is a build with the line and a build
without it, compared object by object. Three of these have now cost more than they bought, and in each
case the justification in the comment ("`cpu_data_internal.h` uses `mpqueue_head_t`") was a reason the
line might help, never a measurement that it does.

## Where the C++ block is now

Three files, and every one is a distinct thing:

| file | fails on | note |
| --- | --- | --- |
| `libkern/c++/OSKext.cpp` | `kxld_create_context` no matching function | a signature, not a header |
| `libkern/OSKextLib.cpp` | `kext_request` has a different language linkage | C++ vs C linkage on one name |
| `libkern/c++/OSRuntime.cpp` | `operator new[]` takes `size_t` = `unsigned int` | the target triple, same as `vnode_pager.c` |

The seven C failures are unchanged, and both configurations fail the same three C++ files — as they
have since the block entered the build (experiment-154), which is the expected result: these depend on
the language, not on which options are on.

## Reproduce

```bash
# with the line:      686 objects, C++ 78 of 83
# without the line:   688 objects, C++ 80 of 83, the other 686 byte-identical
XNU_KERNEL_OBJ_OUT=/tmp/obj_q   ./tools/build_xnu_arm_kernel.sh     # after restoring the line
XNU_KERNEL_OBJ_OUT=/tmp/obj_noq ./tools/build_xnu_arm_kernel.sh
for o in /tmp/obj_q/*.o; do cmp -s "$o" "/tmp/obj_noq/$(basename $o)" || echo "DIFFERS $o"; done
```

and to see the rewriting directly, preprocess one file and look at the header's own line number:

```bash
clang++ <the build's flags> -E iokit/Kernel/IODataQueue.cpp | grep -A1 'IODataQueue.h"$' | grep -n enqueue
# 131 ".../iokit/IOKit/IODataQueue.h"
#   virtual Boolean enqueue_tail(void *data, UInt32 dataSize);
```

The compile command for a single file is not printed by the build script; it can be captured with
`bash -x tools/build_xnu_arm_kernel.sh --dir iokit 2>&1 | grep -m1 "IODataQueue.cpp -o"`, which is how
this was found rather than guessed.
