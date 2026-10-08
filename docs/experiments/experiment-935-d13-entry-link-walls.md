# Experiment 935 — the D13 entry link, five more walls

Rung 934 made the *ceiling* switch tree-aware (D13 has no `MEM_SIZE_MAX`) and named the next wall: the
entry link's 4570-shaped object list. This rung walks the link five walls past that point, each closed
as the same recurring class — **an asset pinned to 4570 that must follow the selected tree**
(`mi4-913-ios7-rebase-decision`). Nothing pressed the device; all of this is host-side and reversible.

## The five walls, in the order pass 1 hit them

Each is a different *kind* of 4570 pin: a symbol's owner, a check's input path, a check's scheme, a
pool's contents, and a policy's condition grammar.

### 1. `fiq_context_init` — a 4570-only trace wrap

The trace wraps `fiq_context_init` to read the FIQ bank's copy of the timebase handler out of
`cpu_data`. On 4570 the symbol exists (`machine_routines_asm.s:916`, called from
`arm_init.c:385/467/528`). **D13 never defines or calls it** — `grep -rln fiq_context_init <D13>/` is
empty, and D13's `machine_routines_asm.o` has no such global. The `--wrap` therefore rewrites a
reference nothing defines, and the `__real_` half leaves `fiq_context_init` undefined in pass 1.

Fix, in `src/entry/build_entry.sh` + `src/entry/entry_timebase.c`:

- `FIQ_CTX_WRAP` is built from the tree: `--wrap=fiq_context_init` only where the tree's own
  `machine_routines_asm.s` names the symbol; the define `-DSTAGE90_ENTRY_FIQ_CTX=1` goes with it.
- `entry_timebase.c` guards the declaration, `g_fiq_ctx_calls`, the four `g_cpu_*` words, the
  `__wrap_fiq_context_init` function, and its report line, all under `#if STAGE90_ENTRY_FIQ_CTX` (a
  `#ifndef … #define 0` at the top makes the macro well-defined when the build does not set it).
- Both wrap sites (`TRACE_LDFLAGS`, `PASS1_LDFLAGS`) spell
  `${FIQ_CTX_WRAP[@]+"${FIQ_CTX_WRAP[@]}"}` so the empty array expands to nothing under `set -u`.

On 4570 the wrap is present and the flags are the exact words the line used to spell by hand.

### 2. `tools/check_assym_cswitch.py` — the input paths and the scheme

Two defects in one check, both because the check was written when 4570 was the only tree:

- **The paths.** `assym`/`object` defaulted to the *unsuffixed* 4570 roots. The check runs inside
  `build_entry.sh` (`:1426`), so on D13 it compared the D13 object it had just assembled against
  **4570's** `assym.s` and failed with three offsets differing by exactly the two trees' `struct
  thread` layout — the check working, on the wrong pair. Fixed by deriving the suffix from the tree's
  own discriminator (`osfmk/sys/types.h`, the same rule as `tools/xnu_tree_roots.sh`), reading
  `XNU_OBJ_SUFFIX` when the sourced shell already set it.
- **The scheme.** D13's `machine_load_context` (`osfmk/arm/cswitch.s:106-127`) is a **different
  context-switch sequence**: it writes `r0` straight into TPIDRURO (`mcr p15, 0, r0, c13, c0, 4`, no
  `CTH_SELF` load), loads one field `MACHINE_THREAD_CTHREAD_SELF` into `r1`, reads TPIDRURW into `r2`
  (`mrc`), and takes the register save area from `TH_PCB_ISS` — one load after the `mcr`, not three.
  `SCHEMES` now holds both arrangements; the tree picks. The D13 scheme also asserts the `mcr` is
  present, because a `machine_load_context` with no thread-register write is not the sequence at all.
  Both trees pass their own `--selftest` (all fields refused when moved by 16).

### 3. `tools/assemble_arm_layer.sh` — the pool is pruned to the manifest

The pool directory is per-tree (`_d13`), so trees do not mix. **A manifest change does not remove the
previous run's products, and the consumer is a glob**: `build_entry.sh`'s 436 step adopts every `*.o`
in `$XNU_ASM_OBJ_OUT` (930), gated only on a `.log` beside it. Measured cost: `out/xnu_asm_obj_d13/`
held `strncmp.o`/`strnlen.o`/`strlen.o` — the **4570** `osfmk/arm/*.s` files — left by an earlier run
into the same root. D13 has no such `.s` (its `subrs.c`/`loose_ends.c` define those in C), so the
stale copies became pass 1's `multiple definition of 'strncmp'`. The assembler now removes every `.o`
in its own output root that the selected manifest does not name (with its `.log`); it pruned 6 on
D13 and is a **no-op on 4570** (its pool already matches its manifest).

### 4. `build_entry.sh` — D13 splits `locore.o`, and the entry still delegates to Apple's handlers

4570's `locore.s` alone carries the vectors, the `fleh_*` handlers, `ResetHandlerData`, and the three
context returns; the entry image renames seventeen of its globals to `locore_*` so Apple's copy is
**unreachable except through the names `entry_vectors.s` delegates to** (slots 2/3/4/6 go to Apple's
`locore_fleh_{swi,prefabt,dataabt,irq}`). D13 splits that object across `cswitch.o` (the context
returns), `exctramps.o`/`exctramps_hi.o` (`ExceptionVectorsBase`, `HighExceptionVectorsBase`), and
`traps_lo.o` (the `fleh_*` handlers). The pool glob pulls all of them whole, and three collide with
what the entry image already defines:

| D13 pool object | name | entry definer | resolution |
|---|---|---|---|
| `traps_lo.o` | `fleh_dataabt/irq/prefabt/reset/swi/undef` | `entry_stubs.o` | renamed to `locore_*` in a copy that is linked |
| `exctramps.o` | `ExceptionVectorsBase` | `entry_vectors.o` | **refused** — the entry's page is the one that runs |
| `memcpy4.o` | `__aeabi_memcpy4` | `entry_rtabi.o` | **refused** — the alias already exists |

`traps_lo.o` gets the same treatment 4570's `locore.o` gets with the object changed: a copy with the
six colliding names renamed `locore_*` is linked (the rename set is the *intersection* with the
entry's own definitions, not "every name the object defines" — `fleh_dataexc`, which nothing here
defines, stays unprefixed so `osfmk_arm_cpu.o`/`exctramps.o` still resolve it). The other two join the
436 refusal list via a tree-gated `D13_EXTRA_REFUSE`, empty on 4570 so the refusal list is the exact
three names it was.

### 5. `tools/xnu_config/list_sources.py` — the attribute words in a condition

D13's `osfmk/conf/files.arm:12` is `optional vc device-driver`. Apple's reader
(`SETUP/config/mkmakefile.c:470-477`) **stops the option list at the attribute words** `device-driver`,
`profiling-routine`, `ordered`, `sedit`. Treating `device-driver` as a required condition made
`vc device-driver` unmet, so `video_console.o` — the definer of `vcputc` — was dropped from the
manifest while its caller `serial_console.o` was compiled, leaving `vcputc` undefined in pass 1. The
reader now drops the attribute words before deciding; the D13 manifest grew 728→743/751 paths and
4570's is **byte-identical** with and without the change (verified via `git stash`).

## State after this rung

The D13 entry link now reaches **`gPhysSize`**. It is declared by D13's `osfmk/mach/arm/vm_param.h:108`
(`extern unsigned long gVirtBase, gPhysBase, gPhysSize`) and **defined by nothing in D13** — 4570's
`arm_vm_init.c:80` defines it, D13's `:81` does not, and D13 has no `isphysmem()` caller. The reference
is `out/stage90/xnu_arm_start.o`, which is **4570's `osfmk/arm/start.s`** assembled by
`scripts/xnu_arm_assemble.sh` — that script hard-pins `XNU=$REPO_ROOT/external/xnu-4570.1.46` and D13
ships no `start.s` at all (its `_start` is in `locore.s`, `EnterARM(_start)`). That is the next wall
and it is architectural — D13's entry is a different file — so it is its own rung, not a path fix.

## Provenance

`src/entry/build_entry.sh`, `src/entry/entry_timebase.c`, `tools/assemble_arm_layer.sh`,
`tools/check_assym_cswitch.py`, `tools/xnu_config/list_sources.py`; D13
`osfmk/arm/{cswitch,exctramps,traps_lo}.s`, `osfmk/conf/files.arm`, `osfmk/arm/arm_vm_init.c`; this
session's `/tmp/d13_entry_build.log`. Host-side, reversible, no press. `make check` exits 0.