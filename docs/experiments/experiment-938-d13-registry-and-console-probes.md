# 938 — the 456 registry probe and the 458 console census follow the tree (2026-10-08)

937 closed the D13 entry link and the build advanced into the clause phase, where it stopped on

    FAIL: entry_registry_probe reads gIOResourceMatchedKey, which the image defines as 'T' and not as storage

That probe (456) and the console census (458) are the next two 4570-only instruments. Both are the
recurring class — an instrument that reads a 4570 mechanism the tree does not have
(`mi4-913-ios7-rebase-decision`) — and both are closed the same way 937 closed the idle instrument.

## Finding 1: the 456 registry probe reads a mechanism D13 implements differently

`entry_registry_probe` reads 4570's resource machinery: `gIOResourceMatchedKey` (the
`"IOResourceMatched"` symbol `copyExistingServices` sets on the resources root, `IOService.cpp:3751`)
and `gIOBSDKey`, walking the resulting `OSArray` for `"IOBSD"`. **D13 has neither global.**
`grep -rn gIOResourceMatchedKey` and `grep -rn gIOBSDKey` over the whole D13 tree are **empty**; D13's
`IOResources::matchPropertyTable` (`IOService.cpp:4458`) reads `gIOResourceMatchKey` (an `OSString`
or `OSSet`, `:3073`) and tests membership with `getProperty(str)`, and its resource matching has no
BSD key at all. So the probe's *reading* is a different mechanism, not a renamed symbol.

Rather than fabricate D13's mechanism into a 4570-shaped probe (whose keys and semantics would then
be wrong), the probe is **compiled out on D13** — its definition, its declaration, its call site in
`entry_note_iolock`, and its seven `xnu_live_reg_*` keys. `entry_note_iolock`'s own records (454) are
unchanged, and so is `entry_rs_state` (455), which uses only the shared `getResourceService` accessor.

## Finding 2: the 458 console census reads one 4570-only global

`entry_os_state_record` reads five console globals; four (`cons_ops_index`, `disable_serial_output`,
`disableConsoleOutput`, `PE_kputc`) are in both trees. The fifth, `kernel_debugger_entry_count`
(`osfmk/kern/debug.c:117`), **D13 does not define** — the whole name is absent from the D13 tree. Its
`xnu_live_console_dbgcnt` line is gated out on D13; the other four keys are written unchanged.

## The change

- **`src/entry/build_entry.sh`**: `entry_stubs.c`'s compile line gains
  `${STUB_DEFINES_TRACE[@]}` — the tree switch (`STAGE90_ENTRY_D13`) now reaches `entry_stubs.c` as
  well as `entry_trace.c`.
- **`src/entry/entry_stubs.c`**: an `#ifndef STAGE90_ENTRY_D13 / #define … 0` (0 = 4570), the 456
  probe's declaration/definition/call each under `#if !STAGE90_ENTRY_D13`, and the 458
  `kernel_debugger_entry_count` extern and its one key under the same gate.
- **`src/entry/build_entry.sh`'s clauses**: `verify_registry_probe` (456) and `verify_console_state`
  (458) are tree-aware. On D13 an absent symbol is the checked reading — `gIOResourceMatchedKey`/
  `gIOBSDKey`/`kernel_debugger_entry_count` must be **absent**, and a `T` there is the generated stub
  the gate exists to prevent. This is a **positive** reading: it catches a gate that failed to compile
  the instrument out.

**A `set -e` trap, found by the failing build.** The first D13 clause used
`type=$(nm … | awk '… END { exit(found?0:1) }')` *without* `|| true`; `awk` exits 1 when the symbol is
absent, so `set -e` killed the build **silently** at line 36630 — no `FAIL`, exit 1, the last line a
successful `say`. The D13 branches now carry `|| type=""`, which is the whole of the fix; it is
[[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]]'s shape (a status is only a verdict
if its producer delivered one — here the producer was the shell and the status was the *test*'s).

## State after this rung

The D13 build passes both clauses and advances **further** into the clause phase, to a deeper wall:

    FAIL: bsdinit_task (0x8003405c..0x80035000) does not branch to __wrap_load_init_program (0x80499c28)

That is 459's `load_init_program` call-site check: it wants `bsdinit_task`'s body to branch to the
`--wrap`ped loader, and on D13 it does not. This is **not** a gating problem (the fix is not another
`#if`) — either D13's `bsdinit_task` calls the loader from another object, or the boot reaches pid 1
by a different path — so it needs call-site analysis and is its own rung.

`make check` exits **0**.

## Provenance

`src/entry/build_entry.sh`, `src/entry/entry_stubs.c`. Host-side, reversible, **no press**.

- D13 build log: `/tmp/d13_entry_build15.log` (456 and 458 both pass; the 458 line reads
  *"the four shared names are real storage symbols, and the 4570-only kernel_debugger_entry_count is
  absent"*).
- 4570 neutrality: the switch is undefined on 4570 (the `#ifndef` sets it to 0), so every gate
  compiles the instruments in; the only 4570-visible change is `entry_stubs.c`'s compile line gaining
  an empty `${STUB_DEFINES_TRACE[@]}` when `D13_TRACE=0`.
- `make check` exits 0.