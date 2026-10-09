# 948 — the ARM layer's offsets follow the tree (2026-10-09)

The eighth rung in the tree-pin series ([[mi4-913-ios7-rebase-decision]]). 947 made 485's boot-tail check
tree-aware; the D13 build advanced through 486/487 and stopped at **488's assembly/configuration claim**
(`tools/check_asm_config.py`), whose claim 4 read *`assym.s` says `TH_KSTACKPTR = 1480` and
`osfmk_arm_model_dep.o`'s `DebuggerXCall` materialises 0 and 1 and 16 and 28 and 31 and 52 instead*.

## The wall

`check_asm_config.py` was rooted in 4570 in **four** places, all with a hardcoded literal or a 4570
name:

1. `XNU = external/xnu-4570.1.46`; `LOCORE` from it (`claim_locore`).
2. `KOBJ = out/xnu_kernel_obj`; the `OBJECTS` paths (`out/xnu_asm_obj/locore.o`) — no `_d13`.
3. `facts["assym"] = read_assym("out/xnu_assym/…")` — the D13 build writes `out/xnu_assym_d13/…`.
4. `OFFSETS` — the two `struct thread` fields (`TH_KSTACKPTR`, `TH_CTH_SELF`) that D13 does not have.

## Finding — D13's context switch addresses two *different* fields

D13's `machine_load_context` (`osfmk/arm/cswitch.s:106-127`) is a different sequence from 4570's (the
scheme `check_assym_cswitch.py` already models one level down): it writes `r0` into TPIDRURO directly,
loads **`MACHINE_THREAD_CTHREAD_SELF`** (`genassym.c:202` `offsetof(thread_t, machine.cthread_self)` =
**944**) and takes the register save area from **`TH_PCB_ISS`** (`genassym.c:221`
`offsetof(thread_t, machine.iss)` = **576**). The 4570 table's `TH_KSTACKPTR`/`TH_CTH_SELF` are
**declared by neither** of D13's `genassym.c` sites, so comparing them here would be a claim about
fields the file does not have.

Verified on the objects: `osfmk_arm_pcb.o`'s `thread_get_cthread_self` / `thread_set_cthread_self` /
`machine_thread_dup` materialise `#944`; `machine_stack_attach` / `machine_stack_handoff` materialise
`#576`. So `OFFSETS_D13` names those five functions as the second source, exactly as `OFFSETS_4570`
names `DebuggerXCall`/`sleh_undef`/`thread_get_cthread_self`/`machine_thread_create`.

## The check is now tree-aware

Same shape as 942-947: `--tree` (default `$XNU_TREE` or 4570; D13 detected by `osfmk/sys/types.h`),
`configure()` sets `XNU`/`LOCORE`/`KOBJ`/`OBJECTS`/`OFFSETS` and **exports `XNU_TREE`** — the child
scripts (`make_defines.sh`, `select_master.sh`, `arm_asm_defines.sh`) read it from the *environment*, so
setting only the module variable would leave a D13 selftest comparing 4570's facts and passing for the
wrong reason (the silent-wrong-tree defect one level down). `assym_for()` selects the `_d13` assym root.

**Claim 3 is skipped on D13.** D13's `osfmk/arm/locore.s` consults `CONFIG_SKIP_PRECISE_USER_KERNEL_TIME`
**zero** times (`grep -c` = 0) and has no `timer_state_event_*` or `telemetry_*` names, so the two
guarded branches the claim compares do not exist. Skipped, not weakened, published by `main()`.

## A second, older wall the fix exposed: the fragment is 4570's

With the offsets fixed, `claim_declaration` (claim 1) fired: *`STAGE90_XNU`'s declaration is
`[ RELEASE mockfs development ]` and mockfs, development is neither a configuration the tree declares
nor an option*. **This failure was already in the log** (build 36, pre-948) — it prints before claim 4's
errors, which is why it was the *first* line and not the last. Root cause: the fragment
`tools/xnu_config/boot/STAGE90_XNU.local` is a 4570-authored file; `select_master.sh` finds it for
*any* tree, so D13 expands the 4570 declaration. `mockfs`/`development` are 4570-only configurations
(D13's `select_master.sh` declares them nowhere).

The fix has two parts:
- `read_fragment()` reads the `tools/xnu_config/*/<CONFIG>.local` fragment the way `select_master.sh`
  does — but only when the tree does **not** declare `<CONFIG>` itself. A tree that declares it has no
  external fragment, and the expansion is compared against the tree's own declaration.
- Claim 1's orphan test (`does this word reach anything`) now unions **both** trees' declared
  configurations on D13: a word that names the *other* tree's configuration is a word the fragment's
  author meant, not a typo. Folded in only when `IS_D13`, so the 4570 `known` set — and its
  `a_named_configuration_stops_resolving` mutation — is byte-for-byte what it was.

Result: D13's `STAGE90_XNU` **is** the tree's own `RELEASE` (41 defines, exactly RELEASE's), and the
4570 fragment adds no flag because its two words are inert here.

## The D13 selftest

`D13_SKIPPED_MUTATIONS` publishes **13** skips: the seven that mutate claim 3 (the option's branches),
four anchored on a 4570 field or object (`TH_CTH_SELF`/`TH_KSTACKPTR`,
`osfmk_arm_machdep_call.o`/`osfmk_arm_trap.o`), `a_named_configuration_stops_resolving` (drops the
4570 configuration `mockfs`, which D13's MASTER has not got — the drop would raise), and
`the_exception_is_not_dropped` (D13's exception list is already empty, so setting it to `[]` is a
no-op). Skipped, published, `IS_D13`-gated. On D13: **8 refused, 13 skipped**. 4570's `known` set is
unchanged, so 4570 still runs all 21 mutations.

## State after this rung

488 passes on D13. The build advances through 486/487 (both already tree-aware, from rungs 486-487's
own work) and stops at **489's `tools/check_fault_recovery.py`**: the D13 image has no
`copyio_error`/`copyin`/`copyout`, so the copy paths' recovery address is not in the image — a
4570-only exec/fault arrangement. The next rung, 949.

## Provenance

`tools/check_asm_config.py`, `src/entry/build_entry.sh` (the two `--tree` arguments). Host-side,
reversible, **no press**.

- D13: `check_asm_config.py --tree external/xnu-hd2-darwin13/xnu` passes; `--selftest` → 8 refused, 13
  skipped; the offsets read `MACHINE_THREAD_CTHREAD_SELF = 944`, `TH_PCB_ISS = 576`.
- 4570: the default path is unchanged (the fragment is found, claims 3/4 run against 4570's fields).
  ⚠️ the *checked-in 4570 pool* objects (`out/xnu_kernel_obj/osfmk_arm_machdep_call.o` at `#1480` vs
  `assym.s` at `#1496`) are stale, so the 4570 non-selftest fails on pre-existing data — a
  pool-rebuild matter for the 4570 lane, not this rung (this tool is not in `make check`).
- D13 entry build: `/tmp/d13_entry_build38.log` (`xnu_entry_488` at line 1006; stops at 489's
  `check_fault_recovery.py` wall at line 1036).
- `make check` exits 0.