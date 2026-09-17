# Experiment 148 — `NBPFILTER` is a count, and `if_bridge.c` uses a macro outside its own guard

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/gen_device_headers.sh`

| Configuration | before (experiment-147) | after |
| --- | --- | --- |
| `RELEASE` | 607 of 615 | 607 of 615 |
| `STAGE90_BOOT` | 413 of 426 | 413 of 426 |

No count moved. This is the last of the eight investigated, and it belongs to a category with two
other members.

## The diagnosis

`bsd/net/if_bridge.c`:

```c
:134   #if NBPFILTER > 0
:135   #include <net/bpf.h>
:136   #endif
...
:1419  error = bpf_attach(ifp, DLT_EN10MB, sizeof (struct ether_header), NULL, NULL);
```

`DLT_EN10MB` is `bsd/net/bpf.h:277`, **unguarded** in that header. So the macro is missing only
because `bpf.h` is never included — `NBPFILTER` is undefined, and `#if NBPFILTER > 0` on an
undefined identifier is the `0` branch. **`if_bridge.c:1419` sits outside the guard its own include
is inside.**

`NBPFILTER` is not an option. `bsd/net/net_osdep.h:212-215` records the convention in the source
itself:

> `- number of bpf pseudo devices`
> `others: bpfilter.h, NBPFILTER`

**It is a count, like `NLOOP` and `NPTY`** — a `pseudo-device bpfilter N` declaration 4570 does not
publish (experiment-127) — and the sources test it as `NBPFILTER > 0` for that reason.

And the configuration does not agree with itself:

```
RELEASE defines  -DIF_BRIDGE=1
                 (and not BPFILTER)
```

so `if_bridge.c` is built and `bpf.c` is not. `bsd/net/bpf.c` is `optional bpfilter`, so turning the
count on would require the option too, which pulls the packet-filter machinery into a configuration
built for a first boot.

## Recorded, not turned on

`gen_device_headers.sh` now writes `bpfilter.h` with `#define NBPFILTER 0`, and the comment says what
0 costs and what changing it would require. **The honest statement is that this file cannot be made
to compile by a header value**: `NBPFILTER 0` is correct for this configuration, and `NBPFILTER 1`
alone would compile `if_bridge.c` while leaving `bpf_attach` undefined at link time — a move sideways
presented as progress, which is exactly what experiment-132 declined to do with `NPTY`.

## The category this belongs to

Three files now fail because **a source path is only valid with an option built, and the file builds
either way**:

| file | the mismatch |
| --- | --- |
| `bsd/dev/arm/conf.c` | `NPTY 0`'s `#else` branch is missing `ptsselect`, which exists nowhere (experiment-130) |
| `bsd/net/if_bridge.c` | `DLT_EN10MB` outside `#if NBPFILTER > 0` (this experiment) |
| `bsd/net/if_loop.c` | `optional loop` with nothing providing `loopattach` (experiment-127) |

All three are Apple source that does not compile in a configuration Apple presumably never built —
and all three are answerable only by a **configuration decision** (turn the option on and take its
dependencies), not by a header, a flag or an include order.

## The eight, final

| file | why | category |
| --- | --- | --- |
| `vm_object.c` | `const` member assigned | **not fixable here** |
| `subr_prof.c` | malformed source, identical upstream | **not fixable here** |
| `vnode_pager.c` | `size_t` — the target triple | needs the toolchain decision |
| `kperfbsd.c` | `ffs`/`fls` — the Mach/BSD view collision | needs the include-model decision |
| `OSAtomicOperations.c` | `false`/`true` are macros by then | needs Apple's header order (unknown) |
| `log.c`, `subr_log.c` | `FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT` | **a value with no evidence in the tarball** |
| `if_bridge.c` | `NBPFILTER` | needs a configuration decision |

**Which is 607 of 615, and no two of the eight share a cause.** That is the end of the host-side
compile work as a source of movement, and the project's own rule says so: each remaining file needs a
decision, a fact from outside the tarball, or a source edit this project does not make.
