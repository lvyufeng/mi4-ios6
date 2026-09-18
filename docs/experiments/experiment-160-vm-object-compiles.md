# Experiment 160 — `vm_object.c` compiles, and 58 of the 189 close with it

Date: 2026-09-18
Host only — nothing here runs on the device.
Artifacts: `tools/build_xnu_arm_kernel.sh` (one per-file flag)

| | before | after |
| --- | --- | --- |
| `RELEASE`, C | 608 of 615 | **609 of 615** |
| `RELEASE`, C++, unchanged | 80 of 83 | 80 of 83 |
| undefined after a whole-kernel link | 189 | **132** |
| boot-path stubs, from `arm_init` | 42 of 189 | **15 of 132** |
| reachable from `arm_init` without hitting a stub | 2054 | **2119** |

No line of XNU's code was changed.

## The question, and why a closed finding was reopened

experiment-158 ranked the remaining work by what a whole-kernel link leaves undefined, and put one
file at the top: **`osfmk/vm/vm_object.c`, 58 of the 189** — a third of the gap, referenced by
`vm_map.o`, `vm_pageout.o`, `memory_object.o` and `bsd_vm.o`, the biggest names in the kernel.

experiment-127 had already examined this file and closed it:

> So the `const` was introduced in the 4570 line, Apple built it, and a current compiler does not
> accept the construct. […] the honest statement is that **this one file is not a configuration
> gap**: every other failure in this project's list has been a missing value, a missing header or a
> scope error, and this is a source construct that no flag, include order or macro on this host can
> make legal. **It is left failing, and recorded, rather than patched.**

Both halves of that are still true. The re-measurement below confirms the "no flag" claim with a
wider battery than the original had. What experiment-127 did not have is the *price*: it measured
`boot_closure.py`'s boot-path attribution, where this file is worth 2 symbols of 62, and wrote "a
fair measure of how much it matters". Measured against the whole-kernel link, it is worth 58 of 189.
The finding was correct and its conclusion was drawn from the wrong denominator.

## The construct, re-measured

`osfmk/vm/vm_object.c:355`:

```c
	*object = vm_object_template;
```

`osfmk/vm/vm_object.h:174`:

```c
	const unsigned int	wired_page_count; /* number of wired pages */
```

C11 6.3.2.1p1: a structure is not a modifiable lvalue if any member is const-qualified. So the
assignment is a constraint violation, and clang reports it as an **error with no `[-W...]` group**,
which is what makes the usual levers useless. Every one of these was run against a five-line
reproduction of the construct:

| | |
| --- | --- |
| `clang --target=armv7-none-eabi`, `-std=gnu89/gnu99/gnu11/gnu17` | error, all four |
| `arm-none-eabi-gcc` | `error: assignment of read-only location '*p'` |
| `-Wno-error -w` | unchanged — it is not a warning, `-w` is already on |
| `-fms-extensions`, `-fms-compatibility`, `-fms-compatibility-version=19` | unchanged |
| `-fno-strict-aliasing`, `-Wno-incompatible-pointer-types` | unchanged |

There is no flag that turns this error off, and the file is not otherwise at fault: everything it
needs is present.

## What does work, and the two things that decide its shape

A macro named `const` with an empty body — `-Dconst=` — makes the assignment legal, because there is
then no const-qualified member to object to. `nm --defined-only` on the resulting object gives **152
symbols**, and exactly **58** of them are in the 189. The attribution in experiment-158 is confirmed
symbol by symbol.

Two measurements decided how it is applied, and both went the "no" way first.

**It cannot be scoped to the header.** The obvious refinement is a force-include that neutralizes
`const` only while `vm/vm_object.h` is read. It fails, and the failure is instructive: a
declaration reached *inside* that window then disagrees with the same declaration reached outside
it. `stages/stage90/shims/kern/debug.h:6` and `osfmk/kern/debug.h:423` both declare `Debugger` —
read one `const char *` and the other `char *` and clang says "conflicting types for 'Debugger'". A
unit-wide macro cannot produce that class of disagreement, because every header has exactly one
reading. Placed *first* in the force-include list so that everything would be inside the window, it
fails differently and earlier — `osfmk/kern/sched.h:214` wants `u_int`, which the type
force-includes define and which now run after it. So the flag has to be defined before the first
token, which means it is an ordinary `-D` and not a header.

**It must be per-file.** Applied to the whole `osfmk/vm` directory it *costs* two files:

| `--dir vm` | pass | fail |
| --- | --- | --- |
| no flag | 27 of 29 | `vm_object.c`, `vnode_pager.c` |
| `-Dconst=` for the directory | 26 of 29 | `vm_object.c` now passes, but `lz4.c` and `vm_compressor_algorithms.c` break |
| `-Dconst=` for `vm_object.c` only | **28 of 29** | `vnode_pager.c` |

`osfmk/vm/lz4.h:68` is `static const size_t lz4_encode_scratch_size = lz4_hash_table_size;` — an
initializer that *needs* `const` to be a constant expression — and dropping it turns
`vm_compressor_algorithms.c:52`'s `uint8_t lz4state[lz4_encode_scratch_size]` into a
variable-length array in a struct, which is an error. One flag, three files, opposite directions.

## What the change buys, and what it does not

**189 → 132**, by both routes. 58 symbols closed and **one opened**:
`vnode_pager_issue_reprioritize_io`, which `vm_object.c` itself references (`CONFIG_IOSCHED`'s
reprioritize thread) and which `bsd/vm/vnode_pager.c` defines. A file entering the link brings its
references with it; the net is 57.

The two routes were made to agree on the *before* state as well, by moving the one new object out
and re-linking: 189 undefined, 42 boot-path stubs. So the change is 189 → 132 and 42 → 15 stubs on
the path from `arm_init`, with reachable-without-a-stub up from 2054 to 2119.

```
arm-none-eabi-ld -r -o /tmp/xnu_all_objs.o out/xnu_kernel_obj/*.o out/xnu_asm_obj/*.o
arm-none-eabi-nm -u /tmp/xnu_all_objs.o | wc -l        # 132
./tools/measure_link.sh --keep-stubs                   # 132, by the other route
./tools/stub_reach.py --from arm_init                  # 15 of 132
```

## The next file is a different kind of problem

`bsd/vm/vnode_pager.c` is the last failure in the directory, and it is **not** a source disagreement
like this one. `vm_protos.h:224` declares

```c
extern uint32_t vnode_trim (struct vnode *, int64_t offset, unsigned long len);
```

and `vnode_pager.c:211` defines `u_int32_t vnode_trim(struct vnode *, off_t, size_t)`. Preprocessing
the file shows `u_int32_t`, `uint32_t` and `__uint32_t` all resolving to `unsigned int` — the return
types agree. The **third parameter** does not: `unsigned long` versus `size_t`.

`size_t` here is `unsigned int` because of the target triple. `bsd/arm/_types.h:67-71` is

```c
#if defined(__SIZE_TYPE__)
typedef __SIZE_TYPE__		__darwin_size_t;	/* sizeof() */
#else
typedef unsigned long		__darwin_size_t;	/* sizeof() */
#endif
```

— Apple's own header takes `__SIZE_TYPE__`, and clang defines that per target: `long unsigned int`
for `armv7-apple-ios`, `unsigned int` for `armv7-none-eabi`. Apple's kernel is the first; this
project's ARM build is the second, because experiment-150 closed the Mach-O path (`ld64.lld` cannot
link 32-bit ARM Mach-O) and the ELF triple is what is left.

So the remaining work has a component that is not a file at all but the **target's ABI**. It is
already named in this project's notes — the C++ failures include "`operator new[]`'s `size_t` (the
target triple, same as `vnode_pager.c`)" — and it is measurable in one command:

```bash
XNU_KERNEL_EXTRA_DEFINES='-D__SIZE_TYPE__=long unsigned int' ./tools/build_xnu_arm_kernel.sh
```

That is the next stage. This one stops at one file, one flag, and a number that moved.

## Reproduce

```bash
./tools/build_xnu_arm_kernel.sh                          # 609 of 615 C, 80 of 83 C++
./tools/build_xnu_arm_kernel.sh --dir vm                 # 28 of 29, only vnode_pager.c left
./tools/measure_link.sh                                  # 132 undefined, 132 stubs
```

and the two negative controls, which is where the shape of the fix comes from:

```bash
# the scoped form, which fails with "conflicting types for 'Debugger'"
# -include a header containing:   #define const / #include <vm/vm_object.h> / #undef const

# the directory-wide form, which costs lz4.c and vm_compressor_algorithms.c
XNU_KERNEL_EXTRA_DEFINES='-Dconst=' ./tools/build_xnu_arm_kernel.sh --dir vm
```
