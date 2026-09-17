# Experiment 137 — the work order, computed from the image

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/stub_reach.py` (new)

| | |
| --- | --- |
| functions in the image | 8 063 |
| reachable from `arm_init` without hitting a stub | 1 846 |
| stubs the boot path reaches | **136 of 427** |

Experiment-135 produced an image whose 427 missing symbols are stubs. The obvious next question —
*which of them must become real first?* — had no answer beyond "all of them", so this computes it:
walk the call graph from the entry, stop at each stub, and rank what is left by how few edges away
it is.

## What it found, and the shape was not what was expected

```
  edges  stub                              via
      1  __aeabi_memcpy4                    arm_init
      1  fiq_context_init                   arm_init
      1  get_mmu_control                    arm_init
      1  set_mmu_control                    arm_init
      2  IODTGetDefault                     PE_get_default <- arm_init
      2  __aeabi_memcpy8                    thread_bootstrap <- arm_init
      2  bcopy                              PE_init_platform <- arm_init
      2  flush_mmu_tlb                      arm_vm_init <- arm_init
      2  memcpy                              PE_get_default <- arm_init
      2  ml_get_timebase                    early_random <- arm_init
      2  set_mmu_ttb                        arm_vm_init <- arm_init
      2  set_mmu_ttb_alternate              arm_vm_init <- arm_init
```

**`arm_init` reaches a stub on its first call.** Not after mounting a filesystem, not after the
scheduler — one edge in. So the answer to "what would the first crash be" is: *the third callee in
the first non-entry function*.

And the stubs are not spread across the 427. They are **concentrated in two assembly files**:

| file | stubs it defines | what blocks it |
| --- | --- | --- |
| `osfmk/arm/machine_routines_asm.s` | **35** | `.macro COPYIO_BODY` invoked with a positional argument |
| `osfmk/arm/data.s` | **6** | `.section __DATA, __data` — Mach-O section syntax |
| all others | 386 | — |

That is 41 of 427 from two files, and they are the two *deepest* on the boot path. `data.s` alone
defines `BootCpuData`, `CpuDataEntries`, `intstack_top` and `fiqstack_top` — the CPU-data structure
`_start` and `arm_init` are built around, and the guard `ml_init_timebase` checks
(`cpu_data_ptr == &BootCpuData`, spec §2.2).

## Both are the same question, and it is not a value to choose

Neither failure is a missing macro, a missing header or a number nobody wrote down. Every fix since
experiment-119 has been one of those; these two are **the target format and the assembler dialect**.

**`data.s:41`** is `.section __DATA, __data` — Apple's assembler takes `<segment>, <section>`, GNU as
wants `.section <name>, "<flags>"`, which is why the error is `expected string in directive`. XNU's
kernel is Mach-O; this build emits ELF.

**`machine_routines_asm.s:696`** is `.macro COPYIO_BODY` — declared with **no parameters** and
invoked as `COPYIO_BODY copyin`, with the body referring to the argument as `$0`. That is the classic
ARM assembler convention (positional `$0`), and this was tested against both assemblers on this host:

| | `.macro FOO` + `FOO bar` |
| --- | --- |
| clang's integrated assembler | `error: Wrong number of arguments` |
| `arm-none-eabi-as` | `Error: too many positional arguments` |

and declaring a parameter does not rescue it — `$0` then produces
`warning: positional parameter found in body which will have no effect`. So Apple's assembler
accepted a form that no assembler on this host does; fixing it means changing XNU's source or
transforming its input, and **that is a decision rather than a mechanical step**.

Both of them, therefore, come back to the choice experiment-123 measured and did not take:
**`--target=armv7-apple-darwin`**. It compiles one file more (390 vs 389), it produces Mach-O
objects — which is what `data.s`'s directives are written for, and what a Mach-O assembler dialect
would accept — and **nothing on this host can link them**: `/usr/lib/llvm-14/bin` has `llvm-nm`,
`llvm-size`, `llvm-objdump` and `llvm-ar` but no `ld64.lld`.

So the honest statement of the next step is: **41 of the 427 stubs, and the three nearest to
`arm_init`, are behind one question — Mach-O or ELF — and the answer requires a linker this host does
not have.** That is worth deciding deliberately rather than discovering, which is the same conclusion
`docs/status/roadmap.md` reaches about Phase 4 for a different reason.

## What the tool is not

An **indirect call through a function pointer is invisible** to it — the walk follows direct `bl`
targets that resolve to a symbol and nothing else. So 1846 reachable functions is a lower bound, and
"136 stubs" is a lower bound on what a boot reaches. `_start` itself has no parsed edges at all: it
loads `arm_init`'s address into `lr` and tail-calls, which is why the walk starts at `arm_init`
rather than at the entry — a fact the tool reports rather than hides.

## How to reproduce

```bash
./tools/measure_link.sh --keep-stubs
./tools/stub_reach.py --from arm_init --list 12
```
