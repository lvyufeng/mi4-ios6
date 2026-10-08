# 936 — the entry file follows the tree; D13's `_start` is `locore.s` (2026-10-08)

935 left the D13 entry link at `gPhysSize`: a symbol D13 declares
(`osfmk/mach/arm/vm_param.h:108`) and defines **nowhere**, refusing the stub generator because it is an
object. The reference is `out/stage90/xnu_arm_start.o` — and that object is **4570's `start.s`**,
because the entry assembler hard-pinned the tree.

## The finding

`scripts/xnu_arm_assemble.sh` (the very first step of the entry build, `build_entry.sh:1399`) opened
with:

    XNU=$REPO_ROOT/external/xnu-4570.1.46

and unconditionally assembled `$XNU/osfmk/arm/start.s` into `out/stage90/xnu_arm_start.o` — the
object the link takes `_start` from. On 4570 that is correct. On Darwin 13 it is wrong twice over:

1. **D13 ships no `osfmk/arm/start.s`.** Its `_start` is `osfmk/arm/locore.s`'s `EnterARM(_start)`
   (`locore.s:52`), a different boot flow (MMU re-init inside `_start`, `BOOT_ARGS_*` reads, a
   trampoline through `start_trampoline`), backed by `__start`-style stacks in the same file.
2. **The pinned 4570 `start.s` drags in `gPhysSize`.** It is 4570's `globals_asm.h:34`
   (`LOAD_ADDR_GEN_DEF(gPhysSize)`) that emits a `R_ARM_ABS32 gPhysSize` word — **unused by the
   `_start` path**, but enough to make the symbol undefined in the link. `grep` for it across D13's
   whole kernel pool is empty; D13's only other mention is the `isphysmem()` macro, whose callers are
   zero. So it is a pure artifact of assembling the wrong tree's entry file.

This is the same **class** as 935's five walls — an asset pinned to 4570 that must follow the selected
tree (`mi4-913-ios7-rebase-decision`) — this time the entry *file* itself.

## The change

`scripts/xnu_arm_assemble.sh` now sources `tools/xnu_tree_roots.sh` (the one place
`XNU_TREE -> XNU_OBJ_SUFFIX -> every root` is written down, 933) and selects the entry file from the
pivot's own discriminator (`osfmk/sys/types.h`):

    if [[ -f $XNU/osfmk/sys/types.h ]]; then ENTRY_SRC=…/locore.s; ENTRY_NAME=locore; D13_ENTRY=1
    else                                     ENTRY_SRC=…/start.s;  ENTRY_NAME=start;  D13_ENTRY=; fi

Three consequences, each necessary:

- **The output name is kept** (`xnu_arm_start.o`), so the entry link's `LINK_OBJS` is not respelled —
  only which tree's file produces it changes.
- **`src/entry/assym.s` cannot serve D13's `locore.s`.** That file carries 4570's `BA_*` spelling and
  its header says so ("only what `osfmk/arm/start.s` uses"). D13's `locore.s` does
  `#include <assym.s>` and reads `BOOT_ARGS_*`, names only the **generated** assym has
  (`out/xnu_assym_d13/STAGE90_XNU/assym.s`). So on D13 the generated dir goes **first** on the include
  path; on 4570 it is not added at all, and 4570's include list is character-for-character what it was.
- **D13's entry file is written with Apple's underscore convention and this build is ELF.** `start.s`
  uses `EXT()`/`LEXT()`, which `-D__NO_UNDERSCORES__` un-prefixes; D13's `locore.s` also names
  `_intstack`/`_debstack` by hand and `asm_help.h`'s `EnterARM` always writes `_ ## function`, so its
  `_start` comes out as `__start`. A de-underscore pass (the rule `tools/assemble_arm_layer.sh` already
  applies to the pool's `.s` files) renames `_x -> x`, with the one exception `__start -> _start`.
  On 4570 there are no such names, so the pass is a no-op.

## State after this rung

The build now assembles `osfmk/arm/locore.s` **for the D13 tree**, defines `_start`, `intstack`,
`debstack`, `sleep_test`, and settles every `gPhysSize`-class undefined: **pass 1 succeeds** (42
undefined symbols, down from the object-kind refusal) and the build reaches the **final `ld` link at
`0x80000000`**.

That link now reports **20 unique undefined names**, and the set is diagnostic: every one is a
**`entry_trace.c` trace target** — `uart_putc`, `ml_get_timebase`, `platform_cache_idle_exit`,
`CleanPoC_Dcache`, `os_reason_create`, `kdebug_free_early_buf`, `cpu_signal_handler_internal`,
`copyin_word`, `Idle_load_context`, `timer_call_enter_with_leeway`, and
`IORegistryEntry::getChildCount(...)`. **None exists in any D13 pool object.** They are the 4570
`--wrap` list carried onto a tree that never links the wrapped function — `fiq_context_init` again,
twenty times over — and the next rung, the lazy fix being a tree-gate on the wrap set.

## Provenance

`scripts/xnu_arm_assemble.sh`; D13 `osfmk/arm/locore.s`, `osfmk/arm/asm_help.h`,
`osfmk/mach/arm/vm_param.h`, `osfmk/arm/genassym.c`; 4570 `osfmk/arm/start.s`, `globals_asm.h`. Host
-side, reversible, **no press**.

- 4570 neutrality: `xnu_arm_start.o`'s undefined set is **byte-identical** with and without the change
  (verified by `git stash` + `diff`), and the tree's own pool never gets the generated assym on its
  include path.
- `make check` exits **0**.
- Build runs recorded at `/tmp/d13_entry_build2.log`.