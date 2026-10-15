# 1020 — KILL THE VFP FALSE POSITIVE: a genuine `udf` must raise, not loop

**This is a rung design.** It fixes the ROOT CAUSE named in `records/1019-rootcause.txt`: `sleh_undef`'s
user-mode arm returns WITHOUT advancing PC for every undefined instruction except one hard-coded value,
so pid 1's `udf #254` re-executes forever (8183× measured). No build/press/device action is done while
authoring it.

## 0. THE DEFECT, IN THE TREES BY VALUE

`external/xnu-hd2-darwin13/xnu/osfmk/arm/trap.c:1108-1126` (`sleh_undef`, user mode):

```c
        instruction = OSSwapInt32(instruction);
        if(instruction != 0xfedeffe7) {
            thread->machine.vfp_dirty = 0;
            if (!thread->machine.vfp_enable) {
                vfp_enable_exception(TRUE);
                vfp_context_load(&thread->machine.vfp_regs);
                thread->machine.vfp_enable = TRUE;
            }
            return;                       /* <-- UNCONDITIONAL for this branch: PC never advances */
        }
        printf("...[%d]: usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x%08x ...");
        exception_type = EXC_BAD_INSTRUCTION;
        ...
        if (exception_type) { ml_set_interrupts_enabled(TRUE); doexception(...); }
```

The `!= 0xfedeffe7` test is a STAND-IN for "is this a coprocessor/VFP instruction" (the code's own
comment says so: *"i should really fix this crap properly"*). It is wrong for EVERY other undefined
instruction: a genuine `udf` is mis-classified as NEON, VFP is enabled, and the handler returns with the
PC still on the `udf` — a perfect re-trap loop the moment VFP is already on.

**Confirmed identically in the upstream port** (agent, `records/1020-ref-sleh-undef.txt`):
`HTC-Leo-Revival-Project/xnu` `gary_codeunfuckery` `osfmk/arm/trap.c:896`/`:913` carries the SAME
`!= 0xfedeffe7` + unconditional `return;`. **There is no reference to copy from there.**

**The correct implementation is Apple's own generic upstream**, present locally at
`external/xnu-4570.1.46/osfmk/arm/trap.c:118`: it classifies with a REAL VFP bitmask
(`IS_ARM_VFP`/`IS_THUMB_VFP`, `trap.h:196-240`), never returns-to-rerun, and falls through to
`exception_triage(EXC_BAD_INSTRUCTION, ...)`. That is the shape this rung adopts, minimally.

## 1. THE FIX (minimal, one line moved, switch-gated)

Move the `return;` INSIDE the `if (!thread->machine.vfp_enable)` block, so the branch reads:

```c
            if (!thread->machine.vfp_enable) {
                vfp_enable_exception(TRUE);
                vfp_context_load(&thread->machine.vfp_regs);
                thread->machine.vfp_enable = TRUE;
                return;        /* first trip: VFP now on; a real VFP insn re-executes and advances */
            }
            /* 1020: VFP was ALREADY enabled and the instruction is STILL undefined -> NOT a VFP insn.
             * Fall through to the printf + EXC_BAD_INSTRUCTION below. */
```

**Why this is the right boundary and not a hack.** The port's OWN LEO machinery already encodes this
exact two-trip intent: `ios7leo_dyld_matches` (`trap.c:84`) latches the dyld `udf` state on the FIRST
trip, and `ios7leo_dyld_repeat` (`:105`) detects the IDENTICAL state on a SECOND trip
(`bcmp(&state, &saved) == 0`). The design *expects* the second trip — it simply never wired the first
trip's `return` to allow it on non-LEO builds. This change makes the first trip the "lazy VFP enable"
and the second trip the real fault, which is what the LEO code assumed.

**Semantics, both ways (the honest reading):**
- A REAL VFP/NEON instruction with VFP off: trip 1 enables VFP and returns; the CPU re-executes it with
  VFP on and it now executes — no second trap. **Unchanged behaviour for the legitimate case.**
- A genuine `udf`/non-VFP undefined instruction, VFP off: trip 1 enables VFP and returns; trip 2 finds
  VFP on, falls through, prints, and raises `EXC_BAD_INSTRUCTION`. **One extra trap, then correct.**
- A genuine `udf` with VFP already on: trip 1 falls through immediately. **Correct.**
- A kernel-mode undefined instruction: unchanged (the `cpsr == 0x13` panic above this branch).

The cost of the fix is at most ONE extra trap per undefined instruction; the cost of the defect is an
infinite loop. The fix is strictly better.

## 2. THE ARM

Switch: **`STAGE90_XNU_UNDEF_RAISE`** (default 0). `trap.c` is a **kernel-pool** file
(`out/xnu_kernel_obj_d13/osfmk_arm_trap.o`), so the switch rides the pool `DEFINES` array
(`tools/build_xnu_arm_kernel.sh:858` precedent, beside `STAGE90_XNU_DYLD_WITNESS`) — NOT `EXTRA_DEFINES`.

The guard is `defined()`-first ([[mi4-off-option-two-spellings]]):
```c
#if defined(STAGE90_XNU_UNDEF_RAISE) && STAGE90_XNU_UNDEF_RAISE
            /* ...moved-return form... */
#else
            return;   /* the tree's original text, byte for byte */
#endif
```
placed so that switch OFF compiles the file's pre-existing bytes exactly.

## 3. THE STAGING (external/ is re-provisionable)

Like every pool edit (`tools/stage_d13_dyld_witness.sh` is the template), the change is a SCRIPT, not a
hand edit:
- `tools/patch_d13_undef_raise.py` — idempotent; asserts the anchor window's occurrence count exactly,
  REFUSES (file untouched) on a partial/double/foreign edit, PROVES switch-OFF is byte-identical with
  real `clang -E -P` runs before writing.
- `tools/stage_d13_undef_raise.sh` — the single entry point; calls the patcher, edits nothing itself.
- `tools/check_d13_undef_raise.py` — asserts BY VALUE on the LINKED image: with the switch ON, the
  `sleh_undef` body's user arm has its `return` reachable ONLY under the VFP-enable branch, by walking
  the disassembly and checking the branch target of the VFP-enable path — never a source grep
  ([[mi4-a-claim-in-a-comment-is-not-a-check]], [[mi4-linked-code-order-is-not-source-order]]).

## 4. THE SWITCH PLUMBING (the seven places)

1. **Guard `#if`** in `osfmk/arm/trap.c` around the moved `return` (§2).
2. **`-D`**: `[[ ${STAGE90_XNU_UNDEF_RAISE:-0} -eq 1 ]] && DEFINES+=(-DSTAGE90_XNU_UNDEF_RAISE=1)`
   beside `build_xnu_arm_kernel.sh:858`.
3. **`XNU_KERNEL_EXTRA_DEFINES`** carries `-DSTAGE90_XNU_UNDEF_RAISE=1` on the build (the pool
   channel, measured not assumed — `stage_d13_globalfix.sh:31-32`).
4. **`ENTRY_ARM_KEYS`** (`src/entry/build_entry.sh:2035`) — add `STAGE90_XNU_UNDEF_RAISE`.
5. **The record writer** — `echo "STAGE90_XNU_UNDEF_RAISE=$UNDEF_RAISE"` beside the `UNDEF_DIRECT`
   echo (`build_entry.sh:40188`), as the RESOLVED value.
6. **`scripts/preflight_boot_check.sh`'s `ENTRY_CFG_KEYS`** (`:716`) — add it at the tail.
7. **The checker** — `tools/check_1020_capture.py`, modelled on `tools/check_1018_capture.py`, cells in
   §5.

## 5. THE PRE-REGISTERED VERDICT CELLS

The witness is the EXISTING `xnu_live_undef_*` keys (they already ride the direct channel under
`STAGE90_XNU_UNDEF_DIRECT=1`, which this arm carries) PLUS the handler's own `usermode undefined
instruction` printf and any panic.

| # | condition (read from the 1020 capture) | verdict | meaning |
|---|---|---|---|
| **R-RAISES-ONCE** | `xnu_live_undef_seq` advances **to 2 at one pc and STOPS** (no lockstep climb to thousands), the `usermode undefined instruction` printf fires ≥1×, and either the process is gone or a panic follows | **FIXED** | the genuine udf raised after the lazy-VFP trip; the loop is dead |
| **R-RAISES-CLEAN** | `_seq == 1` only (VFP was already on) + printf ≥1× + no loop | **FIXED (VFP already on)** | even cleaner: first trip raised |
| **R-STILL-LOOPS** | `_seq` climbs to `> 100` at one invariant pc, printf 0× | **NOT FIXED** | the moved `return` did not take effect — check the `-D` reached the pool object (the 1010 lesson: an absent effect whose channel was wrong) |
| **R-VFP-CONTINUES** | the boot advances PAST the udf to a DIFFERENT frontier (no undef loop, no panic) | **MOVED** | the udf is passed — the next frontier is downstream (the `_mach_vm_map`/allocator question, `records/1019-rootcause.txt` §6) |
| **R-CTL (WITHHOLD)** | no `xnu_live_undef_*` keys at all AND no boot self-test `undef_handler_undef_handled` row | **INSTRUMENT_UNSOUND** | the witness was not live; do not read (§1 of 1018) |

`R-CTL` is mandatory first: the control is the boot self-test row
`stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled=0x1` (present in the 1018 capture at
`:3480`) **and** a `xnu_live_undef_seq` row.

## 6. THE FALSIFICATION

If `R-STILL-LOOPS` holds, the fix is falsified — the boundary is not where this design says. The
candidate causes are all testable: the `-D` did not reach `osfmk_arm_trap.o` (read `nm`/`objdump` of
the object, not the env); the `return` is placed inside a different block after preprocessing; or the
`udf` is being reached on a kernel-mode path this branch does not cover.

## 7. WHAT THIS DOES AND DOES NOT CLAIM

- **CONFIRMS (goal):** removes an infinite re-trap loop that is the current terminal frontier. After it,
  pid 1 either dies legibly (`EXC_BAD_INSTRUCTION` → launchd terminates → an orderly panic is a
  MEASURED frontier) or the boot moves past the allocator's `udf` — both are readable next frontiers.
- **does NOT claim:** that 1020 alone boots iOS 7. If the udf raises, pid 1 dies at the allocator and
  the NEXT rung is the allocator's `_mach_vm_map` being refused (`records/1019-rootcause.txt` §6,
  `docs/1019-design.md`'s shim witness). If it is passed, the frontier is whatever the boot reaches
  next. Either way 1020 is a strict improvement: it trades an infinite silent loop for a legible state.
- **SAFETY:** a kernel-pool behavioural change of the smallest kind (one `return` moved). Blast radius:
  undefined-instruction handling only. The device is booted with `fastboot boot` (never flashed), so the
  flash-resident Android and fastboot stay reachable — [[mi4-hardware-run-safety-gate]].

## 8. FILE:LINE ANCHORS

- **The defect:** `external/xnu-hd2-darwin13/xnu/osfmk/arm/trap.c:1108-1126` (the branch), `:1167`
  (EXC_BAD_INSTRUCTION), `:1184` (`doexception`); the LEO intent `:84`/`:105`.
- **The reference (correct):** `external/xnu-4570.1.46/osfmk/arm/trap.c:118` (`sleh_undef`),
  `trap.h:196-240` (`IS_ARM_VFP`/`IS_THUMB_VFP`).
- **The upstream port (same defect):** `HTC-Leo-Revival-Project/xnu` `gary_codeunfuckery`
  `osfmk/arm/trap.c:896`/`:913` — quoted in `records/1020-ref-sleh-undef.txt`.
- **The evidence:** `records/1019-rootcause.txt`, `records/1019-osalloc-extract.txt`,
  `docs/1019-design.md`, and the capture
  `out/stage90/captures/recovered-unknown-arm-20261015-184618-last_kmsg.txt`.
- **The plumbing:** `tools/build_xnu_arm_kernel.sh:858` (pool `-D`), `tools/stage_d13_dyld_witness.sh`
  (staging template), `scripts/preflight_boot_check.sh:716` (`ENTRY_CFG_KEYS`),
  `src/entry/build_entry.sh:2035`/`:40188` (arm keys / record echo).

**No build, no press, no device action, no `git add`, no write to `out/stage90` was performed while
authoring this file.**