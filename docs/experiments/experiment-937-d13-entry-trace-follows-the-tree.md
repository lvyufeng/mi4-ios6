# 937 — the entry's trace instrument follows the tree; the D13 link closes (2026-10-08)

936 left the D13 entry link with **20 unique undefined names, every one a `entry_trace.c` 4570
`--wrap` target** — `uart_putc`, `ml_get_timebase`, `platform_cache_idle_exit`, `CleanPoC_Dcache`,
`os_reason_create`, `kdebug_free_early_buf`, `cpu_signal_handler_internal`, `copyin_word`,
`Idle_load_context`, `timer_call_enter_with_leeway`, `IORegistryEntry::getChildCount(...)`. None
exists in any D13 pool object: the instrument is 4570's, carried onto a tree whose idle machinery is
different. This rung is the gate that makes the instrument follow the tree, and its payoff is that
**the D13 entry link now closes** — the build advances from the linker into the clause phase.

## The finding: the instrument measures a machine D13 does not ship

The whole arming of `entry_trace.c` — 512 through 535, plus the four standalone wraps — reads values
that only exist because 4570's ARM layer defines them:

| 4570 name | where 4570 defines it | D13 |
|---|---|---|
| `platform_cache_idle_enter/_exit` | `osfmk/arm/caches.c` | **no `caches.c` at all** |
| `cpu_idle`, `Idle_context`, `Idle_load_context` | `osfmk/arm/…` | absent |
| `FlushPoU_Dcache`, `CleanPoC_Dcache`, `FlushPoC_DcacheRegion` | `caches_asm.s` | `caches_asm.s` absent |
| `idle_enable` | `osfmk/arm/cpu_common.c:67` | absent (D13's analogue is `do_power_save`, `pmCPU.c:41`) |
| `up_style_idle_exit`, `SetIdlePop` | 4570 idle | absent |
| `copyin_word` | `machine_routines_asm.s:732` | only the `copyin`/`copyout` family in `bcopyinout.s` |
| `ml_get_timebase`, `os_reason_create`, `uart_putc`, `kdebug_free_early_buf` | 4570 | each absent or renamed |
| `timer_call_enter_with_leeway` / `_quantum_timer_enter` | 4570 | absent |
| `IORegistryEntry::getChildCount` | 4570 IOKit | **absent; `grep -rn getChildCount` over D13 is empty** |

This is the recurring class — an asset pinned to 4570 that must follow the tree
(`mi4-913-ios7-rebase-decision`) — and 936 named it "`fiq_context_init` twenty times over": each name
is a `--wrap` on a function the tree never links, so the wrapper's `__real_<name>` stays undefined
and the stub generator, refusing to invent an object, refuses the build.

## The change: one switch, driving both sides

**A single D13 switch (`STAGE90_ENTRY_D13`) drives a build-side wrap prune and the matching body
gates.** It is derived, not passed — the build reads the pivot's own discriminator
(`[[ -f $XNU_TREE/osfmk/sys/types.h ]]`) into `D13_TRACE`, the same test 936 used to pick the entry
file.

### Build side (`src/entry/build_entry.sh`)

- After the FIQ_CTX_WRAP block: `D13_TRACE=1` when the tree is D13.
- After `TRACE_LDFLAGS` is assembled (~`:277`): a **prune** that drops the twelve 4570-only members
  from the wrap list, keeping the rest (the IOKit, BSD, timer and boot-tail wraps that are in both
  trees), and defines `STUB_DEFINES_TRACE=(-DSTAGE90_ENTRY_D13=1)` for `entry_trace.c`'s compile
  line. The wrap list goes **75 → 64** on D13 (`dropped 11` in the log; `FlushPoU_Dcache` is the
  twelfth and is handled below).
- **The seam's `FlushPoU_Dcache` wrap is added *after* the prune** (`:1161`), so the prune alone
  would miss it — it is now guarded with `&& $D13_TRACE -eq 0`. On D13 the seam body is `#if
  !STAGE90_ENTRY_D13` anyway, so the wrap would name a function nothing defines and nothing calls.
- The compile line gains `${STUB_DEFINES_TRACE[@]}`.
- The file's own `#ifndef STAGE90_ENTRY_D13 / #define … 0` makes the switch well-defined when the
  build does not set it.

### `src/entry/entry_trace.c`

Every body that reads a D13-absent symbol is wrapped in `#if !STAGE90_ENTRY_D13`, and every 4570
value is kept textually identical on 4570 so its object is byte-identical:

- `__wrap_poll`: the 514 `snap_*` declarations and their snapshot, and the whole 512–519
  idle-census report block (which names `idle_enable`, `up_style_idle_exit`, `g_repair_*`,
  `g_pce_*`, `g_pcx_*`). The repair `cpu_signal_handler_internal(0)` and its `rb` guard. The shared
  parts — `entry_note_poll`, the 593 hand-off — stay.
- `__wrap_machine_idle` (the **shared** wrapper — D13 defines `machine_idle`): its idle-enable read
  becomes `EN_IDLE_ENABLE`, a macro that is `((uint32_t)idle_enable)` on 4570 (**the same expression
  the call carried before 937**) and `((uint32_t)do_power_save)` on D13.
- `__wrap_read`/`__wrap_wait4`: the `copyin_word` blocks become `#if !STAGE90_ENTRY_D13`, with the
  wait status left at its `0xFFFFFFFF` sentinel on D13 (an absent reading is a reading).
- The 513–535 idle-and-seam block (from `__real_Idle_load_context` to the seam's
  `_seam_end_run`) is one `#if !STAGE90_ENTRY_D13 … #endif`.
- `ml_get_timebase`, `uart_putc`, `os_reason_create`, `kdebug_free_early_buf`, the two
  leeway/quantum timer entries: each body gated.
- `entry_xnu_child_count`: D13 has no `getChildCount`, so the D13 arm reads the count through
  `getChildSetReference` → `OSArray::getCount` (the object the number lives in), same callers.
- The `entry_wdt_pet` forward declaration is gated `STAGE90_XNU_RESIDENT && !STAGE90_ENTRY_D13`.

Publishers in `entry_stubs.c` are unchanged (they are unconditional globals; uncalled is harmless).

## State after this rung

The D13 build's **final `ld` link closes**: the undefined list is back to its baseline (42 entries,
none of them the twenty), the linker warns only `cannot find entry symbol _start` (the ELF link's
own default base), and the build runs the whole clause phase.

`make check` exits **0**.

**NEXT WALL = 456's registry probe.** The build now stops at:

    FAIL: entry_registry_probe reads gIOResourceMatchedKey, which the image defines as 'T' and not as storage

`gIOResourceMatchedKey`/`gIOBSDKey` are 4570-only globals (`IOService.cpp` defines them and
`kIOResourceMatchedKey`); D13's `IOService.cpp` has **no such pair** — its resource matching uses
`gIOResourceMatchKey` (an `OSSet` matched through `checkResource`, `IOService.cpp:3073`) with **no
`gIOBSDKey` at all**. So `entry_stubs.c`'s `entry_registry_probe` (456) reads two names D13 does not
define; the `verify_registry_probe` clause catches it (`T` = a generated stub). The fix is the same
shape as this rung — a D13 arm reading D13's own resource names — and it is its own rung because the
probe's *reading* changes, not only its compilation.

## Provenance

`src/entry/build_entry.sh`, `src/entry/entry_trace.c`. Host-side, reversible, **no press**.

- D13 link: 20 unique undefined names → 0; build log `/tmp/d13_entry_build12.log`.
- 4570 neutrality: every edit is an `#if`/`#endif` wrapper; `STAGE90_ENTRY_D13` is 0 on 4570, the
  `#if !…` blocks compile in, and each 4570 argument is textually unchanged (`EN_IDLE_ENABLE` reads
  the same `idle_enable` the call carried before). The full 4570 entry *build* is blocked at its
  first prerequisite by a pre-existing stale `out/xnu_asm_obj/cswitch.o` (`out/` is gitignored build
  output) — that check firing is the prerequisite check working, and it is unrelated to this diff
  (it refuses before `entry_trace.c`).
- `make check` exits 0.