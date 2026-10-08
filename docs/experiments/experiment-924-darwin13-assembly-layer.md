# 924 — Darwin-13's ARM assembly layer assembles, 33/33 (2026-10-08)

923 left the D13 C++ spine at 96/96 but the **assembly** layer at 26/32: six `.s` files the EABI
assembler rejected. This rung closes all six. The D13 ARM assembly layer now assembles **33/33**, and
the only work left below the link is the kernel's C walls.

## The two halves

The six failures were not one wall. Three were **spelling** — the D13 tree writes its assembly the way
Apple's `as` accepts, and clang's ELF assembler does not — and three were the **SLIDABLE** wall 921c
named, plus one **semantic** switch that turned out to belong to the wrong tree.

### The SLIDABLE wall (a tree edit, staged)

`osfmk/mach/arm/asm.h:204` was `#ifdef _ARM_ARCH_7 / #define SLIDABLE 1`. clang sets
`__ARM_ARCH_7A__`, which `arm/arch.h:15` turns into `_ARM_ARCH_7`, so the `#define` **overrode** the
`-DSLIDABLE=0` this project already passes — and the SLIDABLE `LOAD_ADDR_GEN_DEF` emits Darwin Mach-O
non-lazy pointers (`.section __DATA,__nl_symbol_ptr,non_lazy_symbol_pointers` / `.indirect_symbol`),
which the ELF assembler has no equivalent for. The fix is the one guard the ELF toolchain needs:
`#ifndef SLIDABLE` around the `#define`, so a Darwin build keeps the default (1) and this build's
`-DSLIDABLE=0` wins and selects the position-dependent branch. It is the **third** staged tree edit
(after 923's two), made by `tools/stage_d13_board.sh`, re-derived by `tools/check_d13_board_staged.sh`.
That one change took the assembly from 26 to **30** of 32.

### The dialect translation (a translator change, no tree edit)

The other three files are the category `tools/translate_arm_asm.py` exists for — dialect, not source.
Three constructs were added, each counted and reported as before, and each a spelling rewrite (no
semantics moved):

| D13 spelling | ELF spelling | where |
| --- | --- | --- |
| `EnterARM(foo)` / `EnterThumb(foo)` | `.code 32`/`16` + `.globl _foo` + `.align 4` + `_foo:` | `hw_lock.s`, `machine_routines_asm.s`, `cswitch.s` |
| `ldm<cond>fd` / `stm<cond>fd` | `ldmia<cond>` / `stmia<cond>` | `bcopyinout.s` (14) |
| `ldr<cond>[b]t` / `str<cond>[b]t`, and `ldr<cond>b` | `ldr[b]t<cond>` / `str[b]t<cond>`, `ldrb<cond>` | `bcopyinout.s` (13) |

`EnterARM`/`EnterThumb` are the D13-only macros in `osfmk/arm/asm_help.h:53,59`; their expansion
contains `.thumb_func _ ##function`, the Mach-O-only spelling the translator's existing `.thumb_func`
rule rewrites — but that rule runs **before** the preprocessor, so it never sees the expansion. The
translator now expands the macros itself.

**`EnterThumb` is not always thumb, and the translator reads which guard the file uses.** Two files
redefine it, under guards of opposite polarity, both decided by this build's own `ARMA7=1`:

- `machine_routines_asm.s:42` — `#if __ARM_ARCH == 7` → **true** here → `EnterThumb` is `EnterARM`;
- `hw_lock.s:27` — `#ifndef _ARM_ARCH_7` → **false** here → `EnterThumb` stays the thumb form.

Blanket-expanding `EnterThumb` as thumb would have been wrong for the first file and would have
silently placed `machine_signal_idle`/`machine_callstack` in ARM state. So the translator reads the
guard's polarity (from a regex on `__ARM_ARCH == 7`), expands each use the way the preprocessor would,
and deletes the `#undef`/`#define` pair from the output so clang never sees a redefinition. The two
`ldm`/`ldr` reorderings are the ARM assembler's own spellings: `fd` is `full descending`, ARM's name
for the `ia` addressing mode with `sp`, and the condition codes belong *after* the `t`/`bt` suffix.
Verified against every mnemonic the file uses: all 27 failing lines fall in these two classes.

The translator is shared with 4570. Neither new pattern appears anywhere in 4570's manifest
(grep counts 0), and 4570's 17 assembly objects are **byte-identical** after the change.

### The semantic switch — `cswitch.s`, and why it is off for D13

`cswitch.s` failed the **idle-stack patch** (`patch_idle_stack.py` found no `LEXT(Idle_context)`), not
assembly. That patch is experiment 519's fix for a real 4570 collision: 4570's `Idle_context` puts the
idle thread on `cpu_data->istackptr` — the same field the exception vectors read for a handler's stack
— so the idle thread and every handler share one stack.

**D13 has no such collision, and no `CPU_ISTACKPTR` anywhere** (grep count 0 across the whole tree).
Its `cswitch.s` transplants winocm's own scheme: the idle thread is an ordinary `kernel_thread_create`
thread and takes its stack from `TH_PCB_ISS` like every other thread. There is no `Idle_context` for
the patch to match, and no interrupt stack to borrow. So the patch is 4570-specific, and the switch
default now **reads the tree** (`grep CPU_ISTACKPTR` under `osfmk/arm`) instead of being hand-set: `1`
for 4570, `0` for D13. An explicit `STAGE90_XNU_IDLE_STACK` still overrides. D13's `cswitch.s` builds
**unpatched**, which is correct for it. The summary line's "(expected N)" now follows the switch, so a
patch that fires when the tree says it should not is still visible.

## Result

| | before 924 | after 924 |
|---|---|---|
| D13 `.s` assembled | 26/32 | **33/33** |
| D13 asm objects | 26 | **33** |
| 4570 asm objects | 17 | 17 (byte-identical) |
| `make check` | 0 | **0** |

## 4570 neutrality

- The translator's two new rules match **nothing** in 4570's manifest (grep counts 0 for both the
  `EnterARM(`/`EnterThumb(` macros and the two mnemonic reorderings).
- `tools/assemble_arm_layer.sh` derives its idle default from the tree; for 4570 that is `1`, its
  previous hard default, and the run still patches `Idle_context` into **1** object.
- **All 17 `out/xnu_asm_obj/*.o` byte-identical.**

## Verification

| check | result |
|---|---|
| D13 assembly | 33/33, 23 translated, 366 symbols de-underscored |
| `tools/stage_d13_board.sh` | idempotent; the asm.h edit reproduced **identically** from the pre-924 text |
| `tools/check_d13_board_staged.sh` | ok — now also re-derives the SLIDABLE guard and `-DSLIDABLE=0` (in `make check`) |
| D13 tree | only the three intended staged edits; no accidental write |
| 4570 assembly | 17/17 byte-identical |
| `make check` | 0 |

## What this does not do

It does not link the D13 image. The remaining walls are all **C**: `libkern/kxld/*` (4,
`kxld_object.h:60 expected ')'`), `bsd/nfs/nfs_node.c` (`strncasecmp`), and the `build_entry.sh`
object-pool retarget (4570's closure → `out/xnu_*_obj_d13`). The assembly layer, which 921c called
"the last substantial piece", is complete.