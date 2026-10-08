# 919 — The HD2 fork's shell-comment marker in the generated `version.h` (2026-10-08)

After 918 the Darwin-13 whole-kernel compile stood at **354 of 367**, and the largest single
failure cluster (5 of the 8 remaining files) was one line:

```
out/xnu_generated_d13/libkern/version.h:1:3: error: invalid preprocessing directive
# HTC HD2 integration/publication changes: Garysss123, 2026-10-04. Original license notices are preserved.
```

## 1. The marker is the fork's, and it is written two ways

The HD2 lab fork prepends a provenance line to every file it touches — **2,318 files** carry it. In
**2,249** of them it is a C comment:

```
/* HTC HD2 integration/publication changes: … */
```

and those compile. In **69** it is a **bare `#`** — a *shell* comment — and 68 of those are files
where `#` is legitimate (man pages, `*.sh`, `*.pl`, `*.y`). Exactly **one** is a C header:

```
external/xnu-hd2-darwin13/xnu/libkern/libkern/version.h.template:1:# HTC HD2 integration/…
```

## 2. Why it reaches the compile

`tools/gen_libkern_version.sh` copies `version.h.template` verbatim and then stamps it with
`newvers.pl` (Apple's own rule: `install -c … $< $@; $(NEWVERS) $@`). The bare-`#` line 1 rides
along into `<out>/libkern/version.h`, and every file that reaches `#include <libkern/version.h>` —
`osfmk/kern/startup.c`, `osfmk/kern/task.c` (via `osfmk/pmc/pmc.h`), `osfmk/kdp/kdp.c`,
`osfmk/kdp/kdp_udp.c`, `osfmk/pmc/pmc.c` — dies at the preprocessor, before any XNU code is read.

## 3. The fix lives in our generator, not the tree

`external/` is `.gitignore`d (the trees are vendored, untracked), and the defect is the fork's, so
editing the tree would be neither durable nor ours. `gen_libkern_version.sh` normalises the one
malformed marker to the C-comment form the fork uses in every other C file:

```sh
if head -1 "$OUT/libkern/version.h" | grep -q '^# HTC HD2 .*\.$'; then
    sed -i '1s;^# \(HTC HD2 .*\)$;/* \1 */;' "$OUT/libkern/version.h"
fi
```

**Inert on 4570:** its template's line 1 is `/*`, so the test never matches and the generated header
is byte-identical.

## 4. The number

**Darwin-13: 354 → 359 of 367.** The five `version.h` failures are gone. 4570 unchanged (the
condition is false; `make check` and the 703/703 byte control were re-run after 918).

### The 3 remaining Darwin-13 failures

| file | error | cause |
|---|---|---|
| `bsd/kern/bsd_init.c` | `loop.h` not found | Darwin-13 declares devices **per component** (`bsd/conf/MASTER`); the device table reads only `osfmk/conf/MASTER`, so `loop`/`pty`/`ptmx`/`ether`/`bpfilter` are missing |
| `bsd/dev/arm/conf.c` | host `<pty.h>` → `bits/wordsize.h` | the same missing device header, falling through to glibc |
| `bsd/conf/param.c` | `confdep.h` not found | the legacy BSD config tool's per-component header; genuinely absent from D13's source |

The first two are one cause: the device set is the next per-component wall (919 §5 / rung 920).

*Provenance: `out/d13_rebuild2.log`, `out/xnu_kernel_obj_d13/failed.txt`. Host-side, reversible, no
press. Related: experiment-918, [[mi4-918-per-component-config-defines]].*