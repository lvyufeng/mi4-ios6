# Experiment 153 — the shims audited: 16 shadow real headers, and none of them matter

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: none — an audit with a measurement, no change

The project has 26 hand-written headers in `stages/stage90/shims/` and `shims_arm/`. Sixteen of them
**have the same path as a real header in the XNU tree**, which is this project's most-repeated defect
class — a hand-written file that shadows a real one, four times over five turns
(experiments 117, 120, 124, 138). So they were audited against the tree, and the answer is the
opposite of alarming.

## What is shadowed, and why it does not matter

```
SHADOW: libkern/OSAtomic.h              (tree: libkern/libkern/OSAtomic.h)
SHADOW: kern/kern_types.h               (tree: osfmk/kern/kern_types.h)
SHADOW: kern/debug.h                    (tree: osfmk/kern/debug.h)
SHADOW: kern/kalloc.h                   (tree: osfmk/kern/kalloc.h)
SHADOW: sys/appleapiopts.h, sys/types.h (tree: bsd/sys/...)
SHADOW: machine/machine_routines.h      (tree: osfmk/machine/machine_routines.h)
SHADOW: mach/{kern_return,boolean,mach_types,vm_types}.h, mach/machine/vm_types.h
SHADOW: pexpert/{protos,pexpert,boot}.h, pexpert/arm/consistent_debug.h
```

**Every one of them is inert in the kernel build, and that is by construction.** The build places
`-I$SHIMS` and `-I$SHIMS_ARM` **last** in the include order (`build_xnu_arm_kernel.sh:269-271`), after
every real component root, so a real header always wins. Checked directly rather than inferred:

```
$ printf '#include <kern/kern_types.h>\n' | clang ... -I$XNU/osfmk -I.../shims/kern -E -H -
 . /mnt/data/mi4-ios6/external/xnu-4570.1.46/osfmk/kern/kern_types.h      <- the tree's, not the shim's
```

and removing `-I$SHIMS` from the build entirely:

| | files compiling |
| --- | --- |
| with `shims/` on the path (last) | **608 of 615** |
| with it removed | **490 of 615** |

**125 regressions.** So the directory is not dead weight either — it is load-bearing *as a fallback*,
and the shadowing is the price of being a fallback. That is the opposite arrangement from the four
earlier cases, where a shim was **first** on the path and hid a real header (`security/_label.h`,
`san/kasan.h`), or where a whole source directory went on the path and shadowed by existing
(`osfmk/libsa`).

**The rule that separates the two:** a shim may shadow a real header if and only if it is placed
where it can only ever be reached when the real one is not. Four failures came from breaking that
rule; this directory obeys it.

## The ten that shadow nothing

`shims_arm/` holds ten headers with no counterpart in the tree, and they are the ones the kernel
build actually needs:

```
TargetConditionals.h            string.h                mi4ios6_build_config.h
sys/_posix_availability.h       sys/_symbol_aliasing.h  os/firehose_buffer_private.h
chud/arm/chud_xnu_private.h     sys/_pthread/_pthread_types.h
System/mach/clock_types.h       System/mach/resource_monitors.h
```

Each has a reason recorded in its own comment, and they fall into three kinds: **build-generated**
(`_symbol_aliasing.h`, `_posix_availability.h`, `TargetConditionals.h`), **the project's own
configuration** (`mi4ios6_build_config.h`), and **genuinely unpublished**
(`_pthread_types.h`, `firehose_buffer_private.h`, `chud_xnu_private.h`) — the last of which
experiment-132 narrowed to a single constant, `FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT`, that the tarball
does not contain.

## And the payload's copy of `shims/` is a third thing

`stages/stage90/shims/` is used by `xnu_object_subset_compile.sh` for five pexpert objects — a
**separate, older** build graph from the kernel build, with its own flag set. Its audit is separate
and was not done here; what was checked is that it still runs and that the payload still builds and
passes its gate, both true.

One thing found while checking: `xnu_object_subset_compile.sh` reports
`failure_mask=0x80000010` and has since before this session's changes — verified by running it against
`26f14c5`, the commit this session started from, with the same mask. **Recorded so it is not mistaken
for a regression later**, and not chased.

## Why this is worth a stage

Because it is the first audit in this project that came back **clean**, and saying so is what makes the
other four credible. Each of those was found by noticing a shim whose contents did not match reality;
the natural inference is that the rest are the same. They are not — they are a working fallback with
one specific, checkable property, and the property is now stated.
