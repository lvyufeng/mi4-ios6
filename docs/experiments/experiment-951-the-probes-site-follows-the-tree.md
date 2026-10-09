# 951 — the probes' call site follows the tree (2026-10-09)

The eleventh rung in the tree-pin series ([[mi4-913-ios7-rebase-decision]]). 949 made the fault/copy
path tree-aware; 950 made 909's pet skip tree-aware (`entry_wdt_pet is not a defined (t) symbol`).
The D13 entry build then advanced through 950's skip and stopped at **910** — the *linked-image* clause
that reads the USB/SMEM probes' call site.

## The wall

```
FAIL: STAGE90_XNU_SMEM_PROBE=1 says the SMEM bank probe should be called 1 time(s)
      from the idle-load wrapper but the linked image calls it 0 time(s)
```

The clause passes, but the probe is not where the clause looks. `entry_smem_probe()` (and the four USB
probes) are called from `__wrap_Idle_load_context` — and **that whole wrapper is inside the
`#if !STAGE90_ENTRY_D13` gate 937 opened**, because D13 ships neither `Idle_load_context` nor
`cpu_idle`. So on D13 the probes sat in **dead code**: the build's awk found the symbol defined (the
probe files compile in), but the *call* to it was compiled out — 0 calls from the expected site,
correctly refused.

This is [[mi4-a-lower-rungs-side-effect-poisoned-the-rung-above]]'s shape one level further: in 910a a
lower *arm*'s switch (`IDLE_NO_SLEEP`) made an upper arm's site unreachable; here a *tree*'s missing
API (D13 has no `cpu_idle`) made the probes' only site unreachable. The clause existed precisely to
refuse this, and it did.

## The repair — the site follows the tree

D13's one wrapper every pass reaches is `__wrap_machine_idle` (the shared 512/513/518 entry; its own
`machine_idle` tests `do_power_save` before it will `wfi`). 937 already keeps it shared across trees.
So:

1. **`src/entry/entry_trace.c`** — a D13-only copy of the five-probe seam (`#if STAGE90_ENTRY_D13`),
   placed in `__wrap_machine_idle` immediately before `__real_machine_idle()`, calling the same five
   probes in the same order. The 4570 seam inside `__wrap_Idle_load_context` is untouched (its
   preprocessor block is unselected on D13), so 4570's object is unchanged.
2. **`src/entry/build_entry.sh`** — the 910 clause's expected symbol is picked from `$D13_TRACE`:
   `__wrap_Idle_load_context` on 4570, `__wrap_machine_idle` on D13. The awk comparison uses the
   chosen name (`-v oksym=…`), so on D13 a probe left at the 4570 site is **refused**, not silently
   accepted. The failure text and the pass message name the tree's own site.

The clause is not weakened to accept *both* names: accepting both would let a probe sit at the dead
4570 site on D13 and read as present — [[mi4-silence-is-a-reading-only-if-success-is-silent]].

## State after this rung

The D13 entry build reaches its **end** (exit 0): the SMEM probe (this arm's only on probe) links at
`__wrap_machine_idle` and the 910 clause passes naming that site; 949/950 stay green; 909/513-522 print
their D13 skips. `make check` exits 0. 4570 is untouched (the new seam is preprocessor-gated to D13 and
no 4570 build was run, per the standing 4570-backup directive).

## Provenance

`src/entry/entry_trace.c`, `src/entry/build_entry.sh`. Host-side, reversible, **no press**.