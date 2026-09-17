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
| manifest `.c` that compile | **607 of 615** | 601 of 615 |
| boot-path stubs closed by the assembly | — | **23 of 89** |

(The C figure is 601 after the three per-component fixes below; it started at 252, and the gap to
607 is the mechanisms the ELF path has accumulated, not the triple.)

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

The C side **started 32 files behind and is now 6**, and the reason is not the triple — it is that
**the ELF path has thirty stages of fixes in it.** The Mach-O failures were the same failures the ELF
path had before those stages, and they came down as each was ported:

```
20  sync_qos_count_t          <- needs the kserver placement (experiment-145) in its full form
 7  memory_object.h conflict  <- needs the include-order work of experiment-124
 5  clock_t in bsd/sys/times.h <- needs experiment-126's per-component handling
 1  the firehose chunk count, OSAtomicOperations, uthread_t ...
```

**So the price of A is: porting what the ELF path already has, and that is measured to be cheap.**
Every one of those mechanisms — `COMP_FIRST`, the kserver root, the per-component `-D_CLOCK_T`, the
BSD-only `sys/types.h`, `gen_device_headers.sh` — is target-independent and ports directly; three of
them took the count from 252 to 601 in this stage. What is left is 6 files and four assembly files.

## The script reproduced two of this project's own defects, and then found a third thing

Both were the *global-versus-per-component* shape, twice in one file:

| in the script's first version | result | fix |
| --- | --- | --- |
| `-DMACH_KERNEL_PRIVATE=1` global | **271 files on `ffs`** — experiment-118's defect | per-component |
| `-D_CLOCK_T=1` global | `clock_t` undefined in `bsd/sys/times.h` — experiment-126's defect | osfmk only |

`252 → 544 → 601` in two steps, from moving two flags into the row they belong to. **The fixes
existed, were written down, and a new file did not inherit them.** A build configuration living in
two scripts will drift; the comment now points at `component_defines.sh` as the single source.

### And a real ABI finding, which cuts against a naive switch

`bsd/net/dlil.c:1419` carries a **compile-time assertion in XNU's own source**:

```c
/* The following fields must be 64-bit aligned for atomic operations. */
IF_DATA_REQUIRE_ALIGNED_64(ifi_ipackets);
```

It **fails under `armv7-apple-darwin` and passes under `armv7-none-eabi`**, and the reason is
measurable:

| target | `offsetof(struct S { char c; unsigned long long v; }, v)` |
| --- | --- |
| `armv7-none-eabi` | **8** |
| `armv7-apple-darwin` (clang 14) | **4** |

XNU asserts 8-byte alignment of a `long long` member. **The ELF target gives that; today's
`armv7-apple-darwin` does not.** So Apple's kernel was built with an ABI in which `long long` is
8-byte aligned — which is AAPCS, and which their clang evidently produced for this target, while
clang 14's `armv7-apple-darwin` defaults to 4.

**That is worth stating plainly because it complicates the choice**: switching the triple is not a
pure win. It fixes the assembler and `size_t`, and it would introduce an alignment the other way. The
honest version of option A is *"the Darwin triple **with the alignment flags Apple's build used**"*,
and those flags are not established here. A first attempt at A that only changes `--target` would
trade four assembly files for one C file and a set of alignment questions.

## The choice, with numbers

| | A: Mach-O | B: stay ELF | C: stop |
| --- | --- | --- | --- |
| assembly | **17 of 17**, closes the 23 deepest boot-path stubs | 13 of 17, and those 23 stay stubs | — |
| C sources | 601 of 615, and the six-file gap is mechanisms, not the target | 607 of 615 | — |
| needs installed | a Mach-O linker (`lld`; `apt-cache policy lld` has a candidate) | nothing | — |
| works now | `--assemble` and `--compile` do; only `--link` waits | all of it | — |
| what it unblocks | the 23 stubs nearest `arm_init`, `vnode_trim`, the underscore convention | nothing further | — |
| what it risks | a `long long` alignment Apple's build must have set separately | — | — |

**B and C do not advance the goal.** The goal is XNU running, the boot path's nearest 23 stubs are
blocked on the triple, and A is the only option that moves them.

## How to reproduce

```bash
./tools/build_xnu_arm_macho.sh --assemble   # 17 ok, 0 failed
./tools/build_xnu_arm_macho.sh --compile    # 601 ok, 14 failed
./tools/build_xnu_arm_macho.sh --link       # refuses: no ld64.lld on this host, with the reason
```
