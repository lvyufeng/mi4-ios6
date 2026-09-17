# Experiment 149 — the Mach-O path, built and measured, so the decision has a price tag

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/build_xnu_arm_macho.sh` (new)

The three-way choice (A: Mach-O toolchain / B: stay ELF / C: stop) has been open for several turns,
and every argument for it so far has been a *finding* rather than a *measurement of the choice*. This
stage builds the Mach-O path as far as it goes without installing anything, so the decision has
numbers on both sides.

**Nothing was installed, and the ELF build is untouched** — the two live in separate output
directories and neither reads the other.

## What the Mach-O path gives, measured

| | `armv7-none-eabi` (ELF) | `armv7-apple-darwin` (Mach-O) |
| --- | --- | --- |
| manifest `.s` that assemble | 13 of 17 present | **17 of 17 present** |
| manifest `.c` that compile | **607 of 615** | 575 of 615 |
| boot-path stubs closed by the assembly | — | **23 of 89** |

and the 23 are the ones that matter:

```
BootCpuData  CpuDataEntries  intstack_top  fiqstack_top
get_mmu_control  set_mmu_control  fiq_context_init  ml_get_timebase
flush_mmu_tlb  flush_mmu_tlb_asid  flush_mmu_tlb_entry  flush_mmu_tlb_entries
copyin  copyinstr  copyout  arm_debug_set_cp14  ... (23)
```

They are `data.s` and `machine_routines_asm.s`, and they are **the 23 nearest `arm_init`** — the
first thing a boot reaches (experiment-137). All 17 Mach-O objects now exist on this host and carry
those symbols, verified with `llvm-nm`.

## What it costs, measured

The C side is **32 files behind**, and the reason is not the triple — it is that **the ELF path has
thirty stages of fixes in it and this script has four.** The Mach-O failures are the same failures
the ELF path had before those stages:

```
20  sync_qos_count_t          <- needs the kserver placement (experiment-145) in its full form
 7  memory_object.h conflict  <- needs the include-order work of experiment-124
 5  clock_t in bsd/sys/times.h <- needs experiment-126's per-component handling
 1  the firehose chunk count, OSAtomicOperations, uthread_t ...
```

**So the honest price of A is: rebuilding what the ELF path already has.** Every one of those
mechanisms — `COMP_FIRST`, the kserver root, the per-component `-D_CLOCK_T`, the BSD-only
`sys/types.h`, `gen_device_headers.sh` — is target-independent and would port directly. That is
mechanical work of a known kind, and the measurement says roughly **32 files plus the four assembly
files**, not a rewrite.

## One defect this stage's own script reproduced, and it is the same one twice

The first version of `build_xnu_arm_macho.sh` put `-DMACH_KERNEL_PRIVATE=1` in the **global** defines.
Result: **271 files failed on `ffs`** — experiment-118's defect, reproduced exactly, in a file whose
comment says it must not be. The fix was to move it to the per-component set, where the ELF script
already had it, and the compile count went 252 → 544 in one step.

That is worth recording because it is the clearest demonstration of the defect class this project has
met ten times: **the fix existed, was written down, and a new file did not inherit it.** A build
configuration that lives in two scripts will drift; the Mach-O script's comment now points at
`component_defines.sh` as the single source, which is what `build_xnu_arm_kernel.sh` does too.

## The choice, with numbers

| | A: Mach-O | B: stay ELF | C: stop |
| --- | --- | --- | --- |
| assembly | **17 of 17**, closes the 23 deepest boot-path stubs | 13 of 17, and those 23 stay stubs | — |
| C sources | 575 of 615 today, ~607 after porting the mechanisms | 607 of 615 | — |
| needs installed | a Mach-O linker (`lld`; `apt-cache policy lld` has a candidate) | nothing | — |
| works now | `--assemble` and `--compile` do; only `--link` waits | all of it | — |
| what it unblocks | the 23 stubs nearest `arm_init`, `vnode_trim`, the underscore convention | nothing further | — |

**B and C do not advance the goal.** The goal is XNU running, the boot path's nearest 23 stubs are
blocked on the triple, and A is the only option that moves them.

## How to reproduce

```bash
./tools/build_xnu_arm_macho.sh --assemble   # 17 ok, 0 failed
./tools/build_xnu_arm_macho.sh --compile    # 575 ok, 40 failed
./tools/build_xnu_arm_macho.sh --link       # refuses: no ld64.lld on this host, with the reason
```
