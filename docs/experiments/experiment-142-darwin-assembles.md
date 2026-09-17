# Experiment 142 — the Darwin target assembles every file the EABI target cannot

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: none — this is a measurement of a choice, not a change.

| | `armv7-none-eabi` | `armv7-apple-darwin` |
| --- | --- | --- |
| manifest `.s` files that assemble | 13 of 17 present | **17 of 17 present** |
| of the boot path's 23 deepest stubs | blocked | **unblocked** |

**Four independent findings have been pointing at one decision** (experiments 123, 137, 138, 141),
and every one of them has been an argument rather than a demonstration. This is the demonstration:
**the target triple alone — no flag, no source change, no shim — is the difference between four
assembly files failing and all of them assembling, and those four include the two that carry the
boot path's deepest stubs.**

## The measurement

Same source, same `assym.s`, same flags; only `--target` differs:

| file | eabi | darwin |
| --- | --- | --- |
| `osfmk/arm/data.s` | **fail** — `.section __DATA, __data`, "expected string in directive" | **OK** |
| `osfmk/arm/machine_routines_asm.s` | **fail** — `.macro COPYIO_BODY` with a positional `$0` | **OK** |
| `osfmk/arm/WKdmData_new.s` | **fail** — "unknown directive" | **OK** |
| `osfmk/arm/lz4_decode_armv7NEON.s` | **fail** — `.thumb_func`, "unexpected token" | **OK** |
| `locore.s`, `cswitch.s`, the other 12 | OK | OK |

and the mechanism, reduced to nine lines, is unambiguous:

```asm
	.macro COPYIO_BODY
L$0_x:
	b L$0_x
	.endm
f:
	COPYIO_BODY copyin
```

```
$ clang --target=armv7-none-eabi      -c mm.s     →  error: Wrong number of arguments
$ clang --target=armv7-apple-darwin   -c mm.s     →  accepted
```

This is worth stating precisely because experiment-137 concluded the opposite *in principle*: it
tested that form against clang's EABI assembler and `arm-none-eabi-as`, found both reject it, and
recorded that "Apple's assembler accepted a form no assembler on this host does". **One assembler on
this host does — and it is the one the target triple selects.** The earlier conclusion was right
about the mechanism and wrong about the availability, and the test that would have caught it is the
one this stage ran: vary the *target*, not just the flags.

## What it does and does not settle

**Settles** the assembler half of the Mach-O question. `data.s` carries `BootCpuData`,
`CpuDataEntries`, `intstack_top` and `fiqstack_top`; `machine_routines_asm.s` carries
`get_mmu_control`, `set_mmu_control`, `fiq_context_init`, `ml_get_timebase`, `set_mmu_ttb` and 30
more. Together they are **23 of the boot path's 113 stubs, and the 23 nearest `arm_init`**.

It also retroactively explains experiment-138: `osfmk/arm/bcopy.s`'s literal `_bcopy:` is **the right
name** under this target, because `asm.h`'s `EXT(x) = _##x` is the convention and `-D__NO_UNDERSCORES__`
is what makes it wrong. Under the Darwin triple the de-underscoring step would be *unnecessary*, not
merely harmless — which is the cleanest statement of what the EABI build is doing by hand.

**Does not settle** the linker half, which is a separate and larger piece: nothing on this host can
link Mach-O. `/usr/lib/llvm-14/bin` has `llvm-nm`, `llvm-size`, `llvm-objdump` and `llvm-ar` but no
`ld64.lld`, and `apt-cache policy lld` offers `1:14.0-55~exp2` from this host's own repositories —
the present one. So the remaining question is not whether the toolchain *exists* but whether to
switch to it, which changes what every measurement in this project so far was taken against:

- every object in `out/` is ELF and would be rebuilt;
- `tools/measure_link.sh`, `link_xnu_arm.sh`, `stub_reach.py` and `stub_blockers.py` all read ELF
  through `arm-none-eabi-*`; `llvm-nm`/`llvm-objdump` read Mach-O and would replace them;
- the linker script would need a Mach-O equivalent — `ld64` semantics, not `ld`'s;
- the *result* is closer to what XNU is: a Mach-O kernel, which is what `start.s`'s section
  directives and Apple's assembler dialect were written for.

That last point is the one that decides it, and it is not a measurement: this build has been
compiling XNU with a toolchain that rejects its assembly and a `size_t` that contradicts its own
headers, and then working around each. The workarounds are now four. The alternative is one switch.

## Not done, and why

**`lld` was not installed.** It is a host toolchain change, the choice it belongs to is the user's,
and the measurement above needed no install — clang's Mach-O *assembler* is already present
(`file /tmp/m.o` → `Mach-O armv7 object, flags:<|SUBSECTIONS_VIA_SYMBOLS>`), which is why every
number in this document is a real measurement rather than a projection. What is deferred is the
linker and the rebuild that follows it.

## How to reproduce

```bash
# the whole set, both ways
for t in armv7-none-eabi armv7-apple-darwin; do
  ./tools/gen_assym.sh >/dev/null
  # clang --target=$t ... -c each manifest .s
done

# the nine-line reduction
clang --target=armv7-none-eabi    -c mm.s   # error: Wrong number of arguments
clang --target=armv7-apple-darwin -c mm.s   # accepted
```
