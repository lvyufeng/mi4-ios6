# Experiment 150 — `ld64.lld` cannot link 32-bit ARM Mach-O; the dialect translation closes all 17

Date: 2026-09-17
Host only for the result; the linker install is a host-toolchain change, named here
Artifacts: `tools/translate_arm_asm.py` (new), `tools/assemble_arm_layer.sh`

| | before | after |
| --- | --- | --- |
| manifest `.s` that assemble for ELF | 13 of 17 | **17 of 17** |
| stub symbols in the measurement image | 282 | **240** |
| boot-path stubs | 89 | **66** |
| **"assembly the build never attempts"** | **23** | **0** |

**The 23 stubs that were blocked on the target triple are gone, and the answer turned out not to be
the triple.**

## 1. The measurement that closes option A

Three turns of analysis said the Mach-O target was the way to those 23 stubs. Installing the linker
made it checkable, and the answer is no:

```
$ ld64.lld -arch armv7 -e _mystart -platform_version ios 0.0 0.0 -o out mt_armv7.o
ld64.lld: error: unhandled relocation type
$ ld64.lld -arch arm64 ... -o out mt_arm64.o
   -> Mach-O 64-bit arm64 executable
```

**LLVM's `ld64.lld` links arm64 Mach-O and does not link armv7 Mach-O** — in 14 *and* in 15. So the
Darwin target can *assemble* XNU's assembly (experiment-142) and **cannot link the result on this
host**. Option A as framed was not a choice between two viable paths; it was a path that stops at the
linker, and each of the three findings before this one was right about the *cause* and wrong about the
*remedy*.

*(The install is recorded rather than glossed: `apt-get install -y lld-14 lld-15`, the same way this
project's own history shows `clang` and `bison` being installed and `docs/reference/boot-tooling.md`
documents `apt install` for its tooling. It changed nothing outside the host's package set.)*

## 2. So the four files had to be fixed for ELF, and they are four constructs

`--target=armv7-apple-darwin` reached 17 of 17 by understanding Apple's dialect. The same four
constructs can be translated instead, and each was verified against the EABI assembler before being
written into a rule:

| Darwin | GNU as | where |
| --- | --- | --- |
| `.section __DATA, __data` | `.section .data,"aw",%progbits` | `osfmk/arm/data.s:41` |
| `.section __DATA, __const` | `.section .rodata,"a",%progbits` | `osfmk/arm/data.s:100` |
| `.const` | `.section .rodata,"a",%progbits` | `WKdmData_new.s:29` |
| `.thumb_func _name` | `.thumb_func` (bare; applies to the next label) | `lz4_decode_armv7NEON.s:133` |
| `.macro NAME` + `$N` in the body | `.macro NAME p0,p1` + `\pN\()` | `machine_routines_asm.s:570`, `lz4_decode_armv7NEON.s` |

`tools/translate_arm_asm.py` applies them. The last is the substantive one and it is **general**: any
parameterless `.macro` whose body uses `$N` gets the parameters it is evidently called with —
`copy_1x32_and_increment copy_src,copy_dst` needs two. That is a rule rather than a per-file patch, so
a file with the same idiom translates without editing the tool.

**This is a dialect translation, not a source change.** XNU's tree is never written to; the output goes
beside the objects, with a symlink mirror of the tree root so the relative includes
(`cpu_in_cksum.s:50`'s `../../../osfmk/arm/arch.h`) still resolve. It sits in the same category as the
`objcopy --redefine-sym` step, which exists because `asm.h`'s `EXT(x)` is `_##x` and the ELF side has
to be told.

## 3. What it bought, and it is exactly what the triple was for

`BootCpuData`, `CpuDataEntries`, `intstack_top`, `fiqstack_top`, `get_mmu_control`,
`set_mmu_control`, `fiq_context_init` and `ml_get_timebase` are **defined in the ELF objects now** —
the same eight that the Mach-O path produced, and the same ones a boot reaches first. Measured:

```
stub symbols in the measurement image   282 -> 240
boot-path stubs                          89 ->  66
"assembly the build never attempts"      23 ->   0
```

`stub_blockers.py`'s category of 23 — the project's deepest boot-path items for four stages — **is
empty**, and the top of the remaining list is now `__aeabi_memcpy4` (compiler runtime), `IODTGetDefault`
(C++), `bcopy` (see below) and `oslog_init` (a file that fails to compile).

### And the de-underscore step had to be made deterministic

That last 10 of stubs came from a defect in this stage's own script, and it is the project's oldest
shape. The de-underscore rename keyed on **the linker's undefined list** — so `assemble_arm_layer.sh`
depended on the *previous* `measure_link.sh` run. On a clean tree the list was stale, only **2 of 22**
symbols were renamed, and `bcopy` stayed a stub; running the identical command again renamed **22**.
**A build step whose result depends on how many times it has been run.**

The rule is now the flag's own: rename `_x` to `x`, except `_start`, which the linker script enters at
by that name. No list, no ordering, and verified identical across two clean runs (26 symbols each
time, `_start` intact).

## 4. What this means for the decision

**A and B are no longer the choice they were.** The reason to consider Mach-O was the assembler, and
the assembler's four files are now handled without it — so the price of A (a toolchain that cannot
link here, plus AAPCS, plus porting thirty stages of mechanisms) buys **nothing that B does not now
have**. `vnode_trim`'s `size_t` remains a genuine Mach-O advantage, and it is one file.

**The honest summary: the toolchain question is closed by measurement.** Option A is unavailable on
this host for the reason that matters — no linker — and option B now reaches everything A reached
except `size_t`.

## How to reproduce

```bash
./tools/gen_assym.sh                    # 266 defines
./tools/assemble_arm_layer.sh           # 17 ok, 0 failed
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh       # 607 of 615
./tools/measure_link.sh --keep-stubs    # 250 stubs
./tools/stub_reach.py                   # 72 on the boot path
```
