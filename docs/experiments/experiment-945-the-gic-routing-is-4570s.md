# 945 — 482's GIC routing is 4570's machine (2026-10-08)

The fifth rung in the tree-pin series ([[mi4-913-ios7-rebase-decision]]). 944 skipped 481's timer
registration; the D13 build advanced to **482's GIC routing check** (`tools/check_gic_routing.py`) and
stopped. 482 answers "which INTID does the line this kernel programs appear on", and it asserts the
premise in **what the vector page currently does**.

## The wall

    FAIL: the routing this step measures is not the routing this image has:
      the image has no locore_fleh_decirq, so the handler 483 would move into slot 7 is not linked into it at all
      the image has no fleh_irq_handler to disassemble, so the dispatch this step is deciding about was not read

## Finding — 482 is two halves: this project's files (live on D13) and 4570's machine (not)

The check's seven claims split cleanly by *owner*:

- **Claims 1, 2, 6, 7 are about this repository's own files** — `entry_gic.h` vs `src/gic.c` /
  `xnu_msm8974_fiq_probe.c` (the GIC register map, both directions), the two candidate timer PPIs, the
  probe's three source-order guards, and 484's live-table mapper (`entry_stubs.c`'s `entry_mmio_section`
  / `entry_live_ttb_base`). **These pass on D13 unchanged** — they never mentioned the tree.
- **Claims 3–5 are 4570's machine.** `claim_vector` asserts what vector slots 6 and 7 hold and that the
  reporting stubs call `entry_epilogue`; `claim_dispatch` disassembles Apple's
  `fleh_irq_handler`/`fleh_decirq_handler` and reads its five `assym.s` `INTERRUPT_*` words. **Darwin 13
  has none of that**: its `assym.s` declares no `INTERRUPT_HANDLER`/`_NUB`/`_SOURCE`/`_TARGET`/
  `_REFCON`; its `machine_routines_asm.o` defines no `fleh_irq_handler`/`fleh_decirq_handler` (`grep -rl`
  is 0 in the D13 tree; they are **4570-only**); and its IRQ path is its own (`osfmk/arm/traps_lo.s` +
  `trap.c`, with `exctramps.s`'s vector page `.long _fleh_irq`), not the `locore_fleh_irq` entry 482/483
  stage. Slot 6 holds Apple's `locore_fleh_irq` in *both* trees (the entry vector), but the dispatcher
  483 installs behind it — the whole point of the routing decision — does not exist on D13.
- **`claim_live_table`'s premise is 4570's template.** Its tail reads `arm_vm_init.c` for
  `cpu_ttep = boot_ttep + ARM_PGBYTES * 4;` and the `bcopy` beside it — the reason "the console's latch
  reaches the live table". D13's `arm_vm_init.c` has neither line (grep finds 0), so that premise is
  not asserted on D13; the claim's real content (the live-table mapper in `entry_stubs.c`) is this
  project's code and stays live.

So 945 is a **published skip**, not a rename ([[mi4-off-option-two-spellings]], the 937/944 shape):
`compare(facts, d13=)` calls `claim_vector`/`claim_dispatch` only when `not d13`, and
`claim_live_table` branches its `arm_vm_init.c` premise on `d13`. `main()` gains `--tree` and detects
D13 by the same `osfmk/sys/types.h` discriminator the build uses; `GATHER` reads
`out/xnu_assym[_d13]/STAGE90_XNU/assym.s` per tree. `build_entry.sh` passes `--tree "$XNU_TREE"`.

The `--selftest` lists the 4570-only mutations in `D13_SKIPPED_MUTATIONS` (the sixteen that break
claims 3–5 and the `arm_vm_init.c` premise, including `the_justification_is_gone`) so they are not
expected to be refused on D13 — every other mutation still must be. The 4570 default path is unchanged:
`--selftest` against the frozen 4570 elf refuses **all 48** mutations.

## State after this rung

482/483/484 pass on D13. The build advances to the **timer census** — `the image defines no
timer_call_enter_with_leeway, timer_call_quantum_timer_enter, so the wrapper for it calls the
generator's stand-in`. D13 lacks those `timer_call_*` names, so the 5xx census's wrapped declarations
are 4570's — the next rung, 946.

## Provenance

`tools/check_gic_routing.py`, `src/entry/build_entry.sh`. Host-side, reversible, **no press**.

- D13 entry build: `/tmp/d13_entry_build33.log` (482/483/484 pass; stops at the timer census).
- 4570-neutral: `check_gic_routing.py --selftest` against
  `out/stage90/frozen/armed-storage-054f8269/xnu_arm_entry.elf` refuses all 48 mutations (no `--tree` ⇒
  default 4570).
- D13 facts (read here): D13 `assym.s` has no `INTERRUPT_*`; `machine_routines_asm.o` has no
  `fleh_irq_handler`/`fleh_decirq_handler`; `grep -rl` = 0 in the D13 tree for
  `fleh_irq_handler`/`fleh_decirq_handler`; D13 `arm_vm_init.c` has no `boot_ttep + ARM_PGBYTES * 4`.
- `make check` exits 0.