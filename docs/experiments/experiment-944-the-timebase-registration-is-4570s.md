# 944 — 481's timebase registration is 4570's machine (2026-10-08)

The fourth rung in the tree-pin series ([[mi4-913-ios7-rebase-decision]]): an entry-build clause pinned
to 4570 that has to follow the selected tree. 943 closed the root-device check; the D13 build advanced
to **481's timer-table check** and stopped.

## The wall

    FAIL: the timer this image registers is not the timer this kernel would copy:
      the assembled `ml_get_decrementer` touches no `[cpu_data + #108]` ...
      the assembled `ml_set_decrementer` touches no `[cpu_data + #104]` ...
      `cpu_timebase_init` never stores to `[cpu_data + #108]` ...
      `arm_init` has no `__wrap_PE_init_platform -> cpu_timebase_init -> __wrap_fiq_context_init`
      call triple in this image ...

`tools/check_timebase_registration.py` reads five claims about 481's registration
(`ml_init_timebase(BootCpuData, &stage90_tbd_ops, 0, 0)` before Apple's copy), against **4570's own
object code** — it pins `XNU = external/xnu-4570.1.46` at `:70` and three 4570 pool objects at
`:84-86`.

## Finding — D13 has none of 481's names: its timer is a different mechanism

`grep -rl <name> external/xnu-hd2-darwin13/xnu` is **0** for every name 481 is about:

    ml_init_timebase  cpu_timebase_init  fiq_context_init
    ml_get_decrementer  ml_set_decrementer  rtclock_timebase_func  tbd_ops

The D13 `machine_routines_asm.o` defines no `decrement|timebase|fiq|rtclock` symbol at all, and its
`assym.s` has none of the four `CPU_*` words. D13's timer is the **older** `clock_timebase_init()`
(`osfmk/arm/rtclock.c:327`) — a different mechanism with a different owner, not a renamed one. So 481's
clause checks the *body* of a step the tree does not have, exactly the shape 937 applied to 513–522
([[mi4-off-option-two-spellings]]): **a published skip**, not a rename and not silence
([[mi4-silence-is-a-reading-only-if-success-is-silent]]).

`build_entry.sh` now gates the two `check_timebase_registration.py` invocations (the `--verbose` pass
and the `--selftest`) behind `if [[ -f $XNU_TREE/osfmk/sys/types.h ]]`, printing the skip and its
reason; the 4570 path is the `else`, unchanged.

## State after this rung

The build advances past 481 to **482's GIC routing check** — the next wall: the image has no
`locore_fleh_decirq` (479's handler, collapsed in 937's `locore.o` split) and no `fleh_irq_handler` to
disassemble. 482's subject is likewise 4570's routing. That is rung 945.

## Provenance

`src/entry/build_entry.sh`. Host-side, reversible, **no press**.

- D13 entry build: `/tmp/d13_entry_build31.log` (481 skipped; stops at 482's routing check).
- `make check` exits 0.
- The D13 tree facts (read here): `grep -rl` = 0 for all seven names in
  `external/xnu-hd2-darwin13/xnu`; `out/xnu_asm_obj_d13/machine_routines_asm.o` has no
  decrementer/timebase/fiq symbol; `out/xnu_assym_d13/STAGE90_XNU/assym.s` defines none of
  `CPU_DECREMENTER`/`CPU_GET_DECREMENTER_FUNC`/`CPU_SET_DECREMENTER_FUNC`/`CPU_GET_FIQ_HANDLER`
  (it has `CPU_PENDING_AST`, `CPU_PREEMPT_COUNT`, `CPU_FLEH_*` instead).