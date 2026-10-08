# 941 — 511's AST-delivery call site follows the tree (2026-10-08)

940 advanced the D13 entry build to

    FAIL: ast_taken_user is not in the linked image - 511's clause needs the function the AST is
    delivered from to read the call site out of

511 wraps `bsd_ast` — the function the kernel calls on the way **back to user mode** — and asserts the
branch to `__wrap_bsd_ast` is inside the kernel's own AST-delivery function. On 4570 that function is
`ast_taken_user` (`osfmk/kern/ast.c:141`). **D13 does not have it:** `grep -rn ast_taken_user <D13>/`
is empty.

## Finding — the delivery function is `ast_taken` on D13

D13 delivers the user AST through **`ast_taken`** (`osfmk/kern/ast.c:102`). Its arm return path —
`cswitch.s:144`, `thread_exception_return` — loads the pending-AST word and

    mov r0, r5          /* reasons */
    mov r1, #1          /* enable */
    blx _ast_taken

and `ast_taken`'s BSD arm calls `bsd_ast` at `:161` (inside `#ifdef MACH_BSD`). So the property 511
checks is unchanged — the branch to the wrapper is inside the kernel's own AST delivery on the way out
to user mode — but the function's **name** is per-tree.

The clause now picks the symbol by `$D13_TRACE` (`ast_taken` on D13, `ast_taken_user` on 4570), and on
D13 a **re-pointed `ast_taken_user` is itself a defect**: a name D13 lacks appearing in the image means
it was fabricated, so the D13 arm fails if `ast_taken_user` is present at all. The two messages that
named the function now interpolate `$atu_name`.

## State after this rung

511 passes on D13: the record line reads *"its only caller ast_taken (0x800aa078) is in a different
object and branches to the wrapper (0x80499d5c), and every reference to it in the pool is a call
(R_ARM_CALL)"*. The build advances to **512**:

    FAIL: Idle_load_context is not in the linked image - the exits from cpu_idle, which are this
    step's object, are not here to count

Unlike 509/510/511 this is **not** a call-site name: `Idle_load_context` is itself in the D13
**prune list** (937 dropped its `--wrap`), because D13 has no `cpu_idle`/`Idle_load_context` idle model
at all (D13's analogue is `machine_idle` tested against `do_power_save`, `pmCPU.c:41`). So 512's clause
is checking the *body* of a step whose wrap the tree already gated out — the clause needs a tree gate,
not a rename. Its own rung.

`make check` exits **0**.

## Provenance

`src/entry/build_entry.sh`. Host-side, reversible, **no press**.

- D13 build: `/tmp/d13_entry_build21.log` (511 passes; stops at 512's `Idle_load_context`).
- 4570 neutrality: the switch is 0 on 4570, so the `else` branch keeps `ast_taken_user` and the
  `ast_taken_user`-absent check does not run.
- `make check` exits 0.