# 930 — the entry link's pool glob followed the tree everywhere but the glob (2026-10-08)

926 derived the entry link's object paths from `XNU_TREE` by a textual substitution of the contiguous
prefix `$REPO_ROOT/out/xnu_kernel_obj`. The substitution matched the **unquoted** form and missed the
**quoted-before-slash** form `"$REPO_ROOT"/out/xnu_kernel_obj` — twelve live sites, including the one
that matters most.

## The defect

`build_entry.sh:28262` (the `436`/`POOL_OBJS` block) adds the **whole pool** to the link:

    for _o in "$REPO_ROOT"/out/xnu_kernel_obj/*.o "$REPO_ROOT"/out/xnu_asm_obj/*.o; do
        ... POOL_OBJS+=("$_o")
    done
    LINK_OBJS+=(${POOL_OBJS[@]+"${POOL_OBJS[@]}"})

926 left this literal. On the D13 line, `XNU_TREE=<d13>` selects `_d13` for every *named* object but
the glob still reads `out/xnu_kernel_obj` — **4570's pool** — and adds all ~695 of them to the link,
minus the ones already named by their `_d13` path (so both trees' objects, same symbols, duplicated).
`$_436_already` counts the objects named above and skipped, so the count would have looked plausible
and the failure would have surfaced as a duplicate-definition link error naming a symbol, with
nothing to say which copy was 4570's. The other eleven sites are `objdump -r` reference scans
(`:29507`…`:30578`) that read 4570's objects while claiming to read "the pool" — a clause reading the
wrong tree.

## The fix

Fold the twelve sites to the derived variables (set unconditionally at `:67-68`):

    "$REPO_ROOT"/out/xnu_kernel_obj  ->  "$XNU_KERNEL_OBJ_OUT"
    "$REPO_ROOT"/out/xnu_asm_obj     ->  "$XNU_ASM_OBJ_OUT"

4570 neutrality is source-level (the 926 standard): with no env, `XNU_KERNEL_OBJ_OUT` is
`…/out/xnu_kernel_obj` and `XNU_ASM_OBJ_OUT` is `…/out/xnu_asm_obj` character-for-character.

## The check was lying

`tools/check_entry_tree_pools.sh`'s rule (b) — *no live object-path literal* — used the regex
`\$REPO_ROOT/out/xnu_(kernel|asm|platform)_obj`, which requires no quote between `$REPO_ROOT` and
`/out`. That is exactly the form the 926 substitution **did** rewrite, so the rule matched nothing and
passed **vacuously** while the twelve quoted-form literals — one of them the whole-pool glob — sat in
the file. This is the defect class the project already names: *a claim in a comment is not a check*,
and the check was the claim.

Fixed by tolerating the optional closing quote and narrowing to bare `$REPO_ROOT` (the derived line
uses `$_OUT_BASE`, so it cannot match):

    local POOL_RE='\$REPO_ROOT"?/out/xnu_(kernel|asm|platform)_obj'

The selftest now includes the quoted form, so the blind spot cannot return: the pre-926 text plus a
`"$REPO_ROOT"/out/xnu_kernel_obj/*.o` glob must both be refused.

## Verification

| check | result |
|---|---|
| `bash -n src/entry/build_entry.sh` | ok |
| the twelve sites after the fold | 0 quoted-form literals remain |
| derived vars with no env (4570) | `…/out/xnu_kernel_obj`, `…/out/xnu_asm_obj` — the old literals |
| `check_entry_tree_pools.sh --selftest` | refuses the pre-926 text **and** the quoted glob |
| quoted-form-only input | now refused (was accepted) |
| the derived `$_OUT_BASE/xnu_kernel_obj$XNU_OBJ_SUFFIX` line | does **not** match the pattern |
| `make check` | 0 |

## What moved

`src/entry/build_entry.sh` (twelve sites) and `tools/check_entry_tree_pools.sh` (the pattern + the
selftest). No tree edit, no device. The D13 link's next wall is unchanged (the `data.o` require and the
4570 object set, experiment 929) — but it can now be reached without 4570's pool being silently
linked in.