# 947 — the boot tail follows the tree (2026-10-09)

The seventh rung in the tree-pin series ([[mi4-913-ios7-rebase-decision]]). 946 made 484's timer census
tree-aware; the D13 build advanced to **485's boot-completion claim** (`tools/check_boot_completion.py`)
and stopped: *the image defines no `kdebug_free_early_buf`, so the wrapper for it calls the generator's
stand-in*.

## The wall

485 wraps Apple's five boot-tail calls and gives each a *position* in the tail — the index
`tail_seen[i]` counts, so a record says which of Apple's calls ran. The five are
`OSKextRemoveKextBootstrap`, **`kdebug_free_early_buf`**, `serial_keyboard_init`,
`vm_page_init_local_q`, `vm_pageout`. D13's `kernel_bootstrap_thread` (`osfmk/kern/startup.c:403-421`)
has **no `kdebug_free_early_buf`** (`grep -rl`=0 in the D13 tree): four callable calls, not five.

## Finding — the tail is one position shorter, but the array stays five

The index is the *position in 4570's five*, and a renumbering would relabel every record in the log while
every file still compiled. So the fix is the same shape 906 used for the 5-slot counter array: **the
index stays the position, and the absent call is kept as a gap.**

| | 4570 | Darwin 13 |
|---|---|---|
| `OSKextRemoveKextBootstrap` | 0 | 0 |
| `kdebug_free_early_buf` | 1 | **gap** (never called, never written) |
| `serial_keyboard_init` | 2 | 2 |
| `vm_page_init_local_q` | 3 | 3 |
| `vm_pageout` | 4 | 4 |

`entry_stubs.c` already keeps `ENTRY_TAIL_CALLS 5u` with **index 1 a gap** (the counter array is
tree-invariant), and `entry_trace.c` already gates the wrapper body with `#if !STAGE90_ENTRY_D13`
(its `--wrap` sits in `build_entry.sh`'s `D13_ONLY_WRAPS`). **Only the check still pinned 4570.**

## The check is now tree-aware

Same shape as 942/943/945/946: `--tree` (default 4570; D13 detected by D13's `osfmk/sys/types.h`),
`configure()` sets the module's `STARTUP_C`/`IOSERVICE_H`/`IOSERVICE_CPP`, `OBJECT_POOL`
(`out/xnu_kernel_obj_d13`) and the one row that differs. The model splits the fixed *positions* from the
*callable* subset:

- **`TAIL_FIVE`** — the tree-invariant 5-slot list (`WRAPPED_4570`). `ENTRY_TAIL_CALLS` and
  `tail_seen0..4` index this, so claim 4 compares the count against `len(TAIL_FIVE)` (5) on both trees.
- **`WRAPPED`** — the callable subset (`configure()` drops `kdebug_free_early_buf` on D13, so 4). Claims
  2/3/7 iterate this; the wrapper bodies that exist are the ones compared.
- **The index is `TAIL_FIVE.index(name)`**, not `WRAPPED.index` — so D13's wrappers publish 0,2,3,4 and
  claim 3's `expected` matches.

Claim 1 (`claim_apple_order`) reads Apple's `startup.c` per tree: it checks the names *present* (the
callable ones) are called once each in order, and that they are exactly `WRAPPED`.

**Claim 6 is skipped on D13** (`claim_the_bit_registration_sets`, the `doServiceMatch`/`copyNotifiers`
"Matched bit" source read). It is 4570's `IOService.cpp`; **D13's file has no `copyNotifiers` function at
all** (`grep`=0) — its registration path delivers `gIOMatchedNotification` from inside `doServiceMatch`.
Four of that claim's links are links in a file the tree does not have. It is a separate concern from the
boot tail, so it is **skipped, not weakened**, and the skip is published by `main()`
(`xnu_entry_485: ... [claim 6 ... is skipped on this tree: D13 has no copyNotifiers]`) — a skip is a
reading, not a silence ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).

## Two latent breaks in earlier rungs, surfaced here

Both are the same defect class 946 found in the sibling tool: **an anchor that a later rung's edit moved,
with no run to notice it.**

1. **The 934/935 anchor break, again.** `a_wrap_moves_to_pass_one` anchored on
   `PASS1_LDFLAGS=(--wrap=PE_init_platform --wrap=fiq_context_init)`. Rung 934 made `fiq_context_init`
   tree-gated through `${FIQ_CTX_WRAP[@]}` and 935 threaded the expansion into that line — **neither
   updated this anchor.** From 935 until now the `_bump` assertion killed the whole 4570 selftest with a
   `SystemExit` (which reads as "the check cannot find what it mutates", not as a verdict). Confirmed by
   `git stash`-ing the 947 edits and rerunning: the *unmodified* file dies at the same anchor. The anchor
   now carries the expansion.
2. **`the_mangled_accessor_is_dropped` edited only the first spelling.** The claim is an existence test
   over the whole file for `_ZNK7OSArray8getCountEv`; that spelling appears in **two** places
   (`entry_trace.c:4605` and `:5763` — the service-plane census 487 added), so `_bump` changed one and the
   claim still passed — a mutation that mutates nothing. Fixed to `str.replace` every spelling, the same
   fix `the_second_state_word_moves_a_word_back` already carries. **Only visible because (1) revived the
   selftest.**

With both fixed, the **4570 selftest refuses all 62 mutations again** (it had printed no verdict since
935).

## The D13 selftest

`D13_SKIPPED_MUTATIONS` publishes **24** skips: 14 anchored on a 4570-only fact (the `kdebug` position,
the wrapped-name set, the source-number arithmetic) and 10 that mutate claim 6, which D13 skips — "the
check must still refuse this" has no subject where the claim does not run. `selftest()` prints
`all N mutations were refused, M skipped (4570-only on this tree)`. On D13: **38 refused, 24 skipped**;
the skip is gated by `IS_D13` so the 4570 run still refuses all 62.

## State after this rung

485/503 pass on D13. The build advances through **486** (`StartIOKit` adoption), **487** (the plane
census) and stops at **`tools/check_assym_cswitch.py`**: *assym.s says `TH_KSTACKPTR = 1480` and
`osfmk_arm_model_dep.o`'s `DebuggerXCall` materialises 0 and 1 and 16 and 28 and 31 and 52 instead
(`model_dep.c:826 reads current_thread()->machine.kstackptr`)* — a `struct thread` field-offset
disagreement between the generated `assym.s` and the compiled C, on D13's `struct thread` layout. The
next rung, 948.

## Provenance

`tools/check_boot_completion.py`, `src/entry/build_entry.sh` (the two `--tree` arguments). Host-side,
reversible, **no press**.

- 4570: `check_boot_completion.py --selftest` (no `--tree` ⇒ 4570 default) against
  `out/stage90/frozen/armed-storage-054f8269/xnu_arm_entry.elf` → **all 62 refused**; the non-selftest
  run prints the `xnu_entry_485` five-call line.
- D13: `--tree external/xnu-hd2-darwin13/xnu` → **38 refused, 24 skipped**; 485's line names 4 of 5.
- D13 entry build: `/tmp/d13_entry_build36.log` (`xnu_entry_485` at line 929; stops at 948's
  `check_assym_cswitch.py` wall).
- `make check` exits 0.