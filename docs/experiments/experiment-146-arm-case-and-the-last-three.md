# Experiment 146 — `__ARM__` versus `__arm__`, and the three single-file failures left

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/build_xnu_arm_kernel.sh`

| Configuration | before (experiment-145) | after |
| --- | --- | --- |
| `RELEASE` | 606 of 615 | **607 of 615** |
| `STAGE90_BOOT` | 412 of 426 | **413 of 426** |

The nine failures after experiment-145 were individually distinct, so this stage took them one at a
time. Three diagnosed, one fixed, and the other two are recorded as things no flag can address.

## 1. Fixed: `kern_sysctl.c`, where a case difference was the whole defect

```c
/* bsd/kern/kern_sysctl.c:2772 */
#if defined(__ARM__)
SYSCTL_INT(_vm, OID_AUTO, global_no_user_wire_amount, ...);
#else
SYSCTL_QUAD(_vm, OID_AUTO, global_no_user_wire_amount, ...);
#endif
```

The build defines `-D__arm__=1` — the lowercase spelling the compiler itself uses, and which the
whole ARM tree is already written against (`proc_reg.h:73`'s `defined(ARMA7)`, `asm.h`'s
`defined(__arm__)`). **`kern_sysctl.c` uses the uppercase `__ARM__`**, so the `#else` was taken, the
64-bit `SYSCTL_QUAD` form was used for 32-bit values, and the failure was

```
kern_sysctl.c:2776:1: error: '_sysctl__vm_global_no_user_wire_amount_size_check'
                      declared as an array with a negative size
```

**An error that names neither the macro nor the file that uses it** — it is the `SYSCTL_QUAD`
expansion's own static size assertion firing. Both spellings are now defined; one file newly passes,
no regressions.

That is the ninth instance of the *one value, two definitions* class, and its cleanest small case:
two spellings of the same idea, one of them in the source and one on the command line, and nothing
comparing them.

## 2. Not fixable by a flag: `subr_prof.c` is malformed source

```c
/* bsd/kern/subr_prof.c:160-163 */
STATIC int
sysctl_doprofhandle SYSCTL_HANDLER_ARGS
{
sysctl_doprof(int *name, u_int namelen, user_addr_t oldp, size_t *oldlenp,
              user_addr_t newp, size_t newlen)
{
```

`SYSCTL_HANDLER_ARGS` expands correctly (`sys/sysctl.h:185`, under `#ifdef KERNEL`), and the
replacement of that line in the preprocessed output is well-formed. Two things are wrong:

- **`STATIC` is defined nowhere in the tree except in two other `.c` files** — `kern_sysctl.c:204-207`
  and `kern_newsysctl.c:100` — as a file-local macro. Nothing a header could supply reaches
  `subr_prof.c`. It is a loose end in the source.
- the second line is a **function definition nested inside the first function's body** — a GCC
  extension, which then fails to parse as C.

And it is **identical in `xnu-upstream`**, so it is a long-standing malformation in the OSS drop
rather than something 4570 introduced. Left failing and named, like `vm_object.c`'s `const` member
(experiment-127): **the second file in this project that no flag can fix.**

## 3. Diagnosed, not yet fixed: `if_bridge.c` and `DLT_EN10MB`

`bsd/net/if_bridge.c:135` includes `<net/bpf.h>` and `:1419` uses `DLT_EN10MB`, which is
`bsd/net/bpf.h:277` — **unguarded, in a header the file includes.** So the macro should be there and
is not, which means the include is resolving elsewhere or the region is skipped. Not pursued this
stage; it has the shape of the two above (a name that should be reachable and is not) and needs the
same treatment: preprocess the file and read what `bpf.h` actually contributes.

## State, and what the 8 remaining are

| | |
| --- | --- |
| `RELEASE` | **607 of 615** |
| `STAGE90_BOOT` | **413 of 426** |
| image | 282 stubs, `_start` at `0x803cc074` |
| boot path | 89 stubs, unchanged |

The eight: `vm_object.c`'s `const` member and `subr_prof.c`'s malformed source (**not fixable here**);
`OSAtomicOperations.c`'s `false`/`true` macros and `kperfbsd`'s `ffs`/`fls` (both the Mach/BSD view
collision); the firehose chunk count (two files, a value with no evidence in the tarball);
`vnode_pager.c`'s `vnode_trim` (the target triple); `if_bridge.c`'s `DLT_EN10MB`.

**Two of eight need the toolchain decision, two cannot be fixed by any flag, one needs a value
nothing in the tarball supplies, and one is diagnosed and pending.** That is the honest shape of what
is left on the host side.

## How to reproduce

```bash
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh          # 607 of 615
./tools/measure_link.sh --keep-stubs       # 282 stubs
./tools/boot_closure.py                    # boot path
```
