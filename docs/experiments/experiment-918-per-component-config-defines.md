# 918 — Darwin-13's per-component configuration defines, and the `-U` ordering they must keep (2026-10-08)

917 §6 named the next wall: Darwin-13 compiles **per component**, each with its own
`<component>/conf/MASTER`, while `select_master.sh` read only `osfmk/conf/MASTER`. A union of the
seven MASTERs was tried and reverted (20 macros double-defined, a `vol` device condition with no
`conf/files` need-word). This step takes the other path — **give each file its own component's
defines** — and finds that the *position* of that slice on the command line is not free.

## 1. The fix: the configuration slice is per component

The build already computed a per-component CFLAGS set (`component_defines.sh`). The configuration
options now travel the same way:

- **`component_conf_dir()`** returns `$XNU/<component>/conf` when that component ships a
  `conf/MASTER`, else empty. The modern tree (4570) keeps **one** `config/MASTER`, so it returns
  empty there.
- The main compile loop computes **`CONFIG_COMP_DEFINES`**: the one global expansion when there is
  no per-component MASTER, otherwise `XNU_MASTER_DIR=<component conf> make_defines.sh CONFIG`.
  `select_master.sh` already reads `XNU_MASTER_DIR` from the environment, so no change was needed
  there.
- The four non-manifest blocks (EABI runtime, firehose, the two platform-expert loops) keep the
  global `CONFIG_DEFINES`, because their sources belong to no component.

This is exactly Apple's shape: `bsd/conf/Makefile:33-39` runs `doconf` once per component, so a BSD
file sees the BSD MASTER's `RELEASE` (with its own size attributes), not a union of all seven.
Result: **Darwin-13 RELEASE 325 → 354 of 367** manifest sources compile.

## 2. The trap: the slice must come *before* `DEFINES`, not after

The first version appended the slice **after** `$DEFINES` in the compile line. 4570 was then **not
neutral: 182 of 703 objects differed** from the committed baseline.

The cause is not the macro *set* — on 4570 `component_conf_dir()` is empty, so
`CONFIG_COMP_DEFINES` is byte-for-byte the same array as before, and every name has the same value
(`CONFIG_SCHED_TRADITIONAL=1` in both, etc.). It is **order**. `DEFINES` ends with two
*cancellation* flags:

```
    -UCONFIG_NO_PRINTF_STRINGS     # the configuration sets -D…=1; this cancels it
    -USECURE_KERNEL                # 472: RELEASE declares -DSECURE_KERNEL=1; this cancels it
```

clang resolves a macro by the **last** flag naming it. In the original layout `CONFIG_DEFINES` was
`DEFINES[0]`, i.e. *before* the two `-U`s, so the cancellation won. Moving the slice after `DEFINES`
put `-DSECURE_KERNEL=1` last, so the configuration **overrode its own cancellation**. The tell was
the build's own verdict flipping to *"SECURE_KERNEL is defined … this is a secure kernel"*, and the
linked-`cs_enforcement_enable` check failing. (472's whole point — `-U` after the `-D` — was
being undone.)

**Fix:** keep the slice **first**, mirroring the original single-array position. Both the manifest
loop and the four non-manifest blocks put `CONFIG_DEFINES`/`CONFIG_COMP_DEFINES` immediately after
`FORCE_INCLUDES` and before `DEFINES`.

## 3. The numbers, and neutrality

| tree | objects | failures |
|---|---|---|
| **4570** (neutrality control) | **703/703 byte-identical** to `out/xnu_kernel_obj_base` | 1 (pre-existing `osfmk/kperf/kperfbsd.c`) |
| **Darwin-13 RELEASE** | 354 of 362 attempted (367 manifest) | 8 |

`make check` exit 0; the `SECURE_KERNEL is undefined` verdict restored.

### The 8 remaining Darwin-13 failures

| file | error |
|---|---|
| `bsd/kern/bsd_init.c` | `loop.h` not found |
| `bsd/conf/param.c` | `confdep.h` not found |
| `bsd/dev/arm/conf.c` | host `<pty.h>` → `bits/wordsize.h` |
| `osfmk/kdp/{kdp,kdp_udp}.c`, `osfmk/kern/{startup,task}.c`, `osfmk/pmc/pmc.c` | `version.h:1:3: invalid preprocessing directive` — the HD2 fork's malformed `# HTC HD2 …` marker in `version.h.template`/`Makefile.template` |

## 4. What is NOT done

- The HD2 tree's `# HTC HD2` marker is a bare `#` in the two `.template` files (everywhere else the
  fork writes it as `/* … */`); the generated `libkern/version.h` inherits it and fails to
  preprocess. A generator-side strip, or a tree-side fix, is owed.
- `confdep.h` (the old config tool's per-component header) and `loop.h` are Darwin-13 BSD
  design details, not config-define issues.
- `bsdthread_*_args` incomplete (28 sites, generated `sysproto` detail).

*Provenance: `out/{4570_rebuild.log,d13_rebuild.log}`, `out/xnu_kernel_obj_base/`,
`out/xnu_kernel_obj_d13/`. Host-side, reversible, no press. Related: experiment-917,
[[mi4-917-darwin13-walls-closed]], [[mi4-one-value-two-definitions]].*