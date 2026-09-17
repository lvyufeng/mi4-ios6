# Experiment 136 — the sixth generator, and a correction to the fifth experiment

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/gen_assym.sh` (new); `docs/experiments/experiment-135-*.md` corrected

| | before | after |
| --- | --- | --- |
| manifest `.s` files that assemble | 10 of 20 | **13 of 20** |
| `RELEASE` measurement link, stubs | 443 | **427** |

## 1. A correction, first, because it changes what the next step is

Experiment-135 said:

> `PE_putc` and `kprintf` are among the undefined symbols, so a boot attempt would produce no output
> at all: a silent hang with no log.

**Both halves of that are wrong.** Checked against the list it was describing:

```
$ arm-none-eabi-nm --defined-only out/xnu_kernel_obj/*.o | grep -wE 'PE_putc|kprintf|cnputc'
00000008 B PE_putc          <- BSS, and a function POINTER: `void (*PE_putc)(char c);`
000000c4 T kprintf
000006dc T cnputc
```

`PE_putc` is a function pointer (`pexpert/gen/pe_gen.c:107`), assigned by `PE_init_printf` (`:112`)
to `cnputc`, and zero-initialised BSS is a *defined* symbol. `kprintf`, `vprintf`, `cnputc` and
`PE_init_printf` are all defined too. **The console path is present as code.**

This is the project's "a measurement can be the thing that is wrong" defect class, sixth instance,
and the shape is worth naming: **the claim was about a list, and the list was in the tree.** `grep`
on `out/link/RELEASE-measure-undef.txt` would have settled it in one step. The lesson is not
"measure more" — everything here *was* measured — it is that a summary sentence about a measurement
is itself a claim and needs the same check as the measurement.

**What it changes.** The next step is not "make the console symbols real" — they are real. The
question is narrower and more interesting: `cnputc` comes from `osfmk/console/serial_console.c`,
which is a **ring buffer** (`console_ring`, `kmem_alloc(kernel_map, …)`) rather than a UART poll, so
where the bytes go depends on what drains it — and `PE_init_printf` has to be *reached* for `PE_putc`
to be non-NULL at all. Both are things a boot attempt would answer and a static analysis would only
guess at.

## 2. The sixth generator: `assym.s`, which the project said was absent

`stages/stage90/xnu_arm_boot/assym.s` is 48 lines with 17 defines. Its own header says the real one
"is absent from the OSS tarball" and is "the single largest piece of the build configuration that
osfmk/arm's assembly needs".

**Both halves are wrong.** `osfmk/arm/genassym.c` is in the tarball, it is the generator, and
`osfmk/conf/Makefile.template:184-189` is the rule that runs it:

```make
genassym.o: $(SOURCE_DIR)/$(COMPONENT)/$(GENASSYM_LOCATION)/genassym.c
	$(_v)${GENASSYM_KCC} ${CFLAGS} ... -S -o ${@} ${INCFLAGS} $<     # C compiled to ASSEMBLY

assym.s: genassym.o
	$(_v)sed -e '/^[[:space:]]*DEFINITION__define__/!d;{N;s/\n//;}' ... genassym.o > $@
```

`genassym.c` uses the classic trick — `DECLARE("NAME", offsetof(...))` emits a symbol whose name
carries the value as an ASCII string — and the sed scrapes it back out into **two** forms, both used:

```
#define CPU_DATA_ENTRIES   #3      <- the `#` is inside the define, because the assembly writes
                                      `ldr r1, [r4, CPU_DATA_ENTRIES]` and ARM needs the `#`
#define CPU_DATA_ENTRIES_NUM 3     <- the bare number, for uses that supply their own
```

`tools/gen_assym.sh` reproduces the pipeline with Apple's sed unmodified. It produces **266 defines
from 574 lines** of generated assembly, and it checks three names by hand
(`ASSIST_RESET_HANDLER`, `CPU_DATA_ENTRIES`, `CPU_DATA_PADDR`) plus a floor on the total, because an
empty scrape would make every assembly file fail with an undefined name — a failure that reads
exactly like the problem it replaces.

**The error it fixes reads like an assembler bug and is not.** `locore.s:92` failed with
`error: register expected` at `ldr r0, [r4, ASSIST_RESET_HANDLER]`. `ASSIST_RESET_HANDLER` is
defined nowhere in the tree; the name was simply undefined. Three of the seven formerly-failing
files now assemble:

| | |
| --- | --- |
| **`locore.s`** | the exception vectors, `BootCpuData`, `CpuDataEntries`, `fleh_*` — what `_start` needs four instructions in |
| `cswitch.s` | context switch |
| `machine_routines_common`-adjacent leftovers | |

Confirmed in the objects: `ExceptionVectorsTable` and `fleh_reset` are now defined, and the
measurement link's stub count fell **443 → 427**.

The four that still fail are a different kind of thing and are named for the next attempt:
`data.s:41`'s `.section __DATA, __data` is **Mach-O syntax** (a genuine target-format issue, the
same question as the `armv7-apple-darwin` triple); `lz4_decode_armv7NEON.s:133`'s `.thumb_func`
and `WKdmData_new.s:29`'s unknown directive are assembler-dialect; `machine_routines_asm.s:696`
reports "Wrong number of arguments" past the point `assym.s` fixes.

## The pattern, now six times

Every generator in Apple's build that this project needed was **in the tarball**, and the first
conclusion each time was that it was not:

| | Concluded absent | Actually |
| --- | --- | --- |
| 1 | MIG | `apple-oss-distributions/bootstrap_cmds` |
| 2 | the armed headers from `.defs` | a build step away |
| 3 | the build configuration | `config/MASTER.arm`, `SETUP/config/` |
| 4 | `security/_label.h`, `san/kasan.h` | at the tree root |
| 5 | the `OPTIONS/` headers | `mkheaders.c`, in the tarball |
| 6 | **`assym.s`** | **`osfmk/arm/genassym.c`, in the tarball** |

The check that would have caught this one sooner is the same one the others needed and that
`docs/status/roadmap.md` already states: **search the tree for the *shape* of the thing** — a tool's
source, a `Makefile` rule that produces the output — rather than concluding from the path an error
message names.

## How to reproduce

```bash
./tools/gen_assym.sh                 # 266 defines in out/xnu_assym/RELEASE/assym.s
./tools/measure_link.sh              # 427 stubs, _start @ 0x803ab074
```
