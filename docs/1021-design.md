# 1021 — THE RUNG AFTER 1020, BRANCHED: what the 1020 capture decides, cell by cell

**This is a design, not a decision.** No build, no press, no device action, no `git add`, no write to
`out/stage90` was performed while authoring it. It is written on top of `docs/1020-design.md`
(`5d1d9f9`), the trap dispatch read `records/1020-trap-dispatch.txt`, the root cause
`records/1019-rootcause.txt`, and the press record `records/1020-press-read.txt`.

**Status on write.** 1020's arm `armed-d13-47f664c4` **ran resident and did not return** (run exit 2,
`records/1020-press-read.txt`). The RAM-console log died with the power transition and is being
recovered (`scripts/recover_last_kmsg.sh --read-only`, then `tools/check_1020_capture.py`). **Until
that capture is adjudicated, 1021 cannot be chosen** — this document is the map from each possible
adjudication to its immediate next rung and the ONE question that rung decides, so the choice is made
from the reading and not from a guess.

**Read `R-CTL` first.** It is the mandatory-first cell (1020 design §5): if the witness was not live,
**none** of the other four cells is a reading and every branch below is withheld.

---

## 0. THE FIVE CELLS, AND WHAT EACH ONE MEANS

`tools/check_1020_capture.py` announces exactly one of:

| cell | condition (from the 1020 capture) | 1020's own verdict |
| --- | --- | --- |
| **R-CTL** | no `xnu_live_undef_*` `_seq` row **AND** no boot self-test row `…undef_handler_undef_handled=0x1` | **INSTRUMENT_UNSOUND — WITHHOLD** |
| **R-RAISES-ONCE** | `_seq` reaches ≥2 at ONE pc and **stops** (≤ `UNDEF_LOOP_MAX`) **and** printf ≥1× | **FIXED** |
| **R-RAISES-CLEAN** | `_seq` max == 1 (VFP was already on) **and** printf ≥1× | **FIXED (cleaner)** |
| **R-STILL-LOOPS** | `_seq` climbs **past** `UNDEF_LOOP_MAX` at one invariant pc, printf == 0 | **NOT FIXED** |
| **R-VFP-CONTINUES** | `_seq` bounded, printf == 0 (the boot advanced past the `udf`) | **MOVED** |

---

## 1. THE BRANCH TABLE — the immediate next rung and the ONE question per cell

### R-CTL (WITHHOLD)

- **Next rung: 1021 = RE-RUN THE SAME ARM, INSTRUMENT FIRST.** Not a new mechanism — re-establish the
  witness. Rebuild `armed-d13-47f664c4`'s switch set (`UNDEF_RAISE=1`, `UNDEF_DIRECT=1`,
  `GLOBAL_FIX=1` via the arm-extra file) and press again, OR read why the DIRECT channel did not appear
  (the cap: 1018's lesson — a record whose **absence** is evidence must be cap-exempt; the
  `xnu_live_undef_*` keys already ride the DIRECT channel, which is why they were chosen).
- **The ONE question:** *was the `udf` reached at all on this run, or did the run stop somewhere else
  entirely (so the witness had nothing to report)?* Answered by the boot self-test row
  (`undefined_handler` path exercised at boot) plus the presence of any `xnu_live_*` record.
- **Falsification:** if the self-test row is absent too, the vector itself is not the one this arm
  thinks it compiled — a staging/pool question, not a 1020-behaviour question.

### R-RAISES-ONCE / R-RAISES-CLEAN (FIXED — the loop is DEAD)

These two cells are the same branch for 1021's purposes; they differ only in how many lazy-VFP trips it
took.

- **Next rung: 1021 = WITNESS THE EXCEPTION DELIVERY.** The `udf` now reaches
  `exception_type = EXC_BAD_INSTRUCTION` → `doexception` (`trap.c:1167`/`:1184`). The frontier has
  **moved**: pid 1 either terminates (`launchd` dies → an orderly `panic` is a MEASURED frontier) or the
  kernel's exception delivery hands the thread to a handler. So 1021 instruments what happens **after**
  the raise: a `panic` string / `doexception` witness, and — critically — whether the allocator's
  `_mach_vm_map` path is the thing that is refused (1020 design §7, `records/1019-rootcause.txt` §6).
- **The ONE question:** *does pid 1 die legibly, or does the boot reach a DIFFERENT frontier?* If it
  dies: the next work is the **allocator's `mach_vm_map` being refused** (`docs/1019-design.md`'s shim
  witness, `--wrap=mach_vm_map` on the kernel MIG demux). If it moves on: name whatever the boot stops
  on next (a new frontier is a new rung).
- **The open question 1021 must carry, and it is already on the table:** `records/1020-trap-dispatch.txt`
  §3/§6 proved `mvn ip,#14` ⇒ **table index 15**, but `mach_trap_table[15]` in the linked D13 image is
  **`kern_invalid` by value** — so a `-15` `svc` would **panic** at `swi_mach_error`, and the capture
  shows **no panic and never `-15`** (the last recorded user SVC is `-3` = the `ios7lab_timebase` trap).
  **So either pid 1's `mach_vm_map` never issued the fast-trap `svc`** (the shim's three-field guard at
  `0x381c663c` failed and it fell to the MIG demux `0x381c7f74` / the indirect hook `blx r6`), **or the
  `udf` fired first.** Which one is **UNMEASURED** — the guard's live field values were never recorded.
  **This is the specific thing 1021 (or a sibling) must record** if the raise exposes the allocator:
  the shim's guard inputs, so (a)/(b) is decided rather than assumed.
- **Falsification:** if a raise produces neither a legible death nor a moved frontier (the run wedges in
  exception delivery), 1020's "strict improvement" claim is weaker than stated.

### R-STILL-LOOPS (NOT FIXED)

- **Next rung: 1021 = THE CHANNEL DID NOT REACH THE OBJECT (the 1010 lesson), not a new mechanism.**
  1020's `-D` must reach `osfmk_arm_trap.o`; `build_entry.sh` only **LINKS** that pool object
  (`[[mi4-a-verdict-only-if-its-producer-delivered-one]]`). The immediate work is **by value, on the
  object** — not a re-read of the env:
  - `arm-none-eabi-nm out/xnu_kernel_obj_d13/osfmk_arm_trap.o` and disassemble `sleh_undef`'s user arm;
    confirm the `return` is reachable **only** under the VFP-enable branch (the shape
    `tools/check_d13_undef_raise.py --expect` asserts on the **linked** image — but that checker proved
    **reachability, not execution**, which is exactly the gap `records/1020-press-read.txt`'s
    falsification names).
  - Confirm the pool was **rebuilt** with `-DSTAGE90_XNU_UNDEF_RAISE=1` (`XNU_KERNEL_EXTRA_DEFINES`),
    because `osfmk_arm_trap.o` is stale otherwise. **This is `[[mi4-the-step-after-the-point-of-no-return]]`
    in reverse:** a green entry build says nothing about a pool object it only links.
- **The ONE question:** *did the switch reach the compiled object, or is the linked image stale?*
- **Falsification:** if the object **does** carry the moved `return` and the loop persists, then 1020's
  **mechanism** is wrong (the loop is not what §1 of the design says) — that is the finding, and 1021
  becomes a re-derivation of the defect, not a plumbing fix.

### R-VFP-CONTINUES (MOVED)

- **Next rung: 1021 = NAME THE NEW FRONTIER.** No `undef` loop and no raise means the boot advanced
  **past** the `udf` to something downstream — most likely the allocator's `mach_vm_map` returning and
  the boot proceeding, or a new stall. 1021's job is to **read the capture's tail** and name whatever
  it stopped on (the existing instrument already covers the stall/abort/usvc families), then design the
  rung for **that** object.
- **The ONE question:** *what is the object the boot now stops on?* (A new frontier is answered by
  naming its object, `[[mi4-stub-walk-frontier-kinds]]`.)
- **Note:** this cell and R-RAISES-* differ only in whether a raise was logged; both mean **the `udf`
  is no longer the terminal wall**, and both put the allocator's `mach_vm_map` (or whatever the capture
  tail names) next.

---

## 2. WHAT IS COMMON TO EVERY NON-CTL BRANCH (the things 1021 owes regardless of cell)

1. **The control is two halves.** `R-CTL` needs **both** the boot self-test row
   (`stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled=0x1`) **and** a `xnu_live_undef_seq`
   row (1020 design §5). A capture with one and not the other is not "the witness ran".
2. **The cap.** The undef witness rides `STAGE90_XNU_UNDEF_DIRECT=1` (DIRECT, cap-exempt by
   construction). If a 1021 rung adds a **new** key on a path whose **absence** would be read as
   evidence, it must be cap-exempt too — the 1018 lesson
   (`[[mi4-998-g1-confirmed-map-file-kr0]]`: any record whose absence is evidence must be cap-exempt).
3. **The arm's switch set lives in two files.** The record (`xnu_arm_entry-config.txt`) **and** the
   arm-extra file (`xnu_arm_entry-arm-extra.txt`, which carries
   `STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix`). **`GLOBAL_FIX` is a pool switch no writer in
   `src/entry/` echoes, so it is NOT in the record and CANNOT be** (`press_1020.sh:48-53`, MEASURED).
   A 1021 press must grep **both** or it silently drops the 1015b fix — see `docs/905-design.md` §7.3.
4. **The return.** 1020's non-return was a **RESIDENT** rung, not necessarily a wedge
   (`[[mi4-911-identity-map-bounded-by-window-not-memsize]]`'s distinction). But the missing self-ending
   (`STAGE90_XNU_POST_END_TICKS=0`) is the class 905d named: a switch with a default that compiles the
   return **out**. Any 1021 arm that means to return must carry `POST_END_TICKS=115200000` explicitly.

---

## 3. THE ONE QUESTION DOCUMENT (so 1021 is decided by a reading)

For the recovery in progress: the **first** thing to establish is which of the five cells the capture
adjudicates, and **R-CTL gates the other four**. The decision tree is exactly §1. The single sentence
1021 should be able to say when it is written is:

> *The 1020 capture adjudicated as **<cell>**, so the `udf` is **<dead / still looping / passed>**, and
> the frontier is now **<the exception delivery / the pool object / the allocator's `mach_vm_map` /
> whatever the boot stops on next>** — and the ONE question this rung decides is **<…>**.*

Until then this document is a map, not a decision. `docs/1020-design.md` §7's claim stands and is
unchanged: 1020 is a strict improvement (an infinite silent loop → a legible state), and it does **not**
claim to boot iOS 7 by itself.

---

## 4. ANCHORS

- The five cells: `tools/check_1020_capture.py` (the reader) and `docs/1020-design.md` §5 (the
  pre-registration); `scripts/press_1020.sh:18-27` restates them.
- The press: `records/1020-press-read.txt` (RUN-DID-NOT-RETURN, recovery owed).
- The dispatch read: `records/1020-trap-dispatch.txt` (trap 15 = `kern_invalid` by value; the
  `mvn ip,#14` arithmetic; the shim guard at `0x381c663c`).
- The root cause: `records/1019-rootcause.txt` §6 (the `mach_vm_map`-is-a-MIG-message reading and its
  open reconciliation), `docs/1019-design.md`.
- The mechanism: `docs/1020-design.md`; `external/xnu-hd2-darwin13/xnu/osfmk/arm/trap.c:1108-1126`.
- The channel precedent: `records/1020-build-defects.txt` (the two pool-patch tool defects — both
  fixed in the tool, which is why 1021's staging tools must be re-checked if 1021 edits the same file).