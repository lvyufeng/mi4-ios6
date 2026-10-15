# 1018 — the CAP-EXEMPT undef witness: does the `udf #254` execute, and does XNU terminate pid 1?

**This is a rung design, not a build.** No build, no press, no device action, no `git add` while
authoring it. It is written on top of a READ of the already-recovered 1015b capture
(`records/1018-recon.txt`, `a7bafd7`) — no new arm and no press has happened.

> **STATUS ON WRITE: 1017 is READ and 1018-RECON is READ.**
> 1017 (`docs/1017-shared-cache-naming.md`, `6dd3b2f`) named, statically and by value, the
> instruction pid 1's frame sits on: un-slid VA `0x3823aa54` = `libsystem_platform.dylib`
> `__os_alloc_slow` + `0x108` = the Thumb **`udf #254`** (`bytes fede`), reached **only** when
> `_mach_vm_map` returns non-zero. 1018-RECON then found **why the capture could not name it
> dynamically**: the recorder that would have was **capped**.

---

## 0. THE ONE THING THIS RUNG DECIDES

1017 named a `udf` and wrote, honestly, `# UNMEASURED` for the mechanism:

> *"which of the two mechanisms produces the identical pc — (i) the undefined-instruction handler
> returns to `0x3823aa54` and it re-traps forever, or (ii) an outer retry re-enters the same failure
> path — **cannot be decided from this capture**."* (`docs/1017-shared-cache-naming.md` §3.3)

**1018-RECON found the reason it cannot be decided, and it is a defect of the instrument, not of the
device.** This tree ALREADY carries a recorder for exactly that `udf` — `__wrap_sleh_undef`
(`src/entry/entry_trace.c:5537`, installed by `--wrap=sleh_undef`, `build_entry.sh:197`, reached from
Apple's user vector `undef_from_user -> bl __wrap_sleh_undef`, linked `8001cf34`). It fired **zero
times** in the capture (`grep -c xnu_live_undef_seq = 0`). Not because no `udf` happened — because by
then its channel was exhausted:

```
capture :29963   xnu_live_capped=0x00006000            (0x6000 = 24576 = ENTRY_LIVE_CAP)
capture :28988   xnu_live_sleh_upc=0x804975c0          (the LAST entry_live_write record)
capture :33977   xnu_live_stall_pc=0x3b04aa54          (the frontier — 4000 records LATER)
```

`_sleh_*` and `_undef_*` are written by **`entry_live_write`** (the CAPPED channel,
`entry_stubs.c:4254` `ENTRY_LIVE_CAP 24576u`, drop at `:4444`). `_stall_*`, `_usvc_*`, `_abt_*` are
written by **`entry_write_kv`** (the DIRECT channel) and kept writing to the frontier — 921 of
`_abt_nfar`'s 959 records are past the cap. **The two recorders that carry a FRAME were both on the
channel that stopped; the one sampler that survived (`entry_note_stall_sample`) is the whole of
1016-A's verdict.**

### 0.1 THE SINGLE QUESTION (1018)

> **When pid 1's frame sits on the `udf #254` at `__os_alloc_slow+0x108`, does the ARM UNDEFINED
> vector fire, and — if it does — does XNU's own `sleh_undef` user arm terminate pid 1, or does the
> process resume at the same instruction?**

The answer selects §2, §3 or §4. **No arm is specified until §1's instrument exists**, because the
current capture cannot answer it (§0's cap) and guessing would repeat the defect
([[mi4-measurement-defects]], "a record whose absence was read as evidence").

---

## 1. THE ARM — ONE new switch, `STAGE90_XNU_UNDEF_DIRECT`, and one key move

**The 1000 lesson, applied.** *A record whose ABSENCE is read as evidence must be **cap-exempt and
direct**.* `__wrap_sleh_undef` is neither. The fix is not "raise the cap" (that only moves the
cliff, and the run's record volume is not known in advance — 1015b's own cap was already raised to
24576u and still hit). The fix is to publish the undef witness on the **DIRECT** channel
(`entry_write_kv`), the same channel `entry_note_stall_sample` uses, so the witness survives
whatever the capped channel does.

### 1.1 The change, precisely

* **New switch** `STAGE90_XNU_UNDEF_DIRECT` (default 0), threaded the same seven ways every switch in
  this tree is ([[mi4-seven-way-name-contract]], `docs/1015-design.md` §8d): the `#if` in
  `entry_trace.c`, the `-D` on the compile line, `ENTRY_ARM_KEYS` (`build_entry.sh:1987`), the record
  writer (`xnu_arm_entry-config.txt`), `scripts/preflight_boot_check.sh`'s config read, the build
  script, and the checker.
* **Inside `__wrap_sleh_undef` (`entry_trace.c:5537-5561`)**, under that switch, route the FIRST
  `UNDEF_LIVE_MAX` entries' keys (`_seq`, `_user_seq`, `_pc`, `_lr`, `_spsr`, `_user`) through
  `entry_write_kv` instead of `entry_live_write` so they survive the cap. **This is the whole
  change**; the six keys already exist and are already read correctly.
* **Raise `UNDEF_LIVE_MAX`** (currently `4u`, `entry_trace.c:5534`) **only on the direct channel**:
  on the direct path the band is bounded by the console's own `max = 0x00200000u - 12u`
  (`entry_stubs.c:4019`), which the capture uses ~1.5 MB of 2 MB of. 64 entries is ~nothing and
  makes a re-trap loop a *ladder*, not a single row.

> **⚠ SCOPE REDUCTION, stated rather than hidden.** An earlier draft of this section added a `_far`
> field, "pairing each undef with the next data abort's `far`". **It is DROPPED**, and the reason is
> [[mi4-a-claim-in-a-comment-is-not-a-check]]: an undefined-instruction entry reads **no** cp15 fault
> pair (they are stale — `entry_trace.c:5514`), so a `_far` there could only be a *label for a stale
> register*, and a field whose meaning is "the previous abort's address" is a field that will be read
> as "this undef's address". **The re-trap-vs-retry question needs no `_far`**: `_seq` climbing with
> an invariant `_pc` at the frontier is a re-trap loop, and `_seq == 1` then a moving sampler pc is a
> transition. Both are already readable from the six existing keys. Adding `_far` would have been
> instrument for instrument's sake.
* **Entry-toolchain only.** This touches `entry_trace.c` (an entry object) and the switch plumbing —
  **no kernel-pool edit, no `--wrap` change, no `external/` source change**. The 1015b
  `XNU_KERNEL_EXTRA_DEFINES`-only rule is **not** needed here.

### 1.2 THE NULL CONTROL (mandatory — this is the whole reason §2's cells can be trusted)

The direct undef witness must publish **at least one entry whose `cpsr` is KERNEL mode** (`& 0x1f !=
0x10`) in the same capture, OR the arm must carry a way to prove its own channel works. **A witness
that can only ever read USR entries cannot show it is reading the vector at all.** The boot self-test
`stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled=0x1` (capture :3480) proves the *vector*
fires once at boot; the direct witness must match that with a `_seq >= 1` row. If it does not, the
arm WITHHOLDS (C-CTL below) — it does not conclude (B).

---

## 2. BRANCH U-FIRES — the vector fires at the frontier (expected if 1017 is right)

If the direct witness shows `_seq` climbing to the frontier and an entry at `pc = 0x3b04aa54`:

| # | condition | verdict | what it means |
|---|---|---|---|
| **C-FIRES-DIES** | `_seq` reaches the frontier, the last entry is `pc=0x3b04aa54` USER, and the run then STOPS (`wdt_pets` flat, no further ticks) | **THE TRAP TERMINATED pid 1** | XNU's `sleh_undef` user arm ran to `exception_triage(EXC_BAD_INSTRUCTION)`. The frontier is a *crash*, not a spin, and the next rung is the exception delivery |
| **C-FIRES-LOOPS** | `_seq` climbs and the SAMPLER's `pc` stays `0x3b04aa54` across ticks | **THE HANDLER RETURNS AND RE-TRAPS** | 1017 §3.3 mechanism (i). The interesting defect: `sleh_undef`'s user arm does not terminate the process on this port |
| **C-FIRES-ONCE** | exactly one `0x3b04aa54` entry, then the sampler's pc is elsewhere | **THE TRAP IS A TRANSITION** | the `udf` is reached, handled, and pid 1 proceeds — the wall is downstream, not here |

**All three are readings; none is a failure of the run.** C-FIRES-LOOPS is the one that would make
this the same shape as 1010–1015 (a fault re-executed at a constant pc) now in USER mode — the
"success reported, fault re-executes" class, at a `udf` instead of a bad PTE.

---

## 3. BRANCH U-SILENT — the vector does NOT fire (the honest falsification)

If the direct witness is proven live (its `_seq >= 1`, §1.2's control passes) and yet **no entry has
`pc` in the shared-cache region** near the frontier:

> **1017's static identification is contradicted by the dynamic one.** The sampler reads
> `pc = 0x3b04aa54` and the byte there is `fede`, but the CPU is **not** taking an undefined-
> instruction exception on it. The candidate explanations, all testable, are then: the pc is a
> *stale* saved value (the sampler reads a frame the vector did not just build), the `fede` halfword
> is not the executed halfword (Thumb/ARM state confusion — `cpsr` bit5 says Thumb), or the frame
> offset `STAGE90_SS_PC` is wrong for THIS frame.

**This branch is a real outcome and must not be papered over.** It is the same shape as the 1013c
lesson: a walker that *refused* a page was read as "the leaf is gone", and the defect was in the
instrument. §1.2's control is what keeps U-SILENT from being an instrument artefact.

---

## 4. BRANCH U-CTL — the witness never fires at all

If `_seq` is 0 in the new capture **and** §1.2's boot self-test row is also absent, the direct
channel itself did not run this arm. **INSTRUMENT_UNSOUND — withhold.** Check the cap FIRST (the 1000
lesson): confirm `xnu_live_capped` is absent or its value is the raised cap.

---

## 5. THE MEASUREMENT-DEFECT SELF-CHECK ([[mi4-measurement-defects]]) — applied to 1018

| the trap | how it would bite here | how 1018 avoids it |
|---|---|---|
| **an absent record read as a stop** | `xnu_live_undef_seq` absent was read by a careless reader as "no `udf` happened" | §0: the absence is explained by the CAP, measured (`:29963`), not assumed; §1 moves the witness to the direct channel so absence means absence |
| **a capped channel read as silence** | `sleh`/`undef` both capped; the frontier is 4000 records past the cap | §0's three-line proof by capture line numbers; §1's direct-channel routing |
| **a key name trusted, not read** | 1016a cited `_farn` for a value that is `_nfar` (`records/1018-recon.txt` §2) | every key in this doc is cited to its WRITER's file:line, and the recon corrected the existing record in place |
| **a ladder's top row read as a frontier** | `_pcn`'s top row is `0x200`, `_farn`'s is `0x200`, `usvc_n`'s is `0x40` | §0 relies on the DIRECT `_nfar`=959 and the window argument (per `records/1016a-read.txt` §3), never on a ladder's top row |
| **an instrument with no control** | the direct witness could be USR-only by construction and "prove" the vector fires | §1.2 REQUIRES a kernel-mode or boot-self-test control; §4 withholds if absent |
| **a fault pair read for an undefined instruction** | `IFSR`/`DFSR` are stale on the undef path (`entry_trace.c:5514`) | §1 does NOT read a cp15 fault pair at all — it carries only the frame's own `pc`/`lr`/`cpsr`, and the dropped `_far` (scope note above) is why it does not |

**Honest limit.** §2/§3/§4 are mutually exclusive and jointly exhaustive over the witness's outcome.
The one new instrument (§1) is a NEW routing of an EXISTING recorder — its absence withholds (§4)
rather than passing. **No number is invented below.**

---

## 6. WHAT IS UNMEASURED UNTIL THE ARM LANDS (no invented numbers)

* `# UNMEASURED — measure at first build` whether `__wrap_sleh_undef`'s direct routing reaches the
  console at all under the raised cap; the entry-toolchain clause must prove the `entry_write_kv`
  call is in the LINKED `__wrap_sleh_undef` (read the linked disassembly by value, bound to the
  function's own base — [[mi4-linked-code-order-is-not-source-order]]).
* `# UNMEASURED` the `_seq` band's true ceiling: 64 direct entries is a guess sized to the console
  headroom, not to a measured undef rate. If `_seq` pins at 64 the band is the wall and the rung must
  re-size it (a ladder top row read as a frontier is `records/1016a-read.txt` §3's defect — do not
  repeat it).
* `# UNMEASURED` whether the run is RESIDENT under the new arm (the 1015b fix is retained, so the
  storm should stay gone — but that is a prediction, not a measurement).
* `# UNMEASURED — measure at first build` the seven-way name set and the checker, per §8d of
  `docs/1015-design.md`.

---

## 7. FILE:LINE AND RECORD ANCHORS

**The recon:** `records/1018-recon.txt` (`a7bafd7`) — the cap proof, the channel table, the counts.

**The instrument:** `src/entry/entry_trace.c:5537-5561` (`__wrap_sleh_undef`, `UNDEF_LIVE_MAX 4u` at
`:5534`); `src/entry/entry_stubs.c:4254` (`ENTRY_LIVE_CAP 24576u`), `:4441` (`entry_live_write`,
drop `:4444`), `:4476` (`entry_write_kv`), `:4019` (console bound `0x00200000-12`), `:1728-1734`
(`SLEH_LIVE_*`), `:4635` (`entry_note_stall_sample`).

**The switch contract:** `src/entry/build_entry.sh:197` (`--wrap=sleh_undef`), `:1987`
(`ENTRY_ARM_KEYS`); `docs/1015-design.md` §8d (the seven-way name contract).

**The prior rung:** `docs/1017-shared-cache-naming.md` (§0 the naming, §3.3 the `# UNMEASURED`);
`records/1016a-read.txt` (§1 the storm-absent reading, §4 the stall sampler, §6 the verdict);
`docs/1017-design.md` (the design pattern this file follows).

**The capture:** `out/stage90/captures/recovered-unknown-arm-20261015-163856-last_kmsg.txt`
(`:29963` the cap marker, `:28988` the last sleh, `:33976-34009` the stall block, `:3480` the undef
boot self-test).

**No build, no press, no device action, and no `git add` was performed while authoring this file.**