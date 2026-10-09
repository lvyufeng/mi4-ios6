# 955 — the press-readiness chain follows the tree too (2026-10-09)

The fifteenth rung in the tree-pin series ([[mi4-913-ios7-rebase-decision]]). 954 made the **gate**
(`scripts/preflight_boot_check.sh`) read the tree out of the hash-bound entry record; the **press-readiness
chain** (`tools/verify_press_ready.sh`) still read 4570 in three places, so a D13 arm was refused by a row
whose subject the tree does not have:

```
FAIL  the arm is named by a reading      ... REFUSING: symbol cpu_idle not in
      out/stage90/xnu_arm_entry.elf - this is not an image of this phase
```

`tools/check_idle_window_unreachable.py` walks `cpu_idle`, `cpu_idle_exit`, `SetIdlePop`, the three
`__wrap_platform_cache_idle_*` and `cpu_signal_handler_internal` — the 4570 idle block. 937 pruned that
block against exactly the fact that **D13 ships no `cpu_idle`/`caches.c`**, so on a D13 image the tool's
required symbols are absent by construction and its refusal is the tool asserting its own name's subject
onto a tree that does not have it.

## The repair — four pieces, each a tree-gated branch

1. **`tools/check_idle_window_unreachable.py`** — `--tree` (via `is_d13`, the `osfmk/sys/types.h`
   discriminator) and a **D13 skip that verifies its premise** (949/950's shape): it confirms the tree's
   own `osfmk/arm/cpu.c` really carries no `cpu_idle` and refuses otherwise, then prints
   `VERDICT: the idle window is not in this tree (SKIPPED on D13).` The verdict subject is distinct from
   both real verdicts so the joining reader below can tell a skip from a reading. 4570's path is
   **code-unchanged** — `is_d13` is false, the skip branch is not entered.
2. **`tools/verify_press_ready.sh` row 4** — reads `STAGE90_XNU_TREE_D13` out of the **same hash-bound
   record** the gate reads (not the shell, `mi4-build-variant-comes-from-an-env-default`), defaults the
   absent key to `0` (4570, the only value a pre-954 build could write), and threads `--tree "$ARM_TREE"`
   into the tool.
3. **row 4's join** — on a skip there is no window verdict to join, so the arm is named by the **record's
   switch set** instead. The three switches the comparison rests on are confirmed: the idle switch named
   exactly once (reusing `nsc`), and the record's other names quoted. The `swe != want` comparison is
   gated `d13_skip -eq 0` — on a skip `want IS $swe` and comparing a value with itself is vacuous.
4. **the narration checks are skipped for a skip** — the name-vs-image, rung-paragraph and backtick
   scans verify a *window* narration's claims, which a D13 arm does not make; the D13 identification is
   written **last** (after the 4570 rungs' narration blocks would otherwise overwrite it with a sentence
   about "every reading above" that a D13 record does not have).

## What the D13 row prints

```
ok    the arm is named by a reading      **THE D13 ARM - WHICH IS NAMED BY ITS RECORD'S SWITCH SET
      AND NOT BY A WINDOW READING.** `STAGE90_XNU_TREE_D13=1` ... resolved set `armed-d13-3af667a3`.
      ... `STAGE90_XNU_STORAGE_PROBE=60`, `STAGE90_XNU_SMEM_PROBE=1`, ... `STAGE90_XNU_IDLE_NO_SLEEP=0`
      (D13's idle is `do_power_save`, pmCPU.c:41, a compile default - 954). **WHAT CARRIES THIS ARM IS
      THE STORAGE STACK AND THE SMEM MEASUREMENT, NOT THE IDLE WINDOW** ...
```

## The two tripwire halves, both a false positive in a DIFFERENT tool

`preflight_boot_check.sh`'s storage tripwire and `tools/check_storage_refs.py` are the early warning
that a payload has started naming or hard-coding a storage controller. On the D13 payload both fired on
**inert data, not a reference**:

- **symbol half** — `arm-none-eabi-nm -a` matched a **STT_FILE debug symbol** (`leo_sdcc.c`, the name of
  a GC'd module's compilation unit). Dropping `-a` excludes STT_FILE and the false positive is gone,
  while a real `T`/`D`/`t`/`d` storage symbol is still caught.
- **address half** — five words in the storage range live inside the **embedded entry-image blob**
  (`osfmk_console_panic_dialog.o` / `bsd_hfs_hfs_encodinghint.o`), inert data the payload links, not
  code it runs. `check_storage_refs.py` now finds the blob's `[addr, addr+size)` via `nm -S` and excludes
  it; the boundary is pinned both ways by a selftest cell.

Both are the same class: a range/symbol scan over the linked image counting things that are **not**
storage references ([[mi4-measurement-defects]], [[mi4-silence-is-a-reading-only-if-success-is-silent]]).

## Provenance

`tools/check_idle_window_unreachable.py`, `tools/verify_press_ready.sh`, `tools/check_storage_refs.py`,
`scripts/preflight_boot_check.sh` (the storage-tripwire `-a` drop and the shape-test-anchor D13 skip —
peer lane, `run-experiment-526`, **not live**; this session holds the tree), `records/revert-set.txt`
(the `armed-d13-3af667a3` park block, recorded in the same sitting). Host-side, reversible, **no press**.
`verify_press_ready.sh` → **5/5** on the D13 arm; `make check` = 0. The 4570 path is verified unchanged
by value: `--tree .../xnu-4570.1.46` on a 4570 park still prints `reachable EXACTLY ONCE`.